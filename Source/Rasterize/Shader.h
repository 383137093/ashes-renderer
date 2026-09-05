#pragma once

#include <memory>
#include <vector>
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/ValueRange.h"
#include "Asset/Mesh.h"
#include "Asset/Light.h"
#include "Asset/Texture.h"
#include "Asset/Material.h"
#include "Asset/CubeTexture.h"

namespace Ashes { namespace Rasterize {

enum class ShadowFilter { None, Point, PCF };

struct RenderIBLData
{
    Texture     brdf_lut;
    CubeTexture irradiance_map;
    CubeTexture prefilter_map;
	float       specular_scale = 1.0f;
};

struct RenderUniformsPerFrame
{
    Vector3f                                eye_pos = Vector3f::Zero();
    Matrix4f                                view = Matrix4f::Identity();
    Matrix4f                                view_proj = Matrix4f::Identity();
    Vector3f                                ambient_light = Vector3f::Zero();
    std::vector<std::shared_ptr<LightBase>> lights;
    std::shared_ptr<RenderIBLData>          ibl_data;
    ShadowFilter                            shadow_filter = ShadowFilter::None;
    std::vector<std::shared_ptr<Texture>>   shadow_maps;
    std::vector<Matrix4f>                   shadow_transforms;
};

struct RenderUniformsShadowPass
{
    Matrix4f view;
    Matrix4f proj;
    Matrix4f view_proj;
};

struct RenderUniformsPerObject
{
    std::shared_ptr<RenderUniformsPerFrame> frame;
    Matrix4f                                world = Matrix4f::Identity();
    Matrix4f                                world_inv_transpose = Matrix4f::Identity();
    Matrix4f                                world_view_proj = Matrix4f::Identity();
    const Matrix4f*                         skin_matrices = nullptr;
    std::shared_ptr<MaterialBase>           material;
    TextureSamplerState                     sampler;
};

struct RenderVaryings
{
    Vector3f position_w;
    Vector3f normal_w;
    Vector3f tangent_w;
    float    tangent_sign;
    Vector2f texcoord;
};

//==============================================================================
// ShaderVS
//==============================================================================

Vector4f ShaderVS_Basic(
    const RenderUniformsPerObject& uniforms,
    const BasicVertex& vertex,
    RenderVaryings& varyings);

Vector4f ShaderVS_Skin(
    const RenderUniformsPerObject& uniforms,
    const SkinnedVertex& vertex,
    RenderVaryings& varyings);

Vector4f ShaderVS_ShadowPass(
    const RenderUniformsPerObject& uniforms,
    const float& vertex,
    RenderVaryings& varyings,
    const RenderUniformsShadowPass& shadow_uniforms);

//==============================================================================
// ShaderFS: Common
//==============================================================================

Vector4f ShaderFS_ViewDepth(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord,
    FloatRange view_depth_range);

Vector4f ShaderFS_Floor(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord,
    const Vector3f& ambient_term,
    float world_units_per_uv,
    float luma_scale);

Vector4f ShaderFS_MissingMaterial(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

//==============================================================================
// ShaderFS: Blinn-Phong Shading
//==============================================================================

Vector4f ShaderFS_Phong_Ambient(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_Phong_Diffuse(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_Phong_Specular(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_Phong_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_BlinnPhong(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

//==============================================================================
// ShaderFS: Physically-Based Rendering
//==============================================================================

Vector4f ShaderFS_PBR_MR_BaseColor(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MR_Metallic(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MR_Roughness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MR_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MR_Occlusion(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MR_Emissive(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Albedo(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Specular(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Glossiness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Occlusion(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SG_Emissive(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_MetallicRoughness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

Vector4f ShaderFS_PBR_SpecularGlossiness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord);

}}