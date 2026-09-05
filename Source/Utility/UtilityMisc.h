#pragma once

#include <memory>
#include <type_traits>

namespace Ashes {

template <typename ContainerType, typename IndexType>
inline bool IsValidIndex(const ContainerType& c, IndexType idx) noexcept
{
    return 0 <= idx && static_cast<std::size_t>(idx) < std::size(c);
}

template <typename T, typename U, typename=std::enable_if_t<std::is_base_of_v<U, T>>>
inline std::shared_ptr<T> DownCast(const std::shared_ptr<U>& ptr) noexcept
{
    return std::dynamic_pointer_cast<T>(ptr);
}

template <typename T, typename U, typename=std::enable_if_t<std::is_base_of_v<U, T>>>
inline std::shared_ptr<T> DownCast(std::shared_ptr<U>&& ptr) noexcept
{
    return std::dynamic_pointer_cast<T>(ptr);
}

template <typename T, typename U, typename=std::enable_if_t<std::is_base_of_v<U, T>>>
inline const T* DownCast(const U* ptr) noexcept
{
    return dynamic_cast<const T*>(ptr);
}

template <typename T, typename U, typename=std::enable_if_t<std::is_base_of_v<U, T>>>
inline T* DownCast(U* ptr) noexcept
{
    return dynamic_cast<T*>(ptr);
}

}