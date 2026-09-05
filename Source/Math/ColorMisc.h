#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include "Math/Vector.h"
#include "Math/MathMisc.h"

namespace Ashes { namespace Color {

constexpr Vector3f kLumaFactor = {0.2126f, 0.7152f, 0.0722f};

//==============================================================================
// convert color data type
//==============================================================================

constexpr float kRecip255 = 1.0f / 255.0f;

inline float UByteToFloat(std::uint8_t b)
{
    return b * kRecip255;
}

inline Vector2f UByteToFloat(Vector2b b)
{
    return {b[0] * kRecip255, b[1] * kRecip255};
}

inline Vector3f UByteToFloat(Vector3b b)
{
    return {b[0] * kRecip255, b[1] * kRecip255, b[2] * kRecip255};
}

inline Vector4f UByteToFloat(Vector4b b)
{
    return {b[0] * kRecip255, b[1] * kRecip255, b[2] * kRecip255, b[3] * kRecip255};
}

inline std::uint8_t FloatToUByte(float f)
{
    return static_cast<std::uint8_t>(Math::Saturate(f) * 255.0f);
}


//==============================================================================
// convert color space
// << A close look at the sRGB formula >>
// https://entropymine.com/imageworsener/srgbformula/
//==============================================================================

inline float SRGBToLinear(float s)
{
    return s > 0.04045f ? std::pow((s + 0.055f) / 1.055f, 2.4f) : s / 12.92f;
}

inline Vector3f SRGBToLinear(const Vector3f& s)
{
    return {SRGBToLinear(s[0]), SRGBToLinear(s[1]), SRGBToLinear(s[2])};
}

inline Vector4f SRGBToLinear(const Vector4f& s)
{
    return {SRGBToLinear(s[0]), SRGBToLinear(s[1]), SRGBToLinear(s[2]), s[3]};
}

inline float ACESFilm(float x)
{
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return Math::Saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}


//==============================================================================
// combine color
//==============================================================================

inline void AlphaBlending(Vector3f& dest, const Vector4f& src)
{
    if (const float alpha = src.W(); alpha > 0.0f)
    {
        if (alpha < 1.0f)
        {
            dest[0] = Math::Lerp(dest[0], src[0], alpha);
            dest[1] = Math::Lerp(dest[1], src[1], alpha);
            dest[2] = Math::Lerp(dest[2], src[2], alpha);
        }
        else
        {
            std::memcpy(&dest, &src, sizeof(dest));
        }
    }
}

}}