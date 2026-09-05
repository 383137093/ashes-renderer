#pragma once

#include <vector>
#include <algorithm>
#include "Asset/Animation.h"

namespace Ashes {

struct Movie
{
    int NumAnimationOfRoots() const
    {
        return root_animation.NumNonEmptyChannels();
    }

    int NumAnimationOfBodys() const
    {
        return static_cast<int>(std::count_if(
            body_animations.begin(), body_animations.end(),
            [](int anim_idx) { return anim_idx >= 0; }));
    }

    bool IsEmpty() const
    {
        return NumAnimationOfRoots() <= 0 && NumAnimationOfBodys() <= 0;
    }

    Animation        root_animation;   // root animation of each object
    std::vector<int> body_animations;  // skeletal animation index of each object
};

}