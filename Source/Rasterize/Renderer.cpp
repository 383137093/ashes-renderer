#include "Renderer.h"
#include <cmath>
#include <cfloat>
#include <cassert>
#include <cstring>
#include <numeric>
#include <algorithm>
#include "Math/MathMisc.h"
#include "Math/ColorMisc.h"
#include "Math/Transform.h"
#include "Geometry/Clipping.h"

namespace Ashes { namespace Rasterize {

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
}

float Renderer::GetComputedExposure() const
{
    return luminance_info_.computed_exposure;
}

void Renderer::SetRenderStates(const RenderStates& states)
{
    render_states_ = states;
    render_states_.view_proj = states.projection * states.view;
}

void Renderer::AddDrawCall(RenderDrawCall draw_call)
{
    if (draw_call.transparent)
    {
        transparent_draw_calls_.push_back(std::move(draw_call));
    }
    else
    {
        opaque_draw_calls_.push_back(std::move(draw_call));
    }
}

void Renderer::AddShadowCaster(RenderShadowCaster caster)
{
    shadow_casters_.push_back(std::move(caster));
}

void Renderer::Present()
{
    // sort opacified objects from near to far.
    // sort transparent objects from far to near.
    SortDrawCalls<1>(render_states_.view, opaque_draw_calls_);
    SortDrawCalls<0>(render_states_.view, transparent_draw_calls_);

    InitFrameBuffer();
    DepthPass();
    ShadowPass();
    ShadingPass();
    ScreenPass();
    ReleaseResources();
}

void Renderer::InitFrameBuffer()
{
    using TransformationMatrix::Viewport;

    const int  width  = render_states_.render_size.X();
    const int  height = render_states_.render_size.Y();
    const bool ecsaa  = (render_states_.antialias & RenderAntialias_ECSAA);

    frame_buffer_.SetDepthBuffer(render_states_.depth_buffer.get());
    frame_buffer_.SetColorBuffer(render_states_.color_buffer.get());
    frame_buffer_.SetColorBuffer2nd(ecsaa ? &shared_buffer_ : nullptr);
    frame_buffer_.SetOITBuffer(render_states_.oit ? &oit_buffer_ : nullptr);

    if (render_states_.antialias & RenderAntialias_MSAA)
    {
        render_states_.viewport = Viewport(width, height);
        frame_buffer_.ResizeBuffers(width * 2, height * 2);
    }
    else if (render_states_.antialias & RenderAntialias_SSAA)
    {
        render_states_.viewport = Viewport(2 * width, 2 * height);
        frame_buffer_.ResizeBuffers(2 * width, 2 * height);
    }
    else
    {
        render_states_.viewport = Viewport(width, height);
        frame_buffer_.ResizeBuffers(width, height);
    }

    DispatchScreenPass(&ScreenPassClear, &frame_buffer_);
}

void Renderer::DepthPass()
{
    MeshPassRenderStates pass_states = {};
    pass_states.thread_pool = render_states_.thread_pool.get();
    pass_states.num_threads = render_states_.num_threads;
    pass_states.inv_projection = render_states_.projection.Inverse();
    pass_states.viewport = render_states_.viewport;
    pass_states.antialias = render_states_.antialias;
    pass_states.backface_culling = render_states_.backface_culling;
    pass_states.frame_buffer = &frame_buffer_;
    Geom::ExtractFrustumFromTransform(render_states_.view_proj, pass_states.frustum);
    pass_states.tri_rasterizer = ((render_states_.antialias & RenderAntialias_MSAA)
        ? &RasterizeTriangleDepthMSAA : &RasterizeTriangleDepth);

    MeshPassRenderInfo pass;
    pass.pass_id = MeshPassID::DepthPass;
    pass.draw_calls = opaque_draw_calls_.data();
    pass.num_draw_calls = static_cast<int>(opaque_draw_calls_.size());
    frame_buffer_.SwitchLockToDepthData();
    DispatchMeshPass(pass_states, pass);
}

void Renderer::ShadowPass()
{
    if (!shadow_casters_.empty())
    {
        MeshPassRenderStates pass_states = {};
        pass_states.thread_pool = render_states_.thread_pool.get();
        pass_states.num_threads = render_states_.num_threads;
        pass_states.antialias = RenderAntialias_None;
        pass_states.tri_rasterizer = &RasterizeTriangleDepth;
        pass_states.frame_buffer = &frame_buffer_;

        MeshPassRenderInfo pass;
        pass.pass_id = MeshPassID::ShadowPass;
        pass.draw_calls = opaque_draw_calls_.data();
        pass.num_draw_calls = static_cast<int>(opaque_draw_calls_.size());

        render_states_.thread_pool->Dispatch(
            pass_states.num_threads, pass_states.num_threads, &ClearShadowMap,
            std::ref(shadow_casters_), pass_states.num_threads);

        for (const RenderShadowCaster& caster : shadow_casters_)
        {
            const int width  = caster.shadow_map->Width();
            const int height = caster.shadow_map->Height();
            pass_states.viewport = TransformationMatrix::Viewport(width, height);
            pass_states.shadow_vs = &caster.shader_vs;
            Geom::ExtractFrustumFromTransform(caster.view_proj, pass_states.frustum);
            Texture* depth_buffer = frame_buffer_.DepthBuffer();
            frame_buffer_.SetDepthBuffer(caster.shadow_map.get());
            frame_buffer_.SwitchLockToDepthData();
            DispatchMeshPass(pass_states, pass);
            frame_buffer_.SetDepthBuffer(depth_buffer);
        }
    }
}

void Renderer::ShadingPass()
{
    MeshPassRenderStates pass_states = {};
    pass_states.thread_pool = render_states_.thread_pool.get();
    pass_states.num_threads = render_states_.num_threads;
    pass_states.inv_projection = render_states_.projection.Inverse();
    pass_states.viewport = render_states_.viewport;
    pass_states.antialias = render_states_.antialias;
    pass_states.backface_culling = render_states_.backface_culling;
    pass_states.frame_buffer = &frame_buffer_;
    Geom::ExtractFrustumFromTransform(render_states_.view_proj, pass_states.frustum);
    pass_states.tri_rasterizer = ((render_states_.antialias & RenderAntialias_MSAA)
        ? &RasterizeTriangleMSAA : &RasterizeTriangle);

    MeshPassRenderInfo pass;
    pass.pass_id = MeshPassID::BasePass;
    pass.draw_calls = opaque_draw_calls_.data();
    pass.num_draw_calls = static_cast<int>(opaque_draw_calls_.size());
    frame_buffer_.SwitchLockToColorData();
    DispatchMeshPass(pass_states, pass);

    if (!transparent_draw_calls_.empty())
    {
        // ECSAA must be finished before rendering transparent objects.
        if (render_states_.antialias & RenderAntialias_ECSAA)
        {
            DispatchScreenPass(&ScreenPassECSAA, &frame_buffer_);
            frame_buffer_.SetColorBuffer2nd(nullptr);
            pass_states.antialias &= ~RenderAntialias_ECSAA;
        }

        // transparent objects must be rendered after opaque objects.
        pass_states.alpha_blending = true;
        pass.draw_calls = transparent_draw_calls_.data();
        pass.num_draw_calls = static_cast<int>(transparent_draw_calls_.size());
        DispatchMeshPass(pass_states, pass);
    }
}

void Renderer::ScreenPass()
{
    if (render_states_.antialias & (RenderAntialias_MSAA | RenderAntialias_SSAA))
    {
        const int width  = render_states_.render_size.X();
        const int height = render_states_.render_size.Y();

        // ECSAA & OIT: complete them before downsample.
        if (frame_buffer_.ColorData2nd() != nullptr || render_states_.oit)
        {
            DispatchScreenPass([this](int first, int last)
            {
                if (frame_buffer_.ColorData2nd() != nullptr)
                    ScreenPassECSAA(&frame_buffer_, first, last);
                if (render_states_.oit)
                    ScreenPassOIT(&frame_buffer_, first, last);
            });
            frame_buffer_.SetColorBuffer2nd(nullptr);
        }
        
        // MSAA / SSAA: downsample color texture to shared texture.
        shared_buffer_.CreateUninitialized(TexelFormat::FLOAT4, height, width);
        auto* colors2 = reinterpret_cast<Vector4f*>(shared_buffer_.MipData0());
        DispatchScreenPass(&ScreenPassDownsampleStep1, &frame_buffer_, colors2);
        frame_buffer_.ResizeBuffers(width, height);
        DispatchScreenPass(&ScreenPassDownsampleStep2, &frame_buffer_, colors2);

        // ACES & GammaCorrection & FillBackground & PostProcess.
        DispatchScreenPass([this](int thread_idx, int first, int last)
        {
            const Vector3f& bkg_color = render_states_.background_color;
            Vector3f* colors = frame_buffer_.ColorData();
            if (render_states_.aces)
                ScreenPassACES(colors, luminance_info_, thread_idx, first, last);
            if (render_states_.gamma_correction)
                ScreenPassGammaCorrection(colors, first, last);
            if (!bkg_color.IsZero())
                ScreenPassFillBackground(&frame_buffer_, bkg_color, first, last);
            if (render_states_.post_process)
                render_states_.post_process(colors, first, last);
        }, ThreadPool::kWithBatchID);
    }
    else
    {
        // ECSAA & OIT & ACES & GammaCorrection & FillBackground & PostProcess.
        DispatchScreenPass([this](int thread_idx, int first, int last)
        {
            const Vector3f& bkg_color = render_states_.background_color;
            Vector3f* colors = frame_buffer_.ColorData();
            if (frame_buffer_.ColorData2nd() != nullptr)
                ScreenPassECSAA(&frame_buffer_, first, last);
            if (render_states_.oit)
                ScreenPassOIT(&frame_buffer_, first, last);
            if (render_states_.aces)
                ScreenPassACES(colors, luminance_info_, thread_idx, first, last);
            if (render_states_.gamma_correction)
                ScreenPassGammaCorrection(colors, first, last);
            if (!bkg_color.IsZero())
                ScreenPassFillBackground(&frame_buffer_, bkg_color, first, last);
            if (render_states_.post_process)
                render_states_.post_process(colors, first, last);
        }, ThreadPool::kWithBatchID);
    }

    luminance_info_.computed_exposure = 1.0f;
    if (render_states_.aces)
    {
        auto& pixel_counts = luminance_info_.pixel_count_per_batch;
        auto& luma_sums = luminance_info_.luminance_sum_per_batch;
        int pixel_count = std::accumulate(pixel_counts.begin(), pixel_counts.end(), 0);
        double luma_sum = std::accumulate(luma_sums.begin(), luma_sums.end(), 0.0);
        double luma_avg = (pixel_count > 0 ? std::exp(luma_sum / pixel_count) : 0.18);
        double exposure = Math::Clamp(0.18 / luma_avg, 0.01, 100.0);
        luminance_info_.computed_exposure = static_cast<float>(exposure);
        std::fill_n(pixel_counts.data(), pixel_counts.size(), 0);
        std::fill_n(luma_sums.data(), luma_sums.size(), 0.0);
    }
}

void Renderer::ReleaseResources()
{
    frame_buffer_.SetDepthBuffer(nullptr);
    frame_buffer_.SetColorBuffer(nullptr);
    frame_buffer_.SetColorBuffer2nd(nullptr);

    render_states_.color_buffer.reset();
    render_states_.depth_buffer.reset();

    opaque_draw_calls_.clear();
    transparent_draw_calls_.clear();
    shadow_casters_.clear();
}

//==============================================================================
// Mesh Pass Handling
//============================================================================== 

template <bool NearToFar>
void Renderer::SortDrawCalls(
    const Matrix4f& view,
    std::vector<RenderDrawCall>& draw_calls)
{
    struct SortItem { RenderDrawCall* draw_call; float view_depth; };

    std::vector<SortItem> items(draw_calls.size());
 
    for (std::size_t i = 0; i < draw_calls.size(); ++i)
    {
        Geom::AxisAlignedBox view_bounds = draw_calls[i].world_bounds;
        Geom::TransformAxisAlignedBoundingBox(view, view_bounds);
        items[i] = {&draw_calls[i], -view_bounds.center.Z()};
    }

    std::sort(items.begin(), items.end(), (NearToFar
        ? [](SortItem& a, SortItem& b) { return a.view_depth < b.view_depth; }
        : [](SortItem& a, SortItem& b) { return a.view_depth > b.view_depth; }));

    for (std::size_t i = 0; i < draw_calls.size(); ++i)
    {
        if (&draw_calls[i] != items[i].draw_call)
            std::swap(draw_calls[i], *items[i].draw_call);
    }
}

void Renderer::ClearShadowMap(
    const std::vector<RenderShadowCaster>& casters,
    int num_threads, int thread_idx, int)
{
    for (const RenderShadowCaster& caster : casters)
    {
        if (const std::shared_ptr<Texture>& shadow_map = caster.shadow_map)
        {
            const int num_texels = shadow_map->Width() * shadow_map->Height();
            const auto [first, last] = ThreadPool::ComputeBatchRange(
                num_texels, num_threads, thread_idx);
            float* texels = reinterpret_cast<float*>(shadow_map->MipData0());
            std::memset(texels + first, 0, (last - first) * sizeof(float));
        }
    }
}

void Renderer::DispatchMeshPass(
    const MeshPassRenderStates& pass_states,
    MeshPassRenderInfo& pass)
{
    assert(pass_states.num_threads > 0);
    assert(pass_states.tri_rasterizer != nullptr);
    assert(pass_states.frame_buffer != nullptr);
    
    pass.pending_draw_call = 0;
    pass.pending_tris = std::vector<std::atomic_int>(pass.num_draw_calls);

    if (pass.num_draw_calls > 0)
    {
        pass_states.thread_pool->Dispatch(
            pass_states.num_threads, pass_states.num_threads,
            &HandleMeshPass, std::ref(pass_states), std::ref(pass));
    }
}

void Renderer::HandleMeshPass(
    const MeshPassRenderStates& pass_states,
    MeshPassRenderInfo& pass, int thread_idx, int)
{
    CullingDrawCalls(pass_states, pass, thread_idx);
    int draw_call_idx = -1, tri_idx = -1;
    while (FetchPendingTriangleFromMeshPass(pass, draw_call_idx, tri_idx))
    {
        const RenderDrawCall& draw_call = pass.draw_calls[draw_call_idx];
        HandleTriangle(pass_states, pass.pass_id, draw_call, tri_idx);
    }
}

void Renderer::CullingDrawCalls(
    const MeshPassRenderStates& pass_states,
    MeshPassRenderInfo& pass, int thread_idx)
{
    const auto [first, last] = ThreadPool::ComputeBatchRange(
        pass.num_draw_calls, pass_states.num_threads, thread_idx);

    for (int i = first; i < last; ++i)
    {
        if (Geom::IsAxisAlignedBoxOutsideOfFrustum(
            pass.draw_calls[i].world_bounds, pass_states.frustum))
        {
            pass.pending_tris[i] = pass.draw_calls[i].num_triangles;
        }
    }
}

bool Renderer::FetchPendingTriangleFromMeshPass(
    MeshPassRenderInfo& pass,
    int& draw_call_idx, int& tri_idx)
{
    // try fetching pending triangle from current draw call.
    if (0 <= draw_call_idx && draw_call_idx < pass.num_draw_calls)
    {
        if (FetchPendingTriangleFromDrawCall(pass, draw_call_idx, tri_idx))
            return true;
    }

    // try fetching pending triangle from pending draw call.
    while ((draw_call_idx = pass.pending_draw_call) < pass.num_draw_calls)
    {
        if (FetchPendingTriangleFromDrawCall(pass, draw_call_idx, tri_idx))
            return true;
    }

    return false;
}

bool Renderer::FetchPendingTriangleFromDrawCall(
    MeshPassRenderInfo& pass,
    int draw_call_idx, int& tri_idx)
{
    tri_idx = pass.pending_tris[draw_call_idx].fetch_add(1);

    // if all triangles of the draw call are done, then the draw call is done,
    // increase the pending draw call.
    if (tri_idx >= pass.draw_calls[draw_call_idx].num_triangles)
    {
        pass.pending_draw_call.compare_exchange_strong(
            draw_call_idx, draw_call_idx + 1);
        return false;
    }
    
    return true; 
}

//==============================================================================
// Screen Pass Handling
//==============================================================================

template <typename FuncType, typename... ArgsType>
void Renderer::DispatchScreenPass(
    FuncType&& func,
    ArgsType&&... args) const
{
    const int rows = frame_buffer_.ColorDataSize().Y();
    const int cols = frame_buffer_.ColorDataSize().X();
    render_states_.thread_pool->Dispatch(
        rows * cols, render_states_.num_threads,
        std::forward<FuncType>(func), std::forward<ArgsType>(args)...);
}

void Renderer::ScreenPassClear(
    const FrameBuffer* fbuf,
    int first, int last)
{
    const int count = last - first;

    if (float* depths = fbuf->DepthData())
    {
        std::memset(depths + first, 0, count * sizeof(float));
    }

    if (Vector3f* colors = fbuf->ColorData())
    {
        std::memset(colors + first, 0, count * sizeof(Vector3f));
    }

    if (Vector4f* colors_2nd = fbuf->ColorData2nd())
    {
        std::memset(colors_2nd + first, 0, count * sizeof(Vector4f));
    }

    if (OITSample* samples = fbuf->OITData())
    {
        std::memset(samples + 4 * first, 0, count * 4 * sizeof(OITSample));
    }
}

void Renderer::ScreenPassECSAA(
    const FrameBuffer* fbuf,
    int first, int last)
{
    for (int i = first; i < last; ++i)
    {
        Color::AlphaBlending(fbuf->ColorData()[i], fbuf->ColorData2nd()[i]);
    }
}

void Renderer::ScreenPassOIT(
    const FrameBuffer* fbuf,
    int first, int last)
{
    for (int i = first; i < last; ++i)
    {
        OITSample* samples = fbuf->OITData() + 4 * i;
        std::sort(samples, samples + 4, [](OITSample& a, OITSample& b) {
            return a.depth < b.depth; });
        Vector3f& color = fbuf->ColorData()[i];
        Color::AlphaBlending(color, samples[0].color);
        Color::AlphaBlending(color, samples[1].color);
        Color::AlphaBlending(color, samples[2].color);
        Color::AlphaBlending(color, samples[3].color);
    }
}

void Renderer::ScreenPassDownsampleStep1(
    const FrameBuffer* fbuf,
    Vector4f* dest,
    int src_first, int src_last)
{
    const int src_cols   = fbuf->DepthDataSize().X();
    const int dest_cols  = src_cols  / 2;
    const int dest_first = src_first / 4;
    const int dest_last  = src_last  / 4;

    for (int dest_idx = dest_first; dest_idx < dest_last; ++dest_idx)
    {
        const int dest_row = dest_idx / dest_cols;
        const int dest_col = dest_idx % dest_cols;

        const int src_idx0 = (2 * dest_row) * src_cols + (2 * dest_col);
        const int src_idx1 = src_idx0 + 1;
        const int src_idx2 = src_idx0 + src_cols;
        const int src_idx3 = src_idx0 + src_cols + 1;

        const float d0 = fbuf->DepthData()[src_idx0];
        const float d1 = fbuf->DepthData()[src_idx1];
        const float d2 = fbuf->DepthData()[src_idx2];
        const float d3 = fbuf->DepthData()[src_idx3];
        const float d = (d0 + d1 + d2 + d3) * 0.25f;

        const Vector3f c0 = fbuf->ColorData()[src_idx0];
        const Vector3f c1 = fbuf->ColorData()[src_idx1];
        const Vector3f c2 = fbuf->ColorData()[src_idx2];
        const Vector3f c3 = fbuf->ColorData()[src_idx3];
        const Vector3f c = (c0 + c1 + c2 + c3) * 0.25f;
        
        dest[dest_idx] = {c[0], c[1], c[2], d};
    }
}

void Renderer::ScreenPassDownsampleStep2(
    const FrameBuffer* fbuf,
    const Vector4f* src,
    int first, int last)
{
    for (int i = first; i < last; ++i)
    {
        fbuf->ColorData()[i] = src[i].Head<3>();
        fbuf->DepthData()[i] = src[i][3];
    }
}

void Renderer::ScreenPassACES(
    Vector3f* colors,
    ScreenLuminanceInfo& luma_info,
    int thread_idx, int first, int last)
{
    assert(thread_idx < static_cast<int>(ScreenLuminanceInfo::kMaxBatches));
    for (int i = first; i < last; ++i)
    {
        constexpr float kLumaEps = 1e-5f;
        float luma = colors[i].Dot(Color::kLumaFactor);
        colors[i].X() = Color::ACESFilm(luma_info.computed_exposure * colors[i].X());
        colors[i].Y() = Color::ACESFilm(luma_info.computed_exposure * colors[i].Y());
        colors[i].Z() = Color::ACESFilm(luma_info.computed_exposure * colors[i].Z());
        if (luma < kLumaEps) { continue; }
        luma_info.luminance_sum_per_batch[thread_idx] += std::log(luma + kLumaEps);
        ++luma_info.pixel_count_per_batch[thread_idx];
    }
}

void Renderer::ScreenPassGammaCorrection(
    Vector3f* colors,
    int first, int last)
{
    for (int i = first; i < last; ++i)
    {
        colors[i].X() = std::pow(colors[i].X(), 0.45f);
        colors[i].Y() = std::pow(colors[i].Y(), 0.45f);
        colors[i].Z() = std::pow(colors[i].Z(), 0.45f);
    }
}

void Renderer::ScreenPassFillBackground(
    const FrameBuffer* fbuf,
    const Vector3f& color,
    int first, int last)
{
    const float* depths = fbuf->DepthData();
    Vector3f*    colors = fbuf->ColorData();

    for (int i = first; i < last; ++i)
    {
        if (depths[i] == 0.0f && colors[i].IsZero())
            colors[i] = color;
    }
}

//==============================================================================
// Triangle Handling
//==============================================================================

void Renderer::HandleTriangle(
    const MeshPassRenderStates& pass_states,
    MeshPassID pass_id,
    const RenderDrawCall& draw_call, int tri_idx)
{
    const RenderShaderVS* shader_vs = (pass_id == MeshPassID::ShadowPass
        ? pass_states.shadow_vs : &draw_call.shader_vs);

    // construct model space triangle from vertex buffer and index buffer.
    const auto* vertices = static_cast<const std::uint8_t*>(draw_call.vertices);
    const std::uint16_t idx0 = draw_call.indices[3 * tri_idx + 0];
    const std::uint16_t idx1 = draw_call.indices[3 * tri_idx + 1];
    const std::uint16_t idx2 = draw_call.indices[3 * tri_idx + 2];
    const void* vertex0 = vertices + idx0 * draw_call.vertex_size;
    const void* vertex1 = vertices + idx1 * draw_call.vertex_size;
    const void* vertex2 = vertices + idx2 * draw_call.vertex_size;
    
    Vector4f clip_vertices[kMaxNumOfClippedVertices];
    float varyings[kMaxNumOfClippedVertices * kMaxNumOfVaryings];

    // construct homogeneous space triangle by executing vertex shader.
    float* varyings0 = varyings;
    float* varyings1 = varyings0 + draw_call.num_varyings;
    float* varyings2 = varyings1 + draw_call.num_varyings;
    clip_vertices[0] = (*shader_vs)(draw_call.uniforms, vertex0, varyings0);
    clip_vertices[1] = (*shader_vs)(draw_call.uniforms, vertex1, varyings1);
    clip_vertices[2] = (*shader_vs)(draw_call.uniforms, vertex2, varyings2);
    
    if (BackFaceCulling(pass_states, draw_call, clip_vertices))
        return;

    // clip homogeneous space triangle.
    const int num_clip_vertices = HomogeneousSpaceClipping(
        clip_vertices, varyings, draw_call.num_varyings);
    if (num_clip_vertices < 3)
        return;
    
    TriangleRenderStates tri_states;
    tri_states.pass_id = pass_id;
    tri_states.ecsaa = (pass_states.antialias & RenderAntialias_ECSAA);
    tri_states.texturing = (draw_call.texcoord_index >= 0);
    tri_states.alpha_blending = pass_states.alpha_blending;
    tri_states.uniforms = draw_call.uniforms;
    tri_states.shader_vs = shader_vs;
    tri_states.shader_fs = &draw_call.shader_fs;
    tri_states.frame_buffer = pass_states.frame_buffer;

    // handle each triangle devided from homogeneous space polygon.
    for (int i = 0; i + 2 < num_clip_vertices; ++i)
    {
        Geom::Triangle4f clip_tri;
        clip_tri.v0 = clip_vertices[0];
        clip_tri.v1 = clip_vertices[i + 1];
        clip_tri.v2 = clip_vertices[i + 2];
        
        TriangleRenderInfo tri;
        tri.varyings[0] = &varyings[0];
        tri.varyings[1] = &varyings[(i + 1) * draw_call.num_varyings];
        tri.varyings[2] = &varyings[(i + 2) * draw_call.num_varyings];
        tri.num_varyings = draw_call.num_varyings;

        InitTriangleRenderInfo(tri, draw_call.texcoord_index,
            clip_tri, pass_states.viewport);
        pass_states.tri_rasterizer(tri_states, tri);
    }
}

bool Renderer::BackFaceCulling(
    const MeshPassRenderStates& pass_states,
    const RenderDrawCall& draw_call,
    const Vector4f* clip_vertices)
{
    if (!pass_states.backface_culling)
        return false;

    if (draw_call.double_sided)
        return false;

    Vector4f a = pass_states.inv_projection * clip_vertices[0];
    Vector4f b = pass_states.inv_projection * clip_vertices[1];
    Vector4f c = pass_states.inv_projection * clip_vertices[2];

    // view space edge vector 
    float ab_x = (b.X() / b.W()) - (a.X() / a.W());
    float ab_y = (b.Y() / b.W()) - (a.Y() / a.W());
    float ac_x = (c.X() / c.W()) - (a.X() / a.W());
    float ac_y = (c.Y() / c.W()) - (a.Y() / a.W());

    // component z of view space normal
    const float normal_z = ab_x * ac_y - ab_y * ac_x;
    return normal_z < 0.0f;
}

int Renderer::HomogeneousSpaceClipping(
    Vector4f* vertices,
    float* varyings,
    int num_varyings_per_vertex)
{
    if (Geom::IsPolygonInsideHomogeneousClipSpace(vertices, 3))
        return 3;

    Vector3f barys[Geom::HomogeneousClipMaxOut(3)];
    int num_vertices = Geom::HomogeneousClipOfTriangle(vertices, barys);
    if (num_vertices < 3)
        return 0;
    
    float intermediate_varyings[3 * kMaxNumOfVaryings];
    std::copy_n(varyings, 3 * num_varyings_per_vertex, intermediate_varyings);
    const float* varying0 = intermediate_varyings;
    const float* varying1 = varying0 + num_varyings_per_vertex;
    const float* varying2 = varying1 + num_varyings_per_vertex;

    for (int v = 0; v < num_vertices; ++v)
    {
        for (int i = 0; i < num_varyings_per_vertex; ++i)
        {
            *varyings++ = Math::BaryInterp3(
                varying0[i], varying1[i], varying2[i], barys[v]);
        }
    }
    
    return num_vertices;
}

void Renderer::InitTriangleRenderInfo(
    TriangleRenderInfo& tri,
    int texcoord_idx_in_varyings,
    const Geom::Triangle4f& clip_tri,
    const Matrix4f& viewport)
{
    // the w-component is also the view space depth.
    tri.view_recip_depths[0] = 1.0f / clip_tri.v0.W();
    tri.view_recip_depths[1] = 1.0f / clip_tri.v1.W();
    tri.view_recip_depths[2] = 1.0f / clip_tri.v2.W();

    // Perspective-Divide.
    Vector3f v0 = clip_tri.v0.Head<3>() * tri.view_recip_depths[0];
    Vector3f v1 = clip_tri.v1.Head<3>() * tri.view_recip_depths[1];
    Vector3f v2 = clip_tri.v2.Head<3>() * tri.view_recip_depths[2];

    // viewport transform.
    tri.positions.v0 = TransformationMatrix::TransformPoint(viewport, v0);
    tri.positions.v1 = TransformationMatrix::TransformPoint(viewport, v1);
    tri.positions.v2 = TransformationMatrix::TransformPoint(viewport, v2);

    // interpolated depth could bigger than max depth because of floating-point
    // error, handle it by add FLT_EPSILON.
    tri.rev_depths = {v0.Z(), v1.Z(), v2.Z()};
    tri.max_rev_depth = tri.rev_depths.MaxCoeff() + 10.0f * FLT_EPSILON;

    // extract texture coordinates of vertices.
    if (texcoord_idx_in_varyings >= 0)
    {
        const float* ptex0 = tri.varyings[0] + texcoord_idx_in_varyings;
        const float* ptex1 = tri.varyings[1] + texcoord_idx_in_varyings;
        const float* ptex2 = tri.varyings[2] + texcoord_idx_in_varyings;
        tri.texcoords[0] = *reinterpret_cast<const Vector2f*>(ptex0);
        tri.texcoords[1] = *reinterpret_cast<const Vector2f*>(ptex1);
        tri.texcoords[2] = *reinterpret_cast<const Vector2f*>(ptex2);
    }
}

//==============================================================================
// Triangle Handling - Rasterizing
//==============================================================================

void Renderer::RasterizeTriangleDepth(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri)
{
    // compute screen information of current triangle.
    const FrameBuffer* fbuf = tri_states.frame_buffer;
    TriangleScreenInfo screen_tri;
    ConstructTriangleScreenInfo(tri, fbuf->DepthDataSize(), screen_tri);
    const IntRange& px_x_bounds = screen_tri.bounds.px_x;
    const IntRange& px_y_bounds = screen_tri.bounds.px_y;

    for (int px_y = px_y_bounds.min; px_y <= px_y_bounds.max; ++px_y)
    {
        // point range covered by the screen triangle at y-th line pixel.
        const FloatRange pt_x_range = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.5f);

        // pixel range covered by the screen triangle at y-th line pixel.
        const IntRange px_x_range = ScreenPointRangeToPixelRange(
            pt_x_range, 0.5f, px_x_bounds, 0);
        
        for (int px_x = px_x_range.min; px_x <= px_x_range.max; ++px_x)
        {
            const Vector2f pt = {px_x + 0.5f, px_y + 0.5f};
            const int idx = fbuf->DepthDataIndex(px_x, px_y);
            HandleFragmentDepth(tri_states, tri, screen_tri, pt, idx);
        }
    }
}

