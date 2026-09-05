#include "RenderApp.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <string_view>
#include "Math/ColorMisc.h"

namespace Ashes { namespace Rasterize {

constexpr std::string_view kExtensionObj = ".obj";
constexpr std::string_view kExtensionGltf = ".gltf";

// std::filesystem::path::extension return empty on Linux, let’s do it ourselves.
static std::string ExtensionString(const std::filesystem::path& filename)
{
    std::string extension = filename.filename().string();
    return extension.erase(0, extension.find('.'));
}

static std::filesystem::path ResourceRootDirectory()
{
    return GenericWindow::CurrentPath() / "Resources";
}

RenderApp::RenderApp()
{
    thread_pool_ = std::make_shared<ThreadPool>(8);
}

RenderApp::~RenderApp()
{
    thread_pool_->Stop();
}

void RenderApp::Run(int width, int height, const std::filesystem::path& filename)
{
    render_engine_.Initialize(width, height, thread_pool_);
    CreateWindow(width, height);
    render_thread_ = thread_pool_->Enqueue(&RenderApp::RenderThreadFunc, this);
    modified_settings_.scene_filename = filename;

    while (render_wnd_.RunMessageLoop())
    {
        FetchCommittedEngineSettings();
        StageModifiedEngineSettings();
        UpdateWindowTitle(rendering_fps_);
    }
}

//==============================================================================
// Render Engine
//==============================================================================

void RenderApp::RenderThreadFunc()
{
    float delta_time = 0.0f;
    float time_count = 0.0f;
    int   frame_count = 0;

    while (WaitRenderingContinue())
    {
        using namespace std::chrono;

        CommitStagedEngineSettings();
        const steady_clock::time_point start_point = steady_clock::now();
        render_engine_.TickScene(delta_time);
        render_engine_.RenderScene(post_process_);
        const steady_clock::time_point end_point = steady_clock::now();

        delta_time = duration<float>(end_point - start_point).count();
        time_count += delta_time;
        frame_count += 1;
        render_wnd_.SubmitMemoryImageMT();

        if (time_count >= 1.0f)
        {
            rendering_fps_ = frame_count / time_count;
            time_count = 0.0f;
            frame_count = 0;
        }
    }
}

void RenderApp::LoadSceneFromFile(const std::filesystem::path& filename)
{
    const std::string extension = ExtensionString(filename);

    if (extension == kExtensionObj)
    {
        if (render_engine_.LoadSceneFromObj(filename))
        {
            render_engine_.SetDefaultLight(0.5f, 0.75f);
            render_engine_.SetPlayingAnimation(0, 0);
            render_engine_.SetPlayingMovie(0);
        }
    }
    else if (extension == kExtensionGltf)
    {
        if (render_engine_.LoadSceneFromGltf(filename))
        {
            render_engine_.SetDefaultLight(1.0f, 3.0f);
            render_engine_.SetPlayingAnimation(0, 0);
            render_engine_.SetPlayingMovie(0);
        }
    }
}

bool RenderApp::WaitRenderingContinue()
{
    if (rendering_paused_)
    {
        std::unique_lock<std::mutex> locker(settings_mutex_);
        rendering_continue_.wait(locker);
    }
    return !rendering_stopped_;
}

//==============================================================================
// Render Window
//==============================================================================

void RenderApp::CreateWindow(int width, int height)
{
    render_wnd_.SetOnWindowClose([this](){ OnWindowClose(); });
    render_wnd_.SetOnMouseDragL([this](float x, float y) { OnMouseDragL(x, y); });
    render_wnd_.SetOnMouseDragR([this](float x, float y) { OnMouseDragR(x, y); });
    render_wnd_.SetOnMouseWheel([this](float x){ OnMouseWheel(x); });
    render_wnd_.Create("Ashes Renderer", width, height);
    render_wnd_.CreateMemoryImage();
    post_process_ = [this](Vector3f* colors, int first, int last) {
        OnPostProcess(colors, first, last); };
}

void RenderApp::CreateWindowMenu()
{
    auto params = std::make_shared<GenericMenuParams>();
    EngineSettings& settings = modified_settings_;
    MakeFileMenuParams(*params->AppendPopupButton("File"), settings);
    MakePauseMenuParams(*params, settings);
    MakeCameraMenuParams(*params->AppendPopupButton("Camera"), settings);
    MakeAnimationMenuParams(*params->AppendPopupButton("Animation"), settings);
    MakeEnvirMenuParams(*params->AppendPopupButton("Envir"), settings);
    MakeModeMenuParams(*params->AppendPopupButton("Mode"), settings);
    MakeQualityMenuParams(*params->AppendPopupButton("Quality"), settings);
    MakeMiscMenuParams(*params->AppendPopupButton("Misc"), settings);
    render_wnd_.BuildMenu(params);
}

void RenderApp::UpdateWindowTitle(float fps)
{
    char title[128];
    std::snprintf(title, sizeof(title),
        "Ashes Renderer  FPS: %.1f / %.2f ms", fps, 1000.0f / fps);
    render_wnd_.SetTitle(title);
}

void RenderApp::OnPostProcess(Vector3f* colors, int first, int last)
{
    render_wnd_.WriteMemoryImageMT(reinterpret_cast<float*>(colors), first, last);
}

void RenderApp::OnWindowClose()
{
    // ensure render thread has exited before destroying window.
    rendering_stopped_ = true;
    rendering_continue_.notify_all();
    render_thread_.wait();
}

void RenderApp::OnMouseDragL(float x, float y)
{
    modified_settings_.camera_rotation.X() -= y;
    modified_settings_.camera_rotation.Y() -= x;
    modified_settings_.camera_option = std::monostate();
}

void RenderApp::OnMouseDragR(float x, float y)
{
    modified_settings_.camera_translation.X() -= x;
    modified_settings_.camera_translation.Y() += y;
    modified_settings_.camera_option = std::monostate();
}

void RenderApp::OnMouseWheel(float x)
{
    modified_settings_.camera_translation.Z() += x;
    modified_settings_.camera_option = std::monostate();
}

void RenderApp::OnChooseModelFile()
{
    modified_settings_.scene_filename = render_wnd_.ChooseModelFile();
}

//==============================================================================
// Make Menu Params
//==============================================================================

static void BuildModelOptions(
    const std::filesystem::path& root_dir,
    std::string_view extension,
    std::vector<std::pair<std::string, std::filesystem::path>>& models)
{
    const std::size_t keep_num_models = models.size();
    std::error_code error;
    std::string category = "[" + std::string(extension.substr(1)) + "] ";
    std::transform(category.begin(), category.end(), category.begin(),
        [](char ch) { return static_cast<char>(std::toupper(ch)); });

    for (const std::filesystem::directory_entry& folder :
         std::filesystem::directory_iterator(root_dir, error))
    {
        const std::filesystem::path& folder_path = folder.path();
        const std::string folder_name = folder_path.filename().string();

        for (const std::filesystem::directory_entry& file :
             std::filesystem::directory_iterator(folder_path, error))
        {
            if (file.is_regular_file() && ExtensionString(file.path()) == extension)
                models.emplace_back(category + folder_name, file.path());
        }
    }

    if (models.size() == keep_num_models && error)
        models.emplace_back(error.message(), "");
}

void RenderApp::MakeFileMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    std::filesystem::path root_dir = ResourceRootDirectory() / "Models";
    std::vector<std::pair<std::string, std::filesystem::path>> models;
    BuildModelOptions(root_dir / "obj", kExtensionObj, models);
    models.emplace_back(kGenericMenuSeparatorText, std::filesystem::path());
    BuildModelOptions(root_dir / "gltf", kExtensionGltf, models);
    params.AppendButton("Open", {}, [this]() { OnChooseModelFile(); });
    params.AppendSeparator();
    params.AppendButtonGroup<std::filesystem::path>(std::move(models),
        [&settings]() { return settings.scene_filename; },
        [&settings](auto model) { settings.scene_filename = model; });
    params.AppendSeparator();
    params.AppendButton("Exit", {}, [this]() { render_wnd_.Close(); });
}

void RenderApp::MakePauseMenuParams(
    GenericMenuParams& params,
    EngineSettings& /*settings*/)
{
    params.AppendButton("Pause", {}, [this]() {
        rendering_paused_ = !rendering_paused_;
        rendering_continue_.notify_all(); });
}

void RenderApp::MakeCameraMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    std::vector<std::pair<std::string, CameraOption>> options = {
        {"Front",        StandardViewType::Front},
        {"Back",         StandardViewType::Back},
        {"Left",         StandardViewType::Left},
        {"Right",        StandardViewType::Right},
        {"Top",          StandardViewType::Top},
        {"Bottom",       StandardViewType::Bottom},
        {"TopFrontLeft", StandardViewType::TopFrontLeft}};

