#pragma once

#include <mutex>
#include <atomic>
#include <future>
#include <vector>
#include <cstdint>
#include <utility>
#include <filesystem>
#include <condition_variable>
#include "Utility/ThreadPool.h"
#include "Platform/GenericWindow.h"
#include "Platform/GenericMenuParams.h"
#include "Rasterize/RenderEngine.h"

namespace Ashes { namespace Rasterize {

class RenderApp
{
public:

    RenderApp();
    RenderApp(const RenderApp&) = delete;
    ~RenderApp();
    RenderApp& operator = (const RenderApp&) = delete;

    void Run(int width, int height, const std::filesystem::path& filename);

private:
    
    using ShadowOptions = std::pair<float, ShadowFilter>;

    struct EngineSettings
    {
        std::filesystem::path scene_filename;
        std::filesystem::path ibl_directory;
        bool                  floor_visibility = false;
        int                   playing_movie = -1;
        std::vector<int>      playing_anims;
        CameraOption          camera_option;
        Vector3f              camera_translation = Vector3f::Zero();
        Vector3f              camera_rotation = Vector3f::Zero();
        RenderMode            render_mode = RenderMode::FinalColor;
        TextureFilter         tex_filter = TextureFilter::POINT;
        ShadowOptions         shadow = {0.0f, ShadowFilter::None};
        int                   num_render_threads = 1;
        Vector3f              background_color = Vector3f::Zero();
        std::uint8_t          antialias = 0;
        bool                  backface_culling = false;
        bool                  oit = false;
        bool                  aces = false;
        bool                  gamma_correction = false;
    };

    // Render Engine
    void RenderThreadFunc();
    void LoadSceneFromFile(const std::filesystem::path& filename);
    bool WaitRenderingContinue();

    // Render Window
    void CreateWindow(int width, int height);
    void CreateWindowMenu();
    void UpdateWindowTitle(float fps);
    void OnPostProcess(Vector3f* colors, int first, int last);
    void OnWindowClose();
    void OnMouseDragL(float x, float y);
    void OnMouseDragR(float x, float y);
    void OnMouseWheel(float x);
    void OnChooseModelFile();
    
    // Make Menu Params
    void MakeFileMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakePauseMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeCameraMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeAnimationMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeEnvirMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeModeMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeQualityMenuParams(GenericMenuParams& params, EngineSettings& settings);
    void MakeMiscMenuParams(GenericMenuParams& params, EngineSettings& settings);

    // Engine Settings
    void ObtainEngineSettings(EngineSettings& settings) const;
    void ApplyEngineSettings(const EngineSettings& settings);
    void StageModifiedEngineSettings();
    void CommitStagedEngineSettings();
    void FetchCommittedEngineSettings();

private:
    
    std::shared_ptr<ThreadPool> thread_pool_;
    RenderEngine                render_engine_;
    RenderPostProcess           post_process_;
    GenericWindow               render_wnd_;
    std::future<void>           render_thread_;
    std::condition_variable     rendering_continue_;
    std::atomic_bool            rendering_paused_ = false;
    std::atomic_bool            rendering_stopped_ = false;
    std::atomic<float>          rendering_fps_ = 0.0f;

    bool           new_settings_staged_ = false;
    bool           new_settings_committed_ = false;
    bool           new_scene_loaded_ = false;
    std::mutex     settings_mutex_;
    EngineSettings modified_settings_;
    EngineSettings staged_settings_;
    EngineSettings committed_settings_;
};

}}