void Renderer::RasterizeTriangleDepthMSAA(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri)
{
    // compute screen information of current triangle.
    // note frame buffer size is 2x2 of screen size.
    const FrameBuffer* fbuf = tri_states.frame_buffer;
    TriangleScreenInfo screen_tri;
    ConstructTriangleScreenInfo(tri, fbuf->DepthDataSize() / 2, screen_tri);
    const IntRange& px_x_bounds = screen_tri.bounds.px_x;
    const IntRange& px_y_bounds = screen_tri.bounds.px_y;

    for (int px_y = px_y_bounds.min; px_y <= px_y_bounds.max; ++px_y)
    {
        // point range covered by the screen triangle at 2y-th line sample.
        const FloatRange pt_x_range0 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.25f);

        // point range covered by the screen triangle at (2y+1)-th line sample.
        const FloatRange pt_x_range1 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.75f);

        // pixel range covered by the screen triangle at y-th line pixel.
        const IntRange px_x_range = ScreenPointRangeToPixelRange(
            pt_x_range0.Union(pt_x_range1), 0.25f, px_x_bounds, 0);

        for (int px_x = px_x_range.min; px_x <= px_x_range.max; ++px_x)
        {
            const Vector2f pt0 = {px_x + 0.25f, px_y + 0.25f};
            const Vector2f pt1 = {px_x + 0.75f, px_y + 0.25f};
            const Vector2f pt2 = {px_x + 0.25f, px_y + 0.75f};
            const Vector2f pt3 = {px_x + 0.75f, px_y + 0.75f};
            
            const int idx0 = fbuf->DepthDataIndex(2 * px_x,     2 * px_y);
            const int idx1 = fbuf->DepthDataIndex(2 * px_x + 1, 2 * px_y);
            const int idx2 = fbuf->DepthDataIndex(2 * px_x,     2 * px_y + 1);
            const int idx3 = fbuf->DepthDataIndex(2 * px_x + 1, 2 * px_y + 1);

            if (pt_x_range0.Contain(pt0.X()))
                HandleFragmentDepth(tri_states, tri, screen_tri, pt0, idx0);
            if (pt_x_range0.Contain(pt1.X()))
                HandleFragmentDepth(tri_states, tri, screen_tri, pt1, idx1);
            if (pt_x_range1.Contain(pt2.X()))
                HandleFragmentDepth(tri_states, tri, screen_tri, pt2, idx2);
            if (pt_x_range1.Contain(pt3.X()))
                HandleFragmentDepth(tri_states, tri, screen_tri, pt3, idx3);
        }
    }
}

