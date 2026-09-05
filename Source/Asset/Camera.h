#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Geometry/AxisAlignedBox.h"

namespace Ashes {

enum class StandardViewType
{
    Front, Back, Left, Right, Top, Bottom, TopFrontLeft,
};

class Camera
{
public:
    
    Camera();
    ~Camera();

    //==========================================================================
    // Camera Space
    //==========================================================================
    
    const Vector3f& GetPosition() const;
    const Vector3f& GetLook() const;
    const Vector3f& GetRight() const;
    const Vector3f& GetUp() const;
    const Matrix4f& GetView() const;

    void LookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& up);
    void LookAlong(const Vector3f& pos, const Vector3f& look, const Vector3f& up);
    void SetView(const Matrix4f& view);

    //==========================================================================
    // Standard View
    //==========================================================================

    void SwitchToStandardView(StandardViewType type, const Geom::AxisAlignedBox& bounds);
    void SwitchToFrontView(const Geom::AxisAlignedBox& bounds);
    void SwitchToBackView(const Geom::AxisAlignedBox& bounds);
    void SwitchToLeftView(const Geom::AxisAlignedBox& bounds);
    void SwitchToRightView(const Geom::AxisAlignedBox& bounds);
    void SwitchToTopView(const Geom::AxisAlignedBox& bounds);
    void SwitchToBottomView(const Geom::AxisAlignedBox& bounds);
    void SwitchToTopFrontLeftView(const Geom::AxisAlignedBox& bounds);

    //==========================================================================
    // Camera Lens
    //==========================================================================

    float GetFov() const;
    float GetAspectRatio() const;
    float GetNear() const;
    float GetFar() const;
    
    Vector2f GetNearDim() const;            // get dimension of near plane.
    Vector2f GetFarDim() const;             // get dimension of far plane.
    const Matrix4f& GetProjection() const;  // get projection matrix.
    
    void SetAspectRatio(float aspect_ratio);
    void SetFar(float far);
    void SetLens(float fov, float aspect_ratio, float near, float far);

    //==========================================================================
    // Camera Transforming
    //==========================================================================

    void Walk(float distance);    // translate camera along it's look vector.
    void Strafe(float distance);  // translate camera along it's right vector.
    void Pitch(float angle);      // rotate camera around it's right vector.
    void RotateY(float angle);    // rotate camera around world's up vector.
    void UpdateView();            // rebuild view matrix after transforming.

private:

    Vector3f position_;
    Vector3f look_;
    Vector3f right_;
    Vector3f up_;
    Matrix4f view_;
    Vector4f lens_;
    Matrix4f projection_;
};

}