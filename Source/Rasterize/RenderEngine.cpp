#include "RenderEngine.h"
#include <thread>
#include <cassert>
#include <algorithm>
#include "Math/MathMisc.h"
#include "Math/ColorMisc.h"
#include "Math/Transform.h"
#include "Parser/ObjParser.h"
#include "Parser/GltfParser.h"
#include "Parser/AshesParser.h"
#include "Utility/UtilityMisc.h"
#include "Geometry/AxisAlignedBox.h"

namespace Ashes { namespace Rasterize {

static const std::string kExtensionAshes = ".ashes";

RenderEngine::RenderEngine()
{
    ibl_data_ = std::make_shared<RenderIBLData>();
}

RenderEngine::~RenderEngine()
{
}

void RenderEngine::Initialize(
    int width, int height,
    std::shared_ptr<ThreadPool> thread_pool)
{
    renderer_ = std::make_unique<Renderer>();
    render_states_.thread_pool = thread_pool;
    render_states_.num_threads = (std::thread::hardware_concurrency() / 3 + 1) & ~1;
    render_states_.render_size = {width, height};
    render_states_.backface_culling = true;
    frame_uniforms_ = std::make_shared<RenderUniformsPerFrame>();
    depth_buffer_ = std::make_shared<Texture>();
    color_buffer_ = std::make_shared<Texture>();
}

bool RenderEngine::LoadSceneFromObj(const std::filesystem::path& filename)
{
    UnloadScene();
    if (AssetContent assets; LoadAssetFromObj(filename, assets))
    {
        std::filesystem::path ashes_filename = filename;
        ashes_filename.replace_extension(kExtensionAshes);
        RenderConfig config;
        // ObtainRenderConfig(config);
        LoadExtendAssetFromAshes(ashes_filename, assets, config);
        scene_.BuildScene(assets);
        SwitchToStandardViewCamera(StandardViewType::TopFrontLeft);
        EnableGammaCorrection(false);
        ApplyRenderConfig(config);
        return true;
    }
    return false;
}

bool RenderEngine::LoadSceneFromGltf(const std::filesystem::path& filename)
{
    UnloadScene();
    if (AssetContent assets; LoadAssetFromGltf(filename, assets))
    {
        std::filesystem::path ashes_filename = filename;
        ashes_filename.replace_extension(kExtensionAshes);
        RenderConfig config;
        // ObtainRenderConfig(config);
        LoadExtendAssetFromAshes(ashes_filename, assets, config);
        scene_.BuildScene(assets);
        animatable_objs_ = scene_.AnimatableObjectIndices();
        SwitchToStandardViewCamera(StandardViewType::TopFrontLeft);
        EnableGammaCorrection(true);
        ApplyRenderConfig(config);
        return true;
    }
    return false;
}

void RenderEngine::UnloadScene()
{
    animatable_objs_.clear();
    frame_uniforms_->lights.clear();
    frame_uniforms_->shadow_maps.clear();
    obj_uniforms_.clear();
    scene_.CleanScene();
}

void RenderEngine::RenderScene(RenderPostProcess post_process)
{
    UpdateCameraViewAndLens();
    UpdateRenderStates(std::move(post_process));
    UpdateUniformsPerFrame();
    SubmitDrawCalls();
    SubmitShadowCasters();
    renderer_->SetRenderStates(render_states_);
    renderer_->Present();
}

//==============================================================================
// Scene Entities
//==============================================================================

bool RenderEngine::IsSceneFloorVisible() const
{
    return scene_.IsFloorVisible();
}

void RenderEngine::SetSceneFloorVisibility(bool visible)
{
    scene_.SetFloorVisibility(visible);
}

void RenderEngine::TickScene(float delta)
{
    scene_.TickScene(delta);
}

//==============================================================================
// Lighting
//==============================================================================

std::filesystem::path RenderEngine::GetIBLDirectory() const
{
    const std::string& tex_source_path = ibl_data_->irradiance_map.SourcePath();
    return std::filesystem::path(tex_source_path).parent_path();
}

float RenderEngine::GetIBLSpecularScale() const
{
    return ibl_data_->specular_scale;
}

void RenderEngine::LoadIBLFromDirectory(const std::filesystem::path& directory)
{
    std::error_code ec;
    if (directory.empty() || !std::filesystem::exists(directory, ec))
    {
        ibl_data_->brdf_lut.CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
        ibl_data_->irradiance_map.CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
        ibl_data_->prefilter_map.CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
    }
    else if (directory != GetIBLDirectory())
    {
        ibl_data_->brdf_lut.CreateFromFile(
            (directory.parent_path() / "brdf_lut.hdr").string());
        ibl_data_->irradiance_map.CreateFromHorizontalStripFile(
            (directory / "diffuse.hdr").string());
        ibl_data_->prefilter_map.CreateFromHorizontalStripFile(
            (directory / "specular{lod}.hdr").string());
    }
}

void RenderEngine::SetIBLSpecularScale(float scale)
{
    ibl_data_->specular_scale = scale;
}

void RenderEngine::SetDefaultLight(float ambient, float directional)
{
    default_ambient_light_ = {ambient, ambient, ambient};
    default_directional_light_ = std::make_shared<DirectionalLight>();
    default_directional_light_->direction = Vector3f(1.0f, -1.0f, -1.0f).Normalized();
    default_directional_light_->color = {directional, directional, directional};
}

//==============================================================================
// Scene Movie & Object Animation
//==============================================================================

static const std::string kEmptyName = "UNNAMED";

std::vector<std::string> RenderEngine::MovieDisplayNames() const
{
    std::vector<std::string> movie_names;
    movie_names.reserve(scene_.Movies().size());

    for (const Movie& movie : scene_.Movies())
    {
        const std::string& movie_name = movie.root_animation.name;
        movie_names.push_back(movie_name.empty() ? kEmptyName : movie_name);
    }

    return movie_names;
}

std::vector<std::vector<std::string>> RenderEngine::AnimationDisplayNames() const
{
    std::vector<std::vector<std::string>> anim_names;
    anim_names.reserve(animatable_objs_.size());

    for (int obj_idx : animatable_objs_)
    {
        const std::shared_ptr<Object3D> obj = scene_.Objects()[obj_idx];
        const std::vector<Animation>* anims = scene_.Animations(obj_idx);
        std::vector<std::string>& anim_names_of_obj = anim_names.emplace_back();
        anim_names_of_obj.reserve(anims->size());

        for (const Animation& anim : *anims)
        {
            std::string display_name;
            display_name.append(obj->Name().empty() ? kEmptyName : obj->Name());
            display_name.append(".");
            display_name.append(anim.name.empty() ? kEmptyName : anim.name);
            anim_names_of_obj.push_back(display_name);
        }
    }

    return anim_names;
}

int RenderEngine::NumAnimatableObjects() const
{
    return static_cast<int>(animatable_objs_.size());
}

int RenderEngine::CurrentPlayingMovieIndex() const
{
    return scene_.CurrentPlayingMovieIndex();
}

int RenderEngine::CurrentPlayingAnimationIndex(int obj_idx) const
{
    return IsValidIndex(animatable_objs_, obj_idx) ?
        scene_.CurrentPlayingAnimationIndex(animatable_objs_[obj_idx]) : -1;
}

void RenderEngine::SetPlayingMovie(int movie_idx)
{
    scene_.SetPlayingMovie(movie_idx, true);
}

void RenderEngine::SetPlayingAnimation(int obj_idx, int anim_idx)
{
    if (IsValidIndex(animatable_objs_, obj_idx))
        scene_.SetPlayingAnimation(animatable_objs_[obj_idx], anim_idx, true);
}

//==============================================================================
// Camera
//==============================================================================

int RenderEngine::NumCustomCameras() const
{
    return static_cast<int>(scene_.Cameras().size());
}

CameraOption RenderEngine::CurrentCameraOption() const
{
    return camera_option_;
}

void RenderEngine::SwitchToCustomCamera(int idx)
{
    if (IsValidIndex(scene_.Cameras(), idx))
    {
        camera_.SetView(scene_.Cameras()[idx].GetView());
        camera_option_ = idx;
    }
}

void RenderEngine::SwitchToStandardViewCamera(StandardViewType type)
{
    camera_.SwitchToStandardView(type, scene_.Bounds());
    camera_option_ = type;
}

void RenderEngine::SwitchCamera(CameraOption option)
{
    if (option.index() == 1)
        SwitchToStandardViewCamera(std::get<1>(option));
    else if (option.index() == 2)
        SwitchToCustomCamera(std::get<2>(option));
}

void RenderEngine::MoveCameraInScreenSpace(const Vector3f& screen_distance)
{
    const float screen_w = render_states_.render_size.X() * 1.0f;
    const float screen_h = render_states_.render_size.Y() * 1.0f;
    const float near_w = camera_.GetNearDim().X();
    const float near_h = camera_.GetNearDim().Y();
    const float near = camera_.GetNear();
    const float min_view_depth = SceneViewDepthRange().min;

    const float scale_x = (near_w / screen_w) * (min_view_depth / near);
    const float scale_y = (near_h / screen_h) * (min_view_depth / near);
    const float scale_z = 0.5f * (scale_x + scale_y) * 50.0f;

    Vector3f pos = camera_.GetPosition();
    pos += scale_x * screen_distance.X() * camera_.GetRight();
    pos += scale_y * screen_distance.Y() * camera_.GetUp();
    pos += scale_z * screen_distance.Z() * camera_.GetLook();
    camera_.LookAt(pos, pos + camera_.GetLook(), camera_.GetUp());
    camera_option_ = std::monostate();
}

void RenderEngine::RotateCameraAroundScene(const Vector3f& euler)
{
    Quaternion quat_x(camera_.GetRight(), euler.X());
    Quaternion quat_y(Vector3f::UnitY(), euler.Y());
    Quaternion quat = quat_y.Cross(quat_x);
    Vector3f center = scene_.Bounds().center;
    Vector3f pos = quat.RotateVector(camera_.GetPosition() - center) + center;
    Vector3f look = quat.RotateVector(camera_.GetLook());
    Vector3f up = quat.RotateVector(camera_.GetUp());
    camera_.LookAt(pos, pos + look, up);
    camera_option_ = std::monostate();
}

//==============================================================================
// Get Render Settings
//==============================================================================

RenderMode RenderEngine::GetRenderMode() const
{
    return render_mode_;
}

TextureFilter RenderEngine::GetTextureFilter() const
{
    return tex_filter_;
}

float RenderEngine::GetShadowResolutionScale() const
{
    return shadow_resolution_scale_;
}

ShadowFilter RenderEngine::GetShadowFilter() const
{
    return shadow_filter_;
}

int RenderEngine::GetNumberOfRenderThreads() const
{
    return render_states_.num_threads;
}

Vector3f RenderEngine::GetBackgroundColor() const
{
    return render_states_.background_color;
}

std::uint8_t RenderEngine::GetAntialias() const
{
    return render_states_.antialias;
}

bool RenderEngine::IsBackfaceCullingEnabled() const
{
    return render_states_.backface_culling;
}

bool RenderEngine::IsOITEnabled() const
{
    return render_states_.oit;
}

bool RenderEngine::IsACESEnabled() const
{
    return render_states_.aces;
}

bool RenderEngine::IsGammaCorrectionEnabled() const
{
    return render_states_.gamma_correction;
}

//==============================================================================
// Set Render Settings
//==============================================================================

void RenderEngine::SetRenderMode(RenderMode render_mode)
{
    render_mode_ = render_mode;
}

void RenderEngine::SetTextureFilter(TextureFilter tex_filter)
{
    tex_filter_ = tex_filter;
}

void RenderEngine::SetShadowResolutionScale(float scale)
{
    shadow_resolution_scale_ = Math::Clamp(scale, 0.1f, 10.f);
}

void RenderEngine::SetShadowFilter(ShadowFilter shadow_filter)
{
    shadow_filter_ = shadow_filter;
}

void RenderEngine::SetNumberOfRenderThreads(int num)
{
    render_states_.num_threads = std::max(num, 1);
}

void RenderEngine::SetBackgroundColor(const Vector3f& color)
{
    render_states_.background_color = color;
}

void RenderEngine::SetAntialias(std::uint8_t antialias)
{
    render_states_.antialias = antialias;
}

void RenderEngine::EnableBackfaceCulling(bool enable)
{
    render_states_.backface_culling = enable;
}

void RenderEngine::EnableOIT(bool enable)
{
    render_states_.oit = enable;
}

void RenderEngine::EnableACES(bool enable)
{
    render_states_.aces = enable;
}

void RenderEngine::EnableGammaCorrection(bool enable)
{
    render_states_.gamma_correction = enable;
}

//==============================================================================
// Render Update
//==============================================================================

void RenderEngine::ObtainRenderConfig(RenderConfig& config) const
{
    config.ibl_directory = GetIBLDirectory();
	config.ibl_specular_scale = GetIBLSpecularScale();
    config.floor_visibility = IsSceneFloorVisible();
    config.camera = CurrentCameraOption();
    config.tex_filter = GetTextureFilter();
    config.shadow_resolution_scale = GetShadowResolutionScale();
    config.shadow_filter = GetShadowFilter();
    config.background_color = GetBackgroundColor();
    config.ecsaa = (GetAntialias() & RenderAntialias_ECSAA);
    config.msaa = (GetAntialias() & RenderAntialias_MSAA);
    config.ssaa = (GetAntialias() & RenderAntialias_SSAA);
    config.backface_culling = IsBackfaceCullingEnabled();
    config.oit = IsOITEnabled();
    config.aces = IsACESEnabled();
}

void RenderEngine::ApplyRenderConfig(const RenderConfig& config)
{
    std::uint8_t antialias = 0;
    if (config.ecsaa) { antialias |= RenderAntialias_ECSAA; }
    if (config.msaa) { antialias |= RenderAntialias_MSAA; }
    if (config.ssaa) { antialias |= RenderAntialias_SSAA; }
    LoadIBLFromDirectory(config.ibl_directory);
	SetIBLSpecularScale(config.ibl_specular_scale);
    SetSceneFloorVisibility(config.floor_visibility);
    SwitchCamera(config.camera);
    SetTextureFilter(config.tex_filter);
    SetShadowResolutionScale(config.shadow_resolution_scale);
    SetShadowFilter(config.shadow_filter);
    SetBackgroundColor(config.background_color);
    SetAntialias(antialias);
    EnableBackfaceCulling(config.backface_culling);
    EnableOIT(config.oit);
    EnableACES(config.aces);
}

void RenderEngine::UpdateCameraViewAndLens()
{
    camera_.UpdateView();
    const float screen_w = render_states_.render_size.X() * 1.0f;
    const float screen_h = render_states_.render_size.Y() * 1.0f;
    const float aspect_ratio = screen_w / screen_h;
    const float far = SceneViewDepthRange().max * 1.1f;
    camera_.SetLens(45.0f, aspect_ratio, 0.1f, far);
}

void RenderEngine::UpdateRenderStates(RenderPostProcess post_process)
{
    render_states_.view = camera_.GetView();
    render_states_.projection = camera_.GetProjection();
    render_states_.post_process = std::move(post_process);
    render_states_.color_buffer = color_buffer_;
    render_states_.depth_buffer = depth_buffer_;
}

void RenderEngine::UpdateUniformsPerFrame()
{
    frame_uniforms_->eye_pos = camera_.GetPosition();
    frame_uniforms_->view = camera_.GetView();
    frame_uniforms_->view_proj = camera_.GetProjection() * camera_.GetView();
    frame_uniforms_->ambient_light = scene_.AmbientLightColor();
    frame_uniforms_->lights = scene_.Lights();
    frame_uniforms_->shadow_filter = shadow_filter_;
    frame_uniforms_->shadow_maps.clear();
    frame_uniforms_->shadow_transforms.clear();
    frame_uniforms_->ibl_data = (ibl_data_->brdf_lut.IsEmpty() ? nullptr : ibl_data_);

    if (frame_uniforms_->ambient_light.IsZero() &&
        frame_uniforms_->lights.empty() &&
        frame_uniforms_->ibl_data == nullptr)
    {
        frame_uniforms_->ambient_light = default_ambient_light_;
        frame_uniforms_->lights.push_back(default_directional_light_);
    }
}

FloatRange RenderEngine::SceneViewDepthRange() const
{
    Geom::AxisAlignedBox view_bounds = scene_.Bounds();
    Geom::TransformAxisAlignedBoundingBox(camera_.GetView(), view_bounds);
    float min_view_z = view_bounds.MinV().Z();
    float max_view_z = view_bounds.MaxV().Z();
    return {-max_view_z, -min_view_z};
}

//==============================================================================
// Submit Draw Call
//==============================================================================

template <typename U, typename VT, typename V, typename... Args, typename... Args2>
static RenderShaderVS WrapShaderVS(
    Vector4f (*func)(U&, VT&, V&, Args...), Args2&&... args)
{
    return [func, args...](const void* u, const void* vt, float* v) {
        return func(*(U*)u, *(VT*)vt, *(V*)v, args...); };
}

template <typename U, typename V, typename... Args, typename... Args2>
static RenderShaderFS WrapShaderFS(
    Vector4f (*func)(U&, V&, float, Args...), Args2&&... args)
{
    return [func, args...](const void* u, float* v, float d) {
        return func(*(U*)u, *(V*)v, d, args...); };
}

void RenderEngine::SubmitDrawCalls()
{
    obj_uniforms_.clear();
    obj_uniforms_.reserve(scene_.Objects().size());

    for (const std::shared_ptr<Object3D>& obj : scene_.Objects())
    {
        if (auto mesh_obj = DownCast<MeshInstance>(obj))
        {
            RenderDrawCall draw_call = CreateDrawCall(*mesh_obj);
            renderer_->AddDrawCall(std::move(draw_call));
        }
    }
}

RenderDrawCall RenderEngine::CreateDrawCall(const MeshInstance& obj)
{
    RenderDrawCall draw_call;

    draw_call.vertices = obj.MeshAsset()->VertexBuffer();
    draw_call.vertex_size = obj.MeshAsset()->VertexSize();
    draw_call.indices = obj.MeshAsset()->IndexBuffer();
    draw_call.num_triangles = obj.MeshAsset()->NumTriangles();
    draw_call.world_bounds = obj.WorldBounds();
    draw_call.transparent = false;
    draw_call.double_sided = false;
    draw_call.num_varyings = sizeof(RenderVaryings) / sizeof(float);
    draw_call.texcoord_index = offsetof(RenderVaryings, texcoord) / sizeof(float);
    draw_call.uniforms = &AllocObjectUniforms(obj);
    draw_call.shader_vs = CreateShaderVS(obj);
    draw_call.shader_fs = CreateShaderFS(obj);

    if (std::shared_ptr<MaterialBase> mat = obj.MaterialAsset())
    {
        draw_call.transparent = (mat->alpha_mode == MaterialAlphaMode::Blend);
        draw_call.double_sided = mat->double_sided;
    }

    return draw_call;
}

RenderUniformsPerObject& RenderEngine::AllocObjectUniforms(const MeshInstance& obj)
{
    assert(obj_uniforms_.size() < obj_uniforms_.capacity());
    RenderUniformsPerObject& uniforms = obj_uniforms_.emplace_back();
    uniforms.frame = frame_uniforms_;
    uniforms.world = obj.WorldTransform();
    uniforms.world_inv_transpose = uniforms.world.InverseTranspose();
    uniforms.world_view_proj = frame_uniforms_->view_proj * uniforms.world;
    uniforms.material = obj.MaterialAsset();
    uniforms.sampler.address_mode = TextureAddressMode::WRAP;
    uniforms.sampler.filter = tex_filter_;
    if (const auto* skeletal_obj = DownCast<SkeletalMeshInstance>(&obj))
        uniforms.skin_matrices = skeletal_obj->JointSkinMatrices();
    return uniforms;
}

RenderShaderVS RenderEngine::CreateShaderVS(const MeshInstance& obj) const
{
    if (DownCast<SkeletalMeshInstance>(&obj))
        return WrapShaderVS(&ShaderVS_Skin);
    return WrapShaderVS(&ShaderVS_Basic);
}

RenderShaderFS RenderEngine::CreateShaderFS(const MeshInstance& obj) const
{
    const std::shared_ptr<MaterialBase> mat = obj.MaterialAsset();
    
    if (DownCast<MaterialFloor>(mat))
    {
        if (render_mode_ == RenderMode::FinalColor)
            return CreateFloorShaderFS(obj);
        else if (render_mode_ == RenderMode::ViewDepth)
            return WrapShaderFS(&ShaderFS_ViewDepth, SceneViewDepthRange());
    }
    else if (DownCast<MaterialPhong>(mat))
    {
        if (render_mode_ == RenderMode::FinalColor)
            return WrapShaderFS(&ShaderFS_BlinnPhong);
        else if (render_mode_ == RenderMode::ViewDepth)
            return WrapShaderFS(&ShaderFS_ViewDepth, SceneViewDepthRange());
        else if (render_mode_ == RenderMode::Normal)
            return WrapShaderFS(&ShaderFS_Phong_Normal);
        else if (render_mode_ == RenderMode::Ambient)
            return WrapShaderFS(&ShaderFS_Phong_Ambient);
        else if (render_mode_ == RenderMode::Diffuse)
            return WrapShaderFS(&ShaderFS_Phong_Diffuse);
        else if (render_mode_ == RenderMode::Specular)
            return WrapShaderFS(&ShaderFS_Phong_Specular);
    }
    else if (DownCast<MaterialPbrMetallicRoughness>(mat))
    {
        if (render_mode_ == RenderMode::FinalColor)
            return WrapShaderFS(&ShaderFS_PBR_MetallicRoughness);
        else if (render_mode_ == RenderMode::ViewDepth)
            return WrapShaderFS(&ShaderFS_ViewDepth, SceneViewDepthRange());
        else if (render_mode_ == RenderMode::Normal)
            return WrapShaderFS(&ShaderFS_PBR_MR_Normal);
        else if (render_mode_ == RenderMode::Emissive)
            return WrapShaderFS(&ShaderFS_PBR_MR_Emissive);
        else if (render_mode_ == RenderMode::Occlusion)
            return WrapShaderFS(&ShaderFS_PBR_MR_Occlusion);
        else if (render_mode_ == RenderMode::BaseColor)
            return WrapShaderFS(&ShaderFS_PBR_MR_BaseColor);
        else if (render_mode_ == RenderMode::Metallic)
            return WrapShaderFS(&ShaderFS_PBR_MR_Metallic);
        else if (render_mode_ == RenderMode::Roughness)
            return WrapShaderFS(&ShaderFS_PBR_MR_Roughness);
    }
    else if (DownCast<MaterialPbrSpecularGlossiness>(mat))
    {
        if (render_mode_ == RenderMode::FinalColor)
            return WrapShaderFS(&ShaderFS_PBR_SpecularGlossiness);
        else if (render_mode_ == RenderMode::ViewDepth)
            return WrapShaderFS(&ShaderFS_ViewDepth, SceneViewDepthRange());
        else if (render_mode_ == RenderMode::Normal)
            return WrapShaderFS(&ShaderFS_PBR_SG_Normal);
        else if (render_mode_ == RenderMode::Emissive)
            return WrapShaderFS(&ShaderFS_PBR_SG_Emissive);
        else if (render_mode_ == RenderMode::Occlusion)
            return WrapShaderFS(&ShaderFS_PBR_SG_Occlusion);
        else if (render_mode_ == RenderMode::Albedo)
            return WrapShaderFS(&ShaderFS_PBR_SG_Albedo);
        else if (render_mode_ == RenderMode::Specular)
            return WrapShaderFS(&ShaderFS_PBR_SG_Specular);
        else if (render_mode_ == RenderMode::Glossiness)
            return WrapShaderFS(&ShaderFS_PBR_SG_Glossiness);
    }

    return WrapShaderFS(&ShaderFS_MissingMaterial);
}

RenderShaderFS RenderEngine::CreateFloorShaderFS(const MeshInstance& obj) const
{
    // floor normal is constant, so ambient/IBL irradiance is the same for
    // every pixel; sample it once here instead of per-pixel.
    const Vector3f normal_w = TransformationMatrix::TransformDirection(
        obj.WorldTransform().InverseTranspose(), Vector3f::UnitY());
    Vector3f ambient_term = frame_uniforms_->ambient_light;
    if (const RenderIBLData* ibl_data = frame_uniforms_->ibl_data.get())
    {
        SampleCubeTextureLod(ibl_data->irradiance_map, {TextureAddressMode::CLAMP,
            TextureFilter::LINEAR}, normal_w, 0.0f, ambient_term);
    }

    // uv 0..1 spans the whole floor, so world units per uv = floor width.
    const Geom::AxisAlignedBox& floor_bounds = obj.WorldBounds();
    const float world_units_per_uv = 2.0f * floor_bounds.extents.X();

    // scale down floor luma to <= scene log-average so the floor
    // doesn't shift ACES exposure regardless of screen coverage.
    float luma_scale = 1.0f;
    if (render_states_.aces)
    {
        Vector3f ref_c_light = ambient_term;
        for (const std::shared_ptr<LightBase>& light : frame_uniforms_->lights)
        {
            Vector3f light_vec, light_color;
            light->Sample(floor_bounds.center, light_vec, light_color);
            float n_dot_l = normal_w.Dot(light_vec);
            if (n_dot_l > 0.0f) { ref_c_light += n_dot_l * light_color; }
        }

        // unshadowed floor luma at floor center — shadow-independent reference.
        const auto& mat = static_cast<const MaterialFloor&>(*obj.MaterialAsset());
        const float ref_luma = static_cast<float>(Math::InvPI) *
            (ref_c_light * mat.color).Dot(Color::kLumaFactor);

        if (ref_luma > 0.0f)
        {
            constexpr float kAcesMiddleGrey = 0.18f;
            const float exposure = renderer_->GetComputedExposure();
            const float target_luma = kAcesMiddleGrey / std::max(exposure, 1.0f);
            luma_scale = std::min(target_luma / ref_luma, 1.0f);
        }
    }

    return WrapShaderFS(&ShaderFS_Floor,
        ambient_term, world_units_per_uv, luma_scale);
}

//==============================================================================
// Submit Shadow Caster
//==============================================================================

void RenderEngine::SubmitShadowCasters()
{
    if (shadow_resolution_scale_ > 0.0f && shadow_filter_ != ShadowFilter::None)
    {
        for (const std::shared_ptr<LightBase>& light : frame_uniforms_->lights)
        {
            if (auto dir_light = DownCast<DirectionalLight>(light))
            {
                SubmitShadowCaster(dir_light);
            }
            else if (auto point_light = DownCast<PointLight>(light))
            {
                // TODO: support point light shadow.
            }
        }
    }
}

void RenderEngine::SubmitShadowCaster(std::shared_ptr<DirectionalLight> light)
{
    using namespace TransformationMatrix;

    const Vector3f scene_center = scene_.Bounds().center;
    const float    scene_radius = scene_.Bounds().extents.Norm();
    const Vector3f eye_pos = scene_center - scene_radius * light->direction;
    const Matrix4f view = LookSquarelyAt(eye_pos, scene_center);
    const Vector3f view_scene_center = TransformPoint(view, scene_center);
    const Vector3f view_minv = view_scene_center - scene_radius;
    const Vector3f view_maxv = view_scene_center + scene_radius;
    const Matrix4f proj = OrthographicProjection(view_minv, view_maxv);
    const Matrix4f view_proj = proj * view;

    RenderUniformsShadowPass uniforms = {view, proj, view_proj};
    RenderShadowCaster caster;
    caster.view_proj = view_proj;
    caster.shader_vs = WrapShaderVS(&ShaderVS_ShadowPass, uniforms);
    caster.shadow_map = AllocShadowMap();

    frame_uniforms_->shadow_maps.push_back(caster.shadow_map);
    frame_uniforms_->shadow_transforms.push_back(NDCToUV() * view_proj);
    renderer_->AddShadowCaster(std::move(caster));
}

std::shared_ptr<Texture> RenderEngine::AllocShadowMap()
{
    auto iter = std::find_if(shadow_maps_.begin(), shadow_maps_.end(),
        [](const auto& tex) { return tex.use_count() == 1; });

    if (iter == shadow_maps_.end())
    {
        shadow_maps_.push_back(std::make_shared<Texture>());
        iter = shadow_maps_.end() - 1;
    }

    Vector2i resolution = render_states_.render_size;
    int cols = static_cast<int>(shadow_resolution_scale_ * resolution.X());
    int rows = static_cast<int>(shadow_resolution_scale_ * resolution.Y());
    (*iter)->CreateUninitialized(TexelFormat::FLOAT1, rows, cols);

    return *iter;
}

}}