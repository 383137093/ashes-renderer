#include "Shader.h"
#include <cmath>
#include <cassert>
#include <algorithm>
#include "Math/MathMisc.h"
#include "Math/ColorMisc.h"
#include "Math/Transform.h"
#include "Utility/UtilityMisc.h"

namespace Ashes { namespace Rasterize {

using namespace TransformationMatrix;

//==============================================================================
// ShaderVS
//==============================================================================

static Matrix4f CalcSkinMatrix(
    const Matrix4f* joint_matrices,
    const SkinnedVertex& vertex)
{
    const Matrix4f& m0 = joint_matrices[vertex.joints[0]];
    const Matrix4f& m1 = joint_matrices[vertex.joints[1]];
    const Matrix4f& m2 = joint_matrices[vertex.joints[2]];
    const Matrix4f& m3 = joint_matrices[vertex.joints[3]];
    
    Matrix4f m;
    for (std::size_t i = 0; i < Matrix4f::kSize; ++i)
    {
        m[i] = vertex.weights[0] * m0[i] + vertex.weights[1] * m1[i]
             + vertex.weights[2] * m2[i] + vertex.weights[3] * m3[i];
    }
    
    return m;
}

Vector4f ShaderVS_Basic(
    const RenderUniformsPerObject& uniforms,
    const BasicVertex& vertex,
    RenderVaryings& varyings)
{
    varyings.position_w = TransformPoint(uniforms.world, vertex.position);
    varyings.normal_w = TransformVector(uniforms.world_inv_transpose, vertex.normal);
    varyings.tangent_w = TransformVector(uniforms.world, vertex.tangent.Head<3>());
    varyings.tangent_sign = vertex.tangent.W();
    varyings.texcoord = vertex.texcoord;
    return uniforms.world_view_proj * vertex.position.Join(1.0f);
}

Vector4f ShaderVS_Skin(
    const RenderUniformsPerObject& uniforms,
    const SkinnedVertex& vertex,
    RenderVaryings& varyings)
{
    Matrix4f skin_matrix = CalcSkinMatrix(uniforms.skin_matrices, vertex);
    Matrix4f world = uniforms.world * skin_matrix;
    Matrix4f world_inv_transpose = world.InverseTranspose();
    varyings.position_w = TransformPoint(world, vertex.position);
    varyings.normal_w = TransformVector(world_inv_transpose, vertex.normal);
    varyings.tangent_w = TransformVector(world, vertex.tangent.Head<3>());
    varyings.tangent_sign = vertex.tangent.W();
    varyings.texcoord = vertex.texcoord;
    return uniforms.frame->view_proj * varyings.position_w.Join(1.0f);
}

Vector4f ShaderVS_ShadowPass(
    const RenderUniformsPerObject& uniforms,
    const float& input,
    RenderVaryings& varyings,
    const RenderUniformsShadowPass& shadow_uniforms)
{
    if (uniforms.skin_matrices != nullptr)
    {
        const auto& vertex = reinterpret_cast<const SkinnedVertex&>(input);
        Matrix4f skin_matrix = CalcSkinMatrix(uniforms.skin_matrices, vertex);
        varyings.position_w = TransformPoint(skin_matrix, vertex.position);
        varyings.position_w = TransformPoint(uniforms.world, varyings.position_w);
        return shadow_uniforms.view_proj * varyings.position_w.Join(1.0f);
    }
    else
    {
        const auto& vertex = reinterpret_cast<const BasicVertex&>(input);
        varyings.position_w = TransformPoint(uniforms.world, vertex.position);
        return shadow_uniforms.view_proj * varyings.position_w.Join(1.0f);
    }
}

//==============================================================================
// ShaderFS: Utility
//==============================================================================

static Vector3f TangentFromNormal(const Vector3f& normal)
{
    Vector3f tangent1 = normal.Cross(Vector3f::UnitZ());
    Vector3f tangent2 = normal.Cross(Vector3f::UnitY());
    return tangent1.SquaredNorm() > tangent2.SquaredNorm() ? tangent1 : tangent2;
}

static Vector3f NormalSampleToWorldSpace(
    const Vector3f& sampled_normal,
    const Vector3f& unit_normal_w,
    const Vector3f& tangent_w, float tangent_sign)
{
    // uncompress each component.
    Vector3f normal_ts = 2.0f * sampled_normal - 1.0f;

    // build orthonormal basis.
    Vector3f N = unit_normal_w;
    Vector3f T = (tangent_w - tangent_w.Dot(N) * N).Normalized();
    Vector3f B = N.Cross(T) * (tangent_sign >= 0.0f ? 1.0f : -1.0f);

    // build transform matrix: it is a coordinate transform.
    Matrix3f TBN = {T.X(), B.X(), N.X(),
                    T.Y(), B.Y(), N.Y(),
                    T.Z(), B.Z(), N.Z()};

    // transform from tangent space to world space.
    return (TBN * normal_ts).Normalized();
}

static float CalcShadowFactorPoint(
    const Texture& shadow_map,
    const Matrix4f& shadow_transform,
    float shadow_bias,
    const Vector3f& position_w)
{
    Vector3f shadow_position = TransformPointDivideW(shadow_transform, position_w);
    Vector2f shadow_uv       = shadow_position.Head<2>();
    float    shadow_depth    = shadow_position.Z() + shadow_bias;

    float min_shadow_depth = 0.0f;
    SampleTexture<float>(shadow_map, TextureSamplerState(),
        shadow_uv, 0.0f, min_shadow_depth);

    return shadow_depth >= min_shadow_depth ? 1.0f : 0.0f;
}

static float CalcShadowFactorPCF(
    const Texture& shadow_map,
    const Matrix4f& shadow_transform,
    float shadow_bias,
    const Vector3f& position_w)
{
    Vector3f shadow_position = TransformPointDivideW(shadow_transform, position_w);
    Vector2f shadow_uv       = shadow_position.Head<2>();
    float    shadow_depth    = shadow_position.Z() + shadow_bias;

    const float shadow_du = 1.0f / shadow_map.Width();
    const float shadow_dv = 1.0f / shadow_map.Height();
    const Vector2f shadow_uv_offsets[] = {
        {-shadow_du, -shadow_dv}, {0.0f, -shadow_dv}, {+shadow_du, -shadow_dv},
        {-shadow_du,  0.0f},      {0.0f,  0.0f},      {+shadow_du,  0.0f},
        {-shadow_du, +shadow_dv}, {0.0f, +shadow_dv}, {+shadow_du, +shadow_dv},};
    
    float shadow_factor = 0.0f;
    for (const Vector2f& offset : shadow_uv_offsets)
    {
        float min_shadow_depth = 0.0f;
        SampleTexture<float>(shadow_map, TextureSamplerState(),
            shadow_uv + offset, 0.0f, min_shadow_depth);
        shadow_factor += (shadow_depth >= min_shadow_depth ? 1.0f : 0.0f);
    }
    return shadow_factor / 9.0f;
}

static float CalcShadowFactorFromLight(
    const RenderUniformsPerFrame& frame,
    std::size_t light_idx,
    const Vector3f& position_w)
{
    if (frame.shadow_filter != ShadowFilter::None)
    {
        const Texture& tex = *frame.shadow_maps[light_idx];
        const Matrix4f& transform = frame.shadow_transforms[light_idx];
        const float bias = 0.01f;
        return frame.shadow_filter == ShadowFilter::PCF
            ? CalcShadowFactorPCF(tex, transform, bias, position_w)
            : CalcShadowFactorPoint(tex, transform, bias, position_w);
    }
    return 1.0f;
}

//==============================================================================
// ShaderFS: Common
//==============================================================================

Vector4f ShaderFS_ViewDepth(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float /*dtexcoord*/,
    FloatRange view_depth_range)
{
    float depth = -TransformPoint(uniforms.frame->view, varyings.position_w).Z();
    float min_depth = view_depth_range.min;
    float max_depth = view_depth_range.max;
    float uniform_depth = (depth - min_depth) / (max_depth - min_depth);
    return {uniform_depth, uniform_depth, uniform_depth, 1.0f};
}

// procedural floor grid blend factor (0 = bare floor, 1 = grid-line).
static float CalcFloorGridLineBlend(
    const Vector3f& pos_w, 
    float dtexcoord, 
    float world_units_per_uv)
{
    // grid_size scales with model so floor always shows a fixed number of cells.
    constexpr float kGridsPerFloor = 10.0f;
    const float world_grid_size = world_units_per_uv / kGridsPerFloor;
    const float inv_world_grid_size = 1.0f / world_grid_size;

    // 'dtexcoord' is the mip footprint (uv area / pixel area).
    const float uv_pixel_size = std::sqrt(std::max(dtexcoord, 0.0f));
    // world units spanned by one pixel.
    const float world_pixel_size = uv_pixel_size * world_units_per_uv;
    // footprint in grid-period units (world units / grid size).
    const float pixel_size = world_pixel_size * inv_world_grid_size;

    // anti-aliasing coverage of one grid-line axis, with energy-conserving
    // darkening. all values are in grid-period units.
    auto CalcCoverage = [](float period_coord, float pixel_size)
    {
        // 0.5% of one grid period.
        constexpr float kLineHalfWidthFrac = 0.005f;
        // half-width clamped to >= 1 pixel so thin lines don't vanish.
        float line_half_width = std::max(kLineHalfWidthFrac, pixel_size);
        // smooth half-width around pixel for anti-aliasing (avoids divide-by-zero).
        float aa_half_width = 1.5f * std::max(pixel_size, 1e-6f);
        // distance from pixel center to nearest grid-line center.
        float dist = std::fabs(period_coord - std::nearbyint(period_coord));

        // approximate coverage: how much of the pixel is covered by grid-line.
        // grid-line: [-line_half_width, line_half_width]
        // pixel: [dist - aa_half_width, dist + aa_half_width]
        // cover ~= len(grid-line ∩ pixel) / len(pixel), exact when pixel <= line.
        float cover = Math::Saturate(
            (line_half_width - (dist - aa_half_width)) / (2.0f * aa_half_width));

        // darken lines widened beyond their true width, keeping average
        // brightness stable as pixels grow.
        float energy = std::max(kLineHalfWidthFrac / line_half_width, 0.1f);
        return cover * energy;
    };

    const float cover_x = CalcCoverage(pos_w.X() * inv_world_grid_size, pixel_size);
    const float cover_z = CalcCoverage(pos_w.Z() * inv_world_grid_size, pixel_size);
    return cover_x + cover_z - cover_x * cover_z;
}

Vector4f ShaderFS_Floor(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord,
    const Vector3f& ambient_term,
    float world_units_per_uv,
    float luma_scale)
{
    const RenderUniformsPerFrame& frame = *uniforms.frame;
    auto& mat = static_cast<MaterialFloor&>(*uniforms.material);
    Vector3f normal = varyings.normal_w.Normalized();
    Vector3f c_light = ambient_term;

    for (std::size_t i = 0; i < frame.lights.size(); ++i)
    {
        Vector3f light_vec, light_color;
        frame.lights[i]->Sample(varyings.position_w, light_vec, light_color);

        float n_dot_l = normal.Dot(light_vec);
        if (n_dot_l <= 0.0f)
            continue;

        float shadow = CalcShadowFactorFromLight(frame, i, varyings.position_w);
        if (shadow <= 0.0f)
            continue;

        c_light += shadow * n_dot_l * light_color;
    }

    const float line_blend = CalcFloorGridLineBlend(
        varyings.position_w, dtexcoord, world_units_per_uv);
    const float line_scale = 1.0f - 0.75f * line_blend;
    const float scale = static_cast<float>(Math::InvPI) * luma_scale * line_scale;
    return (scale * c_light * mat.color).Join(1.0f);
}

Vector4f ShaderFS_MissingMaterial(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float /*dtexcoord*/)
{
    constexpr Vector4f kColorRed = {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr Vector4f kColorGray = {0.5f, 0.5f, 0.5f, 1.0f};
    const Matrix4f& view_proj = uniforms.frame->view_proj;
    Vector3f position = TransformPointDivideW(view_proj, varyings.position_w);
    float diagonal = (position.X() - position.Y() + 2.0f) * 0.5f;
    return static_cast<int>(diagonal * 100) % 2 ? kColorRed : kColorGray;
}

//==============================================================================
// ShaderFS: Blinn-Phong Shading
//==============================================================================

Vector4f ShaderFS_Phong_Ambient(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPhong&>(*uniforms.material);
    MaterialPhong::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.ambient.Join(1.0f);
}

Vector4f ShaderFS_Phong_Diffuse(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPhong&>(*uniforms.material);
    MaterialPhong::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.diffuse;
}

Vector4f ShaderFS_Phong_Specular(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPhong&>(*uniforms.material);
    MaterialPhong::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.specular.Join(1.0f);
}

Vector4f ShaderFS_Phong_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPhong&>(*uniforms.material);
    Vector3f normal = varyings.normal_w.Normalized();
    if (mat.normal_tex != nullptr)
    {
        MaterialPhong::SampleResult matsp;
        mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            TangentFromNormal(normal), 1.0f);
    }
    return (0.5f * (normal + Vector3f::Ones())).Join(1.0f);
}

Vector4f ShaderFS_BlinnPhong(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    const RenderUniformsPerFrame& frame = *uniforms.frame;
    auto& mat = static_cast<MaterialPhong&>(*uniforms.material);
    MaterialPhong::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);

