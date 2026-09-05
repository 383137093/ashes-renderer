#pragma once

#include <cmath>
#include "Math/Vector.h"

namespace Ashes { namespace Geom {

template <typename T, std::size_t N>
struct Triangle
{
    Vector<T, N> v0;
    Vector<T, N> v1;
    Vector<T, N> v2;
};

using Triangle2f = Triangle<float, 2>;
using Triangle3f = Triangle<float, 3>;
using Triangle4f = Triangle<float, 4>;
using Triangle2d = Triangle<double, 2>;
using Triangle3d = Triangle<double, 3>;

template <typename T>
inline T TriangleSignedArea2D(const Vector2<T>& e1, const Vector2<T>& e2)
{
    return (e1.X() * e2.Y() - e1.Y() * e2.X()) / 2;
}

template <typename T>
inline T TriangleArea2D(const Vector2<T>& e1, const Vector2<T>& e2)
{
    return std::abs(TriangleSignedArea2D(e1, e2));
}

template <typename T>
inline T QuadArea2D(const Vector2<T>& v0, const Vector2<T>& v1,
                    const Vector2<T>& v2, const Vector2<T>& v3)
{
    float area012 = TriangleArea2D<T>(v1 - v0, v2 - v0);
    float area312 = TriangleArea2D<T>(v1 - v3, v2 - v3);
    return area012 + area312;
}

template <typename T, std::size_t N>
bool PointInsideTriangle2D(const Triangle<T, N>& tri, const Vector2<T>& p)
{
    auto a = tri.v0.template Head<2>();
    auto b = tri.v1.template Head<2>();
    auto c = tri.v2.template Head<2>();

    T z0 = TriangleSignedArea2D<T>(b - a, p - a);
    T z1 = TriangleSignedArea2D<T>(c - b, p - b);
    T z2 = TriangleSignedArea2D<T>(a - c, p - c);

    return std::signbit(z0) == std::signbit(z1)
        && std::signbit(z1) == std::signbit(z2);
}

template <typename T, std::size_t N>
Vector3<T> BaryCoord2D(const Triangle<T, N>& tri, const Vector2<T>& p)
{
    Vector2<T> a = tri.v0.template Head<2>();
    Vector2<T> b = tri.v1.template Head<2>();
    Vector2<T> c = tri.v2.template Head<2>();

    Vector2<T> ab = b - a;
    Vector2<T> ac = c - a;
    Vector2<T> ap = p - a;
    Vector2<T> pb = b - p;
    Vector2<T> pc = c - p;

    T area_abc = TriangleSignedArea2D(ab, ac);
    T area_abp = TriangleSignedArea2D(ab, ap);
    T area_apc = TriangleSignedArea2D(ap, ac);
    T area_pbc = TriangleSignedArea2D(pb, pc);

    T s = 1 / area_abc;
    T w = area_abp * s;
    T v = area_apc * s;
    T u = area_pbc * s;

    return {u, v, w};
}

}}