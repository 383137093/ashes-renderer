#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/ValueRange.h"
#include "Asset/Texture.h"
#include "Utility/ThreadPool.h"
#include "Geometry/Frustum.h"
#include "Geometry/Triangle.h"
#include "Geometry/AxisAlignedBox.h"
#include "Rasterize/FrameBuffer.h"

namespace Ashes { namespace Rasterize {

using RenderShaderVS = std::function<Vector4f(
    const void* uniforms, const void* vertex, float* varyings)>;

using RenderShaderFS = std::function<Vector4f(
    const void* uniforms, float* varyings, float dtexcoord)>;

using RenderPostProcess = std::function<void(
    Vector3f* colors, int first, int last)>;

enum RenderAntialias : std::uint8_t
{
    RenderAntialias_None  = 0x0,
    RenderAntialias_ECSAA = 0x1,
    RenderAntialias_MSAA  = 0x2,
    RenderAntialias_SSAA  = 0x4,
};

struct RenderStates
{
    std::shared_ptr<ThreadPool> thread_pool;
    int                         num_threads = 1;
    Vector2i                    render_size = Vector2i::Zero();
    Vector3f                    background_color = Vector3f::Zero();
    Matrix4f                    view = Matrix4f::Identity();
    Matrix4f                    projection = Matrix4f::Identity();
    Matrix4f                    view_proj = Matrix4f::Identity();
    Matrix4f                    viewport = Matrix4f::Identity();
    std::uint8_t                antialias = RenderAntialias_None;
    bool                        backface_culling = false;
    bool                        oit = false;
    bool                        aces = false;
    bool                        gamma_correction = false;
    RenderPostProcess           post_process;
    std::shared_ptr<Texture>    color_buffer;
    std::shared_ptr<Texture>    depth_buffer;
};

struct RenderDrawCall
{
    const void*          vertices = nullptr;    // vertex buffer
    std::size_t          vertex_size = 0;       // vertex size in bytes
    const std::uint16_t* indices = nullptr;     // index buffer
    int                  num_triangles = 0;     // number of triangles
    Geom::AxisAlignedBox world_bounds;          // bounds used for frustum culling
    bool                 transparent = false;   // is transparent object?
    bool                 double_sided = false;  // disable backface culling?
    int                  num_varyings = 0;      // number of varyings per vertex
    int                  texcoord_index = -1;   // index of texcoord in float varyings
    const void*          uniforms = nullptr;    // constants passed to shaders
    RenderShaderVS       shader_vs;             // vertex shader
    RenderShaderFS       shader_fs;             // fragment shader
};

struct RenderShadowCaster
{
    Matrix4f                 view_proj;   // transformation of light frustum
    RenderShaderVS           shader_vs;   // vertex shader used for shadow pass
    std::shared_ptr<Texture> shadow_map;  // render target used for shadow pass
};


//==============================================================================
// Renderer
//==============================================================================

class Renderer
{
public:

    Renderer();
    Renderer(const Renderer&) = delete;
    ~Renderer();
    Renderer& operator = (const Renderer&) = delete;

    float GetComputedExposure() const;
    void SetRenderStates(const RenderStates& states);
    void AddDrawCall(RenderDrawCall draw_call);
    void AddShadowCaster(RenderShadowCaster caster);
    void Present();

private:

    static const int kMaxNumOfVaryings = 100;
    static const int kMaxNumOfClippedVertices = 20;

    void InitFrameBuffer();
    void DepthPass();
    void ShadowPass();
    void ShadingPass();
    void ScreenPass();
    void ReleaseResources();

private:

    //==========================================================================
    // Render Structures
    //==========================================================================

    enum class MeshPassID { DepthPass, ShadowPass, BasePass };

    struct TriangleRenderStates
    {
        MeshPassID            pass_id;         // mesh pass identifier
        bool                  ecsaa;           // enable ECSAA antialiasing?
        bool                  texturing;       // enable texture differential?
        bool                  alpha_blending;  // enable alpha blending?
        const void*           uniforms;        // constants passed to shaders
        const RenderShaderVS* shader_vs;       // shader for each vertex
        const RenderShaderFS* shader_fs;       // shader for each fragment
        FrameBuffer*          frame_buffer;    // color buffer and depth buffer
    };

    struct TriangleRenderInfo
    {
        Vector3f         view_recip_depths;  // view space vertex reciprocal depth
        Geom::Triangle3f positions;          // viewport space vertex position
        Vector3f         rev_depths;         // viewport space vertex reversed depth 
        float            max_rev_depth;      // viewport space vertex max reversed depth
        const float*     varyings[3];        // per-vertex varying data
        int              num_varyings;       // number of varying values per vertex
        Vector2f         texcoords[3];       // vertex texture coordinates
    };