void Renderer::RasterizeTriangle(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri)
{
    // compute screen information of current triangle.
    const FrameBuffer* fbuf = tri_states.frame_buffer;
    TriangleScreenInfo screen_tri;
    ConstructTriangleScreenInfo(tri, fbuf->ColorDataSize(), screen_tri);
    const IntRange& px_x_bounds = screen_tri.bounds.px_x;
    const IntRange& px_y_bounds = screen_tri.bounds.px_y;

    // ECSAA: consider pixels partially covered by current triangle.
    const int px_margin = (tri_states.ecsaa ? 1 : 0);

    // handle 2x2 fragment each time to compute texture coordinate differential.
    FragmentRenderInfo frag0, frag1, frag2, frag3;
    
    for (int px_y = px_y_bounds.min; px_y <= px_y_bounds.max; px_y += 2)
    {
        // point range covered by the screen triangle at y-th line pixel.
        const FloatRange pt_x_range0 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.5f);

        // point range covered by the screen triangle at (y+1)-th line pixel.
        const FloatRange pt_x_range1 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 1.5f);

        // pixel range covered by the screen triangle at these two line pixel.
        const IntRange px_x_range = ScreenPointRangeToPixelRange(
            pt_x_range0.Union(pt_x_range1), 0.5f, px_x_bounds, px_margin);

        for (int px_x = px_x_range.min; px_x <= px_x_range.max; px_x += 2)
        {
            const bool frag0_valid = true;
            const bool frag1_valid = (px_x < px_x_bounds.max);
            const bool frag2_valid = (px_y < px_y_bounds.max);
            const bool frag3_valid = (frag1_valid && frag2_valid);

            frag0.point = {px_x + 0.5f, px_y + 0.5f};
            frag1.point = {px_x + 1.5f, px_y + 0.5f};
            frag2.point = {px_x + 0.5f, px_y + 1.5f};
            frag3.point = {px_x + 1.5f, px_y + 1.5f};

            frag0.index = fbuf->ColorDataIndex(px_x,     px_y);
            frag1.index = fbuf->ColorDataIndex(px_x + 1, px_y);
            frag2.index = fbuf->ColorDataIndex(px_x,     px_y + 1);
            frag3.index = fbuf->ColorDataIndex(px_x + 1, px_y + 1);

            frag0.cover_test = frag0_valid && pt_x_range0.Contain(frag0.point.X());
            frag1.cover_test = frag1_valid && pt_x_range0.Contain(frag1.point.X());
            frag2.cover_test = frag2_valid && pt_x_range1.Contain(frag2.point.X());
            frag3.cover_test = frag3_valid && pt_x_range1.Contain(frag3.point.X());

            // ECSAA: consider pixels partially covered by current triangle.
            if (tri_states.ecsaa)
            {
                if (frag0_valid) SetupFragmentCoverRate(pt_x_range0, 1.0f, frag0);
                if (frag1_valid) SetupFragmentCoverRate(pt_x_range0, 1.0f, frag1);
                if (frag2_valid) SetupFragmentCoverRate(pt_x_range1, 1.0f, frag2);
                if (frag3_valid) SetupFragmentCoverRate(pt_x_range1, 1.0f, frag3);
            }

            // compute fragment depth and do depth test.
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag0);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag1);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag2);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag3);

            // skip if no fragment pass cover test and depth test.
            if (!frag0.depth_test && !frag1.depth_test &&
                !frag2.depth_test && !frag3.depth_test)
                continue;

            if (tri_states.texturing)
            {
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag0);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag1);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag2);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag3);
                SetupFragmentDTexCoord(tri.texcoords, frag0, frag1, frag2, frag3);
            }
            else
            {
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag0);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag1);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag2);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag3);
            }

            // compute fragment color and write to frame buffer.
            HandleFragmentInfo(tri_states, tri, frag0);
            HandleFragmentInfo(tri_states, tri, frag1);
            HandleFragmentInfo(tri_states, tri, frag2);
            HandleFragmentInfo(tri_states, tri, frag3);
        }
    }
}

