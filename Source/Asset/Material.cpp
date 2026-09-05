#include "Material.h"

namespace Ashes {

static constexpr auto SampleTexture1f = SampleTexture<float>;
static constexpr auto SampleTexture3f = SampleTexture<Vector3f>;
static constexpr auto SampleTexture4f = SampleTexture<Vector4f>;

void MaterialPhong::Sample(
    TextureSamplerState sampler,
    const Vector2f& uv, float duv,
    SampleResult& result) const
{
    result.ambient = ambient_factor;
    result.diffuse = diffuse_factor;
    result.specular = specular_factor;
    result.specular_exponent = specular_exponent;
    result.normal_ts = Vector3f::Zero();

    Vector4f diffuse = Vector4f::Ones();
    if (diffuse_tex != nullptr)
    {
        SampleTexture4f(*diffuse_tex, sampler, uv, duv, diffuse);
        result.diffuse *= diffuse;
    }

    if (ambient_tex == diffuse_tex)
    {
        result.ambient *= diffuse.Head<3>();
    }
    else if (ambient_tex != nullptr)
    {
        Vector3f ambient = Vector3f::Ones();
        SampleTexture3f(*ambient_tex, sampler, uv, duv, ambient);
        result.ambient *= ambient;
    }

    if (normal_tex != nullptr)
        SampleTexture3f(*normal_tex, sampler, uv, duv, result.normal_ts);
}

void MaterialPbrMetallicRoughness::Sample(
    TextureSamplerState sampler,
    const Vector2f& uv, float duv,
    SampleResult& result) const
{
    result.base_color = base_color_factor;
    result.metallic = metallic_factor;
    result.roughness = roughness_factor;
    result.normal_ts = Vector3f::Zero();
    result.occlusion = 1.0f;
    result.emissive = emissive_factor;

    if (base_color_tex != nullptr)
    {
        Vector4f color = Vector4f::Ones();
        SampleTexture4f(*base_color_tex, sampler, uv, duv, color);
        result.base_color *= color;
    }

    Vector3f orm = Vector3f::Ones();
    if (metallic_roughness_tex != nullptr)
    {
        SampleTexture3f(*metallic_roughness_tex, sampler, uv, duv, orm);
        result.metallic *= orm.Z();
        result.roughness *= orm.Y();
    }

    if (occlusion_tex == metallic_roughness_tex)
        result.occlusion = orm.X();
    else if (occlusion_tex != nullptr)
        SampleTexture1f(*occlusion_tex, sampler, uv, duv, result.occlusion);

    if (normal_tex != nullptr)
        SampleTexture3f(*normal_tex, sampler, uv, duv, result.normal_ts);

    if (emissive_tex != nullptr)
        SampleTexture3f(*emissive_tex, sampler, uv, duv, result.emissive);
}

void MaterialPbrSpecularGlossiness::Sample(
    TextureSamplerState sampler,
    const Vector2f& uv, float duv,
    SampleResult& result) const
{
    result.diffuse = diffuse_factor;
    result.specular = specular_factor;
    result.glossiness = glossiness_factor;
    result.normal_ts = Vector3f::Zero();
    result.occlusion = 1.0f;
    result.emissive = emissive_factor;

    if (diffuse_tex != nullptr)
    {
        Vector4f diffuse = Vector4f::Ones();
        SampleTexture4f(*diffuse_tex, sampler, uv, duv, diffuse);
        result.diffuse *= diffuse;
    }

    if (specular_glossiness_tex != nullptr)
    {
        Vector4f sg = Vector4f::Ones();
        SampleTexture4f(*specular_glossiness_tex, sampler, uv, duv, sg);
        result.specular *= sg.Head<3>();
        result.glossiness *= sg.W();
    }

    if (normal_tex != nullptr)
        SampleTexture3f(*normal_tex, sampler, uv, duv, result.normal_ts);

    if (occlusion_tex != nullptr)
        SampleTexture1f(*occlusion_tex, sampler, uv, duv, result.occlusion);

    if (emissive_tex != nullptr)
        SampleTexture3f(*emissive_tex, sampler, uv, duv, result.emissive);
}

}