    Vector3f diffuse  = matsp.diffuse.Head<3>();
    Vector3f normal   = varyings.normal_w.Normalized();
    Vector3f view_vec = (frame.eye_pos - varyings.position_w).Normalized();
    Vector3f c_final  = frame.ambient_light * matsp.ambient;
    
    if (mat.normal_tex != nullptr)
    {
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            TangentFromNormal(normal), 1.0f);
    }

    for (std::size_t i = 0; i < frame.lights.size(); ++i)
    {
        Vector3f light_vec, light_color;
        frame.lights[i]->Sample(varyings.position_w, light_vec, light_color);

        float n_dot_l = normal.Dot(light_vec);
        if (n_dot_l <= 0.0f)
            continue;

        float shadow = CalcShadowFactorFromLight(frame, i, varyings.position_w);
        if (shadow <= 0.0f)
            continue;

        Vector3f half_vec  = (view_vec + light_vec).Normalized();
        float    n_dot_h   = normal.Dot(half_vec);
        Vector3f term_diff = n_dot_l * diffuse;
        Vector3f term_spec = Vector3f::Zero();

        if (n_dot_h > 0.0f)
        {
            float specular_coef = std::pow(n_dot_h, matsp.specular_exponent);
            term_spec = specular_coef * matsp.specular;
        }

        c_final += shadow * light_color * (term_diff + term_spec);
    }
    
    return c_final.Join(matsp.diffuse.W());
}

