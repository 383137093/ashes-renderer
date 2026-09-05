#pragma once

#include "Math/Vector.h"

namespace Ashes {

template <typename T, std::size_t N>
class SquareMatrixAlgebraAlgorithm;

template <typename T, std::size_t Rows, std::size_t Cols>
class Matrix
{
public:
    
    static constexpr std::size_t kRows = Rows;
    static constexpr std::size_t kCols = Cols;
    static constexpr std::size_t kSize = Rows * Cols;
    static constexpr bool kIsSquare = (Rows == Cols);
    static constexpr T kEpsilon = std::numeric_limits<T>::epsilon();

    using ValueType = T;

    //==========================================================================
    // Constant Matrices
    //==========================================================================

    static constexpr Matrix Zero()     { return Matrix{Vector<T, kSize>::Zero()}; }
    static constexpr Matrix Identity() { return MakeIdentityMat(MakeIndices<>()); }

    //==========================================================================
    // Constructors
    //==========================================================================
    
    Matrix() = default;

    template <typename... Args, typename = std::enable_if_t<sizeof...(Args) == kSize>>
    constexpr Matrix(Args... args)
        : storage_{args...} {}

    template <typename U>
    constexpr explicit Matrix(const U (&arr)[kSize])
        : storage_(arr) {}

    template <typename U, typename = std::enable_if_t<std::is_pointer_v<U>>>
    constexpr explicit Matrix(U ptr)
        : storage_(ptr) {}

    constexpr explicit Matrix(const Vector<T, kSize>& v)
        : storage_(v) {}

    //==========================================================================
    // Coefficient Accessors
    //==========================================================================

    constexpr T operator [] (std::size_t idx) const
    {
        return storage_.coeffs[idx];
    }

    T& operator [] (std::size_t idx)
    {
        return storage_.coeffs[idx];
    }

    constexpr T operator () (std::size_t row, std::size_t col) const
    {
        return storage_.coeffs[row * Cols + col];
    }

    T& operator () (std::size_t row, std::size_t col)
    {
        return storage_.coeffs[row * Cols + col];
    }

    template <std::size_t I, typename = std::enable_if_t<I < Rows>>
    constexpr Vector<T, Cols> Row() const
    {
        return MatRow<I>(*this, MakeIndices<Cols>());
    }

    template <std::size_t I, typename = std::enable_if_t<I < Cols>>
    constexpr Vector<T, Rows> Col() const
    { 
        return MatCol<I>(*this, MakeIndices<Rows>());
    }

    //==========================================================================
    // Derived Matrices
    //==========================================================================
    
    template <typename U>
    constexpr Matrix<U, Rows, Cols> Cast() const
    {
        return Matrix<U, Rows, Cols>{storage_.template Cast<U>()};
    }

    template <std::size_t NewRows, std::size_t NewCols>
    constexpr Matrix<T, NewRows, NewCols> Reshaped() const
    {
        return MatReshaped<NewRows, NewCols>(*this, MakeIndices<NewRows * NewCols>());
    }
    
    //==========================================================================
    // Coefficient Sequence Algorithm
    //==========================================================================

    constexpr bool IsApprox(const Matrix& other, T precision = kEpsilon) const
    {
        return storage_.IsApprox(other.storage_, precision);
    }

    constexpr bool IsZero(T precision = kEpsilon) const
    {
        return storage_.IsZero(precision);
    }

    constexpr bool IsIdentity(T precision = kEpsilon) const
    {
        return IsApprox(Identity(), precision);
    }

    //==========================================================================
    // Coefficient Sequence Operators
    //==========================================================================

    friend constexpr bool operator == (const Matrix& lhs, const Matrix& rhs)
    {
        return lhs.storage_ == rhs.storage_;
    }

    friend constexpr bool operator != (const Matrix& lhs, const Matrix& rhs)
    {
        return lhs.storage_ != rhs.storage_;
    }

    friend constexpr Matrix operator + (const Matrix& lhs, const Matrix& rhs)
    {
        return Matrix{lhs.storage_ + rhs.storage_};
    }

    friend constexpr Matrix operator - (const Matrix& lhs, const Matrix& rhs)
    {
        return Matrix{lhs.storage_ - rhs.storage_};
    }

    friend constexpr Matrix operator + (const Matrix& lhs, T rhs)
    {
        return Matrix{lhs.storage_ + rhs};
    }

    friend constexpr Matrix operator - (const Matrix& lhs, T rhs)
    {
        return Matrix{lhs.storage_ - rhs};
    }

    friend constexpr Matrix operator * (const Matrix& lhs, T rhs)
    {
        return Matrix{lhs.storage_ * rhs};
    }

    friend constexpr Matrix operator / (const Matrix& lhs, T rhs)
    {
        return Matrix{lhs.storage_ / rhs};
    }

    friend constexpr Matrix operator + (T lhs, const Matrix& rhs)
    {
        return Matrix{lhs + rhs.storage_};
    }

