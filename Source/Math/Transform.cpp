#include "Transform.h"
#include <cmath>
#include <cfloat>
#include "Math/MathMisc.h"

namespace Ashes {

namespace TransformationMatrix {

Matrix4f Translation(const Vector3f& offset)
{
    return {1.0f, 0.0f, 0.0f, offset.X(),
            0.0f, 1.0f, 0.0f, offset.Y(),
            0.0f, 0.0f, 1.0f, offset.Z(),
            0.0f, 0.0f, 0.0f, 1.0f};
}

Matrix4f Scale(const Vector3f& scale)
{
    return {scale.X(), 0.0f,      0.0f,      0.0f,
            0.0f,      scale.Y(), 0.0f,      0.0f,
            0.0f,      0.0f,      scale.Z(), 0.0f,
            0.0f,      0.0f,      0.0f,      1.0f};
}

Matrix4f Rotation(const Vector3f& axis, float degrees)
{
    const float radians = Math::Radians(degrees);
    const float cos_angle = std::cos(radians);
    const float sin_angle = std::sin(radians);
    
    Matrix3f axis_matrix = {
        0.0f,     -axis.Z(),  axis.Y(),
        axis.Z(),  0.0f,     -axis.X(),
       -axis.Y(),  axis.X(),  0.0f};

    Matrix3f transform3 =
        (cos_angle * Matrix3f::Identity()) +
        ((1.0f - cos_angle) * axis * Matrix<float, 1, 3>(axis)) +
        (sin_angle * axis_matrix);

    Matrix4f transform4 = transform3.Reshaped<4, 4>();
    transform4(3, 3) = 1.0f;
        
    return transform4;
}

Matrix4f RotationX(float degrees)
{
    return Rotation(Vector3f::UnitX(), degrees);
}

Matrix4f RotationY(float degrees)
{
    return Rotation(Vector3f::UnitY(), degrees);
}

Matrix4f RotationZ(float degrees)
{
    return Rotation(Vector3f::UnitZ(), degrees);
}

Matrix4f RotationEuler(const Vector3f& degrees)
{
    return RotationZ(degrees.Z()) * RotationY(degrees.Y()) * RotationX(degrees.X());
}

Matrix4f LookAt(const Vector3f& camera, const Vector3f& target, const Vector3f& up)
{
    Vector3f zaxis = (camera - target).Normalized();
    Vector3f xaxis = up.Cross(zaxis).Normalized();
    Vector3f yaxis = zaxis.Cross(xaxis);
    
    Matrix4f rotation = {
        xaxis.X(), xaxis.Y(), xaxis.Z(), 0.0f,
        yaxis.X(), yaxis.Y(), yaxis.Z(), 0.0f,
        zaxis.X(), zaxis.Y(), zaxis.Z(), 0.0f,
        0.0f,      0.0f,      0.0f,      1.0f};
    
    Matrix4f translation = Translation(-camera);
    
    return rotation * translation;
}

Matrix4f LookSquarelyAt(const Vector3f& camera, const Vector3f& target)
{
    // by default, use world y axis as up vector.
    Vector3f look = (target - camera).Normalized();
    Vector3f up = Vector3f::UnitY();

    // if look along y axis, choose another axis as up vector.
    float cos = look.Dot(up);
    if (cos >= +1.0f - FLT_EPSILON) { up =  Vector3f::UnitZ(); }
    if (cos <= -1.0f + FLT_EPSILON) { up = -Vector3f::UnitZ(); }
    
    return LookAt(camera, target, up); 
}

Matrix4f PerspectiveProjection(float fov, float aspect_ratio, float near, float far)
{
    const float tan_half_fov = std::tan(Math::Radians(0.5f * fov));
    const float m00 = 1.0f / (tan_half_fov * aspect_ratio);
    const float m11 = 1.0f / (tan_half_fov);
    const float m22 = near / (far - near);
    const float m23 = far * m22;

    return {m00,  0.0f,  0.0f, 0.0f,
            0.0f, m11,   0.0f, 0.0f,
            0.0f, 0.0f,  m22,  m23,
            0.0f, 0.0f, -1.0f, 0.0f};
}

Matrix4f OrthographicProjection(const Vector3f& minv, const Vector3f& maxv)
{
    const float recip_width  = 1.0f / (maxv.X() - minv.X());
    const float recip_height = 1.0f / (maxv.Y() - minv.Y());
    const float recip_depth  = 1.0f / (maxv.Z() - minv.Z());
    
    const float m00 = recip_width  + recip_width;
    const float m11 = recip_height + recip_height;
    const float m22 = recip_depth;
    const float m03 = -(minv.X() + maxv.X()) * recip_width;
    const float m13 = -(minv.Y() + maxv.Y()) * recip_height;
    const float m23 = -minv.Z() * recip_depth;

    return {m00,  0.0f, 0.0f, m03,
            0.0f, m11,  0.0f, m13,
            0.0f, 0.0f, m22,  m23,
            0.0f, 0.0f, 0.0f, 1.0f};
}

Matrix4f Viewport(int screen_width, int screen_height)
{
    const float halfw = 0.5f * screen_width;
    const float halfh = 0.5f * screen_height;
    
    return {halfw, 0.0f,  0.0f, halfw,
            0.0f,  halfh, 0.0f, halfh,
            0.0f,  0.0f,  1.0f, 0.0f,
            0.0f,  0.0f,  0.0f, 1.0f};
}

const Matrix4f& NDCToUV()
{
    static const Matrix4f M = {
        0.5f, 0.0f, 0.0f, 0.5f,
        0.0f, 0.5f, 0.0f, 0.5f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    return M;
}

Vector3f TransformVector(const Matrix4f& m, const Vector3f& v)
{
    return (m * v.Join(0.0f)).Head<3>();
}

Vector3f TransformDirection(const Matrix4f& m, const Vector3f& dir)
{
    return TransformVector(m, dir).Normalized();
}

Vector3f TransformPoint(const Matrix4f& m, const Vector3f& p)
{
    return (m * p.Join(1.0f)).Head<3>();
}

Vector3f TransformPointDivideW(const Matrix4f& m, const Vector3f& p)
{
    Vector4f q = m * p.Join(1.0f);
    return q.Head<3>() * (1.0f / q.W());
}

}

const TransformationSRT& TransformationSRT::Identity()
{
    static const TransformationSRT kIdentity = {
        Vector3f::Ones(), Quaternion::Identity(), Vector3f::Zero()};
    return kIdentity;
}

TransformationSRT::TransformationSRT(
    const Vector3f& s, const Quaternion& r, const Vector3f& t)
    : scale(s) , rotation(r), translation(t)
{
}

TransformationSRT::TransformationSRT(const Matrix4f& m)
{
    translation.X() = m(0, 3);
    translation.Y() = m(1, 3);
    translation.Z() = m(2, 3);

    Vector3f col0 = {m(0, 0), m(1, 0), m(2, 0)};
    Vector3f col1 = {m(0, 1), m(1, 1), m(2, 1)};
    Vector3f col2 = {m(0, 2), m(1, 2), m(2, 2)};

    scale.X() = col0.Norm();
    scale.Y() = col1.Norm();
    scale.Z() = col2.Norm();

    col0 *= 1.0f / scale.X();
    col1 *= 1.0f / scale.Y();
    col2 *= 1.0f / scale.Z();
    
    const Matrix4f rotation_matrix = {
        col0[0], col1[0], col2[0], 0.0f,
        col0[1], col1[1], col2[1], 0.0f,
        col0[2], col1[2], col2[2], 0.0f,
        0.0f,    0.0f,    0.0f,    1.0f};

    rotation = Quaternion(rotation_matrix);
}

Vector3f TransformationSRT::TransformVector(const Vector3f& v) const
{
    return rotation.RotateVector(v * scale);
}

Vector3f TransformationSRT::TransformDirection(const Vector3f& dir) const
{
    return rotation.RotateVector(dir);
}

Vector3f TransformationSRT::TransformPoint(const Vector3f& p) const
{
    return rotation.RotateVector(p * scale) + translation;
}

Matrix4f TransformationSRT::ToMatrix() const
{
    Matrix4f m = rotation.ToRotationMatrix();

    m(0, 0) *= scale.X();
    m(1, 0) *= scale.X();
    m(2, 0) *= scale.X();
    m(0, 1) *= scale.Y();
    m(1, 1) *= scale.Y();
    m(2, 1) *= scale.Y();
    m(0, 2) *= scale.Z();
    m(1, 2) *= scale.Z();
    m(2, 2) *= scale.Z();

    m(0, 3) = translation.X();
    m(1, 3) = translation.Y();
    m(2, 3) = translation.Z();

    return m;
}

}