#include "Skeleton.h"
#include <algorithm>
#include "Utility/UtilityMisc.h"

namespace Ashes {

//==============================================================================
// Skeleton
//==============================================================================

void Skeleton::Initialize(int num_joints)
{
    parents.assign(num_joints, -1);
    inverse_bind_pose.assign(num_joints, Matrix4f::Identity());
    ref_pose.assign(num_joints, TransformationSRT::Identity());
    animations.clear();
}

int Skeleton::NumJoints() const
{
    return static_cast<int>(parents.size());
}

//==============================================================================
// SkeletalMeshInstance
//==============================================================================

void SkeletalMeshInstance::Initialize(
    std::shared_ptr<Skeleton> skeleton,
    const Matrix4f& skeleton_transform)
{
    skeleton_ = skeleton;
    skeleton_transform_ = skeleton_transform;
    joint_transforms_.clear();
    joint_skin_matrices_.clear();
    StopAnimation();
    EvaluateJoints();
}

std::shared_ptr<Skeleton> SkeletalMeshInstance::SkeletonAsset() const
{
    return skeleton_;
}

int SkeletalMeshInstance::NumJoints() const
{
    return skeleton_ == nullptr ? 0 : skeleton_->NumJoints();
}

const Matrix4f& SkeletalMeshInstance::SkeletonTransform() const
{
    return skeleton_transform_;
}

const Matrix4f* SkeletalMeshInstance::JointTransforms() const
{
    return joint_transforms_.data();
}

const Matrix4f* SkeletalMeshInstance::JointSkinMatrices() const
{
    return joint_skin_matrices_.data();
}

Matrix4f SkeletalMeshInstance::SocketTransform(int socket) const
{
    return (0 <= socket && socket < NumJoints())
        ? joint_transforms_[socket] : Matrix4f::Identity();
}

void SkeletalMeshInstance::EvaluateJoints()
{
    if (skeleton_ != nullptr)
    {
        const std::vector<TransformationSRT>& pose = (anim_pose_.empty() 
            ? skeleton_->ref_pose : anim_pose_);

        joint_transforms_.resize(skeleton_->NumJoints());
        joint_skin_matrices_.resize(skeleton_->NumJoints());
        
        for (int i = 0; i < skeleton_->NumJoints(); ++i)
        {
            const int parent = skeleton_->parents[i];
            const Matrix4f& parent_transform = (parent >= 0 
                ? joint_transforms_[parent] : skeleton_transform_);
            joint_transforms_[i] = parent_transform * pose[i].ToMatrix();
        }

        for (int i = 0; i < skeleton_->NumJoints(); ++i)
        {
            joint_skin_matrices_[i] = joint_transforms_[i] * 
                skeleton_->inverse_bind_pose[i];
        }
    }
}

int SkeletalMeshInstance::CurrentPlayingAnimationIndex() const
{
    return anim_idx_;
}

const Animation* SkeletalMeshInstance::CurrentPlayingAnimation() const
{
    return anim_idx_ >= 0 ? &skeleton_->animations[anim_idx_] : nullptr;
}

void SkeletalMeshInstance::PlayAnimation(int anim_idx, bool looping)
{
    if (skeleton_ != nullptr && IsValidIndex(skeleton_->animations, anim_idx))
    {
        anim_idx_ = anim_idx;
        anim_looping_ = looping;
        anim_timepos_ = skeleton_->animations[anim_idx].TimeRange().min;
        anim_pose_.clear();
    }
}

void SkeletalMeshInstance::StopAnimation()
{
    anim_idx_ = -1;
    anim_looping_ = false;
    anim_timepos_ = 0.0f;
    anim_pose_.clear();
}

void SkeletalMeshInstance::TickAnimation(float delta)
{
    SetAnimationTimePosition(anim_timepos_ + delta);
    EvaluateAnimationPose();
    EvaluateJoints();
}

void SkeletalMeshInstance::SetAnimationTimePosition(float timepos)
{
    if (const Animation* anim = CurrentPlayingAnimation())
    {
        anim_timepos_ = anim->NormalizeTimePosition(timepos, anim_looping_);
    }
}

void SkeletalMeshInstance::EvaluateAnimationPose()
{
    if (skeleton_ != nullptr)
    {
        if (anim_pose_.empty())
        {
            anim_pose_ = skeleton_->ref_pose;
        }

        if (const Animation* anim = CurrentPlayingAnimation())
        {
            anim->Sample(anim_timepos_, anim_pose_);
        }
    }
}

}