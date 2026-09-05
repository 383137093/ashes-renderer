#include "ObjParser.h"

#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <string_view>

#include "Utility/StringAlgo.h"
#include "Utility/UtilityMisc.h"

namespace Ashes { namespace ObjParser {

static const Texture* FindOrLoadImage(
    const std::string& filename,
    VecSharedTexture& texs)
{
    auto iter = std::find_if(texs.begin(), texs.end(), [&filename](
        const auto& tex) { return tex->SourcePath() == filename; });

    if (iter == texs.end())
    {
        texs.push_back(std::make_shared<Texture>());
        iter = texs.end() - 1;
        (*iter)->CreateFromFile(filename);
        (*iter)->GenerateMipMaps();
    }

    return iter->get();
}

static std::shared_ptr<MaterialPhong> FindOrAddMaterial(
    std::string_view mat_name,
    VecSharedMaterial& mats)
{
    auto iter = std::find_if(mats.begin(), mats.end(),
        [&mat_name](const auto& mat) { return mat->name == mat_name; });

    if (iter == mats.end())
    {
        mats.push_back(std::make_shared<MaterialPhong>());
        iter = mats.end() - 1;
        (*iter)->name = mat_name;
    }
    
    return DownCast<MaterialPhong>(*iter);
}

static void ParseStatement(
    std::string_view line,
    std::string_view& token,
    std::string_view& args)
{
    auto pred = [](int c) { return std::isblank(c) != 0; };
    auto token_first = std::find_if_not(line.begin(), line.end(), pred);
    auto token_last = std::find_if(token_first, line.end(), pred);
    auto args_first = std::find_if_not(token_last, line.end(), pred);
    auto args_last = std::find_if_not(line.rbegin(), line.rend(), pred).base();
    token = line.substr(token_first - line.begin(), token_last - token_first);
    args = line.substr(args_first - line.begin(), args_last - args_first);
}

//==============================================================================
// ParseMaterialLibrary
//==============================================================================

static void ParseMaterialAttribute(
    const std::filesystem::path& asset_dir,
    std::string_view attribute_name,
    std::string_view sattribute,
    MaterialPhong& mat,
    VecSharedTexture& texs)
{
    if (attribute_name == "Ka")
        StringAlgo::Split(sattribute, " ", mat.ambient_factor.coeffs);
    else if (attribute_name == "Kd")
        StringAlgo::Split(sattribute, " ", mat.diffuse_factor.coeffs);
    else if (attribute_name == "d")
        StringAlgo::FromStringView(sattribute, mat.diffuse_factor.W());
    else if (attribute_name == "Ks")
        StringAlgo::Split(sattribute, " ", mat.specular_factor.coeffs);
    else if (attribute_name == "Ns")
        StringAlgo::FromStringView(sattribute, mat.specular_exponent);
    else if (attribute_name == "map_Ka")
        mat.ambient_tex = FindOrLoadImage((asset_dir / sattribute).string(), texs);
    else if (attribute_name == "map_Kd")
        mat.diffuse_tex = FindOrLoadImage((asset_dir / sattribute).string(), texs);
    else if (attribute_name == "map_Ks")
        mat.specular_tex = FindOrLoadImage((asset_dir / sattribute).string(), texs);
    else if (attribute_name == "bump")
        mat.normal_tex = FindOrLoadImage((asset_dir / sattribute).string(), texs);
    else if (attribute_name == "disp")
        mat.height_tex = FindOrLoadImage((asset_dir / sattribute).string(), texs);
}

static void FinishParseMaterial(std::shared_ptr<MaterialPhong> mat)
{
    if (mat != nullptr)
    {
        mat->alpha_mode = (mat->diffuse_factor.W() < 1.0f
            ? MaterialAlphaMode::Blend
            : MaterialAlphaMode::Opaque);
    }
}

static void ParseMaterialLibrary(
    const std::filesystem::path& asset_dir,
    std::ifstream& ifs,
    VecSharedMaterial& mats,
    VecSharedTexture& texs)
{
    std::shared_ptr<MaterialPhong> mat;
    
    for (std::string line; std::getline(ifs, line);)
    {
        std::string_view token;
        std::string_view args;
        ParseStatement(line, token, args);
        
        if (token == "newmtl")
        {
            FinishParseMaterial(mat);
            mat = FindOrAddMaterial(args, mats);
        }
        else if (mat != nullptr)
        {
            ParseMaterialAttribute(asset_dir, token, args, *mat, texs);
        }
    }

    FinishParseMaterial(mat);
}

static void ParseMaterialLibrary(
    const std::filesystem::path& asset_dir,
    std::string_view mtl_name,
    AssetContent& assets)
{
    if (std::ifstream ifs(asset_dir / mtl_name); ifs.is_open())
    {
        ParseMaterialLibrary(asset_dir, ifs, assets.materials, assets.textures);
    }
}

//==============================================================================
// ParseFaceElementGroup
//==============================================================================

constexpr std::uint32_t kInvalidReferenceNumber = UINT32_MAX;
constexpr std::size_t kMaxNumVerticesPerFace = 4;
constexpr std::size_t kMaxNumAttributesPerVertex = 3;

struct ObjVertexData                     // vertex data provides coordinates
{
    std::vector<Vector3f> positions;     // geometric vertices:  v x y z w
    std::vector<Vector3f> normals;       // vertex normals:      vn i j k
    std::vector<Vector2f> texcoords;     // texture vertices:    vt u v w
    std::vector<Vector3f> face_normals;  // computed from face vertices
};

struct ObjVertexReference
{
    std::uint32_t position = kInvalidReferenceNumber;
    std::uint32_t normal = kInvalidReferenceNumber;
    std::uint32_t texcoord = kInvalidReferenceNumber;
    std::uint32_t face_normal = kInvalidReferenceNumber;
};

struct ObjFaceElementGroup
{
    std::string                     name;
    std::vector<ObjVertexReference> triangle_list;
    std::string                     material_name;
};

static bool operator == (ObjVertexReference lhs, ObjVertexReference rhs)
{
    auto* ilhs = reinterpret_cast<const std::uint64_t*>(&lhs);
    auto* irhs = reinterpret_cast<const std::uint64_t*>(&rhs);
    return ilhs[0] == irhs[0] && ilhs[1] == irhs[1];
}

static bool operator < (ObjVertexReference lhs, ObjVertexReference rhs)
{
    auto* ilhs = reinterpret_cast<const std::uint64_t*>(&lhs);
    auto* irhs = reinterpret_cast<const std::uint64_t*>(&rhs);
    return ilhs[0] == irhs[0] ? ilhs[1] < irhs[1] : ilhs[0] < irhs[0];
}

template <typename T,  typename U>
static std::uint32_t ParseReferenceNumber(
    const T& srefs, std::size_t sref_idx, const U& elems)
{
    if (sref_idx < std::size(srefs))
    {
        if (int ref; StringAlgo::FromStringView(srefs[sref_idx], ref))
        {
            ref = (ref < 0 ? ref + static_cast<int>(std::size(elems)) : ref - 1);
            if (IsValidIndex(elems, ref))
                return static_cast<std::uint32_t>(ref);
        }
    }
    return kInvalidReferenceNumber;
}

static void ParseFaceElement(
    std::string_view sface, ObjVertexData& vertex_data,
    std::vector<ObjVertexReference>& triangle_list)
{
    std::string_view svertices[kMaxNumVerticesPerFace + 1];
    std::size_t num_vertices = StringAlgo::Split(sface, " ", svertices);

    if (3 <= num_vertices && num_vertices <= kMaxNumVerticesPerFace)
    {
        ObjVertexReference refs[kMaxNumVerticesPerFace];
        bool require_face_normal = false;

        for (std::size_t i = 0; i < num_vertices; ++i)
        {
            std::string_view srefs[kMaxNumAttributesPerVertex];
            StringAlgo::Split(svertices[i], "/", srefs);
            refs[i].position = ParseReferenceNumber(srefs, 0, vertex_data.positions);
            refs[i].texcoord = ParseReferenceNumber(srefs, 1, vertex_data.texcoords);
            refs[i].normal = ParseReferenceNumber(srefs, 2, vertex_data.normals);
            require_face_normal |= (refs[i].normal == kInvalidReferenceNumber);
        }

        if (require_face_normal)
        {
            const Vector3f& p0 = vertex_data.positions[refs[0].position];
            const Vector3f& p1 = vertex_data.positions[refs[1].position];
            const Vector3f& p2 = vertex_data.positions[refs[2].position];
            const Vector3f face_normal = (p1 - p0).Cross(p2 - p0).Normalized();
            const std::size_t ref = vertex_data.face_normals.size();
            vertex_data.face_normals.push_back(face_normal);

            for (std::size_t i = 0; i < num_vertices; ++i)
            {
                if (refs[i].normal == kInvalidReferenceNumber)
                    refs[i].face_normal = static_cast<std::uint32_t>(ref);
            }
        }

        for (std::size_t i = 0; i + 2 < num_vertices; ++i)
        {
            triangle_list.push_back(refs[0]);
            triangle_list.push_back(refs[i + 1]);
            triangle_list.push_back(refs[i + 2]);
        }
    }
}

static void ConvertVertexReference(
    const ObjVertexData& vertex_data,
    const ObjVertexReference& ref,
    BasicVertex& vertex)
{
    if (ref.position != kInvalidReferenceNumber)
        vertex.position = vertex_data.positions[ref.position];
    if (ref.normal != kInvalidReferenceNumber)
        vertex.normal = vertex_data.normals[ref.normal];
    if (ref.texcoord != kInvalidReferenceNumber)
        vertex.texcoord = vertex_data.texcoords[ref.texcoord];
    if (ref.face_normal != kInvalidReferenceNumber)
        vertex.normal = vertex_data.face_normals[ref.face_normal];
}

static void ConvertFaceElementGroup(
    const ObjVertexData& vertex_data,
    const ObjFaceElementGroup& face_elems, Mesh& mesh)
{
    const std::vector<ObjVertexReference>& tris = face_elems.triangle_list;
    std::vector<ObjVertexReference> refs = face_elems.triangle_list;
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());