void Renderer::RasterizeTriangleMSAA(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri)
{
    // compute screen information of current triangle.
    // note frame buffer size is 2x2 of screen size.
    const FrameBuffer* fbuf = tri_states.frame_buffer;
    TriangleScreenInfo screen_tri;
    ConstructTriangleScreenInfo(tri, fbuf->ColorDataSize() / 2, screen_tri);
    const IntRange& px_x_bounds = screen_tri.bounds.px_x;
    const IntRange& px_y_bounds = screen_tri.bounds.px_y;

    // ECSAA: consider pixels partially covered by current triangle.
    const int px_margin = (tri_states.ecsaa ? 1 : 0);

    // handle 2x2 sample each time in 4x MSAA.
    FragmentRenderInfo frag0, frag1, frag2, frag3;

    for (int px_y = px_y_bounds.min; px_y <= px_y_bounds.max; ++px_y)
    {
        // point range covered by the screen triangle at 2y-th line sample.
        const FloatRange pt_x_range0 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.25f);

        // point range covered by the screen triangle at (2y+1)-th line sample.
        const FloatRange pt_x_range1 = MeasureScreenTriangleCovering(
            screen_tri, px_y + 0.75f);

        // pixel range covered by the screen triangle at y-th line pixel.
        const IntRange px_x_range = ScreenPointRangeToPixelRange(
            pt_x_range0.Union(pt_x_range1), 0.25f, px_x_bounds, px_margin);

        for (int px_x = px_x_range.min; px_x <= px_x_range.max; ++px_x)
        {
            frag0.point = {px_x + 0.25f, px_y + 0.25f};
            frag1.point = {px_x + 0.75f, px_y + 0.25f};
            frag2.point = {px_x + 0.25f, px_y + 0.75f};
            frag3.point = {px_x + 0.75f, px_y + 0.75f};

            frag0.index = fbuf->ColorDataIndex(2 * px_x,     2 * px_y);
            frag1.index = fbuf->ColorDataIndex(2 * px_x + 1, 2 * px_y);
            frag2.index = fbuf->ColorDataIndex(2 * px_x,     2 * px_y + 1);
            frag3.index = fbuf->ColorDataIndex(2 * px_x + 1, 2 * px_y + 1);

            frag0.cover_test = pt_x_range0.Contain(frag0.point.X());
            frag1.cover_test = pt_x_range0.Contain(frag1.point.X());
            frag2.cover_test = pt_x_range1.Contain(frag2.point.X());
            frag3.cover_test = pt_x_range1.Contain(frag3.point.X());

            // ECSAA: consider pixels partially covered by current triangle.
            if (tri_states.ecsaa)
            {
                SetupFragmentCoverRate(pt_x_range0, 0.5f, frag0);
                SetupFragmentCoverRate(pt_x_range0, 0.5f, frag1);
                SetupFragmentCoverRate(pt_x_range1, 0.5f, frag2);
                SetupFragmentCoverRate(pt_x_range1, 0.5f, frag3);
            }

            // compute fragment depth and do depth test.
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag0);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag1);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag2);
            SetupFragmentDepthInfo(*fbuf, tri, screen_tri, frag3);

            // skip if no fragment pass cover test and depth test.
            if (!frag0.depth_test && !frag1.depth_test &&
                !frag2.depth_test && !frag3.depth_test)
                continue;
          
            if (tri_states.texturing)
            {
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag0);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag1);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag2);
                SetupFragmentInterpCoeff<true>(tri, screen_tri, frag3);
                SetupFragmentDTexCoord(tri.texcoords, frag0, frag1, frag2, frag3);
            }
            else
            {
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag0);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag1);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag2);
                SetupFragmentInterpCoeff<false>(tri, screen_tri, frag3);
            }

            // compute fragment color and write to frame buffer.
            std::optional<Vector4f> color;
            HandleFragmentInfoMSAA(tri_states, tri, frag0, color);
            HandleFragmentInfoMSAA(tri_states, tri, frag1, color);
            HandleFragmentInfoMSAA(tri_states, tri, frag2, color);
            HandleFragmentInfoMSAA(tri_states, tri, frag3, color);
        }
    }
}

