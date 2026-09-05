#pragma once

#include <filesystem>
#include "Asset/AssetContent.h"
#include "Rasterize/RenderConfig.h"

namespace Ashes {

//==============================================================================
// LoadExtendAssetFromAshes
// The .ashes file is a custom asset format used by AshesRenderer.
// It extends a scene with custom lights, cameras, and render configuration
// presets.
// 
// "lights": [
//   {
//     "type": "ambient",
//     "color": [1, 1, 1],
//     "intensity": 1
//   },
//   {
//     "type": "directional",
//     "color": [1, 1, 1],
//     "intensity": 1,
//     "direction": [1, -1, -1]
//   }
// ],
// "cameras": [
//   {
//     "position": [0, 5, 10],
//     "look": [0, -0.447, -0.894]
//   }
// ],
// "config": {
//   "ibl_directory": "./Resources/Envirs/je_gray",
//   "ibl_specular_scale": 2.0,
//   "floor_visibility": true,
//   "camera": 0,
//   "tex_filter": "LINEAR",
//   "shadow_resolution_scale": 1,
//   "shadow_filter": "Point",
//   "background_color": [0.5, 0.5, 0.5],
//   "ecsaa": true,
//   "msaa": true,
//   "ssaa": false,
//   "backface_culling": true,
//   "oit": true,
//   "aces": true
// }
//==============================================================================

bool LoadExtendAssetFromAshes(const std::filesystem::path& filename,
    AssetContent& assets, Rasterize::RenderConfig& config);

}
