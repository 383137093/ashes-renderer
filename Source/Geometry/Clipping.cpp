#include "Clipping.h"
#include <algorithm>
#include "Math/MathMisc.h"

namespace Ashes { namespace Geom {

bool IsPointInsideHomogeneousClipSpace(const Vector4f& p)
{
    return (-p.W() <= p.X() && p.X() <= p.W())
        && (-p.W() <= p.Y() && p.Y() <= p.W())
        && (-p.W() <= p.Z() && p.Z() <= p.W());
}

bool IsPolygonInsideHomogeneousClipSpace(const Vector4f* vertices, int num_vertices)
{
    return std::all_of(vertices, vertices + num_vertices,
        IsPointInsideHomogeneousClipSpace);
}

// return distance from point to homogeneous clip plane.
static float PointHomogeneousClipPlaneDistance(const Vector4f& p, int clip_plane)
{
    switch (clip_plane)
    {
        case 0: return  p.W();          //  W
        case 1: return  p.X() + p.W();  // +X
        case 2: return -p.X() + p.W();  // -X
        case 3: return  p.Y() + p.W();  // +Y
        case 4: return -p.Y() + p.W();  // -Y
        case 5: return  p.Z() + p.W();  // +Z
        case 6: return -p.Z() + p.W();  // -Z
    }
    return 0;
}

// Sutherland–Hodgman algorithm in homogeneous clip space.
static int HomogeneousClipOfPolygonByPlane(
    int clip_plane,
    const Vector4f* vertices,
    const Vector3f* weights,
    int num_vertices,
    Vector4f* out_vertices,
    Vector3f* out_weights)
{
    int num_out_vertices = 0;
    float cur_dist = PointHomogeneousClipPlaneDistance(vertices[0], clip_plane);
    bool cur_inside = (cur_dist >= 0.0f);

    for (int i = 0; i < num_vertices; ++i)
    {
        int j = (i + 1) % num_vertices;
        float next_dist = PointHomogeneousClipPlaneDistance(vertices[j], clip_plane);
        bool next_inside = (next_dist >= 0.0f);

        if (cur_inside)
        {
            *out_vertices++ = vertices[i];
            *out_weights++ = weights[i];
            ++num_out_vertices;
        }

        if (cur_inside != next_inside)
        {
            float t = cur_dist / (cur_dist - next_dist);
            *out_vertices++ = Math::Lerp(vertices[i], vertices[j], t);
            *out_weights++ = Math::Lerp(weights[i], weights[j], t);
            ++num_out_vertices;
        }

        cur_dist = next_dist;
        cur_inside = next_inside;
    }
    
    return num_out_vertices;
}

static int HomogeneousClipOfPolygon(
    Vector4f* intermediate_vertices,
    Vector3f* intermediate_weights,
    int num_vertices,
    Vector4f* out_vertices,
    Vector3f* out_weights)
{
    for (int clip_plane = 0; clip_plane < 7 && num_vertices >= 3; ++clip_plane)
    {
        num_vertices = HomogeneousClipOfPolygonByPlane(clip_plane,
            intermediate_vertices, intermediate_weights, num_vertices,
            out_vertices, out_weights);

        std::swap(intermediate_vertices, out_vertices);
        std::swap(intermediate_weights, out_weights);
    }

    return num_vertices;
}

int HomogeneousClipOfTriangle(Vector4f* vertices, Vector3f* barys)
{
    constexpr int kMaxNumOfOutVertices = HomogeneousClipMaxOut(3);
    
    Vector4f intermediate_vertices[kMaxNumOfOutVertices];
    Vector3f intermediate_barys[kMaxNumOfOutVertices];
    
    intermediate_vertices[0] = vertices[0];
    intermediate_vertices[1] = vertices[1];
    intermediate_vertices[2] = vertices[2];
    intermediate_barys[0] = Vector3f::UnitX();
    intermediate_barys[1] = Vector3f::UnitY();
    intermediate_barys[2] = Vector3f::UnitZ();
    
    return HomogeneousClipOfPolygon(intermediate_vertices, 
        intermediate_barys, 3, vertices, barys);
}

}}