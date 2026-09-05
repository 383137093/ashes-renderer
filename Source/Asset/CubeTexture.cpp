#include "CubeTexture.h"
#include <cmath>
#include <algorithm>

namespace Ashes {

std::pair<CubeTexture::FaceType, Vector2f> CubeTexture::SelectFace(
    const Vector3f& dir)
{
    auto MakeUV = [](float a, float b, float k) {
        float u = (a / k + 1.0f) * 0.5f;
        float v = (b / k + 1.0f) * 0.5f;
        return Vector2f{u, v}; };

    const float x = dir[0];
    const float y = dir[1];
    const float z = dir[2];
    const float absx = std::abs(x);
    const float absy = std::abs(y);
    const float absz = std::abs(z);

    if (absx > absy && absx > absz)
    {
        return x > 0
            ? std::make_pair(FaceType::PX, MakeUV(-z, y, absx))
            : std::make_pair(FaceType::NX, MakeUV( z, y, absx));
    }
    else if (absy > absz)
    {
        return y > 0
            ? std::make_pair(FaceType::PY, MakeUV(x, -z, absy))
            : std::make_pair(FaceType::NY, MakeUV(x,  z, absy));
    }
    else
    {
        return z > 0
            ? std::make_pair(FaceType::PZ, MakeUV( x, y, absz))
            : std::make_pair(FaceType::NZ, MakeUV(-x, y, absz));
    }
}

CubeTexture::CubeTexture()
{
}

CubeTexture::~CubeTexture()
{
}

void CubeTexture::CreateUninitialized(TexelFormat format, int rows, int cols)
{
    source_path_.clear();
    for (Texture& face : faces_)
        face.CreateUninitialized(format, rows, cols);
}

bool CubeTexture::CreateFromCrossFile(const std::string& filename_template)
{
    constexpr int kFaceCells[] = {6, 4, 1, 9, 5, 7};
    return CreateFromFlattenedFile(filename_template, 3, 4, kFaceCells);
}

bool CubeTexture::CreateFromHorizontalStripFile(const std::string& filename_template)
{
    constexpr int kFaceCells[] = {0, 1, 2, 3, 4, 5};
    return CreateFromFlattenedFile(filename_template, 1, 6, kFaceCells);
}

bool CubeTexture::CreateFromVerticalStripFile(const std::string& filename_template)
{
    constexpr int kFaceCells[] = {0, 1, 2, 3, 4, 5};
    return CreateFromFlattenedFile(filename_template, 6, 1, kFaceCells);
}

bool CubeTexture::CreateFromFaceFiles(const std::string& filename_template)
{
    CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
    source_path_ = filename_template;
    const std::size_t face_pos = filename_template.find("{face}");
    if (face_pos != std::string::npos)
    {
        const char* kFaceNames[6] = {"px", "nx", "py", "ny", "pz", "nz"};
        std::string filename;
        for (int i = 0; i < 6; ++i)
        {
            filename = filename_template;
            filename.replace(face_pos, 6, kFaceNames[i]);
            faces_[i].CreateFromFile(filename);
        }
    }
    return !IsEmpty();
}

const std::string& CubeTexture::SourcePath() const
{
    return source_path_;
}

bool CubeTexture::IsEmpty() const
{
    return std::all_of(faces_.begin(), faces_.end(), [](
        const Texture& tex) { return tex.IsEmpty(); });
}

const Texture& CubeTexture::Face(FaceType type) const
{
    return faces_[static_cast<int>(type)];
}

Texture& CubeTexture::Face(FaceType type)
{
    return faces_[static_cast<int>(type)];
}

bool CubeTexture::CreateFromFlattenedFile(
    const std::string& filename_template,
    int cell_rows, int cell_cols,
    const int(&face_cells)[6])
{
    CreateUninitialized(TexelFormat::FLOAT1, 0, 0);
    source_path_ = filename_template;
    Texture flattened_tex;
    if (!flattened_tex.CreateFromFile(filename_template))
        return false;

    const auto build_sub_image = [](
        TextureMipMap& dest, const TextureMipMap& src,
        int start_row, int start_col, int rows, int cols)
    {
        const std::size_t texel_size = TexelSizeInBytes(src.format);
        dest.format = src.format;
        dest.rows = rows;
        dest.cols = cols;
        dest.texels.resize(rows * cols * texel_size);
        const std::size_t src_stride = src.cols * texel_size;
        const std::size_t dest_stride = dest.cols * texel_size;
        const std::uint8_t* src_data = src.texels.data() +
            start_row * src_stride + start_col * texel_size;
        std::uint8_t* dest_data = dest.texels.data();
        for (; rows--; src_data += src_stride, dest_data += dest_stride)
            std::copy_n(src_data, dest_stride, dest_data);
    };

    for (int i = 0; i < 6; ++i)
    {
        Texture& face = faces_[i];
        face.GenerateMipMapsUninitialized(flattened_tex.MaxLod());
        for (int lod = 0; lod <= flattened_tex.MaxLod(); ++lod)
        {
            const TextureMipMap& flattened_image = flattened_tex.MipMap(lod);
            TextureMipMap& face_image = face.MipMap(lod);
            int face_image_rows = flattened_image.rows / cell_rows;
            int face_image_cols = flattened_image.cols / cell_cols;
            build_sub_image(face_image, flattened_image,
                (face_cells[i] / cell_cols) * face_image_rows,
                (face_cells[i] % cell_cols) * face_image_cols,
                face_image_rows, face_image_cols);
        }
    }

    return !IsEmpty();
}

}