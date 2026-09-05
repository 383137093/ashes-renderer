#include "Texture.h"
#include <array>
#include <cmath>
#include <cstring>
#include <type_traits>
#include "Math/MathMisc.h"
#include "Math/ColorMisc.h"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image.h"

namespace Ashes {

//==============================================================================
// Texel
//==============================================================================

std::size_t TexelNumComponents(TexelFormat format)
{
    switch (format)
    {
        case TexelFormat::FLOAT1: return 1;
        case TexelFormat::FLOAT2: return 2;
        case TexelFormat::FLOAT3: return 3;
        case TexelFormat::FLOAT4: return 4;
        case TexelFormat::UBYTE1: return 1;
        case TexelFormat::UBYTE2: return 2;
        case TexelFormat::UBYTE3: return 3;
        case TexelFormat::UBYTE4: return 4;
    }
    return 0;
}

std::size_t TexelComponentSizeInBytes(TexelFormat format)
{
    switch (format)
    {
        case TexelFormat::FLOAT1: return sizeof(float);
        case TexelFormat::FLOAT2: return sizeof(float);
        case TexelFormat::FLOAT3: return sizeof(float);
        case TexelFormat::FLOAT4: return sizeof(float);
        case TexelFormat::UBYTE1: return sizeof(std::uint8_t);
        case TexelFormat::UBYTE2: return sizeof(std::uint8_t);
        case TexelFormat::UBYTE3: return sizeof(std::uint8_t);
        case TexelFormat::UBYTE4: return sizeof(std::uint8_t);
    }
    return 0;
}

std::size_t TexelSizeInBytes(TexelFormat format)
{
    return TexelNumComponents(format) * TexelComponentSizeInBytes(format);
}

//==============================================================================
// TextureMipMap Utility Functions
//==============================================================================

static bool ImageLoadFromFile(const std::string& filename, TextureMipMap& image)
{
    int x, y, comp;

    if (::stbi_is_hdr(filename.data()))
    {
        if (float* data = ::stbi_loadf(filename.data(), &x, &y, &comp, 0))
        {
            image.format = TexelFormat(int(TexelFormat::FLOAT1) + comp - 1);
            image.rows = y;
            image.cols = x;
            image.texels.resize(y * x * TexelSizeInBytes(image.format));
            std::memcpy(image.texels.data(), data, image.texels.size());
            ::stbi_image_free(data);
            return true;
        }
    }
    else
    {
        if (unsigned char* data = ::stbi_load(filename.data(), &x, &y, &comp, 0))
        {
            image.format = TexelFormat(int(TexelFormat::UBYTE1) + comp - 1);
            image.rows = y;
            image.cols = x;
            image.texels.resize(y * x * TexelSizeInBytes(image.format));
            std::memcpy(image.texels.data(), data, image.texels.size());
            ::stbi_image_free(data);
            return true;
        }
    }

    return false;
}

template <typename T, std::size_t N>
static void ImageDownsampling4XImpl(
    const TextureMipMap& src,
    TextureMipMap& dest)
{
    dest.format = src.format;
    dest.rows = src.rows / 2;
    dest.cols = src.cols / 2;
    dest.texels.resize(src.texels.size() / 4);
    
    auto* src_data = reinterpret_cast<const Vector<T, N>*>(src.texels.data());
    auto* dest_data = reinterpret_cast<Vector<T, N>*>(dest.texels.data());

    for (int row = 0; row < dest.rows; ++row)
    {
        for (int col = 0; col < dest.cols; ++col)
        {
            int src_idx00 = (col * 2 + 0) + (row * 2 + 0) * src.cols;
            int src_idx10 = (col * 2 + 1) + (row * 2 + 0) * src.cols;
            int src_idx01 = (col * 2 + 0) + (row * 2 + 1) * src.cols;
            int src_idx11 = (col * 2 + 1) + (row * 2 + 1) * src.cols;
            int dest_idx = row * dest.cols + col;

            if constexpr (std::is_integral_v<T> && sizeof(T) < sizeof(int))
            {
                auto sum = Vector<int, N>::Zero();
                sum += src_data[src_idx00].template Cast<int>();
                sum += src_data[src_idx10].template Cast<int>();
                sum += src_data[src_idx01].template Cast<int>();
                sum += src_data[src_idx11].template Cast<int>();
                dest_data[dest_idx] = (sum / 4).template Cast<T>();
            }
            else
            {
                auto sum = Vector<T, N>::Zero();
                sum += src_data[src_idx00];
                sum += src_data[src_idx10];
                sum += src_data[src_idx01];
                sum += src_data[src_idx11];
                dest_data[dest_idx] = (sum / 4);
            }
        }
    }
}

static void ImageDownsampling4X(const TextureMipMap& src, TextureMipMap& dest)
{
    switch (src.format)
    {
        case TexelFormat::FLOAT1:
            return ImageDownsampling4XImpl<float, 1>(src, dest);
        case TexelFormat::FLOAT2:
            return ImageDownsampling4XImpl<float, 2>(src, dest);
        case TexelFormat::FLOAT3:
            return ImageDownsampling4XImpl<float, 3>(src, dest);
        case TexelFormat::FLOAT4:
            return ImageDownsampling4XImpl<float, 4>(src, dest);
        case TexelFormat::UBYTE1:
            return ImageDownsampling4XImpl<std::uint8_t, 1>(src, dest);
        case TexelFormat::UBYTE2:
            return ImageDownsampling4XImpl<std::uint8_t, 2>(src, dest);
        case TexelFormat::UBYTE3:
            return ImageDownsampling4XImpl<std::uint8_t, 3>(src, dest);
        case TexelFormat::UBYTE4:
            return ImageDownsampling4XImpl<std::uint8_t, 4>(src, dest);
    }
}

//==============================================================================
// Texture
//==============================================================================

Texture::Texture()
{
    CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
}

Texture::~Texture()
{
}

void Texture::CreateUninitialized(TexelFormat format, int rows, int cols)
{
    source_path_.clear();
    mip_maps_.resize(1);
    mip_maps_[0].format = format;
    mip_maps_[0].rows = rows;
    mip_maps_[0].cols = cols;
    mip_maps_[0].texels.resize(rows * cols * TexelSizeInBytes(format));
    max_lod_ = 0;
}

void Texture::CreateFromImage(TextureMipMap image)
{
    source_path_.clear();
    mip_maps_.resize(1);
    mip_maps_[0] = std::move(image);
    max_lod_ = 0;
}

bool Texture::CreateFromFile(const std::string& filename_template)
{
    const std::size_t lod_pos = filename_template.find("{lod}");

    if (lod_pos != std::string::npos)
    {
        std::string filename;
        TextureMipMap image;
        for (mip_maps_.clear(); true; mip_maps_.push_back(std::move(image)))
        {
            filename = filename_template;
            filename.replace(lod_pos, 5, std::to_string(mip_maps_.size()));
            if (!ImageLoadFromFile(filename, image)) { break; }
        }
        if (!mip_maps_.empty())
        {
            source_path_ = filename_template;
            max_lod_ = static_cast<int>(mip_maps_.size() - 1);
            return true;
        }
    }
    else
    {
        TextureMipMap image;
        if (ImageLoadFromFile(filename_template, image))
        {
            CreateFromImage(std::move(image));
            source_path_ = filename_template;
            return true;
        }
    }

    CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
    source_path_ = filename_template;
    return false;
}

void Texture::GenerateMipMapsUninitialized(int max_lod)
{
    max_lod_ = std::max(max_lod, 0);
    mip_maps_.clear();
    mip_maps_.resize(max_lod_ + 1);
}

void Texture::GenerateMipMaps()
{
    const int num_levels = 1 + (IsEmpty() ? 0 : std::min(
        static_cast<int>(std::log2f(Height() * 1.0f)),
        static_cast<int>(std::log2f(Width() * 1.0f))));

    mip_maps_.resize(num_levels);
    max_lod_ = num_levels - 1;

    for (int i = 1; i < num_levels; ++i)
    {
        ImageDownsampling4X(mip_maps_[i - 1], mip_maps_[i]);
    }
}

const std::string& Texture::SourcePath() const
{
    return source_path_;
}

bool Texture::IsEmpty() const
{
    const TextureMipMap& mip_map0 = MipMap0();
    return mip_map0.rows <= 0 || mip_map0.cols <= 0;
}

TexelFormat Texture::Format() const
{
    return MipMap0().format;
}

int Texture::Width() const
{
    return MipMap0().cols;
}

int Texture::Height() const
{
    return MipMap0().rows;
}

int Texture::MaxLod() const
{
    return max_lod_;
}

static constexpr int kLodRecipPrecision = 1000;

// precomputed convert table: 
// from decimal part of pixel magnification to decimal part of lod.
// NOTE: global static is faster than local static, is it because thread-safe
// initialization since C++11?
static const auto kMagDecimalToLodDecimal = []() {
    std::array<float, kLodRecipPrecision> table;
    for (int i = 0; i < static_cast<int>(table.size()); ++i) {
        float mag = i / static_cast<float>(table.size());
        float lod = (Math::FastPow2(2.0f * mag) - 1.0f) / 3.0f;
        table[i] = lod; };
    return table; }();

float Texture::ComputeLod(float duv) const
{
    // compute pixel-size in texel:
    // how many texels is covered by one pixel?
    float pixel_size = duv * Width() * Height();
    if (pixel_size <= 1.0f) { return 0.0f; }

    // compute magnification of pixel: pixel-size = 4^magnification.
    // the integer part of magnification is also the integer part of lod.
    float mag = Math::FastLog2(pixel_size) * 0.5f;
    float mag_i = static_cast<float>(static_cast<int>(mag));
    if (mag_i >= max_lod_) { return mag_i; }

    // convert decimal part of magnification to decimal part of lod.
    // Lerp(4^mag_i, 4^(mag_i+1), lod_t) = 4^(mag_i+mag_t)
    float mag_t = mag - mag_i;
    float lod_t = kMagDecimalToLodDecimal[
        static_cast<int>(mag_t * kLodRecipPrecision)];
    
    return mag_i + lod_t;
}

const TextureMipMap& Texture::MipMap(int lod) const
{
    return mip_maps_[lod];
}

const TextureMipMap& Texture::MipMap0() const
{
    return mip_maps_[0];
}

const std::uint8_t* Texture::MipData(int lod) const
{
    return mip_maps_[lod].texels.data();
}

const std::uint8_t* Texture::MipData0() const
{
    return mip_maps_[0].texels.data();
}

TextureMipMap& Texture::MipMap(int lod)
{
    return mip_maps_[lod];
}

TextureMipMap& Texture::MipMap0()
{
    return mip_maps_[0];
}

std::uint8_t* Texture::MipData(int lod)
{
    return mip_maps_[lod].texels.data();
}

std::uint8_t* Texture::MipData0()
{
    return mip_maps_[0].texels.data();
}

//==============================================================================
// Texel Utility Functions
// TexelRead: read texel at specified index by format.
// TexelBilinearInterp: read four adjacent texels and do bilinear interpolation.
// TexelReinterpretCast: reinterpret texel type safely.
//==============================================================================

template <typename T, TexelFormat Format>
static inline T TexelRead(const void*, int)
{
    assert(false);
    return {};
}

template <>
inline float TexelRead<float, TexelFormat::FLOAT1>(const void* data, int idx)
{
    return static_cast<const float*>(data)[idx];
}

template <>
inline Vector2f TexelRead<Vector2f, TexelFormat::FLOAT2>(const void* data, int idx)
{
    return static_cast<const Vector2f*>(data)[idx];
}

template <>
inline Vector3f TexelRead<Vector3f, TexelFormat::FLOAT3>(const void* data, int idx)
{
    return static_cast<const Vector3f*>(data)[idx];
}

template <>
inline Vector4f TexelRead<Vector4f, TexelFormat::FLOAT4>(const void* data, int idx)
{
    return static_cast<const Vector4f*>(data)[idx];
}

template <>
inline float TexelRead<float, TexelFormat::UBYTE1>(const void* data, int idx)
{
    return Color::UByteToFloat(static_cast<const std::uint8_t*>(data)[idx]);
}

template <>
inline Vector2f TexelRead<Vector2f, TexelFormat::UBYTE2>(const void* data, int idx)
{
    return Color::UByteToFloat(static_cast<const Vector2b*>(data)[idx]);
}

template <>
inline Vector3f TexelRead<Vector3f, TexelFormat::UBYTE3>(const void* data, int idx)
{
    return Color::UByteToFloat(static_cast<const Vector3b*>(data)[idx]);
}

template <>
inline Vector4f TexelRead<Vector4f, TexelFormat::UBYTE4>(const void* data, int idx)
{
    return Color::UByteToFloat(static_cast<const Vector4b*>(data)[idx]);
}

template <typename T, TexelFormat Format>
static inline T TexelBilinearInterp(
    const void* data, float s, float t,
    int idx00, int idx10, int idx01, int idx11)
{
    assert(0.0f <= s && s <= 1.0f);
    assert(0.0f <= t && t <= 1.0f);

    float a00 = (1 - s) * (1 - t);
    float a10 = s * (1 - t);
    float a01 = (1 - s) * t;
    float a11 = s * t;

    T v00 = TexelRead<T, Format>(data, idx00);
    T v10 = TexelRead<T, Format>(data, idx10);
    T v01 = TexelRead<T, Format>(data, idx01);
    T v11 = TexelRead<T, Format>(data, idx11);

    return a00 * v00 + a10 * v10 + a01 * v01 + a11 * v11;
}

template <typename SrcType, typename DestType>
static inline void TexelReinterpretCast(const SrcType& src, DestType& dest)
{
    if constexpr (sizeof(SrcType) < sizeof(DestType))
    {
        // extend smaller type to bigger type.
        std::memcpy(&dest, &src, sizeof(SrcType));
    }
    else
    {
        // truncate bigger type to smaller type.
        dest = reinterpret_cast<const DestType&>(src);
    }
}

//==============================================================================
// TextureAddressMode
//==============================================================================

static inline Vector2f NormalizedUVWrap(const Vector2f& uv)
{
    return {uv.X() - std::floor(uv.X()), uv.Y() - std::floor(uv.Y())};
}

static inline Vector2f NormalizedUVClamp(const Vector2f& uv)
{
    return {Math::Saturate(uv.X()), Math::Saturate(uv.Y())};
}

//==============================================================================
// TextureFilter
//==============================================================================

template <typename T, TexelFormat Format>
struct TextureFilter_PointMipPoint
{
    T operator () (const Texture& tex, const Vector2f& uv, float lod) const
    {
        // use the MipMap nearest to specified lod.
        const int lod_i = std::min(static_cast<int>(lod + 0.5f), tex.MaxLod());
        const TextureMipMap& mip_map = tex.MipMap(lod_i);
        const void* texels = mip_map.texels.data();

        // compute (x, y) coordinates corresponding to (u, v) address. 
        const float x = uv[0] * mip_map.cols;
        const float y = (1.0f - uv[1]) * mip_map.rows;
        assert(0.0f <= x && x <= mip_map.cols);
        assert(0.0f <= y && y <= mip_map.rows);

        // compute (col, row) of the texel where the (x, y) is.
        const int col = std::min(static_cast<int>(x), mip_map.cols - 1);
        const int row = std::min(static_cast<int>(y), mip_map.rows - 1);
        assert(0 <= col && col < mip_map.cols);
        assert(0 <= row && row < mip_map.rows);

        // compute index of the texel nearest to (x, y).
        const int idx = row * mip_map.cols + col;

        return TexelRead<T, Format>(texels, idx);
    }
};

template <typename T, TexelFormat Format>
struct TextureFilter_LinearMipPoint
{
    T operator () (const Texture& tex, const Vector2f& uv, float lod) const
    {
        // use the MipMap nearest to specified lod.
        const int lod_i = std::min(static_cast<int>(lod + 0.5f), tex.MaxLod());
        const TextureMipMap& mip_map = tex.MipMap(lod_i);
        const void* texels = mip_map.texels.data();

        // compute (x, y) coordinates corresponding to (u, v) address. 
        const float x = uv[0] * mip_map.cols;
        const float y = (1.0f - uv[1]) * mip_map.rows;
        assert(0.0f <= x && x <= mip_map.cols);
        assert(0.0f <= y && y <= mip_map.rows);

        // compute (col, row) of the texel where the (x, y) is.
        const int col0 = std::min(static_cast<int>(x), mip_map.cols - 1);
        const int row0 = std::min(static_cast<int>(y), mip_map.rows - 1);
        assert(0 <= col0 && col0 < mip_map.cols);
        assert(0 <= row0 && row0 < mip_map.rows);

        // compute signed distances from (x, y) to the center of texel (col, row).
        const float sdx = (col0 + 0.5f) - x;
        const float sdy = (row0 + 0.5f) - y;
        assert(-0.5f <= sdx && sdx <= 0.5f);
        assert(-0.5f <= sdy && sdy <= 0.5f);

        // find the column secondly nearest to (x, y).
        const int col1 = (sdx < 0.0f
            ? std::min(col0 + 1, mip_map.cols - 1)
            : std::max(col0 - 1, 0));

        // find the row secondly nearest to (x, y).
        const int row1 = (sdy < 0.0f
            ? std::min(row0 + 1, mip_map.rows - 1)
            : std::max(row0 - 1, 0));

        // compute indices of the 4 texels nearest to (x, y).
        const int idx00 = col0 + row0 * mip_map.cols;
        const int idx10 = col1 + row0 * mip_map.cols;
        const int idx01 = col0 + row1 * mip_map.cols;
        const int idx11 = col1 + row1 * mip_map.cols;

        // compute interpolation coefficients.
        const float s = std::abs(sdx);
        const float t = std::abs(sdy);

        return TexelBilinearInterp<T, Format>(texels, s, t,
            idx00, idx10, idx01, idx11);
    }
};

template <typename TextureFilter_MipPoint>
struct TextureFilter_MipLinear
{
    using T = std::invoke_result_t<
        TextureFilter_MipPoint, Texture, Vector2f, float>;

