#pragma once

#include <memory>
#include <vector>
#include <unordered_set>
#include "Math/Vector.h"
#include "Math/Transform.h"
#include "Asset/AssetContent.h"
#include "Geometry/AxisAlignedBox.h"

namespace Ashes { namespace Rasterize {

class Scene
{
public:

    // Special member functions: uncopyable but moveable
    Scene();
    Scene(const Scene&) = delete;
    Scene(Scene&&) = default;
    ~Scene();
    Scene& operator = (const Scene&) = delete;
    Scene& operator = (Scene&&) = default;

    // Build Scene
    void BuildScene(const AssetContent& assets);
    void CleanScene();

    // Access Scene Entities
    const VecSharedObject3D& Objects() const;
    const std::vector<Camera>& Cameras() const;
    const Vector3f& AmbientLightColor() const;
    const VecSharedLight& Lights() const;
    Geom::AxisAlignedBox Bounds() const;
    bool IsFloorVisible() const;

    // Modify Scene Entities
    void SetAmbientLightColor(const Vector3f& color);
    void AddDirectionalLight(const Vector3f& dir, const Vector3f& color);
    void AddPointLight(const Vector3f& pos, const Vector3f& color);
    void SetFloorVisibility(bool visible);
    void TickScene(float delta);

    // Scene Movie
    const std::vector<Movie>& Movies() const;
    int CurrentPlayingMovieIndex() const;
    const Movie* CurrentPlayingMovie() const;
    void PlayMovie(int movie_idx, bool looping);
    void StopMovie();
    void SetPlayingMovie(int movie_idx, bool looping);

    // Object Animation
    std::vector<int> AnimatableObjectIndices() const;
    const std::vector<Animation>* Animations(int obj_idx) const;
    int CurrentPlayingAnimationIndex(int obj_idx) const;
    const Animation* CurrentPlayingAnimation(int obj_idx) const;
    void PlayAnimation(int obj_idx, int anim_idx, bool looping);
    void StopAnimation(int obj_idx);
    void SetPlayingAnimation(int obj_idx, int anim_idx, bool looping);

private:

    void BuildAmbientLight();
    void BuildBounds();
    void BuildFloor();
    void BuildObjectInitTransforms();

    void TickMovie(float delta);
    void TickAnimation(float delta);
    void SolveObjectsTransform();
    void SolveObjectChainTransform(Object3D* tail_obj);

private:

    AssetContent                   assets_;
    Vector3f                       ambient_light_ = Vector3f::Zero();
    Geom::AxisAlignedBox           bounds_ = Geom::AxisAlignedBox::Null();
    std::shared_ptr<MeshInstance>  floor_;
    std::vector<TransformationSRT> object_init_transforms_;
    std::unordered_set<Object3D*>  transform_solved_objects_;
    int                            movie_idx_ = -1;
    bool                           movie_looping_ = false;
    float                          movie_timepos_ = 0.0f;
    std::vector<TransformationSRT> movie_pose_;
};

}}