//==============================================================================
// ShaderFS: Physically-Based Rendering
//==============================================================================

Vector4f ShaderFS_PBR_MR_BaseColor(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.base_color;
}

Vector4f ShaderFS_PBR_MR_Metallic(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return {matsp.metallic, matsp.metallic, matsp.metallic, 1.0f};
}

Vector4f ShaderFS_PBR_MR_Roughness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return {matsp.roughness, matsp.roughness, matsp.roughness, 1.0f};
}

Vector4f ShaderFS_PBR_MR_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    Vector3f normal = varyings.normal_w.Normalized();
    if (mat.normal_tex != nullptr)
    {
        MaterialPbrMetallicRoughness::SampleResult matsp;
        mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            varyings.tangent_w, varyings.tangent_sign);
    }
    return (0.5f * (normal + Vector3f::Ones())).Join(1.0f);
}

Vector4f ShaderFS_PBR_MR_Occlusion(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return {matsp.occlusion, matsp.occlusion, matsp.occlusion, 1.0f};
}

Vector4f ShaderFS_PBR_MR_Emissive(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.emissive.Join(1.0f);
}

Vector4f ShaderFS_PBR_SG_Albedo(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.diffuse;
}

Vector4f ShaderFS_PBR_SG_Specular(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.specular.Join(1.0f);
}

