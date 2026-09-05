#pragma once

#include <cstdlib>
#include <charconv>
#include <iterator>
#include <algorithm>
#include <string_view>

namespace Ashes { namespace StringAlgo {

inline bool FromStringView(std::string_view sv, int& value)
{
    auto r = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    return r.ec == std::errc() && r.ptr == sv.data() + sv.size();
}

inline bool FromStringView(std::string_view sv, float& value)
{
#if defined(__APPLE__)
    // std::from_chars for float is not supported on Apple
    char str[512], *value_end = nullptr;
    if (sv.empty() || sv.size() >= std::size(str)) { return false; }
    *std::copy_n(sv.data(), sv.size(), str) = 0;
    value = std::strtof(str, &value_end);
    return value_end == str + sv.size();
#else
    auto r = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    return r.ec == std::errc() && r.ptr == sv.data() + sv.size();
#endif
}

inline bool FromStringView(std::string_view sv, std::string_view& value)
{
    value = sv;
    return true;
}

template <typename SizeType, typename ForwardIterator, typename ValueType=
    typename std::iterator_traits<ForwardIterator>::value_type>
inline SizeType SplitN(
    std::string_view sv,
    std::string_view token,
    SizeType count,
    ForwardIterator result,
    ValueType value = ValueType())
{
    std::string::size_type first = 0;
    SizeType i = 0;
    
    for (; i < count && first < sv.size(); ++i)
    {
        auto pos = sv.find(token, first);
        auto last = (pos == std::string::npos ? sv.size() : pos);
        bool valid = FromStringView({sv.data() + first, last - first}, value);
        *result++ = (valid ? value : ValueType());
        first = last + token.size();
    }
    
    return i;
}

template <typename Container>
inline std::size_t Split(
    std::string_view sv,
    std::string_view token,
    Container& result)
{
    result.clear();
    return SplitN(sv, token, result.max_size(),
        std::back_inserter(result), Container::value_type());
}

template <typename ValueType, std::size_t N>
inline std::size_t Split(
    std::string_view sv,
    std::string_view token,
    ValueType (&result)[N])
{
    return SplitN(sv, token, N, result, ValueType());
}

}}