    friend constexpr Matrix operator - (T lhs, const Matrix& rhs)
    {
        return Matrix{lhs - rhs.storage_};
    }

    friend constexpr Matrix operator * (T lhs, const Matrix& rhs)
    {
        return Matrix{lhs * rhs.storage_};
    }

    friend constexpr Matrix operator / (T lhs, const Matrix& rhs)
    {
        return Matrix{lhs / rhs.storage_};
    }

    //==========================================================================
    // Algebraic Algorithms
    //==========================================================================

    constexpr Matrix<T, Cols, Rows> Transpose() const
    {
        return MatTranspose(*this, MakeIndices<>());
    }
    
    constexpr Matrix Adjoint() const
    {
        static_assert(kIsSquare);
        return SquareMatrixAlgebraAlgorithm<T, Rows>::Adjoint(*this);
    }

    constexpr T Determinant() const
    {
        static_assert(kIsSquare);
        return SquareMatrixAlgebraAlgorithm<T, Rows>::Determinant(*this);
    }

    constexpr Matrix InverseTranspose() const
    {
        static_assert(kIsSquare);
        return SquareMatrixAlgebraAlgorithm<T, Rows>::InverseTranspose(*this);
    }

    constexpr Matrix Inverse() const
    {
        static_assert(kIsSquare);
        return InverseTranspose().Transpose();
    }

    //==========================================================================
    // Algebra Operators
    // think of Vector as column vector, multiplication should be Matrix * Vector
    //==========================================================================
    
    template <std::size_t RhsCols>
    friend constexpr Matrix<T, Rows, RhsCols> operator * (
        const Matrix& lhs, const Matrix<T, Cols, RhsCols>& rhs)
    {
        return MatMulMat(lhs, rhs, MakeIndices<Rows * RhsCols>());
    }

    friend constexpr Vector<T, Rows> operator * (
        const Matrix& lhs, const Vector<T, Cols>& rhs)
    {
        return MatMulVec(lhs, rhs, MakeIndices<Rows>());
    }

    template <std::size_t VecSize>
    friend constexpr Matrix<T, VecSize, Cols> operator * (
        const Vector<T, VecSize>& lhs, const Matrix& rhs)
    {
        static_assert(Rows == 1);
        return VecMulMat(lhs, rhs, MakeIndices<VecSize * Cols>());
    }

private:
    
    template <std::size_t L = kSize>
    using MakeIndices = std::make_index_sequence<L>;

    Vector<T, kSize> storage_;

private:

    template <std::size_t... I>
    static constexpr Matrix MakeIdentityMat(std::index_sequence<I...>)
    {
        return {(I / Cols == I % Cols ? 1 : 0)...};
    }

    template <std::size_t X, std::size_t... I>
    static constexpr Vector<T, Cols> MatRow(
        const Matrix& m, std::index_sequence<I...>)
    {
        return m.storage_.Coeffs(std::index_sequence<(X * Cols + I)...>());
    }

    template <std::size_t X, std::size_t... I>
    static constexpr Vector<T, Rows> MatCol(
        const Matrix& m, std::index_sequence<I...>)
    {
        return m.storage_.Coeffs(std::index_sequence<(X + I * Cols)...>());
    }

    static constexpr T MatReshapedCoeff(const Matrix& m,
        std::size_t new_cols, std::size_t idx)
    {
        std::size_t row = idx / new_cols;
        std::size_t col = idx % new_cols;
        return row < Rows && col < Cols ? m(row, col) : 0;
    }

    template <std::size_t NewRows, std::size_t NewCols, std::size_t... I>
    static constexpr Matrix<T, NewRows, NewCols> MatReshaped(
        const Matrix& m, std::index_sequence<I...>)
    {
        return {MatReshapedCoeff(m, NewCols, I)...};
    }

    template <std::size_t... I>
    static constexpr Matrix<T, Cols, Rows> MatTranspose(
        const Matrix& m, std::index_sequence<I...>)
    {
        return {m(I % Rows, I / Rows)...};
    }

    template <std::size_t RhsCols, std::size_t... I>
    static constexpr Matrix<T, Rows, RhsCols> MatMulMat(
        const Matrix& lhs, const Matrix<T, Cols, RhsCols>& rhs,
        std::index_sequence<I...>)
    {
        return {(lhs.template Row<I / RhsCols>().Dot(
            rhs.template Col<I % RhsCols>()))...};
    }

    template <std::size_t... I>
    static constexpr Vector<T, Rows> MatMulVec(
        const Matrix& lhs, const Vector<T, Cols>& rhs,
        std::index_sequence<I...>)
    {
        return {(lhs.Row<I>().Dot(rhs))...};
    }