    if (render_engine_.NumCustomCameras() > 0)
    {
        options.emplace_back(kGenericMenuSeparatorText, std::monostate());
        for (int i = 0; i < render_engine_.NumCustomCameras(); ++i)
            options.emplace_back("Custom-" + std::to_string(i + 1), i);
    }

    params.AppendButtonGroup<CameraOption>(std::move(options),
        [&settings]() { return settings.camera_option; },
        [&settings](CameraOption option) { settings.camera_option = option; });
}

void RenderApp::MakeAnimationMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    auto movie_names = render_engine_.MovieDisplayNames();
    auto anim_names = render_engine_.AnimationDisplayNames();

    if (movie_names.empty())
    {
        for (std::size_t i = 0; i < anim_names.size(); ++i)
        {
            params.AppendSeparator();
            params.AppendButtonGroup(std::move(anim_names[i]),
                [i, &settings]() { return settings.playing_anims[i]; },
                [i, &settings](int idx) { settings.playing_anims[i] = idx; });
        }
    }
    else
    {
        params.AppendButtonGroup(std::move(movie_names),
            [&settings]() { return settings.playing_movie; },
            [&settings](int idx) { settings.playing_movie = idx; });
    }
}

void RenderApp::MakeEnvirMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    std::filesystem::path root_dir = ResourceRootDirectory() / "Envirs";
    std::vector<std::pair<std::string, std::filesystem::path>> envirs;
    envirs.emplace_back("None", std::filesystem::path());
    std::error_code error;
    for (const std::filesystem::directory_entry& folder :
         std::filesystem::directory_iterator(root_dir, error))
    {
        if (folder.is_directory())
            envirs.emplace_back(folder.path().filename().string(), folder.path());
    }
    std::sort(envirs.begin(), envirs.end());
    params.AppendButtonGroup<std::filesystem::path>(std::move(envirs),
        [&settings]() { return settings.ibl_directory; },
        [&settings](auto dir) { settings.ibl_directory = dir; });
}

