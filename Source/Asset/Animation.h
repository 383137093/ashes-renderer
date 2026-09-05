#pragma once

#include <string>
#include <vector>
#include "Math/Transform.h"
#include "Math/ValueRange.h"

namespace Ashes {

//==============================================================================
// AnimationChannel
// a set of keyframes of a single transformation track.
//==============================================================================

struct AnimationChannel
{
    bool IsEmpty() const;
    int NumKeyframes() const;
    FloatRange TimeRange() const;
    void Sample(int keyframe, TransformationSRT& srt) const;
    void Sample(float timepos, TransformationSRT& srt) const;
    void Reset();
    
    void CombineChannelSRT(
        const AnimationChannel& scale_channel,
        const AnimationChannel& rotation_channel,
        const AnimationChannel& translation_channel);
    
    std::vector<float>      timestamps;
    std::vector<Vector3f>   scales;
    std::vector<Quaternion> rotations;
    std::vector<Vector3f>   translations;
};


//==============================================================================
// Animation
// a set of animation channels corresponding to animation target entities.
//==============================================================================

struct Animation
{
    bool IsEmpty() const;
    int NumNonEmptyChannels() const;
    FloatRange TimeRange() const;
    float NormalizeTimePosition(float timepos, float looping) const;
    void Sample(float timepos, std::vector<TransformationSRT>& srts) const;
    void Reset(int num_channels);

    void CombineAnimationSRT(
        const Animation& scale_anim,
        const Animation& rotation_anim,
        const Animation& translation_anim);

    std::string                   name;
    std::vector<AnimationChannel> channels;
};

}