#include "Mesh.h"
#include <cassert>
#include "Math/Transform.h"

namespace Ashes {

//==============================================================================
// Mesh
//==============================================================================

const std::string& Mesh::Name() const
{
    return name_;
}

std::size_t Mesh::VertexSize() const
{
    return vertex_size_;
}

int Mesh::NumVertices() const
{
    // vertex size is non-zero certainly when vertices is not empty.
    return vertices_.empty() ? 0 : static_cast<int>(vertices_.size() / vertex_size_);
}

int Mesh::NumTriangles() const
{
    return static_cast<int>(indices_.size() / 3);
}

const void* Mesh::VertexBuffer() const
{
    return vertices_.data();
}

const std::uint16_t* Mesh::IndexBuffer() const
{
    return indices_.data();
}

const Geom::AxisAlignedBox& Mesh::Bounds() const
{
    return bounds_;
}

std::shared_ptr<MaterialBase> Mesh::MaterialAsset() const
{
    return material_;
}

void Mesh::SetName(const std::string& name)
{
    name_ = name;
}

void Mesh::ResizeVertexBuffer(std::size_t n)
{
    vertices_.resize(n * vertex_size_);
}

void Mesh::ResizeIndexBuffer(std::size_t n)
{
    indices_.resize(n);
}

void* Mesh::VertexBuffer()
{
    return vertices_.data();
}

std::uint16_t* Mesh::IndexBuffer()
{
    return indices_.data();
}

void Mesh::RebuildBounds()
{
    Geom::ComputeAxisAlignedBoundingBoxFromPoints(
        reinterpret_cast<const Vector3f*>(vertices_.data() + vertex_field_position_),
        NumVertices(), vertex_size_, bounds_);
}

void Mesh::SetMaterialAsset(std::shared_ptr<MaterialBase> mat)
{
    material_ = std::move(mat);
}

//==============================================================================
// MeshInstance
//==============================================================================

std::shared_ptr<Mesh> MeshInstance::MeshAsset() const
{
    return mesh_;
}

std::shared_ptr<MaterialBase> MeshInstance::MaterialAsset() const
{
    return mesh_ == nullptr ? nullptr : mesh_->MaterialAsset();
}

const Geom::AxisAlignedBox& MeshInstance::WorldBounds() const
{
    return bounds_;
}

void MeshInstance::SetMeshAsset(std::shared_ptr<Mesh> mesh)
{
    mesh_ = std::move(mesh);
}

void MeshInstance::RebuildWorldBounds()
{
    if (mesh_ != nullptr)
    {
        bounds_ = mesh_->Bounds();
        Geom::TransformAxisAlignedBoundingBox(WorldTransform(), bounds_);
    }
    else
    {
        bounds_.center = TransformationSRT(WorldTransform()).translation;
        bounds_.extents = Vector3f::Zero();
    }
}

void MeshInstance::OnTransformUpdated()
{
    RebuildWorldBounds();
}

//==============================================================================
// MeshMaker
//==============================================================================

namespace MeshMaker {

void Floor(Mesh& mesh)
{
    mesh.SetVertexType<BasicVertex>();
    mesh.ResizeVertexBuffer(4);
    mesh.ResizeIndexBuffer(6);

    auto mat = std::make_shared<MaterialFloor>();
    mat->double_sided = true;
    mat->color = {0.729f, 0.729f, 0.729f};
    mesh.SetMaterialAsset(mat);

    constexpr float r = 0.5f;
    auto* v = static_cast<BasicVertex*>(mesh.VertexBuffer());
    auto* i = mesh.IndexBuffer();

    v[0] = {{-r, 0.0f, -r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
    v[1] = {{-r, 0.0f, +r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
    v[2] = {{+r, 0.0f, +r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
    v[3] = {{+r, 0.0f, -r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
    i[0] = 0; i[1] = 1; i[2] = 2;
    i[3] = 0; i[4] = 2; i[5] = 3;

    mesh.RebuildBounds();
}

void FloorInstance(MeshInstance& obj, float width, float depth, float y)
{
    assert(obj.MeshAsset() != nullptr);
    Geom::AxisAlignedBox bounds;
    bounds.center = Vector3f::Zero();
    bounds.extents.X() = width;
    bounds.extents.Z() = depth;
    bounds.center.Y() = y;
    FloorInstance(obj, bounds);
}

void FloorInstance(MeshInstance& obj, const Geom::AxisAlignedBox& bounds)
{
    assert(obj.MeshAsset() != nullptr);
    TransformationSRT srt = TransformationSRT::Identity();
    srt.scale.X() = bounds.extents.X();
    srt.scale.Z() = bounds.extents.Z();
    srt.translation = bounds.center;
    obj.SetWorldTransform(srt.ToMatrix());
    obj.RebuildWorldBounds();
}

void Box(Mesh& mesh)
{
    mesh.SetVertexType<BasicVertex>();
    mesh.ResizeVertexBuffer(24);
    mesh.ResizeIndexBuffer(36);
    mesh.SetMaterialAsset(std::make_shared<MaterialPhong>());

    constexpr float r = 0.5f;
    auto* v = static_cast<BasicVertex*>(mesh.VertexBuffer());
    auto* i = mesh.IndexBuffer();

    // Fill in the front face vertex data.
    v[0] = {{-r, -r, -r}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
    v[1] = {{-r, +r, -r}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
    v[2] = {{+r, +r, -r}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
    v[3] = {{+r, -r, -r}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
    // Fill in the back face vertex data.
    v[4] = {{-r, -r, +r}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
    v[5] = {{+r, -r, +r}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
    v[6] = {{+r, +r, +r}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
    v[7] = {{-r, +r, +r}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
    // Fill in the top face vertex data.
    v[8]  = {{-r, +r, -r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
    v[9]  = {{-r, +r, +r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
    v[10] = {{+r, +r, +r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
    v[11] = {{+r, +r, -r}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
    // Fill in the bottom face vertex data.
    v[12] = {{-r, -r, -r}, {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}};
    v[13] = {{+r, -r, -r}, {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}};
    v[14] = {{+r, -r, +r}, {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}};
    v[15] = {{-r, -r, +r}, {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}};
    // Fill in the left face vertex data.
    v[16] = {{-r, -r, +r}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}, {0.0f, 1.0f}};
    v[17] = {{-r, +r, +r}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}, {0.0f, 0.0f}};
    v[18] = {{-r, +r, -r}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}, {1.0f, 0.0f}};
    v[19] = {{-r, -r, -r}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}, {1.0f, 1.0f}};
    // Fill in the right face vertex data.
    v[20] = {{+r, -r, -r}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}};
    v[21] = {{+r, +r, -r}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}};
    v[22] = {{+r, +r, +r}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}};
    v[23] = {{+r, -r, +r}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}};

    // Fill in the front face index data
    i[0] = 0; i[1] = 1; i[2] = 2;
    i[3] = 0; i[4] = 2; i[5] = 3;
    // Fill in the back face index data
    i[6] = 4; i[7]  = 5; i[8]  = 6;
    i[9] = 4; i[10] = 6; i[11] = 7;
    // Fill in the top face index data
    i[12] = 8; i[13] =  9; i[14] = 10;
    i[15] = 8; i[16] = 10; i[17] = 11;
    // Fill in the bottom face index data
    i[18] = 12; i[19] = 13; i[20] = 14;
    i[21] = 12; i[22] = 14; i[23] = 15;
    // Fill in the left face index data
    i[24] = 16; i[25] = 17; i[26] = 18;
    i[27] = 16; i[28] = 18; i[29] = 19;
    // Fill in the right face index data
    i[30] = 20; i[31] = 21; i[32] = 22;
    i[33] = 20; i[34] = 22; i[35] = 23;

    mesh.RebuildBounds();
}

void BoxInstance(MeshInstance& obj, float width, float depth, float height)
{
    assert(obj.MeshAsset() != nullptr);
    Geom::AxisAlignedBox bounds;
    bounds.center = Vector3f::Zero();
    bounds.extents = 0.5f * Vector3f(width, height, depth);
    BoxInstance(obj, bounds);
}

void BoxInstance(MeshInstance& obj, const Geom::AxisAlignedBox& bounds)
{
    assert(obj.MeshAsset() != nullptr);
    TransformationSRT srt = TransformationSRT::Identity();
    srt.scale = 2.0f * bounds.extents;
    srt.translation = bounds.center;
    obj.SetWorldTransform(srt.ToMatrix());
    obj.RebuildWorldBounds();
}

}

}