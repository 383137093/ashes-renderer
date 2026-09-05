#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Geometry/AxisAlignedBox.h"

namespace Ashes { namespace Geom {

struct Frustum
{
    const Vector4f* Planes() const;
    Vector4f* Planes();

    Vector4f left;
    Vector4f right;
    Vector4f bottom;
    Vector4f top;
    Vector4f near;
    Vector4f far;
};

void ExtractFrustumFromTransform(
    const Matrix4f& M,
    Frustum& frustum);

bool IsAxisAlignedBoxOutsideOfFrustum(
    const AxisAlignedBox& box,
    const Frustum& frustum);

}}