    T operator () (const Texture& tex, const Vector2f& uv, float lod) const
    {
        static constexpr float kLerpThresholdMin = 0.001f;
        static constexpr float kLerpThresholdMax = 0.999f;

        const float lod_i = static_cast<float>(static_cast<int>(lod));
        const float lod_t = lod - lod_i;

        if (lod_t <= kLerpThresholdMin)
            return TextureFilter_MipPoint()(tex, uv, lod_i);

        if (lod_t >= kLerpThresholdMax)
            return TextureFilter_MipPoint()(tex, uv, lod_i + 1.0f);

        T v0 = TextureFilter_MipPoint()(tex, uv, lod_i);
        T v1 = TextureFilter_MipPoint()(tex, uv, lod_i + 1.0f);
        return Math::Lerp(v0, v1, lod_t);
    }
};

template <typename T, TexelFormat Format>
using TextureFilter_PointMipLinear = 
    TextureFilter_MipLinear<TextureFilter_PointMipPoint<T, Format>>;

template <typename T, TexelFormat Format>
using TextureFilter_LinearMipLinear = 
    TextureFilter_MipLinear<TextureFilter_LinearMipPoint<T, Format>>;

//==============================================================================
// SampleTexture
//==============================================================================

template <typename T, template <typename, TexelFormat> typename Filter>
static void SampleTextureImpl(
    const Texture& tex, const Vector2f& uv, float lod, T& ret)
{
    switch (tex.Format())
    {
        case TexelFormat::FLOAT1:
        {
            using Filter1f = Filter<float, TexelFormat::FLOAT1>;
            float texel = Filter1f()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::FLOAT2:
        {
            using Filter2f = Filter<Vector2f, TexelFormat::FLOAT2>;
            Vector2f texel = Filter2f()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::FLOAT3:
        {
            using Filter3f = Filter<Vector3f, TexelFormat::FLOAT3>;
            Vector3f texel = Filter3f()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::FLOAT4:
        {
            using Filter4f = Filter<Vector4f, TexelFormat::FLOAT4>;
            Vector4f texel = Filter4f()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::UBYTE1:
        {
            using Filter1b = Filter<float, TexelFormat::UBYTE1>;
            float texel = Filter1b()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::UBYTE2:
        {
            using Filter2b = Filter<Vector2f, TexelFormat::UBYTE2>;
            Vector2f texel = Filter2b()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::UBYTE3:
        {
            using Filter3b = Filter<Vector3f, TexelFormat::UBYTE3>;
            Vector3f texel = Filter3b()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
        case TexelFormat::UBYTE4:
        {
            using Filter4b = Filter<Vector4f, TexelFormat::UBYTE4>;
            Vector4f texel = Filter4b()(tex, uv, lod);
            return TexelReinterpretCast(texel, ret);
        }
    }
}

template <typename T>
void SampleTextureLod(
    const Texture& tex, TextureSamplerState sampler,
    const Vector2f& uv, float lod, T& ret)
{
    Vector2f normalized_uv = uv;
    if (sampler.address_mode == TextureAddressMode::WRAP)
        normalized_uv = NormalizedUVWrap(uv);
    else if (sampler.address_mode == TextureAddressMode::CLAMP)
        normalized_uv = NormalizedUVClamp(uv);

    switch (sampler.filter)
    {
        case TextureFilter::POINT:
            return SampleTextureImpl<T, TextureFilter_PointMipPoint>(
                tex, normalized_uv, lod, ret);
        case TextureFilter::POINT_MIP_POINT:
            return SampleTextureImpl<T, TextureFilter_PointMipPoint>(
                tex, normalized_uv, lod, ret);
        case TextureFilter::POINT_MIP_LINEAR:
            return SampleTextureImpl<T, TextureFilter_PointMipLinear>(
                tex, normalized_uv, lod, ret);
        case TextureFilter::LINEAR:
            return SampleTextureImpl<T, TextureFilter_LinearMipPoint>(
                tex, normalized_uv, lod, ret);
        case TextureFilter::LINEAR_MIP_POINT:
            return SampleTextureImpl<T, TextureFilter_LinearMipPoint>(
                tex, normalized_uv, lod, ret);
        case TextureFilter::LINEAR_MIP_LINEAR:
            return SampleTextureImpl<T, TextureFilter_LinearMipLinear>(
                tex, normalized_uv, lod, ret);
    }
}

template <typename T>
void SampleTexture(
    const Texture& tex, TextureSamplerState sampler,
    const Vector2f& uv, float duv, T& ret)
{
    const float lod = ((
        sampler.filter == TextureFilter::POINT ||
        sampler.filter == TextureFilter::LINEAR)
        ? 0.0f : tex.ComputeLod(duv));
    return SampleTextureLod(tex, sampler, uv, lod, ret);
}

template void SampleTextureLod<float>(
    const Texture&, TextureSamplerState, const Vector2f&, float, float&);

template void SampleTextureLod<Vector2f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector2f&);

template void SampleTextureLod<Vector3f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector3f&);

template void SampleTextureLod<Vector4f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector4f&);

template void SampleTexture<float>(
    const Texture&, TextureSamplerState, const Vector2f&, float, float&);

template void SampleTexture<Vector2f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector2f&);

template void SampleTexture<Vector3f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector3f&);

template void SampleTexture<Vector4f>(
    const Texture&, TextureSamplerState, const Vector2f&, float, Vector4f&);    

}