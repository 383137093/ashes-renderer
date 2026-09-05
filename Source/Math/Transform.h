#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Quaternion.h"

namespace Ashes {

namespace TransformationMatrix {

//==============================================================================
// Build simple transformation matrix.
//==============================================================================

Matrix4f Translation(const Vector3f& offset);

Matrix4f Scale(const Vector3f& scale);

Matrix4f Rotation(const Vector3f& axis, float degrees);

Matrix4f RotationX(float degrees);

Matrix4f RotationY(float degrees);

Matrix4f RotationZ(float degrees);

Matrix4f RotationEuler(const Vector3f& degrees);

//==============================================================================
// Build complex transformation matrix, use right hand coordinate system.
//==============================================================================

// in view space, camera is at origin and looking along negative Z axis.
Matrix4f LookAt(const Vector3f& camera, const Vector3f& target, const Vector3f& up);

// same as LookAt but using world axis as camera's up vector automatically.
Matrix4f LookSquarelyAt(const Vector3f& camera, const Vector3f& target);

// perspective projection with Reversed-Z, mapping z from [-near,-far] to [1,0].
Matrix4f PerspectiveProjection(float fov, float aspect_ratio, float near, float far);

// orthographic projection with Reversed-Z, mapping z from [-near,-far] to [1,0].
Matrix4f OrthographicProjection(const Vector3f& minv, const Vector3f& maxv);

// mapping x and y from [-1,1]x[-1,1] to [0,w]x[0,h] with unchanged z.
Matrix4f Viewport(int screen_width, int screen_height);

// mapping x and y from [-1,1]x[-1,1] to [0,1]x[0,1] with unchanged z.
const Matrix4f& NDCToUV();

//==============================================================================
// Homogeneous space transform via transformation matrix.
//==============================================================================

Vector3f TransformVector(const Matrix4f& m, const Vector3f& v);

Vector3f TransformDirection(const Matrix4f& m, const Vector3f& dir);

Vector3f TransformPoint(const Matrix4f& m, const Vector3f& p);

Vector3f TransformPointDivideW(const Matrix4f& m, const Vector3f& p);

}


//==============================================================================
// Build decomposed transformation: scale->rotate->translate.
//==============================================================================

struct TransformationSRT
{
    static const TransformationSRT& Identity();

    TransformationSRT() = default;
    TransformationSRT(const Vector3f& s, const Quaternion& r, const Vector3f& t);
    TransformationSRT(const Matrix4f& m);

    Vector3f TransformVector(const Vector3f& v) const;
    Vector3f TransformDirection(const Vector3f& dir) const;
    Vector3f TransformPoint(const Vector3f& p) const;
    Matrix4f ToMatrix() const;

    Vector3f   scale;
    Quaternion rotation;
    Vector3f   translation;
};

}