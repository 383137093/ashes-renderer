#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include "Asset/AssetContent.h"
#include "Rasterize/Scene.h"
#include "Rasterize/Shader.h"
#include "Rasterize/Renderer.h"
#include "Rasterize/RenderConfig.h"

namespace Ashes { namespace Rasterize {

enum class RenderMode
{
    FinalColor, ViewDepth,
    Normal, Emissive, Occlusion,     // Common Material
    Ambient, Diffuse,                // Blinn-Phong
    BaseColor, Metallic, Roughness,  // PBR Metallic-Roughness
    Albedo, Specular, Glossiness,    // PBR Specular-Glossiness
};

class RenderEngine
{
public:
    
    RenderEngine();
    RenderEngine(const RenderEngine&) = delete;
    ~RenderEngine();
    RenderEngine& operator = (const RenderEngine&) = delete;

    void Initialize(int width, int height, std::shared_ptr<ThreadPool> thread_pool);
    bool LoadSceneFromObj(const std::filesystem::path& filename);
    bool LoadSceneFromGltf(const std::filesystem::path& filename);
    void UnloadScene();
    void RenderScene(RenderPostProcess post_process);

    // Scene Entities
    bool IsSceneFloorVisible() const;
    void SetSceneFloorVisibility(bool visible);
    void TickScene(float delta);

    // Lighting
    std::filesystem::path GetIBLDirectory() const;
    float GetIBLSpecularScale() const;
    void LoadIBLFromDirectory(const std::filesystem::path& directory);
    void SetIBLSpecularScale(float scale);
    void SetDefaultLight(float ambient, float directional);

    // Scene Movie & Object Animation
    std::vector<std::string> MovieDisplayNames() const;
    std::vector<std::vector<std::string>> AnimationDisplayNames() const;
    int NumAnimatableObjects() const;
    int CurrentPlayingMovieIndex() const;
    int CurrentPlayingAnimationIndex(int obj_idx) const;
    void SetPlayingMovie(int movie_idx);
    void SetPlayingAnimation(int obj_idx, int anim_idx);

    // Camera
    int NumCustomCameras() const;
    CameraOption CurrentCameraOption() const;
    void SwitchToCustomCamera(int idx);
    void SwitchToStandardViewCamera(StandardViewType type);
    void SwitchCamera(CameraOption option);
    void MoveCameraInScreenSpace(const Vector3f& screen_distance);
    void RotateCameraAroundScene(const Vector3f& euler);

    // Get Render Settings
    RenderMode GetRenderMode() const;
    TextureFilter GetTextureFilter() const;
    float GetShadowResolutionScale() const;
    ShadowFilter GetShadowFilter() const;
    int GetNumberOfRenderThreads() const;
    Vector3f GetBackgroundColor() const;
    std::uint8_t GetAntialias() const;
    bool IsBackfaceCullingEnabled() const;
    bool IsOITEnabled() const;
    bool IsACESEnabled() const;
    bool IsGammaCorrectionEnabled() const;

    // Set Render Settings
    void SetRenderMode(RenderMode render_mode);
    void SetTextureFilter(TextureFilter tex_filter);
    void SetShadowResolutionScale(float scale);
    void SetShadowFilter(ShadowFilter shadow_filter);
    void SetNumberOfRenderThreads(int num);
    void SetBackgroundColor(const Vector3f& color);
    void SetAntialias(std::uint8_t antialias);
    void EnableBackfaceCulling(bool enable);
    void EnableOIT(bool enable);
    void EnableACES(bool enable);
    void EnableGammaCorrection(bool enable);

private:

    void ObtainRenderConfig(RenderConfig& config) const;
    void ApplyRenderConfig(const RenderConfig& config);

    void UpdateCameraViewAndLens();
    void UpdateRenderStates(RenderPostProcess post_process);
    void UpdateUniformsPerFrame();
    FloatRange SceneViewDepthRange() const;

    void SubmitDrawCalls();
    RenderDrawCall CreateDrawCall(const MeshInstance& obj);
    RenderUniformsPerObject& AllocObjectUniforms(const MeshInstance& obj);
    RenderShaderVS CreateShaderVS(const MeshInstance& obj) const;
    RenderShaderFS CreateShaderFS(const MeshInstance& obj) const;
    RenderShaderFS CreateFloorShaderFS(const MeshInstance& obj) const;

    void SubmitShadowCasters();
    void SubmitShadowCaster(std::shared_ptr<DirectionalLight> light);
    std::shared_ptr<Texture> AllocShadowMap();

private:

    std::unique_ptr<Renderer> renderer_;
    Scene                     scene_;
    std::vector<int>          animatable_objs_;
    Camera                    camera_;
    CameraOption              camera_option_;

    // Lighting
    Vector3f                          default_ambient_light_ = Vector3f::Zero();
    std::shared_ptr<DirectionalLight> default_directional_light_;
    std::shared_ptr<RenderIBLData>    ibl_data_;

    // Render Settings
    RenderMode    render_mode_ = RenderMode::FinalColor;
    TextureFilter tex_filter_ = TextureFilter::POINT;
    float         shadow_resolution_scale_ = 0.0f;
    ShadowFilter  shadow_filter_ = ShadowFilter::None;
    RenderStates  render_states_;

    // Render Resources
    std::shared_ptr<RenderUniformsPerFrame> frame_uniforms_;
    std::vector<RenderUniformsPerObject>    obj_uniforms_;
    std::shared_ptr<Texture>                depth_buffer_;
    std::shared_ptr<Texture>                color_buffer_;
    VecSharedTexture                        shadow_maps_;
};

}}