void RenderApp::MakeModeMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    params.AppendButtonGroup<RenderMode>(
        {{"FinalColor",              RenderMode::FinalColor},
         {"ViewDepth",               RenderMode::ViewDepth},
         {kGenericMenuSeparatorText, RenderMode::ViewDepth},
         {"Normal",                  RenderMode::Normal},
         {"Emissive",                RenderMode::Emissive},
         {"Occlusion",               RenderMode::Occlusion},
         {kGenericMenuSeparatorText, RenderMode::Occlusion},
         {"Ambient",                 RenderMode::Ambient},
         {"Diffuse",                 RenderMode::Diffuse},
         {kGenericMenuSeparatorText, RenderMode::Diffuse},
         {"BaseColor",               RenderMode::BaseColor},
         {"Metallic",                RenderMode::Metallic},
         {"Roughness",               RenderMode::Roughness},
         {kGenericMenuSeparatorText, RenderMode::Roughness},
         {"Albedo",                  RenderMode::Albedo},
         {"Specular",                RenderMode::Specular},
         {"Glossiness",              RenderMode::Glossiness},},
        [&settings]() { return settings.render_mode; },
        [&settings](RenderMode render_mode) { settings.render_mode = render_mode; });
}

void RenderApp::MakeQualityMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    params.AppendButtonGroup<TextureFilter>(
        {{"Texture - POINT",             TextureFilter::POINT},
         {"Texture - POINT_MIP_POINT",   TextureFilter::POINT_MIP_POINT},
         {"Texture - POINT_MIP_LINEAR",  TextureFilter::POINT_MIP_LINEAR},
         {"Texture - LINEAR",            TextureFilter::LINEAR},
         {"Texture - LINEAR_MIP_POINT",  TextureFilter::LINEAR_MIP_POINT},
         {"Texture - LINEAR_MIP_LINEAR", TextureFilter::LINEAR_MIP_LINEAR},},
        [&settings]() { return settings.tex_filter; },
        [&settings](TextureFilter tex_filter) { settings.tex_filter = tex_filter; });

    params.AppendSeparator();

    params.AppendButtonGroup<ShadowOptions>(
        {{"Shadow - None",               {0.0f, ShadowFilter::None}},
         {"Shadow - 1x Resolution",      {1.0f, ShadowFilter::Point}},
         {"Shadow - 2x Resolution",      {2.0f, ShadowFilter::Point}},
         {"Shadow - 3x Resolution",      {3.0f, ShadowFilter::Point}},
         {"Shadow - 1x Resolution, PCF", {1.0f, ShadowFilter::PCF}},
         {"Shadow - 2x Resolution, PCF", {2.0f, ShadowFilter::PCF}},
         {"Shadow - 3x Resolution, PCF", {3.0f, ShadowFilter::PCF}}},
        [&settings]() { return settings.shadow; },
        [&settings](ShadowOptions shadow) { settings.shadow = shadow; });

    params.AppendSeparator();
 
    const auto ToggleAntialias = [&settings](int antialias) {
        // MSAA and SSAA are mutually exclusive.
        // before toggle one on, toggle another one off.
        constexpr int MSAA_SSAA = RenderAntialias_MSAA | RenderAntialias_SSAA;
        bool toggle_msaa_ssaa_on = (antialias & MSAA_SSAA) & ~settings.antialias;
        if (toggle_msaa_ssaa_on) { settings.antialias &= ~MSAA_SSAA; }
        settings.antialias ^= antialias; };

    params.AppendButton("Antialias - ECSAA",
        [&settings]() { return settings.antialias & RenderAntialias_ECSAA; },
        std::bind(ToggleAntialias, RenderAntialias_ECSAA));

    params.AppendButton("Antialias - MSAA",
        [&settings]() { return settings.antialias & RenderAntialias_MSAA; },
        std::bind(ToggleAntialias, RenderAntialias_MSAA));

    params.AppendButton("Antialias - SSAA",
        [&settings]() { return settings.antialias & RenderAntialias_SSAA; },
        std::bind(ToggleAntialias, RenderAntialias_SSAA));
}

