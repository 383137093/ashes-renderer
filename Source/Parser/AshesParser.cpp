#include "AshesParser.h"

#include <memory>
#include <vector>
#include <fstream>
#include <string_view>

#include "Parser/JsonProxy.h"
#include "Parser/ObjParser.h"
#include "Utility/StringAlgo.h"
#include "Platform/GenericWindow.h"

namespace Ashes { namespace AshesParser {

using namespace std::string_view_literals;

static void ParseCameras(JsonProxy json_cameras, std::vector<Camera>& cameras)
{
    for (JsonProxy json_camera : json_cameras)
    {
        Vector3f position = json_camera.ChildValue("position", Vector3f::Zero());
        Vector3f look = json_camera.ChildValue("look", -Vector3f::UnitZ());
        Camera camera;
        camera.LookAlong(position, look, Vector3f::UnitY());
        cameras.push_back(camera);
    }
}

static void ParseLights(JsonProxy json_lights, VecSharedLight& lights)
{
    for (JsonProxy json_light : json_lights)
    {
        std::string_view type = json_light.ChildValue("type", ""sv);
        Vector3f color = json_light.ChildValue("color", Vector3f::Zero());
        float intensity = json_light.ChildValue("intensity", 1.0f);
        Vector3f position = json_light.ChildValue("position", Vector3f::Zero());
        Vector3f direction = json_light.ChildValue("direction", -Vector3f::UnitZ());

        if (type == "ambient")
        {
            auto light = std::make_shared<AmbientLight>();
            light->color = intensity * color;
            lights.push_back(light);
        }
        else if (type == "directional")
        {
            auto light = std::make_shared<DirectionalLight>();
            light->color = intensity * color;
            light->direction = direction.Normalized();
            lights.push_back(light);
        }
        else if (type == "point")
        {
            auto light = std::make_shared<PointLight>();
            light->color = intensity * color;
            light->position = position;
            lights.push_back(light);
        }
    }
}

template <typename T>
static void ParseRenderOption(JsonProxy json, const char* key, T& option)
{
    option = json.ChildValue(key, option);
}

template <typename T, typename Parser = T(*)(std::string_view, T&)>
static void ParseRenderOption(JsonProxy json, const char* key, T& option, Parser parser)
{
    parser(json.ChildValue(key, ""sv), option);
}

static void ParsePath(std::string_view sv, std::filesystem::path& dir)
{
    if (!sv.empty())
    {
        dir = GenericWindow::CurrentPath().append(sv).lexically_normal();
    }
}

static void ParseCameraOption(JsonProxy json, Rasterize::CameraOption& camera)
{
    const int custom_camera_idx = json.Value(-1);
    std::string_view sv = json.Value(""sv);
    if (custom_camera_idx >= 0)    { camera = custom_camera_idx; }
    else if (sv == "Front")        { camera = StandardViewType::Front; }
    else if (sv == "Back")         { camera = StandardViewType::Back; }
    else if (sv == "Left")         { camera = StandardViewType::Left; }
    else if (sv == "Right")        { camera = StandardViewType::Right; }
    else if (sv == "Top")          { camera = StandardViewType::Top; }
    else if (sv == "Bottom")       { camera = StandardViewType::Bottom; }
    else if (sv == "TopFrontLeft") { camera = StandardViewType::TopFrontLeft; }
}

static void ParseTextureFilter(std::string_view sv, TextureFilter& tex_filter)
{
    if (sv == "POINT")                  { tex_filter = TextureFilter::POINT; }
    else if (sv == "POINT_MIP_POINT")   { tex_filter = TextureFilter::POINT_MIP_POINT; }
    else if (sv == "POINT_MIP_LINEAR")  { tex_filter = TextureFilter::POINT_MIP_LINEAR; }
    else if (sv == "LINEAR")            { tex_filter = TextureFilter::LINEAR; }
    else if (sv == "LINEAR_MIP_POINT")  { tex_filter = TextureFilter::LINEAR_MIP_POINT; }
    else if (sv == "LINEAR_MIP_LINEAR") { tex_filter = TextureFilter::LINEAR_MIP_LINEAR; }
}

static void ParseShadowFilter(std::string_view sv, Rasterize::ShadowFilter& shadow_filter)
{
    if (sv == "Point")    { shadow_filter = Rasterize::ShadowFilter::Point; }
    else if (sv == "PCF") { shadow_filter = Rasterize::ShadowFilter::PCF; }
}

static void ParseRenderConfig(JsonProxy json, Rasterize::RenderConfig& config)
{
    ParseRenderOption(json, "ibl_directory", config.ibl_directory, &ParsePath);
    ParseRenderOption(json, "ibl_specular_scale", config.ibl_specular_scale);
    ParseCameraOption(json.Child("camera"), config.camera);
    ParseRenderOption(json, "floor_visibility", config.floor_visibility);
    ParseRenderOption(json, "tex_filter", config.tex_filter, &ParseTextureFilter);
    ParseRenderOption(json, "shadow_resolution_scale", config.shadow_resolution_scale);
    ParseRenderOption(json, "shadow_filter", config.shadow_filter, &ParseShadowFilter);
    ParseRenderOption(json, "background_color", config.background_color);
    ParseRenderOption(json, "ecsaa", config.ecsaa);
    ParseRenderOption(json, "msaa", config.msaa);
    ParseRenderOption(json, "ssaa", config.ssaa);
    ParseRenderOption(json, "backface_culling", config.backface_culling);
    ParseRenderOption(json, "oit", config.oit);
    ParseRenderOption(json, "aces", config.aces);
}

static void LoadExtendAssetFromAshes(
    JsonProxy json_doc,
    AssetContent& assets,
    Rasterize::RenderConfig& config)
{
    ParseCameras(json_doc.ChildArray("cameras"), assets.cameras);
    ParseLights(json_doc.ChildArray("lights"), assets.lights);
    ParseRenderConfig(json_doc.ChildObject("config"), config);
}

}

bool LoadExtendAssetFromAshes(
    const std::filesystem::path& filename,
    AssetContent& assets,
    Rasterize::RenderConfig& config)
{
    std::ifstream ifs(filename);
    if (!ifs.is_open())
        return false;

    const nlohmann::json json_doc = nlohmann::json::parse(ifs, nullptr, false);
    if (!json_doc.is_object())
        return false;

    AshesParser::LoadExtendAssetFromAshes(json_doc, assets, config);
    return true;
}

}