//==============================================================================
// Triangle Handling - Screen Space Computing
//==============================================================================

void Renderer::ConstructTriangleScreenInfo(
    const TriangleRenderInfo& tri,
    const Vector2i& screen_size,
    TriangleScreenInfo& out)
{
    // vertex positions & edge vectors & reciprocal area
    out.a = tri.positions.v0.Head<2>();
    out.b = tri.positions.v1.Head<2>();
    out.c = tri.positions.v2.Head<2>();
    out.ab = out.b - out.a;
    out.ac = out.c - out.a;
    const float area = Geom::TriangleSignedArea2D(out.ab, out.ac);
    out.recip_area = 1.0f / area;

    if (std::abs(area) < FLT_EPSILON)
    {
        out.bounds.px_x = IntRange::EmptyRange();
        out.bounds.px_y = IntRange::EmptyRange();
        return;
    }

    // vertex positions sorted by y coordinate
    out.p0 = out.a.Cast<double>();
    out.p1 = out.b.Cast<double>();
    out.p2 = out.c.Cast<double>();
    if (out.p0.Y() > out.p1.Y()) { std::swap(out.p0, out.p1); }
    if (out.p1.Y() > out.p2.Y()) { std::swap(out.p1, out.p2); }
    if (out.p0.Y() > out.p1.Y()) { std::swap(out.p0, out.p1); }

    // reciprocal slope of p1-p0, p2-p1, p0-p2
    Vector2d edge_01 = out.p1 - out.p0;
    Vector2d edge_12 = out.p2 - out.p1;
    Vector2d edge_20 = out.p0 - out.p2;
    out.k0 = edge_01.X() / edge_01.Y();
    out.k1 = edge_12.X() / edge_12.Y();
    out.k2 = edge_20.X() / edge_20.Y();

    // bounding rectangle
    ComputeTriangleScreenBounds({out.a, out.b, out.c}, screen_size, out.bounds);
}