void RenderApp::MakeMiscMenuParams(
    GenericMenuParams& params,
    EngineSettings& settings)
{
    params.AppendButtonGroup<int>(
        {{"Render Thread - 1", 1},
         {"Render Thread - 2", 2},
         {"Render Thread - 4", 4},
         {"Render Thread - 6", 6},
         {"Render Thread - 8", 8},},
        [&settings]() { return settings.num_render_threads; },
        [&settings](int n) { settings.num_render_threads = n; });

    params.AppendSeparator();

    params.AppendButtonGroup<Vector3f>(
        {{"Background - Black", {0.0f, 0.0f, 0.0f}},
         {"Background - Gray",  {0.5f, 0.5f, 0.5f}},
         {"Background - White", {1.0f, 1.0f, 1.0f}},
         {"Background - Red",   {1.0f, 0.0f, 0.0f}},
         {"Background - Green", {0.0f, 1.0f, 0.0f}},
         {"Background - Blue",  {0.0f, 0.0f, 1.0f}}},
        [&settings]() { return settings.background_color; },
        [&settings](Vector3f color) { settings.background_color = color; });

    params.AppendSeparator();

    params.AppendButton("Show Floor",
        [&settings]() { return settings.floor_visibility; },
        [&settings]() { settings.floor_visibility ^= true; });

    params.AppendButton("Backface Culling",
        [&settings]() { return settings.backface_culling; },
        [&settings]() { settings.backface_culling ^= true; });

    params.AppendButton("OIT",
        [&settings]() { return settings.oit; },
        [&settings]() { settings.oit ^= true; });

    params.AppendButton("ACES",
        [&settings]() { return settings.aces; },
        [&settings]() { settings.aces ^= true; });

    params.AppendButton("Gamma Correction",
        [&settings]() { return settings.gamma_correction; },
        [&settings]() { settings.gamma_correction ^= true; });
}

