#pragma once

#include <memory>
#include <vector>
#include "Asset/Mesh.h"
#include "Asset/Light.h"
#include "Asset/Movie.h"
#include "Asset/Camera.h"
#include "Asset/Texture.h"
#include "Asset/Material.h"
#include "Asset/Object3D.h"
#include "Asset/Skeleton.h"
#include "Asset/Animation.h"

namespace Ashes {

using VecSharedTexture  = std::vector<std::shared_ptr<Texture>>;
using VecSharedMaterial = std::vector<std::shared_ptr<MaterialBase>>;
using VecSharedMesh     = std::vector<std::shared_ptr<Mesh>>;
using VecSharedSkeleton = std::vector<std::shared_ptr<Skeleton>>;
using VecSharedObject3D = std::vector<std::shared_ptr<Object3D>>;
using VecSharedLight    = std::vector<std::shared_ptr<LightBase>>;

struct AssetContent
{
    void Reset()
    {
        textures.clear();
        materials.clear();
        meshes.clear();
        skeletons.clear();
        objects.clear();
        movies.clear();
        cameras.clear();
        lights.clear();
    }

    VecSharedTexture    textures;
    VecSharedMaterial   materials;
    VecSharedMesh       meshes;
    VecSharedSkeleton   skeletons;
    VecSharedObject3D   objects;
    std::vector<Movie>  movies;
    std::vector<Camera> cameras;
    VecSharedLight      lights;
};

}