void Renderer::ComputeTriangleScreenBounds(
    const Geom::Triangle2f& tri,
    const Vector2i& screen_size,
    TriangleScreenBounds& bounds)
{
    const Vector3f x3 = {tri.v0.X(), tri.v1.X(), tri.v2.X()};
    const Vector3f y3 = {tri.v0.Y(), tri.v1.Y(), tri.v2.Y()};
    const int w = screen_size.X();
    const int h = screen_size.Y();
    
    const float x_min = x3.MinCoeff();
    const float x_max = x3.MaxCoeff();
    const float y_min = y3.MinCoeff();
    const float y_max = y3.MaxCoeff();

    bounds.pt_x = {std::max(x_min, 0.0f), std::min(x_max, 1.0f * w)};
    bounds.pt_y = {std::max(y_min, 0.0f), std::min(y_max, 1.0f * h)};
    bounds.px_x = ScreenPointRangeToPixelRange(bounds.pt_x, 0.5f, {0, w - 1}, 1);
    bounds.px_y = ScreenPointRangeToPixelRange(bounds.pt_y, 0.5f, {0, h - 1}, 1);
}

FloatRange Renderer::MeasureScreenTriangleCovering(
    const TriangleScreenInfo& screen_tri,
    float flt_y)
{
    // use double for high precision covering test.
    const double y = flt_y;
    
    if (screen_tri.p0.Y() <= y && y <= screen_tri.p2.Y())
    {
        float cross0 = static_cast<float>(
            screen_tri.p0.X() + (y - screen_tri.p0.Y()) * screen_tri.k2);

        float cross1 = static_cast<float>(y <= screen_tri.p1.Y()
            ? screen_tri.p0.X() + (y - screen_tri.p0.Y()) * screen_tri.k0
            : screen_tri.p1.X() + (y - screen_tri.p1.Y()) * screen_tri.k1);

        return {std::min(cross0, cross1), std::max(cross0, cross1)};
    }
    
    return FloatRange::EmptyRange();
}

