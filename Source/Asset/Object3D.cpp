#include "Object3D.h"

namespace Ashes {

Object3D::Object3D()
{
}

Object3D::~Object3D()
{
}

const std::string& Object3D::Name() const
{
    return name_;
}

const Matrix4f& Object3D::WorldTransform() const
{
    return world_transform_;
}

const Matrix4f& Object3D::WorldTransformInverse() const
{
    return world_transform_inverse_;
}

const Matrix4f& Object3D::RelativeTransform() const
{
    return relative_transform_;
}

Matrix4f Object3D::SocketTransform(int) const
{
    return Matrix4f::Identity();
}

std::weak_ptr<Object3D> Object3D::AttachParent() const
{
    return attach_parent_;
}

int Object3D::AttachSocket() const
{
    return attach_socket_;
}

void Object3D::SetName(const std::string& name)
{
    name_ = name;
}

void Object3D::SetWorldTransform(const Matrix4f& m)
{
    world_transform_ = m;
    world_transform_inverse_ = world_transform_.Inverse();

    if (std::shared_ptr<Object3D> attach_parent = attach_parent_.lock())
    {
        Matrix4f world_to_parent = attach_parent->WorldTransformInverse();
        Matrix4f socket_to_parent = attach_parent->SocketTransform(attach_socket_);
        Matrix4f parent_to_socket = socket_to_parent.Inverse();
        relative_transform_ = parent_to_socket * world_to_parent * world_transform_;
    }
    else
    {
        relative_transform_ = world_transform_;
    }
    
    OnTransformUpdated();
}

void Object3D::SetRelativeTransform(const Matrix4f& m)
{
    relative_transform_ = m;

    if (std::shared_ptr<Object3D> attach_parent = attach_parent_.lock())
    {
        Matrix4f parent_to_world = attach_parent->WorldTransform();
        Matrix4f socket_to_parent = attach_parent->SocketTransform(attach_socket_);
        world_transform_ = parent_to_world * socket_to_parent * relative_transform_;
        world_transform_inverse_ = world_transform_.Inverse();
    }
    else
    {
        world_transform_ = relative_transform_;
        world_transform_inverse_ = world_transform_.Inverse();
    }
    
    OnTransformUpdated();
}

void Object3D::AttachTo(
    std::weak_ptr<Object3D> parent,
    int socket,
    const Matrix4f& m)
{
    if (!parent.expired())
    {
        attach_parent_ = parent;
        attach_socket_ = socket;
        SetRelativeTransform(m);
    }
}

void Object3D::DetachFromParent()
{
    if (!attach_parent_.expired())
    {
        attach_parent_.reset();
        attach_socket_ = -1;
        relative_transform_ = world_transform_;
    }
}

void Object3D::OnTransformUpdated()
{
}

}