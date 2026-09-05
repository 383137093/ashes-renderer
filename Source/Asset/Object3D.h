#pragma once

#include <memory>
#include <string>
#include "Math/Matrix.h"

namespace Ashes {

class Object3D
{
public:

    // Special member functions: uncopyable but moveable
    Object3D();
    Object3D(const Object3D&) = delete;
    Object3D(Object3D&&) = default;
    virtual ~Object3D();
    Object3D& operator = (const Object3D&) = delete;
    Object3D& operator = (Object3D&&) = default;

    // Access 3D Object
    const std::string& Name() const;                     // display name
    const Matrix4f& WorldTransform() const;              // local space to world space
    const Matrix4f& WorldTransformInverse() const;       // world space to local space
    const Matrix4f& RelativeTransform() const;           // local space to parent space
    virtual Matrix4f SocketTransform(int socket) const;  // socket space to local space
    std::weak_ptr<Object3D> AttachParent() const;        // object we attach to
    int AttachSocket() const;                            // socket we attach to

    // Modify 3D Object
    void SetName(const std::string& name);
    void SetWorldTransform(const Matrix4f& m);
    void SetRelativeTransform(const Matrix4f& m);
    void AttachTo(std::weak_ptr<Object3D> parent, int socket, const Matrix4f& m);
    void DetachFromParent();
    virtual void OnTransformUpdated();

private:

    std::string             name_;
    Matrix4f                world_transform_ = Matrix4f::Identity();
    Matrix4f                world_transform_inverse_ = Matrix4f::Identity();
    Matrix4f                relative_transform_ = Matrix4f::Identity();
    std::weak_ptr<Object3D> attach_parent_;
    int                     attach_socket_ = -1;
};

}