Vector4f ShaderFS_PBR_SG_Glossiness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return {matsp.glossiness, matsp.glossiness, matsp.glossiness, 1.0f};
}

Vector4f ShaderFS_PBR_SG_Normal(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    Vector3f normal = varyings.normal_w.Normalized();
    if (mat.normal_tex != nullptr)
    {
        MaterialPbrSpecularGlossiness::SampleResult matsp;
        mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            varyings.tangent_w, varyings.tangent_sign);
    }
    return (0.5f * (normal + Vector3f::Ones())).Join(1.0f);
}

Vector4f ShaderFS_PBR_SG_Occlusion(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return {matsp.occlusion, matsp.occlusion, matsp.occlusion, 1.0f};
}

Vector4f ShaderFS_PBR_SG_Emissive(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    return matsp.emissive.Join(1.0f);
}

static inline Vector3f FresnelSchlick(const Vector3f& f0, float cos)
{
    assert(0.0f <= cos && cos <= 1.0f);
    float factor = 1.0f - cos;
    float factor2 = factor * factor;
    float factor5 = factor * factor2 * factor2;
    return f0 + (Vector3f::Ones() - f0) * factor5;
}

static inline Vector3f FresnelSchlickRoughness(
    const Vector3f& f0, float cos, float roughness)
{
    assert(0.0f <= cos && cos <= 1.0f);
    float factor = 1.0f - cos;
    float factor2 = factor * factor;
    float factor5 = factor * factor2 * factor2;
    Vector3f f90 = (1.0f - roughness) * Vector3f::Ones();
    return f0 + (f90.CwiseMax(f0) - f0) * factor5;
}

