#include <cassert>
#include "Math/MathMisc.h"
#include "Math/Quaternion.h"

using namespace Ashes;

#define FLT_2_EPSILON (2 * FLT_EPSILON)

int main()
{
	// Compile-time Test
	{
		// Constant Quaternions
		static_assert(Quaternion::Zero() == Quaternion(0, 0, 0, 0));
		static_assert(Quaternion::Identity() == Quaternion(0, 0, 0, 1));

		// Constructors
		static_assert(std::is_pod_v<Quaternion>);
		static_assert(Quaternion(Vector4f::Zero().coeffs) == Quaternion::Zero());
		static_assert(Quaternion(Vector4f::Zero()) == Quaternion::Zero());
		
		// Special quaternions representing rotation 180 and 360 degrees around X-axis.
		constexpr Quaternion x180 = {1, 0, 0, 0};
		constexpr Quaternion x360 = {0, 0, 0, -1};
		
		// Coefficient Accessors
		static_assert(x180.X() == 1);
		static_assert(x180.Y() == 0);
		static_assert(x180.Z() == 0);
		static_assert(x180.W() == 0);

		// Coefficient Sequence Algorithm
		static_assert(!x180.IsApprox(x360));
		static_assert(!x180.IsIdentity());
		static_assert(x180.IsApprox(x360, 1));
		static_assert(x180.IsIdentity(1));
		static_assert(x360.IsIdentity(0));

		// Algebraic Algorithms
		static_assert(x180.SquaredNorm() == 1);
		static_assert(x180.Cross(Quaternion::Identity()) == x180);
		static_assert(x180.Cross(x180) == x360);
		static_assert(x180.Cross(x180.Inverse()) == Quaternion::Identity());

		// Rotation Algorithms
		static_assert(x180.ToRotationMatrix() * Vector4f::UnitY() == -Vector4f::UnitY());
		static_assert(x180.ToRotationMatrix() * Vector4f::UnitZ() == -Vector4f::UnitZ());
		static_assert(x180.RotateVector(Vector3f::UnitY()) == -Vector3f::UnitY());
		static_assert(x180.RotateVector(Vector3f::UnitZ()) == -Vector3f::UnitZ());
	}

	// Run-time Test
	{
		const float radians45 = Math::Radians(45.0f);
		const float sin45 = std::sin(radians45);

		const Quaternion quat = {Vector3f::UnitX(), radians45};
		assert(quat.RotationAxis().IsApprox(Vector3f::UnitX()));
		assert(std::abs(quat.RotationAngle() - radians45) <= FLT_EPSILON);

		const Matrix4f mat = quat.ToRotationMatrix();
		assert(Quaternion(mat).IsApprox(quat));
	
		const Vector3f kEightDirectionOnYZ[] = {
			 Vector3f::UnitY(), Vector3f(0,  sin45,  sin45),
			 Vector3f::UnitZ(), Vector3f(0, -sin45,  sin45),
			-Vector3f::UnitY(), Vector3f(0, -sin45, -sin45),
			-Vector3f::UnitZ(), Vector3f(0,  sin45, -sin45),};

		for (std::size_t i = 0; i < 8; ++i)
		{
			const Vector3f& v1 = kEightDirectionOnYZ[i];
			const Vector3f& v2 = kEightDirectionOnYZ[(i + 1) % 8];
			const Vector3f& v3 = kEightDirectionOnYZ[(i + 2) % 8];

			assert(quat.RotateVector(v1).IsApprox(v2));
			assert(quat.Cross(quat).RotateVector(v1).IsApprox(v3, FLT_2_EPSILON));
			assert(quat.Inverse().RotateVector(v2).IsApprox(v1));
			assert(Quaternion::MakeFromTwoVectors(v1, v2).IsApprox(quat));
		}
	}

	return 0;
}