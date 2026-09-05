#pragma once

#include <filesystem>
#include "Asset/AssetContent.h"

namespace Ashes {

//==============================================================================
// LoadAssetFromGltf
// 
// << glTF 2.0 Specification >>
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.pdf
// 
// << glTF >>
// https://github.com/KhronosGroup/glTF
// 
// << glTF Tutorials >>
// https://github.com/KhronosGroup/glTF-Tutorials
// 
// << glTF Extensions >>
// https://github.com/KhronosGroup/glTF/tree/main/extensions
// 
// << glTF Viewer >>
// https://gltf-viewer.donmccurdy.com
//==============================================================================

bool LoadAssetFromGltf(const std::filesystem::path& filename, AssetContent& assets);

}