IntRange Renderer::ScreenPointRangeToPixelRange(
    const FloatRange& pt_range, float pt_padding,
    const IntRange& px_bounds, int px_margin)
{
    if (!pt_range.IsEmpty())
    {
        int px_min = int(std::floor(pt_range.min + pt_padding - FLT_EPSILON));
        int px_max = int(std::floor(pt_range.max - pt_padding));
        return px_bounds.Intersect({px_min - px_margin, px_max + px_margin});
    }
    return IntRange::EmptyRange();
}

Vector3f Renderer::BaryCoordOnScreenTriangle(
    const TriangleScreenInfo& screen_tri,
    const Vector2f& p)
{
    Vector2f ap = p - screen_tri.a;
    Vector2f pb = screen_tri.b - p;
    Vector2f pc = screen_tri.c - p;

    float area_abp = Geom::TriangleSignedArea2D(screen_tri.ab, ap);
    float area_apc = Geom::TriangleSignedArea2D(ap, screen_tri.ac);
    float area_pbc = Geom::TriangleSignedArea2D(pb, pc);

    float w = area_abp * screen_tri.recip_area;
    float v = area_apc * screen_tri.recip_area;
    float u = area_pbc * screen_tri.recip_area;

    return {u, v, w};
}

Vector4f Renderer::PerspectiveCorrectInterpCoeff(
    const Vector3f& view_recip_depths,
    const Vector3f& screen_bary)
{
    // Perspective-Correct Interpolation
    // https://www.cnblogs.com/ArenAK/archive/2008/03/13/1103532.html
    float c0 = screen_bary[0] * view_recip_depths[0];
    float c1 = screen_bary[1] * view_recip_depths[1];
    float c2 = screen_bary[2] * view_recip_depths[2];
    float c3 = 1.0f / (c0 + c1 + c2);
    return {c0, c1, c2, c3};
}

