#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"

namespace Ashes {

class Quaternion
{
public:

    static constexpr float kEpsilon = std::numeric_limits<float>::epsilon();

    //==========================================================================
    // Constant Quaternions
    //==========================================================================
    
    static constexpr Quaternion Zero()     { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    static constexpr Quaternion Identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    //==========================================================================
    // Constructors
    //==========================================================================

    Quaternion() = default;

    template <typename T>
    constexpr Quaternion(T x, T y, T z, T w)
        : storage_(x, y, z, w) {}

    template <typename T>
    constexpr explicit Quaternion(const T (&arr)[4])
        : storage_(arr) {}

    template <typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
    constexpr explicit Quaternion(T ptr)
        : storage_(ptr) {}

    constexpr explicit Quaternion(const Vector4f& v)
        : storage_(v) {}
    
    Quaternion(const Vector3f& axis, float radians)
        : Quaternion(MakeFromAxisAndAngle(axis, radians)) {}

    explicit Quaternion(const Matrix4f& m)
        : Quaternion(MakeFromRotationMatrix(m)) {}

    //==========================================================================
    // Coefficient Accessors
    //==========================================================================

    constexpr float X() const { return storage_.X(); }
    constexpr float Y() const { return storage_.Y(); }
    constexpr float Z() const { return storage_.Z(); }
    constexpr float W() const { return storage_.W(); }
    
    float& X() { return storage_.X(); }
    float& Y() { return storage_.Y(); }
    float& Z() { return storage_.Z(); }
    float& W() { return storage_.W(); }

    //==========================================================================
    // Coefficient Sequence Algorithm
    //==========================================================================
    
    constexpr bool IsApprox(const Quaternion& other, float precision = kEpsilon) const;
    constexpr bool IsIdentity(float precision = kEpsilon) const;

    friend constexpr bool operator == (const Quaternion& lhs, const Quaternion& rhs);
    friend constexpr bool operator != (const Quaternion& lhs, const Quaternion& rhs);

    //==========================================================================
    // Algebraic Algorithms
    //==========================================================================

    constexpr float SquaredNorm() const;
    float Norm() const;
    Quaternion Normalized() const;

    constexpr Quaternion Cross(const Quaternion& other) const;
    constexpr Quaternion Inverse() const;

    //==========================================================================
    // Rotation Algorithms
    //==========================================================================

    static Quaternion MakeFromAxisAndAngle(const Vector3f& axis, float radians);
    static Quaternion MakeFromEuler(const Vector3f& radians);
    static Quaternion MakeFromRotationMatrix(const Matrix4f& m);
    static Quaternion MakeFromTwoVectors(const Vector3f& a, const Vector3f& b);
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
    
    Vector3f RotationAxis() const;
    float RotationAngle() const;
    constexpr Matrix4f ToRotationMatrix() const;
    constexpr Vector3f RotateVector(const Vector3f& v) const;

private:

    Vector4f storage_;
};


//==============================================================================
// member function inline implementations
//
// << 3D Math Primer for Graphics and Game Development >> 2nd
//   Chapter 8.5 Quaternions
//
// << 四元数与三维旋转 >>
// https://krasjet.github.io/quaternion/quaternion.pdf
//==============================================================================

inline constexpr bool Quaternion::IsApprox(
    const Quaternion& other, float precision) const
{
    return storage_.IsApprox( other.storage_, precision)
        || storage_.IsApprox(-other.storage_, precision);
}

inline constexpr bool Quaternion::IsIdentity(float precision) const
{
    return IsApprox(Identity(), precision);
}

inline constexpr bool operator == (const Quaternion& lhs, const Quaternion& rhs)
{
    return lhs.IsApprox(rhs, 0);
}

inline constexpr bool operator != (const Quaternion& lhs, const Quaternion& rhs)
{
    return !lhs.IsApprox(rhs, 0);
}

inline constexpr float Quaternion::SquaredNorm() const
{
    return storage_.SquaredNorm();
}

inline float Quaternion::Norm() const
{
    return storage_.Norm();
}

inline Quaternion Quaternion::Normalized() const
{
    return Quaternion{storage_.Normalized()};
}

inline constexpr Quaternion Quaternion::Cross(const Quaternion& other) const
{
    return {X() * other.W() + Y() * other.Z() - Z() * other.Y() + W() * other.X(),
           -X() * other.Z() + Y() * other.W() + Z() * other.X() + W() * other.Y(),
            X() * other.Y() - Y() * other.X() + Z() * other.W() + W() * other.Z(),
           -X() * other.X() - Y() * other.Y() - Z() * other.Z() + W() * other.W()};
}

inline constexpr Quaternion Quaternion::Inverse() const
{
    return {-X(), -Y(), -Z(), W()};
}

inline Quaternion Quaternion::MakeFromAxisAndAngle(
    const Vector3f& axis, float radians)
{
    float sine = std::sin(0.5f * radians);
    float cosine = std::cos(0.5f * radians);
    return {sine * axis.X(), sine * axis.Y(), sine * axis.Z(), cosine};
}

inline Quaternion Quaternion::MakeFromEuler(const Vector3f& radians)
{
    Quaternion qx = MakeFromAxisAndAngle(Vector3f::UnitX(), radians.X());
    Quaternion qy = MakeFromAxisAndAngle(Vector3f::UnitY(), radians.Y());
    Quaternion qz = MakeFromAxisAndAngle(Vector3f::UnitZ(), radians.Z());
    return qz.Cross(qy).Cross(qx);
}

// << 3D Math Primer for Graphics and Game Development >> 2nd
//   Chapter 8.7.4 Converting a Matrix to a Quaternion
//   m[i][j] in the Listing 8.5 is corresponding to m[j-1][i-1] in our code
inline Quaternion Quaternion::MakeFromRotationMatrix(const Matrix4f& m)
{
    const float trace = m(0, 0) + m(1, 1) + m(2, 2);

    if (trace > 0.0f)
    {
        float w = 0.5f * std::sqrt(trace + 1.0f);
        float s = 0.25f / w;
        float x = s * (m(2, 1) - m(1, 2));
        float y = s * (m(0, 2) - m(2, 0));
        float z = s * (m(1, 0) - m(0, 1));
        return {x, y, z, w};
    }
    else if (m(0, 0) >= m(1, 1) && m(0, 0) >= m(2, 2))
    {
        float x = 0.5f * std::sqrt(m(0, 0) - m(1, 1) - m(2, 2) + 1.0f);
        float s = 0.25f / x;
        float y = s * (m(1, 0) + m(0, 1));
        float z = s * (m(0, 2) + m(2, 0));
        float w = s * (m(2, 1) - m(1, 2));
        return {x, y, z, w};
    }
    else if (m(1, 1) >= m(2, 2))
    {
        float y = 0.5f * std::sqrt(m(1, 1) - m(0, 0) - m(2, 2) + 1.0f);
        float s = 0.25f / y;
        float x = s * (m(1, 0) + m(0, 1));
        float z = s * (m(2, 1) + m(1, 2));
        float w = s * (m(0, 2) - m(2, 0));
        return {x, y, z, w};
    }
    else
    {
        float z = 0.5f * std::sqrt(m(2, 2) - m(0, 0) - m(1, 1) + 1.0f);
        float s = 0.25f / z;
        float x = s * (m(0, 2) + m(2, 0));
        float y = s * (m(2, 1) + m(1, 2));
        float w = s * (m(1, 0) - m(0, 1));
        return {x, y, z, w};
    }
}

// << Quaternion from two vectors: the final version >>
// http://lolengine.net/blog/2014/02/24/quaternion-from-two-vectors-final
inline Quaternion Quaternion::MakeFromTwoVectors(
    const Vector3f& a, const Vector3f& b)
{
    // q1=(sinx·u,cosx)                    |  q1 is the unit quaternion from u to v
    // q2=2|a||b|cosx·q1                   |  normalize(q2)=q1
    // q2=(|a||b|sin2x·u,|a||b|(cos2x+1))  |  2cosx·sinx=sin2x 2cosx·cosx=cos2x+1
    // q2=(a×b,a·b+|a||b|)                 |  2x is the angle between u and v
    
    const float norm_a_norm_b = std::sqrt(a.SquaredNorm() * b.SquaredNorm());
    const float w = norm_a_norm_b + a.Dot(b);

    // if a and b are exactly opposite, rotate 180 degrees around an arbitrary
    // orthogonal axis.
    if (w < 1e-6f * norm_a_norm_b)
    {
        return Quaternion{std::abs(a.X()) > std::abs(a.Y())
            ? Vector4f(-a.Z(), 0.f, a.X(), 0.0f).Normalized()
            : Vector4f(0.f, -a.Z(), a.Y(), 0.0f).Normalized()};
    }

    return Quaternion{a.Cross(b).Join(w).Normalized()};
}

inline Quaternion Quaternion::Slerp(
    const Quaternion& a, const Quaternion& b, float t)
{
    // cosine of angle between quaternions.
    const float cosine = a.storage_.Dot(b.storage_);

    // use linear interpolation for small angle.
    float k0 = 1.0f - t;
    float k1 = t;

    // use sphere interpolation for general angle.
    if (std::abs(cosine) <= 0.9999f)
    {
        float angle = std::acos(cosine);
        float inv_sine = 1.0f / std::sin(angle);
        k0 = std::sin((1.0f - t) * angle) * inv_sine;
        k1 = std::sin(t * angle) * inv_sine;
    }
    
    // if negative dot, negate the quaternion a, to take the shorter 4D ”arc”.
    k0 = (cosine >= 0.0f ? k0 : -k0);

    return Quaternion{(k0 * a.storage_ + k1 * b.storage_).Normalized()};
}

inline Vector3f Quaternion::RotationAxis() const
{
    float sine_squared = 1.0f - W() * W();
    if (sine_squared <= 0.0f) { return Vector3f::UnitX(); }
    float inv_sine = 1.0f / std::sqrt(sine_squared);
    return {inv_sine * X(), inv_sine * Y(), inv_sine * Z()};
}

inline float Quaternion::RotationAngle() const
{
    return 2.0f * std::acos(W());
}

// << 3D Math Primer for Graphics and Game Development >> 2nd
//   Chapter 8.7.3 Converting a Quaternion to a Matrix
inline constexpr Matrix4f Quaternion::ToRotationMatrix() const
{
    float xx2 = 2.0f * X() * X();
    float xy2 = 2.0f * X() * Y();
    float xz2 = 2.0f * X() * Z();
    float xw2 = 2.0f * X() * W();
    float yy2 = 2.0f * Y() * Y();
    float yz2 = 2.0f * Y() * Z();
    float yw2 = 2.0f * Y() * W();
    float zz2 = 2.0f * Z() * Z();
    float zw2 = 2.0f * Z() * W();

    return {-yy2 - zz2 + 1.0f,  xy2 - zw2,         xz2 + yw2,        0.0f,
             xy2 + zw2,        -xx2 - zz2 + 1.0f,  yz2 - xw2,        0.0f,
             xz2 - yw2,         yz2 + xw2,        -xx2 - yy2 + 1.0f, 0.0f,
             0.0f,              0.0f,              0.0f,             1.0f};
}

inline constexpr Vector3f Quaternion::RotateVector(const Vector3f& v) const
{
    // v'=(w^2-u·u)v+2w·u×v+2(u·v)u  |  expansion expression qvq*
    // v'=(1-2u·u)v+2w·u×v+2(u·v)u   |  assume quaternion is normalized
    // v'=v+2w·u×v+2((u·v)u-(u·u)v)  |
    // v'=v+2w·u×v+2u×(u×v)          |  vector triple product
    
    Vector3f u = {X(), Y(), Z()};
    Vector3f t = 2.0f * u.Cross(v);
    return v + W() * t + u.Cross(t);
}

}