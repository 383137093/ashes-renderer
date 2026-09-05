#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "ThirdParty/json.hpp"

namespace Ashes {

class JsonProxy
{
public:

    JsonProxy(const nlohmann::json& o) : json_(o) {}

    bool IsNull() const { return json_.is_null(); }
    bool IsObject() const { return json_.is_object(); }
    bool IsArray() const { return json_.is_array(); }
    int ArraySize() const { return IsArray() ? static_cast<int>(json_.size()) : 0; }

    auto begin() const { return json_.begin(); }
    auto end() const { return json_.end(); }

    //==========================================================================
    // Convert Json to Simple Value
    //==========================================================================

    const char* Value(const char* def) const
    {
        return json_.is_string() ? json_.get_ref<const std::string&>().data() : def;
    }
    
    std::string_view Value(std::string_view def) const
    {
        return json_.is_string() ? json_.get_ref<const std::string&>() : def;
    }

    template <typename T, typename = std::enable_if_t<std::is_scalar_v<T>>>
    T Value(T def) const
    {
        try { return json_.get<T>(); }
        catch (...) { return def; }
    }
 
    template <typename T, std::size_t N>
    Vector<T, N> Value(const Vector<T, N>& def) const
    {
        T arr[N];
        try { return Vector<T, N>(json_.get_to(arr)); }
        catch (...) { return def; }
    }

    template <typename T, std::size_t Rows, std::size_t Cols>
    Matrix<T, Rows, Cols> Value(const Matrix<T, Rows, Cols>& def) const
    {
        T arr[Rows * Cols];
        try { return Matrix<T, Rows, Cols>(json_.get_to(arr)); }
        catch (...) { return def; }
    }

    Quaternion Value(const Quaternion& def) const
    {
        return Quaternion(Value(reinterpret_cast<const Vector4f&>(def)));
    }

    //==========================================================================
    // Access Child of Json by Key
    //==========================================================================

    JsonProxy Child(const char* key) const
    {
        auto iter = json_.find(key);
        return iter != json_.end() ? JsonProxy(*iter) : MakeNull();
    }

    JsonProxy ChildObject(const char* key) const
    {
        JsonProxy child = Child(key);
        return child.IsObject() ? child : MakeEmptyObject();
    }

    JsonProxy ChildArray(const char* key) const
    {
        JsonProxy child = Child(key);
        return child.IsArray() ? child : MakeEmptyArray();
    }

    template <typename T>
    T ChildValue(const char* key, T def) const
    {
        JsonProxy child = Child(key);
        return !child.IsNull() ? child.Value(def) : def;
    }

    //==========================================================================
    // Access Child of Json by index
    //==========================================================================

    JsonProxy Child(int idx) const
    {
        bool valid = (0 <= idx && idx < ArraySize());
        return valid ? JsonProxy(json_[idx]) : MakeNull();
    }

    JsonProxy ChildObject(int idx) const
    {
        JsonProxy child = Child(idx);
        return child.IsObject() ? child : MakeEmptyObject();
    }

    JsonProxy ChildArray(int idx) const
    {
        JsonProxy child = Child(idx);
        return child.IsArray() ? child : MakeEmptyArray();
    }

    template <typename T>
    T ChildValue(int idx, T def) const
    {
        JsonProxy child = Child(idx);
        return !child.IsNull() ? child.Value(def) : def;
    }

private:
    
    static JsonProxy MakeNull()
    {
        static const nlohmann::json kNull;
        return kNull;
    }

    static JsonProxy MakeEmptyObject()
    {
        static const nlohmann::json kEmptyObject = nlohmann::json::object();
        return kEmptyObject;
    }

    static JsonProxy MakeEmptyArray()
    {
        static const nlohmann::json kEmptyArray = nlohmann::json::array();
        return kEmptyArray;
    }
    
    const nlohmann::json& json_;
};

}