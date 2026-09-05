#pragma once

#include <array>
#include <string>
#include <cstdint>
#include <utility>
#include "Math/Vector.h"
#include "Asset/Texture.h"

namespace Ashes {

class CubeTexture
{
public:

    enum class FaceType : std::uint8_t { PX, NX, PY, NY, PZ, NZ };
    static std::pair<FaceType, Vector2f> SelectFace(const Vector3f& dir);

    // Special member functions: uncopyable but moveable
    CubeTexture();
    CubeTexture(const CubeTexture&) = delete;
    CubeTexture(CubeTexture&&) = default;
    ~CubeTexture();
    CubeTexture& operator = (const CubeTexture&) = delete;
    CubeTexture& operator = (CubeTexture&&) = default;

    // filename support face type and mipmap level placeholders {face} {lod}
    void CreateUninitialized(TexelFormat format, int rows, int cols);
    bool CreateFromCrossFile(const std::string& filename_template);
    bool CreateFromHorizontalStripFile(const std::string& filename_template);
    bool CreateFromVerticalStripFile(const std::string& filename_template);
    bool CreateFromFaceFiles(const std::string& filename_template);
    const std::string& SourcePath() const;
    bool IsEmpty() const;
    const Texture& Face(FaceType type) const;
    Texture& Face(FaceType type);

private:
    
    bool CreateFromFlattenedFile(const std::string& filename_template,
        int cell_rows, int cell_cols, const int(&face_cells)[6]);

    std::string            source_path_;
    std::array<Texture, 6> faces_;
};

template <typename T>
inline void SampleCubeTextureLod(
    const CubeTexture& cube_tex, TextureSamplerState sampler,
    const Vector3f& dir, float lod, T& ret)
{
    auto [type, uv] = CubeTexture::SelectFace(dir);
    const Texture& face = cube_tex.Face(type);
    SampleTextureLod(face, sampler, uv, lod, ret);
}

template <typename T>
inline void SampleCubeTexture(
    const CubeTexture& cube_tex, TextureSamplerState sampler,
    const Vector3f& dir, float duv, T& ret)
{
    auto [type, uv] = CubeTexture::SelectFace(dir);
    const Texture& face = cube_tex.Face(type);
    SampleTexture(face, sampler, uv, duv, ret);
}

}