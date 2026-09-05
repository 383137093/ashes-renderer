#pragma once

#include <array>
#include "Math/Vector.h"
#include "Asset/Texture.h"
#include "Utility/SpinLock.h"

//==============================================================================
// FrameBuffer: supporting high-performance concurrency access.
//==============================================================================

namespace Ashes { namespace Rasterize {

struct OITSample
{
    Vector4f color;
    float    depth;
};

class FrameBuffer
{
public:

    FrameBuffer();
    FrameBuffer(const FrameBuffer&) = delete;
    ~FrameBuffer();
    FrameBuffer& operator = (const FrameBuffer&) = delete;

    Texture* DepthBuffer() const;
    Texture* ColorBuffer() const;
    Texture* ColorBuffer2nd() const;
    Texture* OITBuffer() const;

    Vector2i DepthDataSize() const;
    Vector2i ColorDataSize() const;
    int DepthDataIndex(int x, int y) const;
    int ColorDataIndex(int x, int y) const;

    float* DepthData() const;
    Vector3f* ColorData() const;
    Vector4f* ColorData2nd() const;
    OITSample* OITData() const;

    void SetDepthBuffer(Texture* tex);
    void SetColorBuffer(Texture* tex);
    void SetColorBuffer2nd(Texture* tex);
    void SetOITBuffer(Texture* tex);

    void ResizeBuffers(int width, int height);
    void SwitchLockToDepthData();
    void SwitchLockToColorData();

    bool DepthTest(int idx, float depth) const;
    void WriteDepth(int idx, float depth);
    void WriteColor(int idx, float depth, Vector3f color);
    void WriteColor2nd(int idx, float depth, Vector3f color, float cover_rate);
    void BlendColor(int idx, float depth, Vector4f color);

private:

    std::array<SpinLock, 3000> locks_;
    std::size_t                lock_length_ = 0;

    Texture* depth_buffer_ = nullptr;
    Texture* color_buffer_ = nullptr;
    Texture* color_buffer_2nd_ = nullptr;
    Texture* oit_buffer_ = nullptr;

    int depth_data_w_ = 0;
    int depth_data_h_ = 0;
    int color_data_w_ = 0;
    int color_data_h_ = 0;

    float*     depth_data_ = nullptr;
    Vector3f*  color_data_ = nullptr;
    Vector4f*  color_data_2nd_ = nullptr;
    OITSample* oit_data_ = nullptr;
};

}}