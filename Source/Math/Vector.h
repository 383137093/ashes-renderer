#pragma once

#include <cmath>
#include <limits>
#include <cstdint>
#include <utility>
#include <functional>
#include <type_traits>

namespace Ashes {

template <typename T, std::size_t N>
class Vector
{
public:

    static constexpr std::size_t kSize = N;
    static constexpr T kEpsilon = std::numeric_limits<T>::epsilon();

    using ValueType = T;

    T coeffs[N];

    //==========================================================================
    // Constant Vectors
    //==========================================================================

    static constexpr Vector Zero() { return MakeRepeatedVec(0, MakeIndices<>()); }
    static constexpr Vector Ones() { return MakeRepeatedVec(1, MakeIndices<>()); }
    static constexpr Vector UnitX() { return MakeUnitVec<0>(MakeIndices<>()); }
    static constexpr Vector UnitY() { return MakeUnitVec<1>(MakeIndices<>()); }
    static constexpr Vector UnitZ() { return MakeUnitVec<2>(MakeIndices<>()); }
    static constexpr Vector UnitW() { return MakeUnitVec<3>(MakeIndices<>()); }

    //==========================================================================
    // Constructors
    //==========================================================================
    
    Vector() = default;

    template <typename... Args, typename = std::enable_if_t<sizeof...(Args) == N>>
    constexpr Vector(Args... args)
        : coeffs{static_cast<T>(args)...} {}

    template <typename U>
    constexpr explicit Vector(const U (&arr)[N])
        : Vector(MakeVecFromArray(arr, MakeIndices<>())) {}

    template <typename U, typename = std::enable_if_t<std::is_pointer_v<U>>>
    constexpr explicit Vector(U ptr)
        : Vector(MakeVecFromArray(ptr, MakeIndices<>())) {}

    //==========================================================================
    // Coefficient Accessors
    //==========================================================================

    constexpr T X() const { static_assert(N > 0); return coeffs[0]; }
    constexpr T Y() const { static_assert(N > 1); return coeffs[1]; }
    constexpr T Z() const { static_assert(N > 2); return coeffs[2]; }
    constexpr T W() const { static_assert(N > 3); return coeffs[3]; }
    constexpr T operator [] (std::size_t idx) const { return coeffs[idx]; }

    T& X() { static_assert(N > 0); return coeffs[0]; }
    T& Y() { static_assert(N > 1); return coeffs[1]; }
    T& Z() { static_assert(N > 2); return coeffs[2]; }
    T& W() { static_assert(N > 3); return coeffs[3]; }
    T& operator [] (std::size_t idx) { return coeffs[idx]; }

    //==========================================================================
    // Derived Vectors
    //==========================================================================
    
    template <typename U>
    constexpr Vector<U, N> Cast() const
    {
        return Vector<U, N>{coeffs};
    }

    template <typename Op>
    constexpr Vector<std::invoke_result_t<Op, T>, N> Convert(Op op) const
    {
        return VecCwiseUnaryOp(*this, op, MakeIndices<>());
    }

    template <typename... Args>
    constexpr Vector<T, N + sizeof...(Args)> Join(Args... args) const
    {
        return VecJoinScalars(*this, MakeIndices<>(), args...);
    }

    template <std::size_t... I>
    constexpr Vector<T, sizeof...(I)> Coeffs(std::index_sequence<I...>) const
    {
        return {coeffs[I]...};
    }

    template <std::size_t M>
    constexpr Vector<T, M> Head() const
    {
        return Coeffs(MakeIndices<M>());
    }

    //==========================================================================
    // Coefficient Sequence Algorithm
    //==========================================================================

    constexpr T MinCoeff() const
    {
        return VecAccumulate<1>(*this, coeffs[0], MinOp());
    }

    constexpr T MaxCoeff() const
    {
        return VecAccumulate<1>(*this, coeffs[0], MaxOp());
    }

    constexpr bool IsApprox(const Vector& other, T precision = kEpsilon) const
    {
        return VecCwiseCmpVec(*this, other, precision, MakeIndices<>());
    }

