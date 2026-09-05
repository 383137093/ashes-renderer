#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "Math/Vector.h"

namespace Ashes {

//==============================================================================
// Texel
//==============================================================================

enum class TexelFormat : std::uint8_t
{
    FLOAT1, FLOAT2, FLOAT3, FLOAT4,
    UBYTE1, UBYTE2, UBYTE3, UBYTE4,
};

std::size_t TexelNumComponents(TexelFormat format);
std::size_t TexelComponentSizeInBytes(TexelFormat format);
std::size_t TexelSizeInBytes(TexelFormat format);


//==============================================================================
// Texture
// for texture loaded from file, their texel components is ordered as following:
// Grey | Grey,Alpha | Red,Green,Blue | Red,Green,Blue,Alpha
//==============================================================================

struct TextureMipMap
{
    TexelFormat format = TexelFormat::FLOAT1;
    int rows = 0;
    int cols = 0;
    std::vector<std::uint8_t> texels;
};

class Texture
{
public:

    // Special member functions: uncopyable but moveable
    Texture();
    Texture(const Texture&) = delete;
    Texture(Texture&&) = default;
    ~Texture();
    Texture& operator = (const Texture&) = delete;
    Texture& operator = (Texture&&) = default;

    // Create Texture: filename support mipmap level placeholders {lod}
    void CreateUninitialized(TexelFormat format, int rows, int cols);
    void CreateFromImage(TextureMipMap image);
    bool CreateFromFile(const std::string& filename_template);
    void GenerateMipMapsUninitialized(int max_lod);
    void GenerateMipMaps();

    // Texture Attributes
    const std::string& SourcePath() const;
    bool IsEmpty() const;
    TexelFormat Format() const;
    int Width() const;
    int Height() const;
    int MaxLod() const;
    float ComputeLod(float duv) const;

    // Texture Mip
    const TextureMipMap& MipMap(int lod) const;
    const TextureMipMap& MipMap0() const;
    const std::uint8_t* MipData(int lod) const;
    const std::uint8_t* MipData0() const;
    TextureMipMap& MipMap(int lod);
    TextureMipMap& MipMap0();
    std::uint8_t* MipData(int lod);
    std::uint8_t* MipData0();

private:

    std::string                source_path_;
    std::vector<TextureMipMap> mip_maps_;
    int                        max_lod_ = 0;
};


//==============================================================================
// Texture Sampling
//==============================================================================

enum class TextureAddressMode : std::uint8_t
{
    WRAP,
    CLAMP,
};

enum class TextureFilter : std::uint8_t
{
    POINT,              // point filter for texel, use level 0 for MipMap
    POINT_MIP_POINT,    // point filter for texel, point filter for MipMap
    POINT_MIP_LINEAR,   // point filter for texel, linear filter for MipMap
    LINEAR,             // linear filter for texel, use level 0 for MipMap
    LINEAR_MIP_POINT,   // linear filter for texel, point filter for MipMap
    LINEAR_MIP_LINEAR,  // linear filter for texel, linear filter for MipMap
};

struct TextureSamplerState
{
    TextureAddressMode address_mode = TextureAddressMode::CLAMP;
    TextureFilter      filter = TextureFilter::POINT;
};

template <typename T>
void SampleTextureLod(
    const Texture& tex, TextureSamplerState sampler,
    const Vector2f& uv, float lod, T& ret);

template <typename T>
void SampleTexture(
    const Texture& tex, TextureSamplerState sampler,
    const Vector2f& uv, float duv, T& ret);

}