    mesh.SetName(face_elems.name);
    mesh.SetVertexType<BasicVertex>();
    mesh.ResizeVertexBuffer(refs.size());
    mesh.ResizeIndexBuffer(tris.size());

    for (std::size_t i = 0; i < refs.size(); ++i)
    {
        BasicVertex& vertex = static_cast<BasicVertex*>(mesh.VertexBuffer())[i];
        ConvertVertexReference(vertex_data, refs[i], vertex);
    }

    for (std::size_t i = 0; i < tris.size(); ++i)
    {
        auto iter = std::lower_bound(refs.begin(), refs.end(), tris[i]);
        mesh.IndexBuffer()[i] = static_cast<std::uint16_t>(iter - refs.begin());
    }

    mesh.RebuildBounds();
}

static void FinishParseFaceElementGroup(
    const ObjVertexData& vertex_data,
    ObjFaceElementGroup& face_elems,
    AssetContent& assets)
{
    if (!face_elems.triangle_list.empty())
    {
        auto mat = (face_elems.material_name.empty() ? nullptr :
            FindOrAddMaterial(face_elems.material_name, assets.materials));

        auto mesh = std::make_shared<Mesh>();
        ConvertFaceElementGroup(vertex_data, face_elems, *mesh);
        face_elems.triangle_list.clear();
        mesh->SetMaterialAsset(mat);
        assets.meshes.push_back(mesh);

        auto obj = std::make_shared<MeshInstance>();
        obj->SetMeshAsset(mesh);
        obj->SetWorldTransform(Matrix4f::Identity());
        obj->RebuildWorldBounds();
        assets.objects.push_back(obj);
    }
}

//==============================================================================
// LoadAssetFromObj
//==============================================================================

static void LoadAssetFromObj(
    const std::filesystem::path& asset_dir,
    std::ifstream& ifs,
    AssetContent& assets)
{
    ObjVertexData       vertex_data;
    ObjFaceElementGroup face_elems;

    for (std::string line; std::getline(ifs, line);)
    {
        std::string_view token;
        std::string_view args;
        ParseStatement(line, token, args);

        if (token == "o" || token == "g")
        {
            FinishParseFaceElementGroup(vertex_data, face_elems, assets);
            face_elems.name = args;
        }
        else if (token == "v")
        {
            Vector3f position = Vector3f::Zero();
            StringAlgo::Split(args, " ", position.coeffs);
            vertex_data.positions.push_back(position);
        }
        else if (token == "vt")
        {
            Vector2f texcoord = Vector2f::Zero();
            StringAlgo::Split(args, " ", texcoord.coeffs);
            vertex_data.texcoords.push_back(texcoord);
        }
        else if (token == "vn")
        {
            Vector3f normal = Vector3f::Zero();
            StringAlgo::Split(args, " ", normal.coeffs);
            vertex_data.normals.push_back(normal);
        }
        else if (token == "f")
        {
            ParseFaceElement(args, vertex_data, face_elems.triangle_list);
        }
        else if (token == "usemtl")
        {
            FinishParseFaceElementGroup(vertex_data, face_elems, assets);
            face_elems.material_name = args;
        }
        else if (token == "mtllib")
        {
            ParseMaterialLibrary(asset_dir, args, assets);
        }
    }

    FinishParseFaceElementGroup(vertex_data, face_elems, assets);
}

}

bool LoadAssetFromObj(const std::filesystem::path& filename, AssetContent& assets)
{
    if (std::ifstream ifs(filename); ifs.is_open())
    {
        assets.Reset();
        ObjParser::LoadAssetFromObj(filename.parent_path(), ifs, assets);
        return true;
    }
    return false;
}

}