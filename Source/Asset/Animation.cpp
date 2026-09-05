#include "Animation.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include "Math/MathMisc.h"

namespace Ashes {

//==============================================================================
// AnimationChannel
//==============================================================================

bool AnimationChannel::IsEmpty() const
{
    return timestamps.empty();
}

int AnimationChannel::NumKeyframes() const
{
    return static_cast<int>(timestamps.size());
}

FloatRange AnimationChannel::TimeRange() const
{
    return timestamps.empty()
        ? FloatRange::EmptyRange()
        : FloatRange{timestamps.front(), timestamps.back()};
}

void AnimationChannel::Sample(int keyframe, TransformationSRT& srt) const
{
    if (0 <= keyframe && keyframe < NumKeyframes())
    {
        if (!scales.empty())
            srt.scale = scales[keyframe];
        if (!rotations.empty())
            srt.rotation = rotations[keyframe];
        if (!translations.empty())
            srt.translation = translations[keyframe];
    }
}

void AnimationChannel::Sample(float timepos, TransformationSRT& srt) const
{
    auto iter = std::lower_bound(timestamps.begin(), timestamps.end(), timepos);
    int keyframe = static_cast<int>(iter - timestamps.begin());

    if (keyframe <= 0)
    {
        Sample(0, srt);
        return;
    }

    if (keyframe >= NumKeyframes())
    {
        Sample(NumKeyframes() - 1, srt);
        return;
    }

    float timestamp0 = timestamps[keyframe - 1];
    float timestamp1 = timestamps[keyframe];
    float percent = (timepos - timestamp0) / (timestamp1 - timestamp0);

    TransformationSRT srt0 = srt;
    TransformationSRT srt1 = srt;
    Sample(keyframe - 1, srt0);
    Sample(keyframe, srt1);

    srt.scale = Math::Lerp(srt0.scale, srt1.scale, percent);
    srt.rotation = Quaternion::Slerp(srt0.rotation, srt1.rotation, percent);
    srt.translation = Math::Lerp(srt0.translation, srt1.translation, percent);
}

void AnimationChannel::Reset()
{
    timestamps.clear();
    scales.clear();
    rotations.clear();
    translations.clear();
}

void AnimationChannel::CombineChannelSRT(
    const AnimationChannel& scale_channel,
    const AnimationChannel& rotation_channel,
    const AnimationChannel& translation_channel)
{
    Reset();

    // merge timestamps of source channels.
    auto IsTimestampApprox = [](float a, float b) { return std::abs(b - a) < 0.001f; };
    const auto& tstamps_s = scale_channel.timestamps;
    const auto& tstamps_r = rotation_channel.timestamps;
    const auto& tstamps_t = translation_channel.timestamps;
    timestamps.resize(tstamps_s.size() + tstamps_r.size() + tstamps_t.size());
    auto end_s = std::copy(tstamps_s.begin(), tstamps_s.end(), timestamps.begin());
    auto end_r = std::copy(tstamps_r.begin(), tstamps_r.end(), end_s);
    auto end_t = std::copy(tstamps_t.begin(), tstamps_t.end(), end_r);
    std::inplace_merge(timestamps.begin(), end_s, end_r);
    std::inplace_merge(timestamps.begin(), end_r, end_t);
    timestamps.erase(std::unique(timestamps.begin(), timestamps.end(),
        IsTimestampApprox), timestamps.end());

    for (const float timestamp : timestamps)
    {
        if (!scale_channel.IsEmpty())
        {
            TransformationSRT srt = TransformationSRT::Identity();
            scale_channel.Sample(timestamp, srt);
            scales.push_back(srt.scale);
        }

        if (!rotation_channel.IsEmpty())
        {
            TransformationSRT srt = TransformationSRT::Identity();
            rotation_channel.Sample(timestamp, srt);
            rotations.push_back(srt.rotation);
        }

        if (!translation_channel.IsEmpty())
        {
            TransformationSRT srt = TransformationSRT::Identity();
            translation_channel.Sample(timestamp, srt);
            translations.push_back(srt.translation);
        }
    }
}

//==============================================================================
// Animation
//==============================================================================

bool Animation::IsEmpty() const
{
    return NumNonEmptyChannels() <= 0;
}

int Animation::NumNonEmptyChannels() const
{
    return static_cast<int>(std::count_if(channels.begin(), channels.end(),
        [](const AnimationChannel& c) { return !c.IsEmpty(); }));
}

FloatRange Animation::TimeRange() const
{
    return std::accumulate(channels.begin(), channels.end(),
        FloatRange::EmptyRange(),
        [](const FloatRange& init, const AnimationChannel& c) {
            return init.Union(c.TimeRange()); });
}

float Animation::NormalizeTimePosition(float timepos, float looping) const
{
    const FloatRange time_range = TimeRange();

    if (looping)
    {
        float time_length = time_range.max - time_range.min;
        float time_delta = std::remainder(timepos - time_range.min, time_length);
        float time_round = (time_delta >= 0.0f ? 0.0f : time_length);
        timepos = time_range.min + time_delta + time_round;
    }
    
    return Math::Clamp(timepos, time_range.min, time_range.max);
}

void Animation::Sample(float timepos, std::vector<TransformationSRT>& srts) const
{
    if (srts.size() >= channels.size())
    {
        for (std::size_t i = 0; i < channels.size(); ++i)
        {
            channels[i].Sample(timepos, srts[i]);
        }
    }
}

void Animation::Reset(int num_channels)
{
    name.clear();
    channels.resize(num_channels);
    std::for_each(channels.begin(), channels.end(),
        [](AnimationChannel& c) { c.Reset(); });
}

void Animation::CombineAnimationSRT(
    const Animation& scale_anim,
    const Animation& rotation_anim,
    const Animation& translation_anim)
{
    if (channels.size() == scale_anim.channels.size() &&
        channels.size() == rotation_anim.channels.size() &&
        channels.size() == translation_anim.channels.size())
    {
        for (std::size_t i = 0; i < channels.size(); ++i)
        {
            channels[i].CombineChannelSRT(
                scale_anim.channels[i],
                rotation_anim.channels[i], 
                translation_anim.channels[i]);
        }
    }
}

}