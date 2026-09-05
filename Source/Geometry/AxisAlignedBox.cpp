#include "AxisAlignedBox.h"
#include <cfloat>
#include "Math/Transform.h"

namespace Ashes { namespace Geom {

const AxisAlignedBox& AxisAlignedBox::Null()
{
    static const AxisAlignedBox kNull = {Vector3f::Zero(), -Vector3f::Ones()};
    return kNull;
}

AxisAlignedBox AxisAlignedBox::MakeFromMinMax(
    const Vector3f& minv,
    const Vector3f& maxv)
{
    Vector3f a = 0.5f * minv;
    Vector3f b = 0.5f * maxv;
    return {a + b, b - a};
}

bool AxisAlignedBox::IsEmpty() const
{
    return extents.MinCoeff() < 0.0f;
}

Vector3f AxisAlignedBox::MinV() const
{
    return center - extents;
}

Vector3f AxisAlignedBox::MaxV() const
{
    return center + extents;
}

AxisAlignedBox AxisAlignedBox::Union(const AxisAlignedBox& rhs) const
{
    if (!IsEmpty() && !rhs.IsEmpty())
    {
        Vector3f minv = MinV().CwiseMin(rhs.MinV());
        Vector3f maxv = MaxV().CwiseMax(rhs.MaxV());
        return MakeFromMinMax(minv, maxv);
    }
    return IsEmpty() ? rhs : *this;
}

AxisAlignedBox AxisAlignedBox::Union(const Vector3f& rhs) const
{
    if (!IsEmpty())
    {
        Vector3f minv = MinV().CwiseMin(rhs);
        Vector3f maxv = MaxV().CwiseMax(rhs);
        return MakeFromMinMax(minv, maxv);
    }
    return {rhs, Vector3f::Zero()};
}

void ComputeAxisAlignedBoundingBoxFromPoints(
    const Vector3f* points,
    std::size_t count,
    std::size_t stride,
    AxisAlignedBox& box)
{
    Vector3f minv = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    Vector3f maxv = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        
    for (std::size_t i = 0; i < count; ++i)
    {
        minv = minv.CwiseMin(*points);
        maxv = maxv.CwiseMax(*points);
        points = reinterpret_cast<const Vector3f*>(
            reinterpret_cast<const std::uint8_t*>(points) + stride);
    }

    box = AxisAlignedBox::MakeFromMinMax(minv, maxv);
}

void ComputeAxisAlignedBoundingBoxFromPoints(
    const Vector3f* points,
    std::size_t count,
    AxisAlignedBox& box)
{
    return ComputeAxisAlignedBoundingBoxFromPoints(
        points, count, sizeof(points[0]), box);
}

void TransformAxisAlignedBoundingBox(
    const Matrix4f& M,
    AxisAlignedBox& box)
{
    static constexpr Vector3f kUnitCubeVertices[8] = {
        {-1.0f, -1.0f, -1.0f}, {+1.0f, -1.0f, -1.0f},
        {-1.0f, +1.0f, -1.0f}, {+1.0f, +1.0f, -1.0f},
        {-1.0f, -1.0f, +1.0f}, {+1.0f, -1.0f, +1.0f},
        {-1.0f, +1.0f, +1.0f}, {+1.0f, +1.0f, +1.0f},};

    Vector3f vertices[8];
    
    // transform each vertex of bounding box.
    for (int i = 0; i < 8; ++i)
    {
        vertices[i] = TransformationMatrix::TransformPoint(
            M, box.center + box.extents * kUnitCubeVertices[i]);
    }
    
    ComputeAxisAlignedBoundingBoxFromPoints(vertices, 8, box);
}

}}