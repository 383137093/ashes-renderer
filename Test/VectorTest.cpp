#include "Math/Vector.h"

using namespace Ashes;

int main()
{
	// Constant Vectors
	static_assert(Vector4f::Zero() == Vector4f(0, 0, 0, 0));
	static_assert(Vector4f::Ones() == Vector4f(1, 1, 1, 1));
	static_assert(Vector4f::UnitX() == Vector4f(1, 0, 0, 0));
	static_assert(Vector4f::UnitY() == Vector4f(0, 1, 0, 0));
	static_assert(Vector4f::UnitZ() == Vector4f(0, 0, 1, 0));
	static_assert(Vector4f::UnitW() == Vector4f(0, 0, 0, 1));

	// Constructors
	static_assert(std::is_pod_v<Vector4f>);
	static_assert(Vector4f(Vector4f::Zero().coeffs) == Vector4f::Zero());

	// Coefficient Accessors
	constexpr Vector4f seq_4f = {1, 2, 3, 4};
	static_assert(seq_4f.X() == 1);
	static_assert(seq_4f.Y() == 2);
	static_assert(seq_4f.Z() == 3);
	static_assert(seq_4f.W() == 4);

	// Derived Vectors
	static_assert(Vector4i::Zero().Cast<float>() == Vector4f::Zero());
	static_assert(Vector3f::UnitX().Join(0) == Vector4f::UnitX());
	static_assert(Vector3f::UnitX().Head<2>() == Vector2f::UnitX());

	// Coefficient Sequence Algorithm
	static_assert(seq_4f.MinCoeff() == 1);
	static_assert(seq_4f.MaxCoeff() == 4);
	static_assert(Vector3f::Zero().IsZero());
	static_assert(Vector3f::Ones().IsZero(1) && !Vector3f::Ones().IsZero());
	static_assert(Vector3f(-1, 0, 1).CwiseAbs() == Vector3f(1, 0, 1));
	static_assert(Vector3f(-1, 0, 1).CwiseMin(Vector3f::Zero()) == Vector3f(-1, 0, 0));
	static_assert(Vector3f(-1, 0, 1).CwiseMax(Vector3f::Zero()) == Vector3f(0, 0, 1));

	// Coefficient Sequence Operators
	static_assert(seq_4f + seq_4f == Vector4f(2, 4, 6, 8));
	static_assert(seq_4f - seq_4f == Vector4f(0, 0, 0, 0));
	static_assert(seq_4f * seq_4f == Vector4f(1, 4, 9, 16));
	static_assert(seq_4f / seq_4f == Vector4f(1, 1, 1, 1));
	static_assert(seq_4f + 1 == Vector4f(2, 3, 4, 5));
	static_assert(seq_4f - 1 == Vector4f(0, 1, 2, 3));
	static_assert(seq_4f * 1 == Vector4f(1, 2, 3, 4));
	static_assert(seq_4f / 1 == Vector4f(1, 2, 3, 4));
	static_assert(1 + seq_4f == Vector4f(2, 3, 4, 5));
	static_assert(1 - seq_4f == Vector4f(0, -1, -2, -3));
	static_assert(1 * seq_4f == Vector4f(1, 2, 3, 4));
	static_assert(1 / seq_4f == Vector4f(1, 0.5f, 1 / 3.0f, 0.25f));
	static_assert(-seq_4f == -1 * seq_4f);

	// Algebra Algorithm
	static_assert(seq_4f.SquaredNorm() == 30);
	static_assert(seq_4f.Dot(Vector4f::Zero()) == 0);
	static_assert(seq_4f.Dot(Vector4f::Ones()) == 10);
	static_assert(seq_4f.Dot(Vector4f::UnitX()) == 1);
	static_assert(Vector3f::UnitX().Cross(Vector3f::UnitX()) == Vector3f::Zero());
	static_assert(Vector3f::UnitX().Cross(Vector3f::UnitY()) == Vector3f::UnitZ());
	static_assert(Vector3f::UnitY().Cross(Vector3f::UnitZ()) == Vector3f::UnitX());
	static_assert(Vector3f::UnitZ().Cross(Vector3f::UnitX()) == Vector3f::UnitY());

	return 0;
}