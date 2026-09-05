#include "FrameBuffer.h"
#include <mutex>
#include <algorithm>
#include "Math/ColorMisc.h"

namespace Ashes { namespace Rasterize {

FrameBuffer::FrameBuffer()
{
}

FrameBuffer::~FrameBuffer()
{
}

Texture* FrameBuffer::DepthBuffer() const
{
    return depth_buffer_;
}

Texture* FrameBuffer::ColorBuffer() const
{
    return color_buffer_;
}

Texture* FrameBuffer::ColorBuffer2nd() const
{
    return color_buffer_2nd_;
}

Texture* FrameBuffer::OITBuffer() const
{
    return oit_buffer_;
}

Vector2i FrameBuffer::DepthDataSize() const
{
    return {depth_data_w_, depth_data_h_};
}

Vector2i FrameBuffer::ColorDataSize() const
{
    return {color_data_w_, color_data_h_};
}

int FrameBuffer::DepthDataIndex(int x, int y) const
{
    return ((depth_data_h_ - 1 - y) * depth_data_w_ + x);
}

int FrameBuffer::ColorDataIndex(int x, int y) const
{
    return ((color_data_h_ - 1 - y) * color_data_w_ + x);
}

float* FrameBuffer::DepthData() const
{
    return depth_data_;
}

Vector3f* FrameBuffer::ColorData() const
{
    return color_data_;
}

Vector4f* FrameBuffer::ColorData2nd() const
{
    return color_data_2nd_;
}

OITSample* FrameBuffer::OITData() const
{
    return oit_data_;
}

void FrameBuffer::SetDepthBuffer(Texture* tex)
{
    if (tex != nullptr)
    {
        depth_buffer_ = tex;
        depth_data_w_ = tex->Width();
        depth_data_h_ = tex->Height();
        depth_data_ = reinterpret_cast<float*>(tex->MipData0());
    }
    else
    {
        depth_buffer_ = nullptr;
        depth_data_w_ = 0;
        depth_data_h_ = 0;
        depth_data_ = nullptr;
    }
}

void FrameBuffer::SetColorBuffer(Texture* tex)
{
    if (tex != nullptr)
    {
        color_buffer_ = tex;
        color_data_w_ = tex->Width();
        color_data_h_ = tex->Height();
        color_data_ = reinterpret_cast<Vector3f*>(tex->MipData0());
        SetColorBuffer2nd(color_buffer_2nd_);
    }
    else
    {
        color_buffer_ = nullptr;
        color_data_w_ = 0;
        color_data_h_ = 0;
        color_data_ = nullptr;
        SetColorBuffer2nd(color_buffer_2nd_);
    }
}

void FrameBuffer::SetColorBuffer2nd(Texture* tex)
{
    if (tex != nullptr)
    {
        tex->CreateUninitialized(TexelFormat::FLOAT4, color_data_h_, color_data_w_);
        color_buffer_2nd_ = tex;
        color_data_2nd_ = reinterpret_cast<Vector4f*>(tex->MipData0());
    }
    else
    {
        color_buffer_2nd_ = nullptr;
        color_data_2nd_ = nullptr;
    }
}

void FrameBuffer::SetOITBuffer(Texture* tex)
{
    if (tex != nullptr)
    {
        int oit_data_w = color_data_w_ * 4 * sizeof(OITSample) / sizeof(float);
        int oit_data_h = color_data_h_;
        tex->CreateUninitialized(TexelFormat::FLOAT1, oit_data_h, oit_data_w);
        oit_buffer_ = tex;
        oit_data_ = reinterpret_cast<OITSample*>(tex->MipData0());
    }
    else
    {
        oit_buffer_ = nullptr;
        oit_data_ = nullptr;
    }
}

void FrameBuffer::ResizeBuffers(int width, int height)
{
    if (depth_buffer_ && (depth_data_w_ != width || depth_data_h_ != height))
    {
        depth_buffer_->CreateUninitialized(TexelFormat::FLOAT1, height, width);
        SetDepthBuffer(depth_buffer_);
        SwitchLockToDepthData();
    }

    if (color_buffer_ && (color_data_w_ != width || color_data_h_ != height))
    {
        color_buffer_->CreateUninitialized(TexelFormat::FLOAT3, height, width);
        SetColorBuffer(color_buffer_);
        SetColorBuffer2nd(color_buffer_2nd_);
        SetOITBuffer(oit_buffer_);
        SwitchLockToColorData();
    }
}

void FrameBuffer::SwitchLockToDepthData()
{
    int num_depth_data = depth_data_w_ * depth_data_h_;
    lock_length_ = (num_depth_data + locks_.size() - 1) / locks_.size();
}

void FrameBuffer::SwitchLockToColorData()
{
    int num_color_data = color_data_w_ * color_data_h_;
    lock_length_ = (num_color_data + locks_.size() - 1) / locks_.size();
}

bool FrameBuffer::DepthTest(int idx, float depth) const
{
    // we use inverse depth, so bigger depth value is nearer.
    // we use depth pass, so thread safe is not required.
    return depth >= depth_data_[idx];
}

void FrameBuffer::WriteDepth(int idx, float depth)
{
    if (DepthTest(idx, depth))
    {
        std::lock_guard locker(locks_[idx / lock_length_]);
        depth_data_[idx] = std::max(depth_data_[idx], depth);
    }
}

void FrameBuffer::WriteColor(int idx, float depth, Vector3f color)
{
    if (DepthTest(idx, depth))
    {
        std::lock_guard locker(locks_[idx / lock_length_]);
        color_data_[idx] = color;
    }
}

void FrameBuffer::WriteColor2nd(int idx, float depth, Vector3f color, float cover_rate)
{
    if (DepthTest(idx, depth) && cover_rate >= color_data_2nd_[idx].W())
    {
        std::lock_guard locker(locks_[idx / lock_length_]);
        if (cover_rate >= color_data_2nd_[idx].W())
            color_data_2nd_[idx] = color.Join(cover_rate);
    }
}

void FrameBuffer::BlendColor(int idx, float depth, Vector4f color)
{
    if (color.W() > 0.0f && DepthTest(idx, depth))
    {
        if (oit_data_ != nullptr)
        {
            // directly overwrite the farthest sample with the new sample.
            // blending would be more accurate but we keep things simple.
            OITSample* samples = oit_data_ + 4 * idx;
            std::lock_guard locker(locks_[idx / lock_length_]);
            int i = (samples[0].depth < samples[1].depth ? 0 : 1);
            int j = (samples[2].depth < samples[3].depth ? 2 : 3);
            int k = (samples[i].depth < samples[j].depth ? i : j);
            samples[k] = {color, depth};
        }
        else
        {
            std::lock_guard locker(locks_[idx / lock_length_]);
            Color::AlphaBlending(color_data_[idx], color);
        }
    }
}

}}