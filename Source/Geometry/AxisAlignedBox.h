#pragma once

#include <cstddef>
#include "Math/Matrix.h"
#include "Math/Vector.h"

namespace Ashes { namespace Geom {

struct AxisAlignedBox
{
    static const AxisAlignedBox& Null();
    static AxisAlignedBox MakeFromMinMax(const Vector3f& minv, const Vector3f& maxv);
    
    bool IsEmpty() const;
    Vector3f MinV() const;
    Vector3f MaxV() const;
    AxisAlignedBox Union(const AxisAlignedBox& rhs) const;
    AxisAlignedBox Union(const Vector3f& rhs) const;

    Vector3f center;
    Vector3f extents;
};

void ComputeAxisAlignedBoundingBoxFromPoints(
    const Vector3f* points,
    std::size_t count,
    std::size_t stride,
    AxisAlignedBox& box);

void ComputeAxisAlignedBoundingBoxFromPoints(
    const Vector3f* points,
    std::size_t count,
    AxisAlignedBox& box);

void TransformAxisAlignedBoundingBox(
    const Matrix4f& M,
    AxisAlignedBox& box);

}}