//==============================================================================
// Engine Settings
//==============================================================================

void RenderApp::ObtainEngineSettings(EngineSettings& settings) const
{
    settings.ibl_directory = render_engine_.GetIBLDirectory();
    settings.playing_movie = render_engine_.CurrentPlayingMovieIndex();
    settings.playing_anims.assign(render_engine_.NumAnimatableObjects(), -1);
    for (int i = 0; i < static_cast<int>(settings.playing_anims.size()); ++i)
        settings.playing_anims[i] = render_engine_.CurrentPlayingAnimationIndex(i);
    settings.camera_option = render_engine_.CurrentCameraOption();
    settings.camera_translation = Vector3f::Zero();
    settings.camera_rotation = Vector3f::Zero();
    settings.floor_visibility = render_engine_.IsSceneFloorVisible();
    settings.render_mode = render_engine_.GetRenderMode();
    settings.tex_filter = render_engine_.GetTextureFilter();
    settings.shadow.first = render_engine_.GetShadowResolutionScale();
    settings.shadow.second = render_engine_.GetShadowFilter();
    settings.num_render_threads = render_engine_.GetNumberOfRenderThreads();
    settings.background_color = render_engine_.GetBackgroundColor();
    settings.antialias = render_engine_.GetAntialias();
    settings.backface_culling = render_engine_.IsBackfaceCullingEnabled();
    settings.oit = render_engine_.IsOITEnabled();
    settings.aces = render_engine_.IsACESEnabled();
    settings.gamma_correction = render_engine_.IsGammaCorrectionEnabled();
}

void RenderApp::ApplyEngineSettings(const EngineSettings& settings)
{
    render_engine_.LoadIBLFromDirectory(settings.ibl_directory);
    for (int i = 0; i < static_cast<int>(settings.playing_anims.size()); ++i)
        render_engine_.SetPlayingAnimation(i, settings.playing_anims[i]);
    render_engine_.SetPlayingMovie(settings.playing_movie);

    if (settings.camera_option.index() != 0)
        render_engine_.SwitchCamera(settings.camera_option);
    else if (!settings.camera_translation.IsZero())
        render_engine_.MoveCameraInScreenSpace(settings.camera_translation);
    else if (!settings.camera_rotation.IsZero())
        render_engine_.RotateCameraAroundScene(settings.camera_rotation * 0.01f);

    render_engine_.SetSceneFloorVisibility(settings.floor_visibility);
    render_engine_.SetRenderMode(settings.render_mode);
    render_engine_.SetTextureFilter(settings.tex_filter);
    render_engine_.SetShadowResolutionScale(settings.shadow.first);
    render_engine_.SetShadowFilter(settings.shadow.second);
    render_engine_.SetNumberOfRenderThreads(settings.num_render_threads);
    render_engine_.SetBackgroundColor(settings.background_color);
    render_engine_.SetAntialias(settings.antialias);
    render_engine_.EnableBackfaceCulling(settings.backface_culling);
    render_engine_.EnableOIT(settings.oit);
    render_engine_.EnableACES(settings.aces);
    render_engine_.EnableGammaCorrection(settings.gamma_correction);
}

