#pragma once

#include <string>
#include "Math/Vector.h"
#include "Asset/Texture.h"

namespace Ashes {

enum class MaterialAlphaMode
{
    Opaque,
    Mask,
    Blend,
};

struct MaterialBase
{
    explicit MaterialBase() = default;
    virtual ~MaterialBase() = default;
    
    std::string       name;
    bool              double_sided = false;
    MaterialAlphaMode alpha_mode = MaterialAlphaMode::Opaque;
    float             alpha_cutoff = 0.0f;
};

struct MaterialFloor : MaterialBase
{
    Vector3f color = Vector3f::Ones();
};

struct MaterialPhong : MaterialBase
{
    struct SampleResult
    {
        Vector3f ambient;
        Vector4f diffuse;
        Vector3f specular;
        float    specular_exponent;
        Vector3f normal_ts;
    };
    
    void Sample(
        TextureSamplerState sampler,
        const Vector2f& uv, float duv,
        SampleResult& result) const;

    Vector3f ambient_factor = Vector3f::Ones();
    Vector4f diffuse_factor = Vector4f::Ones();
    Vector3f specular_factor = Vector3f::Zero();
    float    specular_exponent = 0.0f;

    const Texture* ambient_tex = nullptr;
    const Texture* diffuse_tex = nullptr;
    const Texture* specular_tex = nullptr;
    const Texture* normal_tex = nullptr;
    const Texture* height_tex = nullptr;
};

struct MaterialPbrMetallicRoughness : MaterialBase
{
    struct SampleResult
    {
        Vector4f base_color;  // F0 of metal or diffuse color of dielectric
        float    metallic;    // mix coefficient of metal and dielectric
        float    roughness;   // control the distribution shape of normal
        Vector3f normal_ts;   // tangent space normal
        float    occlusion;   // extra shadowing factor for indirect lighting
        Vector3f emissive;    // color and intensity of the emitted light
    };

    void Sample(
        TextureSamplerState sampler,
        const Vector2f& uv, float duv,
        SampleResult& result) const;

    Vector4f base_color_factor = Vector4f::Ones();
    float    metallic_factor = 1.0f;
    float    roughness_factor = 1.0f;
    Vector3f emissive_factor = Vector3f::Zero();
    
    const Texture* base_color_tex = nullptr;          // XYZW = (base_color.RGBA)
    const Texture* metallic_roughness_tex = nullptr;  // XYZ  = (*, roughness, metallic)
    const Texture* normal_tex = nullptr;              // XYZ  = (normal_ts.XYZ)
    const Texture* occlusion_tex = nullptr;           // X    = (occlusion)
    const Texture* emissive_tex = nullptr;            // XYZ  = (emissive.RGB)
};

struct MaterialPbrSpecularGlossiness : MaterialBase
{
    struct SampleResult
    {
        Vector4f diffuse;     // reflected diffuse color
        Vector3f specular;    // reflectance value at normal incidence (F0)
        float    glossiness;  // defined as glossiness = 1 - roughness
        Vector3f normal_ts;   // tangent space normal
        float    occlusion;   // extra shadowing factor for indirect lighting
        Vector3f emissive;    // color and intensity of the emitted light
    };

    void Sample(
        TextureSamplerState sampler,
        const Vector2f& uv, float duv,
        SampleResult& result) const;

    Vector4f diffuse_factor = Vector4f::Ones();
    Vector3f specular_factor = Vector3f::Ones();
    float    glossiness_factor = 1.0f;
    Vector3f emissive_factor = Vector3f::Zero();

    const Texture* diffuse_tex = nullptr;              // XYZW = (diffuse.RGBA)
    const Texture* specular_glossiness_tex = nullptr;  // XYZW = (specular.RGB, glossiness)
    const Texture* normal_tex = nullptr;               // XYZ  = (normal_ts.XYZ)
    const Texture* occlusion_tex = nullptr;            // X    = (occlusion)
    const Texture* emissive_tex = nullptr;             // XYZ  = (emissive.RGB)
};

}