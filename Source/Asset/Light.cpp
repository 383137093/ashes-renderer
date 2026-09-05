#include "Light.h"

namespace Ashes {

LightBase::~LightBase()
{
}

void AmbientLight::Sample(
    const Vector3f&,
    Vector3f& dir_to_light,
    Vector3f& color_from_light) const
{
    dir_to_light = Vector3f::Zero();
    color_from_light = color;
}

void DirectionalLight::Sample(
    const Vector3f&,
    Vector3f& dir_to_light,
    Vector3f& color_from_light) const
{
    dir_to_light = -direction;
    color_from_light = color;
}

void PointLight::Sample(
    const Vector3f& point,
    Vector3f& dir_to_light,
    Vector3f& color_from_light) const
{
    Vector3f to_light = position - point;
    float light_inv_dist = 1.0f / to_light.Norm();
    dir_to_light = to_light * light_inv_dist;
    color_from_light = color * light_inv_dist * light_inv_dist;
}

}