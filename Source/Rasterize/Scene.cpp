#include "Scene.h"
#include <algorithm>
#include "Utility/UtilityMisc.h"

namespace Ashes { namespace Rasterize {

Scene::Scene()
{
}

Scene::~Scene()
{
}

//==============================================================================
// Build Scene
//==============================================================================

void Scene::BuildScene(const AssetContent& assets)
{
    CleanScene();
    assets_ = assets;
    BuildAmbientLight();
    BuildBounds();
    BuildObjectInitTransforms();
}

void Scene::CleanScene()
{
    StopMovie();
    ambient_light_ = Vector3f::Zero();
    bounds_ = Geom::AxisAlignedBox::Null();
    floor_.reset();
    object_init_transforms_.clear();
    transform_solved_objects_.clear();
    assets_.Reset();
}

void Scene::BuildObjectInitTransforms()
{
    object_init_transforms_.resize(assets_.objects.size());
    for (std::size_t i = 0; i < assets_.objects.size(); ++i)
        object_init_transforms_[i] = assets_.objects[i]->RelativeTransform();
}

//==============================================================================
// Access Scene Entities
//==============================================================================

const VecSharedObject3D& Scene::Objects() const
{
    return assets_.objects;
}

const std::vector<Camera>& Scene::Cameras() const
{
    return assets_.cameras;
}

const Vector3f& Scene::AmbientLightColor() const
{
    return ambient_light_;
}

const VecSharedLight& Scene::Lights() const
{
    return assets_.lights;
}

Geom::AxisAlignedBox Scene::Bounds() const
{
    return IsFloorVisible() ? bounds_.Union(floor_->WorldBounds()) : bounds_;
}

bool Scene::IsFloorVisible() const
{
    const VecSharedObject3D& objs = assets_.objects;
    return floor_ != nullptr
        && std::find(objs.begin(), objs.end(), floor_) != objs.end();
}

//==============================================================================
// Modify Scene Entities
//==============================================================================

void Scene::SetAmbientLightColor(const Vector3f& color)
{
    ambient_light_ = color;
}

void Scene::AddDirectionalLight(const Vector3f& dir, const Vector3f& color)
{
    auto light = std::make_shared<DirectionalLight>();
    light->direction = dir.Normalized();
    light->color = color;
    assets_.lights.push_back(light);
}

void Scene::AddPointLight(const Vector3f& pos, const Vector3f& color)
{
    auto light = std::make_shared<PointLight>();
    light->position = pos;
    light->color = color;
    assets_.lights.push_back(light);
}

void Scene::SetFloorVisibility(bool visible)
{
    if (visible)
    {
        if (!IsFloorVisible())
        {
            BuildFloor();
            assets_.objects.push_back(floor_);
        }
    }
    else
    {
        if (IsFloorVisible())
        {
            VecSharedObject3D& objs = assets_.objects;
            objs.erase(std::find(objs.begin(), objs.end(), floor_));
        }
    }
}

void Scene::TickScene(float delta)
{
    TickMovie(delta);
    TickAnimation(delta);
    SolveObjectsTransform();
    BuildBounds();
}

//==============================================================================
// Scene Movie
//==============================================================================

const std::vector<Movie>& Scene::Movies() const
{
    return assets_.movies;
}

int Scene::CurrentPlayingMovieIndex() const
{
    return movie_idx_;
}

const Movie* Scene::CurrentPlayingMovie() const
{
    return movie_idx_ >= 0 ? &assets_.movies[movie_idx_] : nullptr;
}

void Scene::PlayMovie(int movie_idx, bool looping)
{
    if (IsValidIndex(assets_.movies, movie_idx))
    {
        const Movie& movie = assets_.movies[movie_idx];
        movie_idx_ = movie_idx;
        movie_looping_ = looping;
        movie_timepos_ = movie.root_animation.TimeRange().min;
        movie_pose_.clear();

        for (int i = 0; i < static_cast<int>(movie.body_animations.size()); ++i)
        {
            if (movie.body_animations[i] >= 0)
                PlayAnimation(i, movie.body_animations[i], looping);
        }
    }
}

void Scene::StopMovie()
{
    if (const Movie* movie = CurrentPlayingMovie())
    {
        movie_idx_ = -1;
        movie_looping_ = false;
        movie_timepos_ = 0.0f;
        movie_pose_.clear();
        
        for (int i = 0; i < static_cast<int>(movie->body_animations.size()); ++i)
        {
            if (movie->body_animations[i] == CurrentPlayingAnimationIndex(i))
                StopAnimation(i);
        }

        for (std::size_t i = 0; i < object_init_transforms_.size(); ++i)
        {
            Matrix4f m = object_init_transforms_[i].ToMatrix();
            assets_.objects[i]->SetRelativeTransform(m);
        }
    }
}

void Scene::SetPlayingMovie(int movie_idx, bool looping)
{
    if (movie_idx != CurrentPlayingMovieIndex())
    {
        StopMovie();
        PlayMovie(movie_idx, looping);
    }
}

//==============================================================================
// Object Animation
//==============================================================================

std::vector<int> Scene::AnimatableObjectIndices() const
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(assets_.objects.size()); ++i)
    {
        const std::vector<Animation>* anims = Animations(i);
        if (anims != nullptr && !anims->empty())
            indices.push_back(i);
    }
    return indices;
}

