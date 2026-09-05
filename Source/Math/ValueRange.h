#pragma once

#include <limits>
#include <algorithm>

namespace Ashes {

template <typename T>
struct ValueRange
{
    static constexpr ValueRange EmptyRange()
    {
        return {std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest()};
    }

    bool IsEmpty() const
    {
        return min > max;
    }

    bool Contain(T x) const
    {
        return min <= x && x <= max;
    }
    
    ValueRange Intersect(const ValueRange& rhs) const
    {
        return IsEmpty() ? *this : (rhs.IsEmpty() ? rhs :
            ValueRange{std::max(min, rhs.min), std::min(max, rhs.max)});
    }

    ValueRange Union(const ValueRange& rhs) const
    {
        return rhs.IsEmpty() ? *this : (IsEmpty() ? rhs :
            ValueRange{std::min(min, rhs.min), std::max(max, rhs.max)});
    }
    
    friend ValueRange operator + (const ValueRange& range, T x)
    {
        return {range.min + x, range.max + x};
    }

    friend ValueRange operator - (const ValueRange& range, T x)
    {
        return {range.min - x, range.max - x};
    }

    friend ValueRange operator * (const ValueRange& range, T x)
    {
        return {range.min * x, range.max * x};
    }

    friend ValueRange operator / (const ValueRange& range, T x)
    {
        return {range.min / x, range.max / x};
    }

    T min, max;
};

using IntRange = ValueRange<int>;
using FloatRange = ValueRange<float>;

}