    constexpr bool IsZero(T precision = kEpsilon) const
    {
        return VecCwiseCmpVec(*this, Zero(), precision, MakeIndices<>());
    }

    constexpr Vector CwiseAbs() const
    {
        return VecCwiseUnaryOp(*this, AbsOp(), MakeIndices<>());
    }

    constexpr Vector CwiseMin(const Vector& other) const
    {
        return VecCwiseOpVec(*this, other, MinOp(), MakeIndices<>());
    }

    constexpr Vector CwiseMax(const Vector& other) const
    {
        return VecCwiseOpVec(*this, other, MaxOp(), MakeIndices<>());
    }

    //==========================================================================
    // Coefficient Sequence Operators
    //==========================================================================

    constexpr Vector operator - () const
    {
        return VecCwiseUnaryOp(*this, std::negate<T>(), MakeIndices<>());
    }

    friend constexpr bool operator == (const Vector& lhs, const Vector& rhs)
    {
        return lhs.IsApprox(rhs, 0);
    }

    friend constexpr bool operator != (const Vector& lhs, const Vector& rhs)
    {
        return !lhs.IsApprox(rhs, 0);
    }

    friend constexpr Vector operator + (const Vector& lhs, const Vector& rhs)
    {
        return VecCwiseOpVec(lhs, rhs, std::plus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator - (const Vector& lhs, const Vector& rhs)
    {
        return VecCwiseOpVec(lhs, rhs, std::minus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator * (const Vector& lhs, const Vector& rhs)
    {
        return VecCwiseOpVec(lhs, rhs, std::multiplies<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator / (const Vector& lhs, const Vector& rhs)
    {
        return VecCwiseOpVec(lhs, rhs, std::divides<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator + (const Vector& lhs, T rhs)
    {
        return VecCwiseOpScalar(lhs, rhs, std::plus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator - (const Vector& lhs, T rhs)
    {
        return VecCwiseOpScalar(lhs, rhs, std::minus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator * (const Vector& lhs, T rhs)
    {
        return VecCwiseOpScalar(lhs, rhs, std::multiplies<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator / (const Vector& lhs, T rhs)
    {
        return VecCwiseOpScalar(lhs, rhs, std::divides<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator + (T lhs, const Vector& rhs)
    {
        return ScalarCwiseOpVec(lhs, rhs, std::plus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator - (T lhs, const Vector& rhs)
    {
        return ScalarCwiseOpVec(lhs, rhs, std::minus<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator * (T lhs, const Vector& rhs)
    {
        return ScalarCwiseOpVec(lhs, rhs, std::multiplies<T>(), MakeIndices<>());
    }

    friend constexpr Vector operator / (T lhs, const Vector& rhs)
    {
        return ScalarCwiseOpVec(lhs, rhs, std::divides<T>(), MakeIndices<>());
    }

    //==========================================================================
    // Compound Assignment Operators
    //==========================================================================

    Vector& operator += (const Vector& other) { return *this = *this + other; }
    Vector& operator -= (const Vector& other) { return *this = *this - other; }
    Vector& operator *= (const Vector& other) { return *this = *this * other; }
    Vector& operator /= (const Vector& other) { return *this = *this / other; }

    Vector& operator += (T scalar) { return *this = *this + scalar; }
    Vector& operator -= (T scalar) { return *this = *this - scalar; }
    Vector& operator *= (T scalar) { return *this = *this * scalar; }
    Vector& operator /= (T scalar) { return *this = *this / scalar; }

    //==========================================================================
    // Algebraic Algorithms
    //==========================================================================

    constexpr T SquaredNorm() const
    {
        return VecDotVec(*this, *this, MakeIndices<>());
    }

    T Norm() const
    {
        return std::sqrt(SquaredNorm());
    }

    Vector Normalized() const
    {
        return *this * (1 / Norm());
    }

    constexpr T Dot(const Vector& other) const
    {
        return VecDotVec(*this, other, MakeIndices<>());
    }
    
    constexpr Vector Cross(const Vector& other) const
    {
        static_assert(N == 3);
        return {Y() * other.Z() - Z() * other.Y(),
                Z() * other.X() - X() * other.Z(),
                X() * other.Y() - Y() * other.X()};
    }

private:
    
    template <std::size_t L = N>
    using MakeIndices = std::make_index_sequence<L>;

    struct AbsOp
    {
        constexpr T operator () (T x) const { return x < 0 ? -x : x; }
    };

    struct MinOp
    {
        constexpr T operator () (T a, T b) const { return a < b ? a : b; }
    };

    struct MaxOp
    {
        constexpr T operator () (T a, T b) const { return a > b ? a : b; }
    };

private:

    template <std::size_t... I>
    static constexpr Vector MakeRepeatedVec(T coeff, std::index_sequence<I...>)
    {
        return {((void)I, coeff)...};
    }

    template <std::size_t X, std::size_t... I, typename = std::enable_if_t<X < N>>
    static constexpr Vector MakeUnitVec(std::index_sequence<I...>)
    {
        return {(I == X ? 1 : 0)...};
    }

    template <typename U, std::size_t... I>
    static constexpr Vector MakeVecFromArray(U coeffs, std::index_sequence<I...>)
    {
        return {static_cast<T>(coeffs[I])...};
    }
    
    template <typename... Args, std::size_t... I>
    static constexpr Vector<T, N + sizeof...(Args)> VecJoinScalars(
        const Vector& v, std::index_sequence<I...>, Args... args)
    {
        return {(v.coeffs[I])..., args...};
    }

    template <std::size_t I, typename U, typename Op>
    static constexpr U VecAccumulate(const Vector& v, U init, Op op)
    {
        if constexpr (I >= N) { return init; }
        else { return VecAccumulate<I + 1>(v, op(init, v.coeffs[I]), op); }
    }

    template <std::size_t... I>
    static constexpr bool VecCwiseCmpVec(const Vector& lhs, const Vector& rhs,
        T precision, std::index_sequence<I...>)
    {
        return ((AbsOp()(lhs[I] - rhs[I]) <= precision) && ...);
    }

    template <typename Op, std::size_t... I>
    static constexpr Vector<std::invoke_result_t<Op, T>, N> VecCwiseUnaryOp(
        const Vector& v, Op op, std::index_sequence<I...>)
    {
        return {op(v.coeffs[I])...};
    }

    template <typename Op, std::size_t... I>
    static constexpr Vector VecCwiseOpVec(const Vector& lhs, const Vector& rhs,
        Op op, std::index_sequence<I...>)
    {
        return {op(lhs.coeffs[I], rhs.coeffs[I])...};
    }

    template <typename Op, std::size_t... I>
    static constexpr Vector VecCwiseOpScalar(const Vector& lhs, T rhs,
        Op op, std::index_sequence<I...>)
    {
        return {op(lhs.coeffs[I], rhs)...};
    }
    
    template <typename Op, std::size_t... I>
    static constexpr Vector ScalarCwiseOpVec(T lhs, const Vector& rhs,
        Op op, std::index_sequence<I...>)
    {
        return {op(lhs, rhs.coeffs[I])...};
    }

    template <std::size_t... I>
    static constexpr T VecDotVec(const Vector& lhs, const Vector& rhs,
        std::index_sequence<I...>)
    {
        return ((lhs.coeffs[I] * rhs.coeffs[I]) + ...);
    }
};


template <typename T> using Vector2 = Vector<T, 2>;
template <typename T> using Vector3 = Vector<T, 3>;
template <typename T> using Vector4 = Vector<T, 4>;

using Vector2b = Vector2<std::uint8_t>;
using Vector2i = Vector2<int>;
using Vector2f = Vector2<float>;
using Vector2d = Vector2<double>;

using Vector3b = Vector3<std::uint8_t>;
using Vector3i = Vector3<int>;
using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;

using Vector4b = Vector4<std::uint8_t>;
using Vector4i = Vector4<int>;
using Vector4f = Vector4<float>;
using Vector4d = Vector4<double>;

}