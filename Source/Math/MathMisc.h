#pragma once

#include <cmath>
#include <ctime>
#include <random>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

namespace Ashes { namespace Math {

//==============================================================================
// Formula
//==============================================================================

static constexpr double PI = 3.1415926535897932384626433832795;
static constexpr double InvPI = 1.0 / PI;

template <typename T>
inline constexpr T Degrees(T radians)
{
    return radians * static_cast<T>(180 / PI);
}

template <typename T>
inline constexpr T Radians(T degrees)
{
    return degrees * static_cast<T>(PI / 180);
}

template <typename T>
inline T Clamp(T v, T lo, T hi)
{
    return std::min(std::max(lo, v), hi);
}

template <typename T>
inline T Saturate(T v)
{
    return Clamp(v, static_cast<T>(0), static_cast<T>(1));
}

template <typename T, typename F>
inline T Lerp(const T& a, const T& b, F t)
{
    return a * (static_cast<F>(1) - t) + b * t;
}

template <typename T, typename V>
inline T BaryInterp3(const T& a, const T& b, const T& c, const V& t)
{
    return t[0] * a + t[1] * b + t[2] * c;
}

template <typename T, typename V>
inline auto BaryInterp3(const T& v, const V& t)
{
    return t[0] * v[0] + t[1] * v[1] + t[2] * v[2];
}

template <typename T, typename V>
inline T BaryInterp4(const T& a, const T& b, const T& c, const V& t)
{
    return (t[0] * a + t[1] * b + t[2] * c) * t[3];
}

template <typename T, typename V>
inline auto BaryInterp4(const T& v, const V& t)
{
    return (t[0] * v[0] + t[1] * v[1] + t[2] * v[2]) * t[3];
}


//==============================================================================
// Fast Formula
// << Fast Approximate Logarithm, Exponential, Power, and Inverse Root >>
// http://www.machinedlearnings.com/2011/06/fast-approximate-logarithm-exponential.html
//==============================================================================

inline float FastLog2(float x)
{
    union { float f; std::uint32_t i; } vx = {x};
    union { std::uint32_t i; float f; } mx = {(vx.i & 0x007FFFFF) | (0x7e << 23)};
    float y = vx.i * 1.0f / (1 << 23) - 124.22544637f;
    return y - 1.498030302f * mx.f - 1.72587999f / (0.3520887068f + mx.f);
}

inline float FastPow2(float x)
{
    float y = x - static_cast<int>(x) + std::signbit(x);
    float z = x + 121.2740838f + 27.7280233f / (4.84252568f - y) - 1.49012907f * y;
    float w = (1 << 23) * z;
    union { std::uint32_t i; float f; } mw = {static_cast<std::uint32_t>(w)};
    return mw.f;
}


//==============================================================================
// Random
//==============================================================================

inline void ResetRandomSeed()
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
}

inline float RandomFloat()
{
    return static_cast<float>(std::rand()) / RAND_MAX;
}

inline float RandomFloatInRange(float a, float b)
{
    return RandomFloat() * (b - a) + a;
}

inline bool RandomBool(float weight = 0.5f)
{
    if (weight <= 0.0f) { return false; }
    if (weight >= 1.0f) { return true; }
    return RandomFloat() < weight;
}

}}