#pragma once

#include "Math/Vector.h"

namespace Ashes { namespace Geom {

inline constexpr int HomogeneousClipMaxOut(int n) { return n + 7; }

bool IsPointInsideHomogeneousClipSpace(const Vector4f& p);

bool IsPolygonInsideHomogeneousClipSpace(const Vector4f* vertices, int num_vertices);

int HomogeneousClipOfTriangle(Vector4f* vertices, Vector3f* barys);

}}