const std::vector<Animation>* Scene::Animations(int obj_idx) const
{
    if (IsValidIndex(assets_.objects, obj_idx))
    {
        if (auto obj = DownCast<SkeletalMeshInstance>(assets_.objects[obj_idx]))
        {
            if (std::shared_ptr<Skeleton> skeleton = obj->SkeletonAsset())
                return &skeleton->animations;
        }
    }
    return nullptr;
}

int Scene::CurrentPlayingAnimationIndex(int obj_idx) const
{
    if (IsValidIndex(assets_.objects, obj_idx))
    {
        if (auto obj = DownCast<SkeletalMeshInstance>(assets_.objects[obj_idx]))
            return obj->CurrentPlayingAnimationIndex();
    }
    return -1;
}

const Animation* Scene::CurrentPlayingAnimation(int obj_idx) const
{
    if (IsValidIndex(assets_.objects, obj_idx))
    {
        if (auto obj = DownCast<SkeletalMeshInstance>(assets_.objects[obj_idx]))
            return obj->CurrentPlayingAnimation();
    }
    return nullptr;
}

void Scene::PlayAnimation(int obj_idx, int anim_idx, bool looping)
{
    if (IsValidIndex(assets_.objects, obj_idx))
    {
        if (auto obj = DownCast<SkeletalMeshInstance>(assets_.objects[obj_idx]))
            obj->PlayAnimation(anim_idx, looping);
    }
}

void Scene::StopAnimation(int obj_idx)
{
    if (IsValidIndex(assets_.objects, obj_idx))
    {
        if (auto obj = DownCast<SkeletalMeshInstance>(assets_.objects[obj_idx]))
            obj->StopAnimation();
    }
}

void Scene::SetPlayingAnimation(int obj_idx, int anim_idx, bool looping)
{
    if (anim_idx != CurrentPlayingAnimationIndex(obj_idx))
    {
        StopAnimation(obj_idx);
        PlayAnimation(obj_idx, anim_idx, looping);
    }
}

//==============================================================================
// Private Implementations
//==============================================================================

void Scene::BuildAmbientLight()
{
    std::size_t new_num_lights = 0;
    ambient_light_ = Vector3f::Zero();

    // merge ambient lights and remove them from assets.
    for (const std::shared_ptr<LightBase>& light : assets_.lights)
    {
        if (auto ambient_light = DownCast<AmbientLight>(light))
            ambient_light_ += ambient_light->color;
        else
            assets_.lights[new_num_lights++] = light;
    }

    assets_.lights.resize(new_num_lights);
}

void Scene::BuildBounds()
{
    bounds_ = Geom::AxisAlignedBox::Null();
    for (const std::shared_ptr<Object3D>& obj : assets_.objects)
    {
        auto mesh_obj = DownCast<MeshInstance>(obj);
        if (mesh_obj != nullptr && mesh_obj != floor_)
            bounds_ = bounds_.Union(mesh_obj->WorldBounds());
    }
}

void Scene::BuildFloor()
{
    if (floor_ == nullptr)
    {
        auto floor_mesh = std::make_shared<Mesh>();
        floor_mesh->SetName("Ashes.FloorMesh");
        MeshMaker::Floor(*floor_mesh);
        floor_ = std::make_shared<MeshInstance>();
        floor_->SetName("Ashes.Floor");
        floor_->SetMeshAsset(floor_mesh);
    }

    float floor_extent = 10.0f * bounds_.extents.Norm();
    Geom::AxisAlignedBox floor_bounds;
    floor_bounds.center = bounds_.center;
    floor_bounds.center.Y() -= bounds_.extents.Y();
    floor_bounds.extents = {floor_extent, 0.0f, floor_extent};
    MeshMaker::FloorInstance(*floor_, floor_bounds);
}

void Scene::TickMovie(float delta)
{
    if (const Movie* movie = CurrentPlayingMovie())
    {
        movie_timepos_ = movie->root_animation.NormalizeTimePosition(
            movie_timepos_ + delta, movie_looping_);
        movie_pose_ = object_init_transforms_;
        movie->root_animation.Sample(movie_timepos_, movie_pose_);
        for (std::size_t i = 0; i < object_init_transforms_.size(); ++i)
            assets_.objects[i]->SetRelativeTransform(movie_pose_[i].ToMatrix());
    }
}

void Scene::TickAnimation(float delta)
{
    for (const std::shared_ptr<Object3D>& obj : assets_.objects)
    {
        if (auto skeletal_obj = DownCast<SkeletalMeshInstance>(obj))
            skeletal_obj->TickAnimation(delta);
    }
}

void Scene::SolveObjectsTransform()
{
    transform_solved_objects_.clear();
    for (const std::shared_ptr<Object3D>& obj : assets_.objects)
        SolveObjectChainTransform(obj.get());
}

void Scene::SolveObjectChainTransform(Object3D* tail_obj)
{
    if (transform_solved_objects_.count(tail_obj) <= 0)
    {
        if (std::shared_ptr<Object3D> prev_obj = tail_obj->AttachParent().lock())
            SolveObjectChainTransform(prev_obj.get());
        tail_obj->SetRelativeTransform(tail_obj->RelativeTransform());
        transform_solved_objects_.insert(tail_obj);
    }
}

}}