void RenderApp::StageModifiedEngineSettings()
{
    constexpr auto CompareEngineSettings = 
        [](const EngineSettings& lhs, const EngineSettings& rhs)
    {
        return lhs.floor_visibility == rhs.floor_visibility
            && lhs.playing_movie == rhs.playing_movie
            && lhs.camera_option == rhs.camera_option
            && lhs.render_mode == rhs.render_mode
            && lhs.tex_filter == rhs.tex_filter
            && lhs.shadow == rhs.shadow
            && lhs.num_render_threads == rhs.num_render_threads
            && lhs.background_color == rhs.background_color
            && lhs.antialias == rhs.antialias
            && lhs.backface_culling == rhs.backface_culling
            && lhs.oit == rhs.oit
            && lhs.aces == rhs.aces
            && lhs.gamma_correction == rhs.gamma_correction
            && lhs.scene_filename == rhs.scene_filename
            && lhs.ibl_directory == rhs.ibl_directory
            && lhs.playing_anims == rhs.playing_anims;
    };

    std::unique_lock<std::mutex> locker(
        settings_mutex_, std::try_to_lock);

    if (locker.owns_lock())
    {
        Vector3f camera_translation = modified_settings_.camera_translation;
        Vector3f camera_rotation = modified_settings_.camera_rotation;
        modified_settings_.camera_translation = Vector3f::Zero();
        modified_settings_.camera_rotation = Vector3f::Zero();

        if (!camera_translation.IsZero() || !camera_rotation.IsZero() ||
            !CompareEngineSettings(modified_settings_, staged_settings_))
        {
            camera_translation += staged_settings_.camera_translation;
            camera_rotation += staged_settings_.camera_rotation;
            staged_settings_ = modified_settings_;
            staged_settings_.camera_translation = camera_translation;
            staged_settings_.camera_rotation = camera_rotation;
            new_settings_staged_ = true;
            rendering_continue_.notify_all();
        }
    }
}

void RenderApp::CommitStagedEngineSettings()
{
    std::unique_lock<std::mutex> locker(
        settings_mutex_, std::try_to_lock);

    if (locker.owns_lock() && new_settings_staged_)
    {
        if (staged_settings_.scene_filename != committed_settings_.scene_filename)
        {
            LoadSceneFromFile(staged_settings_.scene_filename);
            ObtainEngineSettings(committed_settings_);
            committed_settings_.scene_filename = staged_settings_.scene_filename;
            new_settings_staged_ = false;
            new_settings_committed_ = true;
            new_scene_loaded_ = true;
        }
        else
        {
            ApplyEngineSettings(staged_settings_);
            ObtainEngineSettings(committed_settings_);
            staged_settings_.camera_translation = Vector3f::Zero();
            staged_settings_.camera_rotation = Vector3f::Zero();
            new_settings_staged_ = false;
            new_settings_committed_ = true;
        }
    }
}

void RenderApp::FetchCommittedEngineSettings()
{
    std::unique_lock<std::mutex> locker(
        settings_mutex_, std::try_to_lock);

    if (locker.owns_lock() && new_settings_committed_)
    {
        Vector3f camera_translation = modified_settings_.camera_translation;
        Vector3f camera_rotation = modified_settings_.camera_rotation;
        modified_settings_ = committed_settings_;
        modified_settings_.camera_translation = camera_translation;
        modified_settings_.camera_rotation = camera_rotation;
        new_settings_committed_ = false;

        if (new_settings_staged_)
        {
            staged_settings_ = committed_settings_;
            new_settings_staged_ = false;
        }

        if (new_scene_loaded_)
        {
            CreateWindowMenu();
            new_scene_loaded_ = false;
        }
    }
}

}}