#include <cassert>
#include "Math/MathMisc.h"
#include "Math/Transform.h"

using namespace Ashes;
using namespace Ashes::TransformationMatrix;

#define FLT_2_EPSILON (2 * FLT_EPSILON)

int main()
{
	const Vector3f ones = Vector3f::Ones();
	const Vector3f scale = {2, 1, 2};
	const Quaternion rotation = {Vector3f::UnitY(), Math::Radians(90.0f)};
	const Vector3f offset = {100, 200, 300};

	// Build transformation matrix.
	const Matrix4f mat_s = TransformationMatrix::Scale(scale);
	const Matrix4f mat_r = rotation.ToRotationMatrix();
	const Matrix4f mat_t = TransformationMatrix::Translation(offset);
	const Matrix4f mat_srt = mat_t * mat_r * mat_s;
	
	// Transform vector via transformation matrix.
	assert(TransformVector(mat_s, ones).IsApprox({2, 1, 2}));
	assert(TransformVector(mat_r, ones).IsApprox({1, 1, -1}));
	assert(TransformVector(mat_t, ones).IsApprox({1, 1, 1}));
	assert(TransformVector(mat_srt, ones).IsApprox({2, 1, -2}, FLT_2_EPSILON));

	// Transform point via transformation matrix.
	assert(TransformPoint(mat_s, ones).IsApprox({2, 1, 2}));
	assert(TransformPoint(mat_r, ones).IsApprox({1, 1, -1}));
	assert(TransformPoint(mat_t, ones).IsApprox({101, 201, 301}));
	assert(TransformPoint(mat_srt, ones).IsApprox({102, 201, 298}));

	// TransformationSRT with scale only.
	TransformationSRT srt = TransformationSRT::Identity();
	srt.scale = scale;
	assert(srt.TransformVector(ones).IsApprox({2, 1, 2}));
	assert(srt.TransformPoint(ones).IsApprox({2, 1, 2}));
	assert(srt.ToMatrix().IsApprox(mat_s));

	// TransformationSRT with rotation only.
	srt = TransformationSRT::Identity();
	srt.rotation = rotation;
	assert(srt.TransformVector(ones).IsApprox({1, 1, -1}));
	assert(srt.TransformPoint(ones).IsApprox({1, 1, -1}));
	assert(srt.ToMatrix().IsApprox(mat_r));

	// TransformationSRT with translation only.
	srt = TransformationSRT::Identity();
	srt.translation = offset;
	assert(srt.TransformVector(ones).IsApprox({1, 1, 1}));
	assert(srt.TransformPoint(ones).IsApprox({101, 201, 301}));
	assert(srt.ToMatrix().IsApprox(mat_t));

	// TransformationSRT with scale-rotation-translation.
	srt = {scale, rotation, offset};
	assert(srt.TransformVector(ones).IsApprox({2, 1, -2}, FLT_2_EPSILON));
	assert(srt.TransformPoint(ones).IsApprox({102, 201, 298}));
	assert(srt.ToMatrix().IsApprox(mat_srt));

	return 0;
}