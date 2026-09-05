#include "Frustum.h"

namespace Ashes { namespace Geom {

const Vector4f* Frustum::Planes() const
{
    return reinterpret_cast<const Vector4f*>(this);
}

Vector4f* Frustum::Planes()
{
    return reinterpret_cast<Vector4f*>(this);
}

// << Fast Extraction of Viewing Frustum Planes from the WorldView-Projection Matrix >>
// https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
void ExtractFrustumFromTransform(
    const Matrix4f& M,
    Frustum& frustum)
{
    frustum.left   = M.Row<3>() - M.Row<0>();
    frustum.right  = M.Row<3>() + M.Row<0>();
    frustum.top    = M.Row<3>() - M.Row<1>();
    frustum.bottom = M.Row<3>() + M.Row<1>();
    frustum.near   = M.Row<3>() - M.Row<2>();
    frustum.far    = M.Row<3>() + M.Row<2>();

    // normalize the plane equations.
    for (int i = 0; i < 6; ++i)
    {
        Vector4f& plane = frustum.Planes()[i];
        plane /= plane.Head<3>().Norm();
    }
}

// returns true if the box is completely behind (in negative half space) of plane.
static bool IsAxisAlignedBoxBehindOfPlane(
    const AxisAlignedBox& box,
    const Vector4f& plane)
{
    Vector3f n = plane.Head<3>().CwiseAbs();
    
    // this is always positive.
    float r = box.extents.Dot(n);

    // signed distance from center point to plane.
    float s = box.center.Dot(plane.Head<3>()) + plane.W();

    // if the center point of the box is a distance of e or more behind the
    // plane (in which case s is negative since it is behind the plane),
    // then the box is completely in the negative half space of the plane.
    return s + r <= 0.0f;
}

// << Introduction to 3D Game Programming with DirectX 11 >>
//   Chapter 19 TERRAIN RENDERING, Terrain.fx
bool IsAxisAlignedBoxOutsideOfFrustum(
    const AxisAlignedBox& box,
    const Frustum& frustum)
{
    for (int i = 0; i < 6; ++i)
    {
        if (IsAxisAlignedBoxBehindOfPlane(box, frustum.Planes()[i]))
            return true;
    }
    return false;
}

}}