#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <typeinfo>
#include "Asset/Material.h"
#include "Asset/Object3D.h"
#include "Geometry/AxisAlignedBox.h"

namespace Ashes {

//==============================================================================
// Vertex
//==============================================================================

struct BasicVertex
{
    Vector3f position;  // unitless position
    Vector3f normal;    // normalized normal
    Vector4f tangent;   // XYZ for normalized tangent and W for sign of bitangent
    Vector2f texcoord;  // UV texture coordinates
};

struct SkinnedVertex
{
    Vector3f position;  // unitless position
    Vector3f normal;    // normalized normal
    Vector4f tangent;   // XYZ for normalized tangent and W for sign of bitangent
    Vector2f texcoord;  // UV texture coordinates
    Vector4b joints;    // indices of the joints that affect the vertex
    Vector4f weights;   // how strongly the joint influences the vertex
};


//==============================================================================
// Mesh
//==============================================================================

class Mesh
{
public:
    
    // Special member functions: uncopyable but moveable
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    Mesh& operator = (const Mesh&) = delete;
    Mesh& operator = (Mesh&&) = default;

    // Access Mesh
    const std::string& Name() const;
    std::size_t VertexSize() const;
    int NumVertices() const;
    int NumTriangles() const;
    const void* VertexBuffer() const;
    const std::uint16_t* IndexBuffer() const;
    const Geom::AxisAlignedBox& Bounds() const;
    std::shared_ptr<MaterialBase> MaterialAsset() const;

    // Modify Mesh
    void SetName(const std::string& name);
    void ResizeVertexBuffer(std::size_t n);
    void ResizeIndexBuffer(std::size_t n);
    void* VertexBuffer();
    std::uint16_t* IndexBuffer();
    void RebuildBounds();
    void SetMaterialAsset(std::shared_ptr<MaterialBase> mat);

    template <class T>
    void SetVertexType()
    {
        vertex_type_info_ = &typeid(T);
        vertex_size_ = sizeof(T);
        vertex_field_position_ = offsetof(T, position);
    }

private:

    std::string                   name_;
    const std::type_info*         vertex_type_info_ = nullptr;
    std::size_t                   vertex_size_ = 0;
    std::size_t                   vertex_field_position_ = 0;
    std::vector<std::uint8_t>     vertices_;
    std::vector<std::uint16_t>    indices_;
    Geom::AxisAlignedBox          bounds_ = Geom::AxisAlignedBox::Null();
    std::shared_ptr<MaterialBase> material_;
};


//==============================================================================
// MeshInstance
//==============================================================================

class MeshInstance : public Object3D
{
public:

    std::shared_ptr<Mesh> MeshAsset() const;
    std::shared_ptr<MaterialBase> MaterialAsset() const;
    const Geom::AxisAlignedBox& WorldBounds() const;
    void SetMeshAsset(std::shared_ptr<Mesh> mesh);
    void RebuildWorldBounds();
    void OnTransformUpdated() override;

private:
    
    std::shared_ptr<Mesh> mesh_;
    Geom::AxisAlignedBox  bounds_ = Geom::AxisAlignedBox::Null();
};


//==============================================================================
// MeshMaker
//==============================================================================

namespace MeshMaker {

void Floor(Mesh& mesh);
void FloorInstance(MeshInstance& obj, float width, float depth, float y);
void FloorInstance(MeshInstance& obj, const Geom::AxisAlignedBox& bounds);

void Box(Mesh& mesh);
void BoxInstance(MeshInstance& obj, float width, float depth, float height);
void BoxInstance(MeshInstance& obj, const Geom::AxisAlignedBox& bounds);

}

}