#pragma once

#include <cstdint>
#include <variant>
#include "Math/Vector.h"
#include "Asset/Camera.h"
#include "Asset/Texture.h"
#include "Rasterize/Shader.h"

namespace Ashes { namespace Rasterize {

using CameraOption = std::variant<std::monostate, StandardViewType, int>;

struct RenderConfig
{
    std::filesystem::path ibl_directory;
    float                 ibl_specular_scale = 1.0f;
    bool                  floor_visibility = false;
    CameraOption          camera;
    TextureFilter         tex_filter = TextureFilter::POINT;
    float                 shadow_resolution_scale = 0.0f;
    ShadowFilter          shadow_filter = ShadowFilter::None;
    Vector3f              background_color = Vector3f::Zero();
    bool                  ecsaa = false;
    bool                  msaa = false;
    bool                  ssaa = false;
    bool                  backface_culling = false;
    bool                  oit = false;
    bool                  aces = false;
};

}}