    template <std::size_t VecSize, std::size_t... I>
    static constexpr Matrix<T, VecSize, Cols> VecMulMat(
        const Vector<T, VecSize>& lhs, const Matrix& rhs,
        std::index_sequence<I...>)
    {
        return {(lhs[I / Cols] * rhs.storage_[I % Cols])...};
    }
};


template <typename T> using Matrix2 = Matrix<T, 2, 2>;
template <typename T> using Matrix3 = Matrix<T, 3, 3>;
template <typename T> using Matrix4 = Matrix<T, 4, 4>;

using Matrix2i = Matrix2<int>;
using Matrix2f = Matrix2<float>;
using Matrix2d = Matrix2<double>;

using Matrix3i = Matrix3<int>;
using Matrix3f = Matrix3<float>;
using Matrix3d = Matrix3<double>;

using Matrix4i = Matrix4<int>;
using Matrix4f = Matrix4<float>;
using Matrix4d = Matrix4<double>;


//==============================================================================
// Private Implementation: Square Matrix Algebra Algorithm
// << 3D Math Primer for Graphics and Game Development >> 2nd
//   Chapter 6 More on Matrices
//==============================================================================

template <typename T, std::size_t N>
class SquareMatrixAlgebraAlgorithm
{
public:
    
    using MatrixN = Matrix<T, N, N>;

    static constexpr MatrixN Adjoint(const MatrixN& m)
    {
        return Adjoint(m, std::make_index_sequence<N * N>());
    }

    static constexpr T Determinant(const MatrixN& m)
    {
        return Determinant(m, Adjoint(m));
    }
    
    static constexpr MatrixN InverseTranspose(const MatrixN& m)
    {
        return InverseTranspose(m, Adjoint(m));
    }

private:

    template <std::size_t... I>
    static constexpr T Minor(const MatrixN& m,
        std::size_t deleted_row, std::size_t deleted_col, std::index_sequence<I...>)
    {
        auto SubmatrixCoeff = [&m, deleted_row, deleted_col](std::size_t subm_idx) {
            std::size_t subm_row = subm_idx / (N - 1);
            std::size_t subm_col = subm_idx % (N - 1);
            std::size_t row = (subm_row < deleted_row ? subm_row : subm_row + 1);
            std::size_t col = (subm_col < deleted_col ? subm_col : subm_col + 1);
            return m(row, col); };

        return Matrix<T, N - 1, N - 1>{SubmatrixCoeff(I)...}.Determinant();
    }

    static constexpr T Cofactor(const MatrixN& m, std::size_t row, std::size_t col)
    {
        T minor = Minor(m, row, col, std::make_index_sequence<(N - 1) * (N - 1)>());
        return (row + col) % 2 == 0 ? minor : -minor;
    }

    template <std::size_t... I>
    static constexpr MatrixN Adjoint(const MatrixN& m, std::index_sequence<I...>)
    {
        return {Cofactor(m, I / N, I % N)...};
    }
    
    static constexpr T Determinant(const MatrixN& m, const MatrixN& adj)
    {
        return m.template Row<0>().Dot(adj.template Row<0>());
    }

    static constexpr MatrixN InverseTranspose(const MatrixN& m, const MatrixN& adj)
    {
        return adj * (1 / Determinant(m, adj));
    }
};


template <typename T>
class SquareMatrixAlgebraAlgorithm<T, 3>
{
public:

    using Matrix3 = Matrix<T, 3, 3>;

    static constexpr Matrix3 Adjoint(const Matrix3& m)
    {
        return {+(m(1, 1) * m(2, 2) - m(2, 1) * m(1, 2)),
                -(m(1, 0) * m(2, 2) - m(2, 0) * m(1, 2)),
                +(m(1, 0) * m(2, 1) - m(2, 0) * m(1, 1)),
                -(m(0, 1) * m(2, 2) - m(2, 1) * m(0, 2)),
                +(m(0, 0) * m(2, 2) - m(2, 0) * m(0, 2)),
                -(m(0, 0) * m(2, 1) - m(2, 0) * m(0, 1)),
                +(m(0, 1) * m(1, 2) - m(1, 1) * m(0, 2)),
                -(m(0, 0) * m(1, 2) - m(1, 0) * m(0, 2)),
                +(m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1))};
    }

    static constexpr T Determinant(const Matrix3& m)
    {
        return +m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1))
               -m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0))
               +m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
    }

    static constexpr Matrix3 InverseTranspose(const Matrix3& m)
    {
        return Adjoint(m) * (1 / Determinant(m));
    }
};


template <typename T>
class SquareMatrixAlgebraAlgorithm<T, 2>
{
public:

    using Matrix2 = Matrix<T, 2, 2>;

    static constexpr Matrix2 Adjoint(const Matrix2& m)
    {
        return {m(1, 1), -m(0, 1), -m(1, 0), m(0, 0)};
    }

    static constexpr T Determinant(const Matrix2& m)
    {
        return m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
    }

    static constexpr Matrix2 InverseTranspose(const Matrix2& m)
    {
        return Adjoint(m) * (1 / Determinant(m));
    }
};

}