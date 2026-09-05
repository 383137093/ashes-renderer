// Online Matrix Computations
// http://www.yunsuan.info/matrixcomputations/index.html

#include "Math/Matrix.h"

using namespace Ashes;

template <std::size_t Rows, std::size_t Cols>
using Matrixf = Matrix<float, Rows, Cols>;

int main()
{
	// Constant Matrices
	static_assert(Matrix2f::Zero() == Matrix2f(0, 0, 0, 0));
	static_assert(Matrix2f::Identity() == Matrix2f(1, 0, 0, 1));

	// Constructors
	static_assert(std::is_pod_v<Matrix2f>);
	static_assert(Matrix2f(Vector4f::Zero().coeffs) == Matrix2f::Zero());
	static_assert(Matrix2f(Vector4f::Zero()) == Matrix2f::Zero());

	// Coefficient Accessors
	constexpr Matrix2f seq_2f = {1, 2, 3, 4};
	static_assert(seq_2f(0, 0) == 1);
	static_assert(seq_2f(0, 1) == 2);
	static_assert(seq_2f(1, 0) == 3);
	static_assert(seq_2f(1, 1) == 4);
	static_assert(seq_2f.Row<0>() == Vector2f(1, 2));
	static_assert(seq_2f.Row<1>() == Vector2f(3, 4));
	static_assert(seq_2f.Col<0>() == Vector2f(1, 3));
	static_assert(seq_2f.Col<1>() == Vector2f(2, 4));

	// Derived Matrices
	static_assert(Matrix2i::Zero().Cast<float>() == Matrix2f::Zero());
	static_assert(seq_2f.Reshaped<2, 1>() == Matrixf<2, 1>(1, 3));
	static_assert(seq_2f.Reshaped<1, 2>() == Matrixf<1, 2>(1, 2));
	static_assert(seq_2f.Reshaped<1, 1>() == Matrixf<1, 1>(1));
	static_assert(seq_2f.Reshaped<2, 3>() == Matrixf<2, 3>(1, 2, 0, 3, 4, 0));
	static_assert(seq_2f.Reshaped<3, 2>() == Matrixf<3, 2>(1, 2, 3, 4, 0, 0));
	static_assert(seq_2f.Reshaped<3, 3>() == Matrixf<3, 3>(1, 2, 0, 3, 4, 0, 0, 0, 0));
	
	// Coefficient Sequence Algorithm
	static_assert(Matrix2f::Zero().IsZero());
	static_assert(Matrix2f::Identity().IsIdentity());
	static_assert(seq_2f.IsZero(4));
	static_assert(seq_2f.IsIdentity(3));
	
	// Coefficient Sequence Operators
	static_assert(seq_2f + seq_2f == Matrix2f(2, 4, 6, 8));
	static_assert(seq_2f - seq_2f == Matrix2f(0, 0, 0, 0));
	static_assert(seq_2f + 1 == Matrix2f(2, 3, 4, 5));
	static_assert(seq_2f - 1 == Matrix2f(0, 1, 2, 3));
	static_assert(seq_2f * 1 == Matrix2f(1, 2, 3, 4));
	static_assert(seq_2f / 1 == Matrix2f(1, 2, 3, 4));
	static_assert(1 + seq_2f == Matrix2f(2, 3, 4, 5));
	static_assert(1 - seq_2f == Matrix2f(0, -1, -2, -3));
	static_assert(1 * seq_2f == Matrix2f(1, 2, 3, 4));
	static_assert(1 / seq_2f == Matrix2f(1, 0.5f, 1 / 3.0f, 0.25f));
	
	// Algebra
	static_assert(seq_2f.Transpose() == Matrix2f(1, 3, 2, 4));
	static_assert(seq_2f.Transpose().Transpose() == seq_2f);
	static_assert(seq_2f.Reshaped<2, 1>().Transpose() == Matrixf<1, 2>(1, 3));
	static_assert(seq_2f.Reshaped<1, 2>().Transpose() == Matrixf<2, 1>(1, 2));
	static_assert(seq_2f * Vector2f::UnitX() == Vector2f(1, 3));
	static_assert(Vector2f::UnitX() * Matrixf<1, 2>(1, 2) == Matrix2f(1, 2, 0, 0));

	// Algebra - Inverse
	constexpr Matrix2f mat_2f = {1, 1, 1, 0};
	constexpr Matrix3f mat_3f = {1, 1, 1, 0, 2, 2, 0, 0, 3};
	constexpr Matrix4f mat_4f = {1, 1, 1, 1, 0, 2, 2, 0, 0, 0, 3, 0, 0, 0, 3, 1};
	static_assert(Matrix2f::Identity().Inverse() == Matrix2f::Identity());
	static_assert(Matrix3f::Identity().Inverse() == Matrix3f::Identity());
	static_assert(Matrix4f::Identity().Inverse() == Matrix4f::Identity());
	static_assert(mat_2f * mat_2f.Inverse() == Matrix2f::Identity());
	static_assert(mat_3f * mat_3f.Inverse() == Matrix3f::Identity());
	static_assert(mat_4f * mat_4f.Inverse() == Matrix4f::Identity());
	
	return 0;
}