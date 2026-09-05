#pragma once

#include "Math/Vector.h"

namespace Ashes {

struct LightBase
{
    virtual ~LightBase();

    virtual void Sample(
        const Vector3f& point,
        Vector3f& dir_to_light,
        Vector3f& color_from_light) const = 0;
};

struct AmbientLight : LightBase
{
    void Sample(
        const Vector3f& point,
        Vector3f& dir_to_light,
        Vector3f& color_from_light) const override;

    Vector3f color = Vector3f::Zero();
};

struct DirectionalLight : LightBase
{
    void Sample(
        const Vector3f& point,
        Vector3f& dir_to_light,
        Vector3f& color_from_light) const override;

    Vector3f direction = Vector3f::UnitX();
    Vector3f color = Vector3f::Zero();
};

struct PointLight : LightBase
{
    void Sample(
        const Vector3f& point,
        Vector3f& dir_to_light,
        Vector3f& color_from_light) const override;

    Vector3f position = Vector3f::Zero();
    Vector3f color = Vector3f::Zero();
};

}