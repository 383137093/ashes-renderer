#include "Camera.h"
#include <cmath>
#include <algorithm>
#include "Math/MathMisc.h"
#include "Math/Transform.h"

namespace Ashes {

Camera::Camera()
{
    LookAt(Vector3f::Zero(), -Vector3f::UnitZ(), Vector3f::UnitY());
    SetLens(45.0f, 1.0f, 0.1f, 1000.0f);
}

Camera::~Camera()
{
}

const Vector3f& Camera::GetPosition() const
{
    return position_;
}

const Vector3f& Camera::GetLook() const
{
    return look_;
}

const Vector3f& Camera::GetRight() const
{
    return right_;
}

const Vector3f& Camera::GetUp() const
{
    return up_;
}

const Matrix4f& Camera::GetView() const
{
    return view_;
}

void Camera::LookAt(
    const Vector3f& pos,
    const Vector3f& target,
    const Vector3f& up)
{
    SetView(TransformationMatrix::LookAt(pos, target, up));
}

void Camera::LookAlong(
    const Vector3f& pos,
    const Vector3f& look,
    const Vector3f& up)
{
    LookAt(pos, pos + look, up);
}

void Camera::SetView(const Matrix4f& view)
{
    Matrix4f inv_view = view.Inverse();
    position_ = TransformationMatrix::TransformPoint(inv_view, Vector3f::Zero());
    look_ = TransformationMatrix::TransformVector(inv_view, -Vector3f::UnitZ());
    right_ = TransformationMatrix::TransformVector(inv_view, Vector3f::UnitX());
    up_ = TransformationMatrix::TransformVector(inv_view, Vector3f::UnitY());
    view_ = view;
}

static Matrix4f BuildPanoramicView(
    const Vector3f& near_plane,
    const Vector3f& dir,
    const Vector3f& up,
    const Geom::AxisAlignedBox& bounds)
{
    // compute bounds extents in view space, it is unrelated to camera position.
    Vector3f view_bounds_extents = Vector3f::Zero();
    {
        Matrix4f view = TransformationMatrix::LookAt(Vector3f::Zero(), dir, up);
        Geom::AxisAlignedBox view_bounds = bounds;
        Geom::TransformAxisAlignedBoundingBox(view, view_bounds);
        view_bounds_extents = view_bounds.extents;
    }

    // compute required distance from camera position to bounds center.
    float required_dist = view_bounds_extents.Z();
    {
        float view_bounds_w = 2.0f * view_bounds_extents.X();
        float view_bounds_h = 2.0f * view_bounds_extents.Y();
        float required_dist_w = view_bounds_w / near_plane[1] * near_plane[0];
        float required_dist_h = view_bounds_h / near_plane[2] * near_plane[0];
        required_dist += std::max(required_dist_w, required_dist_h);
    }

    Vector3f target_pos = bounds.center;
    Vector3f camera_pos = target_pos - required_dist * dir;
    return TransformationMatrix::LookSquarelyAt(camera_pos, target_pos);
}

void Camera::SwitchToStandardView(
    StandardViewType type,
    const Geom::AxisAlignedBox& bounds)
{
    switch (type)
    {
    case StandardViewType::Front:
        return SwitchToFrontView(bounds);
    case StandardViewType::Back:
        return SwitchToBackView(bounds);
    case StandardViewType::Left:
        return SwitchToLeftView(bounds);
    case StandardViewType::Right:
        return SwitchToRightView(bounds);
    case StandardViewType::Top:
        return SwitchToTopView(bounds);
    case StandardViewType::Bottom:
        return SwitchToBottomView(bounds);
    case StandardViewType::TopFrontLeft:
        return SwitchToTopFrontLeftView(bounds);
    }
}

void Camera::SwitchToFrontView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = -Vector3f::UnitZ();
    constexpr Vector3f up = Vector3f::UnitY();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToBackView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = Vector3f::UnitZ();
    constexpr Vector3f up = Vector3f::UnitY();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToLeftView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = -Vector3f::UnitX();
    constexpr Vector3f up = Vector3f::UnitY();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToRightView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = Vector3f::UnitX();
    constexpr Vector3f up = Vector3f::UnitY();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToTopView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = -Vector3f::UnitY();
    constexpr Vector3f up = Vector3f::UnitZ();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToBottomView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = Vector3f::UnitY();
    constexpr Vector3f up = Vector3f::UnitZ();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

void Camera::SwitchToTopFrontLeftView(const Geom::AxisAlignedBox& bounds)
{
    constexpr Vector3f dir = {0.5f, -0.5f, -1.0f};
    constexpr Vector3f up = Vector3f::UnitY();
    Vector3f near_plane = GetNearDim().Join(GetNear());
    SetView(BuildPanoramicView(near_plane, dir, up, bounds));
}

float Camera::GetFov() const
{
    return lens_[0];
}

float Camera::GetAspectRatio() const
{
    return lens_[1];
}

float Camera::GetNear() const
{
    return lens_[2];
}

float Camera::GetFar() const
{
    return lens_[3];
}

Vector2f Camera::GetNearDim() const
{
    float h = GetNear() * std::tan(Math::Radians(0.5f * GetFov()));
    float w = h * GetAspectRatio();
    return {2.0f * w, 2.0f * h};
}

Vector2f Camera::GetFarDim() const
{
    float h = GetFar() * std::tan(Math::Radians(0.5f * GetFov()));
    float w = h * GetAspectRatio();
    return {2.0f * w, 2.0f * h};
}

const Matrix4f& Camera::GetProjection() const
{
    return projection_;
}

void Camera::SetAspectRatio(float aspect_ratio)
{
    SetLens(GetFov(), aspect_ratio, GetNear(), GetFar());
}

void Camera::SetFar(float far)
{
    SetLens(GetFov(), GetAspectRatio(), GetNear(), far);
}

void Camera::SetLens(float fov, float aspect_ratio, float near, float far)
{
    lens_ = {fov, aspect_ratio, near, far};
    projection_ = TransformationMatrix::PerspectiveProjection(
        fov, aspect_ratio, near, far);
}

void Camera::Walk(float distance)
{
    position_ += distance * look_;
}

void Camera::Strafe(float distance)
{
    position_ += distance * right_;
}

void Camera::Pitch(float angle)
{
    Matrix4f rotation = TransformationMatrix::Rotation(right_, angle);
    look_ = TransformationMatrix::TransformVector(rotation, look_);
    up_ = TransformationMatrix::TransformVector(rotation, up_);
}

void Camera::RotateY(float angle)
{
    Matrix4f rotation = TransformationMatrix::RotationY(angle);
    look_ = TransformationMatrix::TransformVector(rotation, look_);
    right_ = TransformationMatrix::TransformVector(rotation, right_);
    up_ = TransformationMatrix::TransformVector(rotation, up_);
}

void Camera::UpdateView()
{
    view_ = TransformationMatrix::LookAt(position_, position_ + look_, up_);
}

}