//==============================================================================
// Fragment Handling
//==============================================================================

void Renderer::HandleFragmentDepth(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri,
    const TriangleScreenInfo& screen_tri,
    const Vector2f& pt, int idx)
{
    Vector3f bary = BaryCoordOnScreenTriangle(screen_tri, pt);
    float rev_depth = Math::BaryInterp3(tri.rev_depths, bary);
    assert(rev_depth < tri.max_rev_depth);
    tri_states.frame_buffer->WriteDepth(idx, rev_depth);
}

void Renderer::SetupFragmentCoverRate(
    FloatRange pt_x_range,
    float frag_size,
    FragmentRenderInfo& frag)
{
    if (!frag.cover_test)
    {
        const float dist0 = pt_x_range.min - frag.point.X();
        const float dist1 = frag.point.X() - pt_x_range.max;

        if (0.0f < dist0 && dist0 < frag_size)
        {
            frag.point.X() = pt_x_range.min;
            frag.cover_test = true;
            frag.cover_rate = 1.0f - dist0 / frag_size;
        }
        else if (0.0f < dist1 && dist1 < frag_size)
        {
            frag.point.X() = pt_x_range.max;
            frag.cover_test = true;
            frag.cover_rate = 1.0f - dist1 / frag_size;
        }
    }
    else
    {
        frag.cover_rate = 1.0f;
    }
}

void Renderer::SetupFragmentDepthInfo(
    const FrameBuffer& fbuf,
    const TriangleRenderInfo& tri,
    const TriangleScreenInfo& screen_tri,
    FragmentRenderInfo& frag)
{
    frag.depth_test = false;

    if (frag.cover_test && fbuf.DepthTest(frag.index, tri.max_rev_depth))
    {
        frag.bary = BaryCoordOnScreenTriangle(screen_tri, frag.point);
        frag.rev_depth = Math::BaryInterp3(tri.rev_depths, frag.bary);
        frag.depth_test = fbuf.DepthTest(frag.index, frag.rev_depth);
    }
}

template <bool Texturing>
void Renderer::SetupFragmentInterpCoeff(
    const TriangleRenderInfo& tri,
    const TriangleScreenInfo& screen_tri,
    FragmentRenderInfo& frag)
{
    if (Texturing && !frag.depth_test)
    {
        frag.bary = BaryCoordOnScreenTriangle(screen_tri, frag.point);
    }

    if (Texturing || frag.depth_test)
    {
        frag.interp_coeff = PerspectiveCorrectInterpCoeff(
            tri.view_recip_depths, frag.bary);
    }
}

void Renderer::SetupFragmentDTexCoord(
    const Vector2f tri_texcoords[3],
    FragmentRenderInfo& frag0, FragmentRenderInfo& frag1,
    FragmentRenderInfo& frag2, FragmentRenderInfo& frag3)
{
    // texture coordinates.
    Vector2f texcoord0 = Math::BaryInterp4(tri_texcoords, frag0.interp_coeff);
    Vector2f texcoord1 = Math::BaryInterp4(tri_texcoords, frag1.interp_coeff);
    Vector2f texcoord2 = Math::BaryInterp4(tri_texcoords, frag2.interp_coeff);
    Vector2f texcoord3 = Math::BaryInterp4(tri_texcoords, frag3.interp_coeff);

    // texture coordinates differential = texture quad area / screen quad area
    float T = Geom::QuadArea2D(texcoord0, texcoord1, texcoord2, texcoord3);
    float S = Geom::QuadArea2D(frag0.point, frag1.point, frag2.point, frag3.point);
    float D = T / S;
    frag3.dtexcoord = frag2.dtexcoord = frag1.dtexcoord = frag0.dtexcoord = D;
}

void Renderer::HandleFragmentInfo(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri,
    const FragmentRenderInfo& frag)
{
    if (frag.depth_test)
    {
        Vector4f color = ComputeFragmentColor(tri_states, tri, frag);
        AcceptFragmentColor(tri_states, frag, color);
    }
}

void Renderer::HandleFragmentInfoMSAA(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri,
    const FragmentRenderInfo& frag,
    std::optional<Vector4f>& color)
{
    if (frag.depth_test)
    {
        if (!color.has_value())
            color = ComputeFragmentColor(tri_states, tri, frag);
        AcceptFragmentColor(tri_states, frag, color.value());
    }
}

Vector4f Renderer::ComputeFragmentColor(
    const TriangleRenderStates& tri_states,
    const TriangleRenderInfo& tri,
    const FragmentRenderInfo& frag)
{
    float varyings[kMaxNumOfVaryings];

    // interpolate varings for fragment via barycentric coordinates.
    for (int i = 0; i < tri.num_varyings; ++i)
    {
        varyings[i] = Math::BaryInterp4(
            tri.varyings[0][i],
            tri.varyings[1][i],
            tri.varyings[2][i],
            frag.interp_coeff);
    }

    // compute fragment color by fragment shader.
    return (*tri_states.shader_fs)(tri_states.uniforms, varyings, frag.dtexcoord);
}

void Renderer::AcceptFragmentColor(
    const TriangleRenderStates& tri_states,
    const FragmentRenderInfo& frag,
    const Vector4f& color)
{
    if (tri_states.alpha_blending)
    {
        tri_states.frame_buffer->BlendColor(
            frag.index, frag.rev_depth, color);
    }
    else if (tri_states.ecsaa && frag.cover_rate < 1.0f)
    {
        tri_states.frame_buffer->WriteColor2nd(
            frag.index, frag.rev_depth, color.Head<3>(), frag.cover_rate);
    }
    else
    {
        tri_states.frame_buffer->WriteColor(
            frag.index, frag.rev_depth, color.Head<3>());
    }
}

}}