    struct TriangleScreenBounds
    {
        FloatRange pt_x, pt_y;  // covered range in point coordinates
        IntRange   px_x, px_y;  // covered range in pixel coordinates
    };

    struct TriangleScreenInfo
    {
        Vector2f             a, b, c;     // vertex positions
        Vector2f             ab, ac;      // edge vectors
        Vector2d             p0, p1, p2;  // vertex positions sorted by y coordinate
        double               k0, k1, k2;  // reciprocal slope of p1-p0, p2-p1, p0-p2
        float                recip_area;  // reciprocal area
        TriangleScreenBounds bounds;      // bounding rectangle
    };
    
    struct FragmentRenderInfo
    {
        Vector2f point;         // screen space position
        bool     cover_test;    // screen space cover test result
        float    cover_rate;    // screen space cover rate used for ECSAA
        Vector3f bary;          // screen space barycentric coordinate
        int      index;         // index of corresponding pixel in frame buffer
        float    rev_depth;     // viewport space reversed depth
        bool     depth_test;    // viewport space depth test result
        Vector4f interp_coeff;  // coefficients for perspective-correct interpolation
        float    dtexcoord;     // texture coordinates differential
    };

    using TriangleRasterizer = void (*)(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri);

    struct MeshPassRenderStates
    {
        ThreadPool*           thread_pool;       // thread pool
        int                   num_threads;       // number of required threads
        Geom::Frustum         frustum;           // world space view frustum
        Matrix4f              inv_projection;    // inverse projection
        Matrix4f              viewport;          // viewport transformation
        std::uint8_t          antialias;         // antialiasing mode
        bool                  backface_culling;  // enable backface culling?
        bool                  alpha_blending;    // enable alpha blending?
        TriangleRasterizer    tri_rasterizer;    // triangle rasterization handler
        const RenderShaderVS* shadow_vs;         // vertex shader used for shadow pass
        FrameBuffer*          frame_buffer;      // depth buffer and color buffer
    };

    struct MeshPassRenderInfo
    {
        MeshPassID                   pass_id;            // mesh pass identifier
        const RenderDrawCall*        draw_calls;         // draw call array
        int                          num_draw_calls;     // number of draw calls
        std::atomic_int              pending_draw_call;  // index of next draw call
        std::vector<std::atomic_int> pending_tris;       // index of next triangle
    };

    struct ScreenLuminanceInfo
    {
        static constexpr std::size_t    kMaxBatches = 64;
        std::array<int, kMaxBatches>    pixel_count_per_batch = {0};
        std::array<double, kMaxBatches> luminance_sum_per_batch = {0.0};
        float                           computed_exposure = 1.0f;
    };

private:

    //==========================================================================
    // Mesh Pass Handling
    //==========================================================================

    template <bool NearToFar>
    static void SortDrawCalls(
        const Matrix4f& view,
        std::vector<RenderDrawCall>& draw_calls);
    
    static void ClearShadowMap(
        const std::vector<RenderShadowCaster>& casters,
        int num_threads, int thread_idx, int);

    static void DispatchMeshPass(
        const MeshPassRenderStates& pass_states,
        MeshPassRenderInfo& pass);

    static void HandleMeshPass(
        const MeshPassRenderStates& pass_states,
        MeshPassRenderInfo& pass, int thread_idx, int);

    static void CullingDrawCalls(
        const MeshPassRenderStates& pass_states,
        MeshPassRenderInfo& pass, int thread_idx);

    static bool FetchPendingTriangleFromMeshPass(
        MeshPassRenderInfo& pass,
        int& draw_call_idx, int& tri_idx);
    
    static bool FetchPendingTriangleFromDrawCall(
        MeshPassRenderInfo& pass,
        int draw_call_idx, int& tri_idx);

    //==========================================================================
    // Screen Pass Handling
    //==========================================================================

    template <typename FuncType, typename... ArgsType>
    void DispatchScreenPass(
        FuncType&& func,
        ArgsType&&... args) const;

    static void ScreenPassClear(
        const FrameBuffer* fbuf,
        int first, int last);

    static void ScreenPassECSAA(
        const FrameBuffer* fbuf,
        int first, int last);

    static void ScreenPassOIT(
        const FrameBuffer* fbuf,
        int first, int last);

    static void ScreenPassDownsampleStep1(
        const FrameBuffer* fbuf,
        Vector4f* dest,
        int first, int last);