static inline float NormalDistributionGGX(float n_dot_h, float alpha2)
{
    assert(0.0f < n_dot_h && n_dot_h <= 1.0f);
    float n_dot_h2 = n_dot_h * n_dot_h;
    float factor = n_dot_h2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (static_cast<float>(Math::PI) * factor * factor);
}

static inline float GeometrySmithGGX(float n_dot_l, float n_dot_v, float alpha2)
{
    // returns G / (4 * N·L * N·V) so that the final Cook–Torrance specular
    // contribution can be computed as: specular = D * F * GeometrySmithGGX
    assert(0.0f < n_dot_l && n_dot_l <= 1.0f);
    assert(0.0f < n_dot_v && n_dot_v <= 1.0f);
    float n_dot_l2 = n_dot_l * n_dot_l;
    float n_dot_v2 = n_dot_v * n_dot_v;
    float ggxl = n_dot_l + std::sqrt(alpha2 + (1 - alpha2) * n_dot_l2);
    float ggxv = n_dot_v + std::sqrt(alpha2 + (1 - alpha2) * n_dot_v2);
    return 1.0f / (ggxl * ggxv);
}

static inline Vector3f ShaderFS_PBR(
    const RenderUniformsPerFrame& frame,
    const Vector3f& position,
    const Vector3f& normal,
    const Vector3f& view_vec,
    float roughness,
    const Vector3f& f0,
    const Vector3f& c_diff)
{
    float    n_dot_v       = normal.Dot(view_vec);
    float    alpha         = std::max(roughness * roughness, 1e-4f);
    float    alpha2        = alpha * alpha;
    Vector3f c_diff_div_pi = c_diff * static_cast<float>(Math::InvPI);
    Vector3f c_final       = Vector3f::Zero();

    for (std::size_t i = 0; i < frame.lights.size(); ++i)
    {
        Vector3f light_vec, light_color;
        frame.lights[i]->Sample(position, light_vec, light_color);

        float n_dot_l = normal.Dot(light_vec);
        if (n_dot_l <= std::numeric_limits<float>::epsilon())
            continue;

        float shadow = CalcShadowFactorFromLight(frame, i, position);
        if (shadow <= std::numeric_limits<float>::epsilon())
            continue;

        Vector3f half_vec = (view_vec + light_vec).Normalized();
        float    n_dot_h  = normal.Dot(half_vec);
        float    v_dot_h  = view_vec.Dot(half_vec);
        float    l_dot_h  = light_vec.Dot(half_vec);

        Vector3f term_f = FresnelSchlick(f0, Math::Saturate(v_dot_h));
        Vector3f f_diff = (Vector3f::Ones() - term_f) * c_diff_div_pi;
        Vector3f f_spec = Vector3f::Zero();

        if (n_dot_v > 0.0f && n_dot_h > 0.0f && v_dot_h > 0.0f && l_dot_h > 0.0f)
        {
            float term_d = NormalDistributionGGX(n_dot_h, alpha2);
            float term_v = GeometrySmithGGX(n_dot_l, n_dot_v, alpha2);
            f_spec = (term_d * term_v) * term_f;
        }

        c_final += shadow * n_dot_l * light_color * (f_diff + f_spec);
    }

    return c_final;
}

