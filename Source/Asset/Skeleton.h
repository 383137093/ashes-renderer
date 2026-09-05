#pragma once

#include <memory>
#include <string>
#include <vector>
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Asset/Mesh.h"
#include "Asset/Animation.h"

namespace Ashes {

//==============================================================================
// Skeleton
// -parents: joints are sorted by hierarchy, that is, root joint is 0 always and
//  parents[0] == -1, for other joints, parents[i] <= i.
// -inverse_bind_pose: undo transformation that has already been applied to model
//  in its bind pose, convert model skinned vertex to model unskinned vertex.
//==============================================================================

struct Skeleton
{
    void Initialize(int num_joints);
    int NumJoints() const;

    std::vector<int>               parents;
    std::vector<Matrix4f>          inverse_bind_pose;
    std::vector<TransformationSRT> ref_pose;
    std::vector<Animation>         animations;
};


//==============================================================================
// SkeletalMeshInstance
// SkeletonTransform: from skeletal root joint space to object local space.
// JointTransforms: from skeletal joint space to object local space.
// JointSkinMatrices: convert model vertex to object local space skinned vertex.
//==============================================================================

class SkeletalMeshInstance : public MeshInstance
{
public:

    void Initialize(
        std::shared_ptr<Skeleton> skeleton,
        const Matrix4f& skeleton_transform);

    // Skeleton
    std::shared_ptr<Skeleton> SkeletonAsset() const;
    int NumJoints() const;
    const Matrix4f& SkeletonTransform() const;
    const Matrix4f* JointTransforms() const;
    const Matrix4f* JointSkinMatrices() const;
    Matrix4f SocketTransform(int socket) const override;
    void EvaluateJoints();

    // Animation
    int CurrentPlayingAnimationIndex() const;
    const Animation* CurrentPlayingAnimation() const;
    void PlayAnimation(int anim_idx, bool looping);
    void StopAnimation();
    void TickAnimation(float delta);
    void SetAnimationTimePosition(float timepos);
    void EvaluateAnimationPose();

private:
    
    std::shared_ptr<Skeleton>      skeleton_;
    Matrix4f                       skeleton_transform_ = Matrix4f::Identity();
    std::vector<Matrix4f>          joint_transforms_;
    std::vector<Matrix4f>          joint_skin_matrices_;
    int                            anim_idx_ = -1;
    bool                           anim_looping_ = false;
    float                          anim_timepos_ = 0.0f;
    std::vector<TransformationSRT> anim_pose_;
};

} 