    static void ScreenPassDownsampleStep2(
        const FrameBuffer* fbuf,
        const Vector4f* src,
        int first, int last);

    static void ScreenPassACES(
        Vector3f* colors,
        ScreenLuminanceInfo& luma_info,
        int thread_idx, int first, int last);

    static void ScreenPassGammaCorrection(
        Vector3f* colors,
        int first, int last);

    static void ScreenPassFillBackground(
        const FrameBuffer* fbuf,
        const Vector3f& color,
        int first, int last);

    //==========================================================================
    // Triangle Handling
    //==========================================================================

    static void HandleTriangle(
        const MeshPassRenderStates& pass_states,
        MeshPassID pass_id,
        const RenderDrawCall& draw_call, int tri_idx);

    static bool BackFaceCulling(
        const MeshPassRenderStates& pass_states,
        const RenderDrawCall& draw_call,
        const Vector4f* clip_vertices);

    static int HomogeneousSpaceClipping(
        Vector4f* vertices,
        float* varyings,
        int num_varyings_per_vertex);

    static void InitTriangleRenderInfo(
        TriangleRenderInfo& tri,
        int texcoord_idx_in_varyings,
        const Geom::Triangle4f& clip_tri,
        const Matrix4f& viewport);

    //==========================================================================
    // Triangle Handling - Rasterizing
    //==========================================================================

    static void RasterizeTriangleDepth(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri);

    static void RasterizeTriangleDepthMSAA(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri);

    static void RasterizeTriangle(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri);

    static void RasterizeTriangleMSAA(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri);

    //==========================================================================
    // Triangle Handling - Screen Space Computing
    //==========================================================================

    static void ConstructTriangleScreenInfo(
        const TriangleRenderInfo& tri,
        const Vector2i& screen_size,
        TriangleScreenInfo& screen_tri);

    static void ComputeTriangleScreenBounds(
        const Geom::Triangle2f& tri,
        const Vector2i& screen_size,
        TriangleScreenBounds& bounds);

    static FloatRange MeasureScreenTriangleCovering(
        const TriangleScreenInfo& screen_tri,
        float y);

    static IntRange ScreenPointRangeToPixelRange(
        const FloatRange& pt_range, float pt_padding,
        const IntRange& px_bounds, int px_margin);

    static Vector3f BaryCoordOnScreenTriangle(
        const TriangleScreenInfo& screen_tri,
        const Vector2f& p);

    static Vector4f PerspectiveCorrectInterpCoeff(
        const Vector3f& view_recip_depths,
        const Vector3f& screen_bary);

    //==========================================================================
    // Fragment Handling
    //==========================================================================

    static void HandleFragmentDepth(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri,
        const TriangleScreenInfo& screen_tri,
        const Vector2f& pt, int idx);

    static void SetupFragmentCoverRate(
        FloatRange pt_x_range,
        float frag_size,
        FragmentRenderInfo& frag);

    static void SetupFragmentDepthInfo(
        const FrameBuffer& fbuf,
        const TriangleRenderInfo& tri,
        const TriangleScreenInfo& screen_tri,
        FragmentRenderInfo& frag);

    template <bool Texturing>
    static void SetupFragmentInterpCoeff(
        const TriangleRenderInfo& tri,
        const TriangleScreenInfo& screen_tri,
        FragmentRenderInfo& frag);

    static void SetupFragmentDTexCoord(
        const Vector2f tri_texcoords[3],
        FragmentRenderInfo& frag0, FragmentRenderInfo& frag1,
        FragmentRenderInfo& frag2, FragmentRenderInfo& frag3);

    static void HandleFragmentInfo(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri,
        const FragmentRenderInfo& frag);

    static void HandleFragmentInfoMSAA(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri,
        const FragmentRenderInfo& frag,
        std::optional<Vector4f>& color);

    static Vector4f ComputeFragmentColor(
        const TriangleRenderStates& tri_states,
        const TriangleRenderInfo& tri,
        const FragmentRenderInfo& frag);

    static void AcceptFragmentColor(
        const TriangleRenderStates& tri_states,
        const FragmentRenderInfo& frag,
        const Vector4f& color);

    //==========================================================================
    // Member Variables
    //==========================================================================

private:

    RenderStates                    render_states_;
    std::vector<RenderDrawCall>     opaque_draw_calls_;
    std::vector<RenderDrawCall>     transparent_draw_calls_;
    std::vector<RenderShadowCaster> shadow_casters_;
    FrameBuffer                     frame_buffer_;
    Texture                         oit_buffer_;
    Texture                         shared_buffer_;
    ScreenLuminanceInfo             luminance_info_;
};

}}