static inline Vector3f ShaderFS_IBL(
    const RenderIBLData& ibl_data,
    const Vector3f& normal,
    const Vector3f& view_vec,
    float occlusion,
    float roughness,
    const Vector3f& f0,
    const Vector3f& c_diff)
{
    Vector3f irradiance = Vector3f::Zero();
    SampleCubeTextureLod(ibl_data.irradiance_map, {TextureAddressMode::CLAMP,
        TextureFilter::LINEAR}, normal, 0.0f, irradiance);

    float    n_dot_v = normal.Dot(view_vec);
    Vector3f term_f  = FresnelSchlickRoughness(f0, Math::Saturate(n_dot_v), roughness);
    float    k_diff  = occlusion * static_cast<float>(Math::InvPI);
    Vector3f f_diff  = k_diff * (Vector3f::Ones() - term_f) * c_diff;
    Vector3f f_spec  = Vector3f::Zero();

    if (n_dot_v > 0.0f)
    {
        Vector2f env_brdf = Vector2f::Zero();
        SampleTextureLod(ibl_data.brdf_lut, {TextureAddressMode::CLAMP,
            TextureFilter::LINEAR}, {n_dot_v, roughness}, 0.0f, env_brdf);
        Vector3f reflect = 2 * n_dot_v * normal - view_vec;
        auto [face_type, uv] = CubeTexture::SelectFace(reflect);
        const Texture& face = ibl_data.prefilter_map.Face(face_type);
        float lod = roughness * face.MaxLod();
        Vector3f prefiltered_color = Vector3f::Zero();
        SampleTextureLod(face, {TextureAddressMode::CLAMP, 
            TextureFilter::LINEAR_MIP_LINEAR}, uv, lod, prefiltered_color);
        f_spec = prefiltered_color * (f0 * env_brdf.X() + env_brdf.Y());
    }

    return irradiance * f_diff + ibl_data.specular_scale * f_spec;
}

constexpr Vector3f kDielectricF0 = {0.04f, 0.04f, 0.04f};

// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metal-brdf-and-dielectric-brdf
Vector4f ShaderFS_PBR_MetallicRoughness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrMetallicRoughness&>(*uniforms.material);
    MaterialPbrMetallicRoughness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);

    Vector3f normal     = varyings.normal_w.Normalized();
    Vector3f view_vec   = (uniforms.frame->eye_pos - varyings.position_w).Normalized();
    Vector3f base_color = matsp.base_color.Head<3>();
    Vector3f f0         = Math::Lerp(kDielectricF0, base_color, matsp.metallic);
    Vector3f c_diff     = (1.0f - matsp.metallic) * base_color;
 
    if (mat.normal_tex != nullptr)
    {
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            varyings.tangent_w, varyings.tangent_sign);
    }

    Vector3f c_final = matsp.emissive;
    c_final += ShaderFS_PBR(*uniforms.frame, varyings.position_w, normal, 
        view_vec, matsp.roughness, f0, c_diff);

    if (uniforms.frame->ibl_data != nullptr)
    {
        c_final += ShaderFS_IBL(*uniforms.frame->ibl_data, normal, view_vec,
            matsp.occlusion, matsp.roughness, f0, c_diff);
    }
    else
    {
        c_final += matsp.occlusion * uniforms.frame->ambient_light * c_diff;
    }
    
    return c_final.Join(matsp.base_color.W());
}

// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/README.md
Vector4f ShaderFS_PBR_SpecularGlossiness(
    const RenderUniformsPerObject& uniforms,
    const RenderVaryings& varyings,
    float dtexcoord)
{
    auto& mat = static_cast<MaterialPbrSpecularGlossiness&>(*uniforms.material);
    MaterialPbrSpecularGlossiness::SampleResult matsp;
    mat.Sample(uniforms.sampler, varyings.texcoord, dtexcoord, matsp);
    
    Vector3f normal    = varyings.normal_w.Normalized();
    Vector3f view_vec  = (uniforms.frame->eye_pos - varyings.position_w).Normalized();
    float    roughness = 1.0f - matsp.glossiness;
    Vector3f f0        = matsp.specular;
    Vector3f c_diff    = matsp.diffuse.Head<3>() * (1.0f - f0.MaxCoeff());

    if (mat.normal_tex != nullptr)
    {
        normal = NormalSampleToWorldSpace(matsp.normal_ts, normal,
            varyings.tangent_w, varyings.tangent_sign);
    }

    Vector3f c_final = matsp.emissive;
    c_final += ShaderFS_PBR(*uniforms.frame, varyings.position_w, normal,
        view_vec, roughness, f0, c_diff);

    if (uniforms.frame->ibl_data != nullptr)
    {
        c_final += ShaderFS_IBL(*uniforms.frame->ibl_data, normal, view_vec,
            matsp.occlusion, roughness, f0, c_diff);
    }
    else
    {
        c_final += matsp.occlusion * uniforms.frame->ambient_light * c_diff;
    }

    return c_final.Join(matsp.diffuse.W());
}

}}