#pragma once

#include <filesystem>
#include "Asset/AssetContent.h"

namespace Ashes {

//==============================================================================
// LoadAssetFromObj
// 
// << Object Files (.obj) >>
// https://paulbourke.net/dataformats/obj/
// 
// << MTL material format (Lightwave, OBJ) >>
// https://paulbourke.net/dataformats/mtl/
//==============================================================================

bool LoadAssetFromObj(const std::filesystem::path& filename, AssetContent& assets);

}
