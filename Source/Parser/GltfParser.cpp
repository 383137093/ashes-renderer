#include "GltfParser.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include "Math/MathMisc.h"
#include "Math/ColorMisc.h"
#include "Math/Transform.h"
#include "Math/Quaternion.h"
#include "Parser/Base64.h"
#include "Parser/JsonProxy.h"
#include "Utility/UtilityMisc.h"

namespace Ashes { namespace GltfParser {

using namespace std::string_view_literals;
using namespace TransformationMatrix;

template <typename T>
static inline T* StepPointer(T* p, std::size_t stride)
{
    return reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(p) + stride);
}

template <typename T>
static inline const T* StepPointer(const T* p, std::size_t stride)
{
    return StepPointer(const_cast<T*>(p), stride);
}

//==============================================================================
// BinaryDataSource
//==============================================================================

static bool ParseBinaryDataURI(
    std::string_view uri,
    std::string_view& undecoded_data,
    bool& base64_encoded)
{
    char media_type[128] = {0};
    char encode_type[128] = {0};
    int num_scanned_items = std::sscanf(uri.data(),
       "data:%[^,;];%[^,]", media_type, encode_type);

    if (num_scanned_items > 0)
    {
        std::size_t offset = (5 + std::strlen(media_type) + 1) + 
            (num_scanned_items > 1 ? std::strlen(encode_type) + 1 : 0);
        undecoded_data = {uri.data() + offset, uri.size() - offset};
        base64_encoded = (std::strcmp(encode_type, "base64") == 0);
        return true;
    }

    return false;
}

static bool ParseBinaryDataSource(
    const std::filesystem::path& asset_dir,
    const std::string& data_src,
    std::size_t data_size_desired,
    std::string& data)
{
    auto ValidateDataSize = [data_size_desired](std::size_t data_size) {
        return data_size_desired == 0 || data_size == data_size_desired; };

    std::string_view uri_undecoded_data;
    bool uri_base64_encoded = false;

    if (ParseBinaryDataURI(data_src, uri_undecoded_data, uri_base64_encoded))
    {
        if (uri_base64_encoded)
        {
            Base64Decode(uri_undecoded_data.data(), uri_undecoded_data.size(), data);
            bool data_size_valid = ValidateDataSize(data.size());
            assert(data_size_valid);
            if (!data_size_valid) { data.clear(); }
            return data_size_valid;
        }
        else
        {
            std::size_t data_size = uri_undecoded_data.size();
            bool data_size_valid = ValidateDataSize(data_size);
            assert(data_size_valid);
            data.assign(uri_undecoded_data.data(), (data_size_valid ? data_size : 0));
            return data_size_valid;
        }
    }
    else
    {
        std::ifstream ifs(asset_dir / data_src, std::ios::binary | std::ios::ate);
        std::streampos ifs_end = ifs.tellg();
        std::streampos ifs_begin = ifs.seekg(0, std::ios::beg).tellg();
        std::size_t data_size = ifs_end - ifs_begin;
        bool data_size_valid = ifs && ValidateDataSize(data_size);
        assert(data_size_valid);
        data.resize(data_size_valid ? data_size : 0);
        ifs.read(data.data(), data.size());
        return data_size_valid;
    }
}

//==============================================================================
// Buffer & BufferView & Accessor
//==============================================================================

constexpr int kGltfComponentTypeByte          = 5120;
constexpr int kGltfComponentTypeUnsignedByte  = 5121;
constexpr int kGltfComponentTypeShort         = 5122;
constexpr int kGltfComponentTypeUnsignedShort = 5123;
constexpr int kGltfComponentTypeInt           = 5124;
constexpr int kGltfComponentTypeUnsignedInt   = 5125;
constexpr int kGltfComponentTypeFloat         = 5126;

constexpr int kGltfElementTypeNone   = 0;
constexpr int kGltfElementTypeScalar = 1;
constexpr int kGltfElementTypeVec2   = 2;
constexpr int kGltfElementTypeVec3   = 3;
constexpr int kGltfElementTypeVec4   = 4;
constexpr int kGltfElementTypeMat2   = 5;
constexpr int kGltfElementTypeMat3   = 6;
constexpr int kGltfElementTypeMat4   = 7;

struct GltfBuffer
{
    std::string name;  // The user-defined name of this object.
    std::string uri;   // The URI (or IRI) of the buffer.
    std::string data;  // The binary data of the buffer.
};

struct GltfBufferView
{
    std::string name;      // The user-defined name of this object.
    int buffer = -1;       // The index of the buffer.
    int byte_offset = 0;   // The offset into the buffer in bytes.
    int byte_length = -1;  // The length of the bufferView in bytes.
    int byte_stride = 0;   // The stride in bytes.
};

struct GltfAccessor
{
    std::string name;         // The user-defined name of this object.
    int buffer_view = -1;     // The index of the bufferView.
    int byte_offset = 0;      // The offset into the bufferView in bytes.
    int component_type = -1;  // The datatype of the accessor's components.
    int count = -1;           // The number of elements of the accessor.
    int type = -1;            // The datatype of the accessor's elements.
};

static int NumComponentsOfElementType(int elem_type)
{
    switch (elem_type)
    {
        case kGltfElementTypeScalar: return 1;
        case kGltfElementTypeVec2:   return 2;
        case kGltfElementTypeVec3:   return 3;
        case kGltfElementTypeVec4:   return 4;
        case kGltfElementTypeMat2:   return 4;
        case kGltfElementTypeMat3:   return 9;
        case kGltfElementTypeMat4:   return 16;
        default: return 0;
    }
}

static int ParseElementType(std::string_view sv)
{
    if (sv == "SCALAR") return kGltfElementTypeScalar;
    if (sv == "VEC2")   return kGltfElementTypeVec2;
    if (sv == "VEC3")   return kGltfElementTypeVec3;
    if (sv == "VEC4")   return kGltfElementTypeVec4;
    if (sv == "MAT2")   return kGltfElementTypeMat2;
    if (sv == "MAT3")   return kGltfElementTypeMat3;
    if (sv == "MAT4")   return kGltfElementTypeMat4;
    return kGltfElementTypeNone;
}

//==============================================================================
// BinaryDataStorage
//==============================================================================

class GltfBinaryDataStorage
{
public:

    std::vector<GltfBuffer>     buffers;
    std::vector<GltfBufferView> buffer_views;
    std::vector<GltfAccessor>   accessors;

    // return the number of elements referenced by the accessor.
    int CountAccessor(int accessor) const
    {
        return IsValidIndex(accessors, accessor)
            ? std::max(accessors[accessor].count, 0) : 0;
    }

    // read array of scalar from buffer via accessor.
    template <typename T, typename = std::enable_if_t<std::is_scalar_v<T>>>
    void ReadAccessor(int accessor, T* dest_elems,
        std::size_t dest_stride = 0) const
    {
        ReadAccessorImpl<T>(accessor, 1, dest_elems, dest_stride);
    }

    // read array of Quaternion from buffer via accessor.
    void ReadAccessor(int accessor, Quaternion* dest_elems,
        std::size_t dest_stride = 0) const
    {
        ReadAccessorImpl<float>(accessor, 4, dest_elems, dest_stride);
    }

    // read array of Vector<T, N> from buffer via accessor.
    template <typename T, std::size_t N>
    void ReadAccessor(int accessor, Vector<T, N>* dest_elems,
        std::size_t dest_stride = 0) const
    {
        ReadAccessorImpl<T>(accessor, N, dest_elems, dest_stride);
    }

    // read array of Matrix<T, Rows, Cols> from buffer via accessor.
    template <typename T, std::size_t Rows, std::size_t Cols>
    void ReadAccessor(int accessor, Matrix<T, Rows, Cols>* dest_elems,
        std::size_t dest_stride = 0) const
    {
        if (ReadAccessorImpl<T>(accessor, Rows * Cols, dest_elems, dest_stride))
        {
            // NOTE: gltf use column-major matrix while we use row-major matrix.
            for (int i = 0; i < accessors[accessor].count; ++i)
            {
                *dest_elems = dest_elems->Transpose();
                dest_elems = StepPointer(dest_elems, dest_stride);
            }
        }
    }

    // read std::vector<T> from buffer via accessor.
    template <typename T>
    void ReadAccessor(int accessor, std::vector<T>& dest_elems) const
    {
        dest_elems.resize(CountAccessor(accessor));
        ReadAccessor(accessor, dest_elems.data(), sizeof(T));
    }

private:

    // read elements from buffer via accessor, write to data of destination
    // elements, source element and destination element can have different 
    // component type while must have same number of components, that is, read
    // int3 to float3 is ok but read float3 to float4 will cause failure.
    template <typename DestCompType>
    bool ReadAccessorImpl(
        int accessor,
        int num_comps_per_elem,
        void* dest_data,
        std::size_t dest_stride) const;

    // transform source elements to destination elements, a element is a
    // composite of several components. eg, TransformElements<int, float> with
    // num_comps_per_elem=3 means transforming int3 array to float3 array.
    template <typename SrcCompType, typename DestCompType>
    static bool TransformElements(
        std::string_view src_data, std::size_t src_stride,
        void* dest_data, std::size_t dest_stride,
        int num_elems, int num_comps_per_elem);
};

template <typename DestCompType>
bool GltfBinaryDataStorage::ReadAccessorImpl(
    int accessor_idx,
    int num_comps_per_elem,
    void* dest_data,
    std::size_t dest_stride) const
{
    if (!IsValidIndex(accessors, accessor_idx))
    {
        assert(false);
        return false;
    }

    // validate the gltf accessor.
    const GltfAccessor& accessor = accessors[accessor_idx];
    if (!IsValidIndex(buffer_views, accessor.buffer_view) ||
         accessor.byte_offset < 0 ||
         accessor.count < 0 ||
         NumComponentsOfElementType(accessor.type) != num_comps_per_elem)
    {
        assert(false);
        return false;
    }

    // validate the gltf buffer view.
    const GltfBufferView& buffer_view = buffer_views[accessor.buffer_view];
    if (!IsValidIndex(buffers, buffer_view.buffer) ||
         buffer_view.byte_offset < 0 ||
         buffer_view.byte_stride < 0)
    {
        assert(false);
        return false;
    }

    // validate the gltf buffer.
    const GltfBuffer& buffer = buffers[buffer_view.buffer];
    const std::size_t offset = buffer_view.byte_offset + accessor.byte_offset;
    if (buffer.data.size() <= offset)
    {
        assert(false);
        return false;
    }

    const std::string_view src_data = std::string_view(buffer.data).substr(offset);
    const std::size_t src_stride = buffer_view.byte_stride;
    const int num_elems = accessor.count;

    // transform source elements to destination elements by component type.
    switch (accessor.component_type)
    {
        case kGltfComponentTypeByte:
            return TransformElements<std::int8_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeUnsignedByte:
            return TransformElements<std::uint8_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeShort:
            return TransformElements<std::int16_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeUnsignedShort:
            return TransformElements<std::uint16_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeInt:
            return TransformElements<std::int32_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeUnsignedInt:
            return TransformElements<std::uint32_t, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
        case kGltfComponentTypeFloat:
            return TransformElements<float, DestCompType>(src_data,
                src_stride, dest_data, dest_stride, num_elems, num_comps_per_elem);
    }

    assert(false);
    return false;
}

template <typename SrcCompType, typename DestCompType>
bool GltfBinaryDataStorage::TransformElements(
    std::string_view src_data,
    std::size_t src_stride,
    void* dest_data,
    std::size_t dest_stride,
    int num_elems,
    int num_comps_per_elem)
{
    // compute byte size and byte stride.
    const std::size_t src_elem_size = num_comps_per_elem * sizeof(SrcCompType);
    const std::size_t dest_elem_size = num_comps_per_elem * sizeof(DestCompType);
    src_stride = (src_stride <= 0 ? src_elem_size : src_stride);
    dest_stride = (dest_stride <= 0 ? dest_elem_size : dest_stride);

    // validate byte size and byte stride.
    if (src_stride < src_elem_size || dest_stride < dest_elem_size ||
        src_data.size() < num_elems * src_stride)
    {
        assert(false);
        return false;
    }

    auto* src_comps = reinterpret_cast<const SrcCompType*>(src_data.data());
    auto* dest_comps = static_cast<DestCompType*>(dest_data);
 
    if ((std::is_same_v<SrcCompType, DestCompType>) &&
        (src_stride == src_elem_size && dest_stride == dest_elem_size))
    {
        // the type cast is required since below code compiled always.
        std::copy_n(src_comps, num_elems * num_comps_per_elem,
            static_cast<SrcCompType*>(dest_data));
    }
    else
    {
        for (int i = 0; i < num_elems; ++i)
        {
            for (int comp = 0; comp < num_comps_per_elem; ++comp)
                dest_comps[comp] = static_cast<DestCompType>(src_comps[comp]);
            src_comps = StepPointer(src_comps, src_stride);
            dest_comps = StepPointer(dest_comps, dest_stride);
        }
    }

    return true;
}

static void ParseBinaryDataStorage(
    const std::filesystem::path& asset_dir,
    JsonProxy json_doc,
    GltfBinaryDataStorage& gltf_storage)
{
    JsonProxy json_buffers = json_doc.ChildArray("buffers");
    JsonProxy json_buffer_views = json_doc.ChildArray("bufferViews");
    JsonProxy json_accessors = json_doc.ChildArray("accessors");

    gltf_storage.buffers.clear();
    gltf_storage.buffer_views.clear();
    gltf_storage.accessors.clear();
    gltf_storage.buffers.reserve(json_buffers.ArraySize());
    gltf_storage.buffer_views.reserve(json_buffer_views.ArraySize());
    gltf_storage.accessors.reserve(json_accessors.ArraySize());

    for (JsonProxy json_buffer : json_buffers)
    {
        GltfBuffer buffer;
        buffer.name = json_buffer.ChildValue("name", "");
        buffer.uri = json_buffer.ChildValue("uri", "");
        int byte_length = json_buffer.ChildValue("byteLength", -1);
        ParseBinaryDataSource(asset_dir, buffer.uri, byte_length, buffer.data);
        gltf_storage.buffers.push_back(std::move(buffer));
    }

    for (JsonProxy json_buffer_view : json_buffer_views)
    {
        GltfBufferView buffer_view;
        buffer_view.name = json_buffer_view.ChildValue("name", "");
        buffer_view.buffer = json_buffer_view.ChildValue("buffer", -1);
        buffer_view.byte_offset = json_buffer_view.ChildValue("byteOffset", 0);
        buffer_view.byte_length = json_buffer_view.ChildValue("byteLength", -1);
        buffer_view.byte_stride = json_buffer_view.ChildValue("byteStride", 0);
        gltf_storage.buffer_views.push_back(std::move(buffer_view));
    }

    for (JsonProxy json_accessor : json_accessors)
    {
        GltfAccessor accessor;
        accessor.name = json_accessor.ChildValue("name", "");
        accessor.buffer_view = json_accessor.ChildValue("bufferView", -1);
        accessor.byte_offset = json_accessor.ChildValue("byteOffset", 0);
        accessor.component_type = json_accessor.ChildValue("componentType", -1);
        accessor.count = json_accessor.ChildValue("count", -1);
        accessor.type = ParseElementType(json_accessor.ChildValue("type", ""));
        gltf_storage.accessors.push_back(std::move(accessor));
    }
}

//==============================================================================
// NodeSummary
//==============================================================================

struct GltfJointNode
{
    int skin = -1;   // the mesh skin containing this scene node as its joint
    int joint = -1;  // the joint represented by this scene node
};

struct GltfNodeSummary
{
    std::vector<int>                node_parents;
    std::vector<TransformationSRT>  node_transforms;
    std::vector<int>                topological_nodes;
    std::vector<std::vector<int>>   skin_owners;
    std::vector<GltfJointNode>      joint_nodes;
};

static TransformationSRT ParseNodeTransform(JsonProxy json_node)
{
    if (JsonProxy json_matrix = json_node.Child("matrix"); !json_matrix.IsNull())
    {
        // NOTE: gltf use column-major matrix while we use row-major matrix.
        Matrix4f matrix = json_matrix.Value(Matrix4f::Identity());
        matrix = matrix.Transpose();
        return TransformationSRT(matrix);
    }
    else
    {
        Vector3f   s = json_node.ChildValue("scale", Vector3f::Ones());
        Quaternion r = json_node.ChildValue("rotation", Quaternion::Identity());
        Vector3f   t = json_node.ChildValue("translation", Vector3f::Zero());
        return TransformationSRT(s, r, t);
    }
}

static void ParseNodechildren(
    JsonProxy json_children, int node,
    std::vector<int>& node_parents)
{
    for (JsonProxy json_child : json_children)
    {
        int child = json_child.Value(-1);
        if (IsValidIndex(node_parents, child))
        {
            assert(node_parents[child] < 0);
            node_parents[child] = node;
        }
    }
}

static void BuildTopologicalNodes(
    const std::vector<int>& node_parents, int root_node,
    std::vector<int>& topological_nodes)
{
    // build topological ordered node list by breadth-first-search.
    std::size_t cur = topological_nodes.size();
    topological_nodes.push_back(root_node);
    for (; cur < topological_nodes.size(); ++cur)
    {
        for (std::size_t child = 0; child < node_parents.size(); ++child)
        {
            if (node_parents[child] == topological_nodes[cur])
                topological_nodes.push_back(static_cast<int>(child));
        }
    }
}

static void ParseSkinJoints(
    JsonProxy json_joints, int skin,
    std::vector<GltfJointNode>& joint_nodes)
{
    for (int joint = 0; joint < json_joints.ArraySize(); ++joint)
    {
        int node = json_joints.ChildValue(joint, -1);
        if (IsValidIndex(joint_nodes, node))
        {
            GltfJointNode& joint_node = joint_nodes[node];
            joint_node.skin = skin;
            joint_node.joint = joint;
        }
    }
}

static void CreateNodeSummary(
    JsonProxy json_nodes,
    JsonProxy json_skins,
    GltfNodeSummary& gltf_node_summary)
{
    const int num_nodes = json_nodes.ArraySize();
    const int num_skins = json_skins.ArraySize();

    // initialize node summary.
    gltf_node_summary.node_parents.assign(num_nodes, -1);
    gltf_node_summary.node_transforms.assign(num_nodes, TransformationSRT::Identity());
    gltf_node_summary.topological_nodes.clear();
    gltf_node_summary.topological_nodes.reserve(num_nodes);
    gltf_node_summary.skin_owners.assign(num_skins, {});
    gltf_node_summary.joint_nodes.assign(num_nodes, GltfJointNode());

    // summarize parent & transformation & skin of all scene nodes.
    for (int node = 0; node < num_nodes; ++node)
    {
        JsonProxy json_node = json_nodes.ChildObject(node);
        JsonProxy json_children = json_node.ChildArray("children");
        int skin = json_node.ChildValue("skin", -1);
        ParseNodechildren(json_children, node, gltf_node_summary.node_parents);
        gltf_node_summary.node_transforms[node] = ParseNodeTransform(json_node);
        if (IsValidIndex(gltf_node_summary.skin_owners, skin))
            gltf_node_summary.skin_owners[skin].push_back(node);
    }

    // summarize topological order of all scene nodes.
    for (int node = 0; node < num_nodes; ++node)
    {
        if (gltf_node_summary.node_parents[node] < 0)
        {
            BuildTopologicalNodes(gltf_node_summary.node_parents,
                node, gltf_node_summary.topological_nodes);
        }
    }
    
    // summarize all scene nodes which representing a mesh skin joint.
    for (int skin = 0; skin < num_skins; ++skin)
    {
        JsonProxy json_skin = json_skins.ChildObject(skin);
        JsonProxy json_joints = json_skin.ChildArray("joints");
        ParseSkinJoints(json_joints, skin, gltf_node_summary.joint_nodes);
    }
}

//==============================================================================
// Image & Material
//==============================================================================

static void ParseImages(
    const std::filesystem::path& asset_dir,
    JsonProxy json_images,
    VecSharedTexture& texs)
{
    const int num_images = json_images.ArraySize();
    texs.assign(num_images, nullptr);

    for (int i = 0; i < num_images; ++i)
    {
        JsonProxy json_image = json_images.ChildObject(i);
        std::string_view uri = json_image.ChildValue("uri", "");
        int buffer_view = json_image.ChildValue("bufferView", -1);

        // we only support images referenced by relative path.
        if (!uri.empty() && buffer_view == -1)
        {
            std::string_view uri_undecoded_data;
            bool uri_base64_encoded = false;
            if (!ParseBinaryDataURI(uri, uri_undecoded_data, uri_base64_encoded))
            {
                texs[i] = std::make_shared<Texture>();
                texs[i]->CreateFromFile((asset_dir / uri).string());
            }
        }
    }
}

static MaterialAlphaMode ParseMaterialAlphaMode(std::string_view sv)
{
    if (sv == "OPAQUE") return MaterialAlphaMode::Opaque;
    if (sv == "MASK")   return MaterialAlphaMode::Mask;
    if (sv == "BLEND")  return MaterialAlphaMode::Blend;
    return MaterialAlphaMode::Opaque;
}

static Texture* ParseTextureIndex(const VecSharedTexture& texs, JsonProxy json_tex)
{
    int index = json_tex.ChildValue("index", -1);
    return IsValidIndex(texs, index) ? texs[index].get() : nullptr;
}

static void ParseMaterialPbrMetallicRoughness(
    const VecSharedTexture& texs,
    JsonProxy json_mat,
    JsonProxy json_pbr,
    MaterialPbrMetallicRoughness& mat)
{
    JsonProxy json_base_color_tex = json_pbr.ChildObject("baseColorTexture");
    JsonProxy json_metallic_tex = json_pbr.ChildObject("metallicRoughnessTexture");
    JsonProxy json_normal_tex = json_mat.ChildObject("normalTexture");
    JsonProxy json_occlusion_tex = json_mat.ChildObject("occlusionTexture");
    JsonProxy json_emissive_tex = json_mat.ChildObject("emissiveTexture");

    mat.name = json_mat.ChildValue("name", "");
    mat.double_sided = json_mat.ChildValue("doubleSided", false);
    mat.alpha_mode = ParseMaterialAlphaMode(json_mat.ChildValue("alphaMode", ""));
    mat.alpha_cutoff = json_mat.ChildValue("alphaCutoff", 0.5f);
    mat.base_color_factor = json_pbr.ChildValue("baseColorFactor", Vector4f::Ones());
    mat.metallic_factor = json_pbr.ChildValue("metallicFactor", 1.0f);
    mat.roughness_factor = json_pbr.ChildValue("roughnessFactor", 1.0f);
    mat.emissive_factor = json_mat.ChildValue("emissiveFactor", Vector3f::Zero());

    mat.base_color_tex = ParseTextureIndex(texs, json_base_color_tex);
    mat.metallic_roughness_tex = ParseTextureIndex(texs, json_metallic_tex);
    mat.normal_tex = ParseTextureIndex(texs, json_normal_tex);
    mat.occlusion_tex = ParseTextureIndex(texs, json_occlusion_tex);
    mat.emissive_tex = ParseTextureIndex(texs, json_emissive_tex);
}

static void ParseMaterialPbrSpecularGlossiness(
    const VecSharedTexture& texs,
    JsonProxy json_mat,
    JsonProxy json_pbr,
    MaterialPbrSpecularGlossiness& mat)
{
    JsonProxy json_diffuse_tex = json_pbr.ChildObject("diffuseTexture");
    JsonProxy json_specular_tex = json_pbr.ChildObject("specularGlossinessTexture");
    JsonProxy json_normal_tex = json_mat.ChildObject("normalTexture");
    JsonProxy json_occlusion_tex = json_mat.ChildObject("occlusionTexture");
    JsonProxy json_emissive_tex = json_mat.ChildObject("emissiveTexture");

    mat.name = json_mat.ChildValue("name", "");
    mat.double_sided = json_mat.ChildValue("doubleSided", false);
    mat.alpha_mode = ParseMaterialAlphaMode(json_mat.ChildValue("alphaMode", ""));
    mat.alpha_cutoff = json_mat.ChildValue("alphaCutoff", 0.5f);
    mat.diffuse_factor = json_pbr.ChildValue("diffuseFactor", Vector4f::Ones());
    mat.specular_factor = json_pbr.ChildValue("specularFactor", Vector3f::Ones());
    mat.glossiness_factor = json_pbr.ChildValue("glossinessFactor", 1.0f);
    mat.emissive_factor = json_mat.ChildValue("emissiveFactor", Vector3f::Zero());

    mat.diffuse_tex = ParseTextureIndex(texs, json_diffuse_tex);
    mat.specular_glossiness_tex = ParseTextureIndex(texs, json_specular_tex);
    mat.normal_tex = ParseTextureIndex(texs, json_normal_tex);
    mat.occlusion_tex = ParseTextureIndex(texs, json_occlusion_tex);
    mat.emissive_tex = ParseTextureIndex(texs, json_emissive_tex);
}

static void ParseMaterials(
    const VecSharedTexture& texs,
    JsonProxy json_mats,
    VecSharedMaterial& mats)
{
    const int num_mats = json_mats.ArraySize();
    mats.assign(num_mats, nullptr);

    for (int i = 0; i < num_mats; ++i)
    {
        JsonProxy json_mat = json_mats.ChildObject(i);
        JsonProxy json_pbr_mr = json_mat.ChildObject("pbrMetallicRoughness");
        JsonProxy json_pbr_sg = json_mat.ChildObject("extensions").Child(
            "KHR_materials_pbrSpecularGlossiness");

        if (!json_pbr_sg.IsNull())
        {
            auto mat = std::make_shared<MaterialPbrSpecularGlossiness>();
            ParseMaterialPbrSpecularGlossiness(texs, json_mat, json_pbr_sg, *mat);
            mats[i] = mat;
        }
        else
        {
            auto mat = std::make_shared<MaterialPbrMetallicRoughness>();
            ParseMaterialPbrMetallicRoughness(texs, json_mat, json_pbr_mr, *mat);
            mats[i] = mat;
        }
    }
}

//==============================================================================
// Primitive & Mesh & Skin
//==============================================================================

template <typename V, typename U, typename T>
void ParsePrimitiveAttribute(
    const GltfBinaryDataStorage& gltf_storage,
    JsonProxy json_attributes,
    const char* attribute_name,
    T U::* vertex_membptr, Mesh& mesh)
{
    int accessor = json_attributes.ChildValue(attribute_name, -1);
    if (gltf_storage.CountAccessor(accessor) == mesh.NumVertices())
    {
        V* vertices = static_cast<V*>(mesh.VertexBuffer());
        T* attribute = &(vertices->*vertex_membptr);
        gltf_storage.ReadAccessor(accessor, attribute, mesh.VertexSize());
    }
}

template <typename V, typename U>
void ParsePrimitiveTexCoord(
    const GltfBinaryDataStorage& gltf_storage,
    JsonProxy json_attributes,
    const char* attribute_name,
    Vector2f U::* vertex_membptr, Mesh& mesh)
{
    ParsePrimitiveAttribute<V>(gltf_storage, json_attributes,
        attribute_name, vertex_membptr, mesh);

    // NOTE: gltf use ST texture coordinates while we use UV texture coordinates.
    for (int i = 0; i < mesh.NumVertices(); ++i)
    {
        V* vertices = static_cast<V*>(mesh.VertexBuffer());
        Vector2f& texcoord = vertices[i].*vertex_membptr;
        texcoord.Y() = 1.0f - texcoord.Y();
    }
}

static void ParseNonSkinnedPrimitiveAttributes(
    const GltfBinaryDataStorage& gltf_storage,
    JsonProxy json_attributes, Mesh& mesh)
{
    ParsePrimitiveAttribute<BasicVertex>(gltf_storage, json_attributes,
        "POSITION", &BasicVertex::position, mesh);
    ParsePrimitiveAttribute<BasicVertex>(gltf_storage, json_attributes,
        "NORMAL", &BasicVertex::normal, mesh);
    ParsePrimitiveAttribute<BasicVertex>(gltf_storage, json_attributes,
        "TANGENT", &BasicVertex::tangent, mesh);
    ParsePrimitiveTexCoord<BasicVertex>(gltf_storage, json_attributes,
        "TEXCOORD_0", &BasicVertex::texcoord, mesh);
}

static void ParseSkinnedPrimitiveAttributes(
    const GltfBinaryDataStorage& gltf_storage,
    JsonProxy json_attributes, Mesh& mesh)
{
    ParsePrimitiveAttribute<SkinnedVertex>(gltf_storage, json_attributes,
        "POSITION", &SkinnedVertex::position, mesh);
    ParsePrimitiveAttribute<SkinnedVertex>(gltf_storage, json_attributes,
        "NORMAL", &SkinnedVertex::normal, mesh);
    ParsePrimitiveAttribute<SkinnedVertex>(gltf_storage, json_attributes,
        "TANGENT", &SkinnedVertex::tangent, mesh);
    ParsePrimitiveTexCoord<SkinnedVertex>(gltf_storage, json_attributes,
        "TEXCOORD_0", &SkinnedVertex::texcoord, mesh);
    ParsePrimitiveAttribute<SkinnedVertex>(gltf_storage, json_attributes,
        "JOINTS_0", &SkinnedVertex::joints, mesh);
    ParsePrimitiveAttribute<SkinnedVertex>(gltf_storage, json_attributes,
        "WEIGHTS_0", &SkinnedVertex::weights, mesh);
}

static void ParsePrimitive(
    const GltfBinaryDataStorage& gltf_storage,
    const VecSharedMaterial& mats,
    JsonProxy json_primitive, Mesh& mesh)
{
    JsonProxy json_attributes = json_primitive.ChildObject("attributes");
    int position = json_attributes.ChildValue("POSITION", -1);
    int joints = json_attributes.ChildValue("JOINTS_0", -1);

    int mode = json_primitive.ChildValue("mode", 4);
    int indices = json_primitive.ChildValue("indices", -1);
    int mat = json_primitive.ChildValue("material", -1);

    int num_vertices = gltf_storage.CountAccessor(position);
    int num_joints = gltf_storage.CountAccessor(joints);
    int num_indices = gltf_storage.CountAccessor(indices);

    // we only support primitives of triangle list (mode == 4).
    if (num_vertices > 0 && num_indices % 3 == 0 && mode == 4)
    {
        if (num_joints == num_vertices)
        {
            mesh.SetVertexType<SkinnedVertex>();
            mesh.ResizeVertexBuffer(num_vertices);
            ParseSkinnedPrimitiveAttributes(gltf_storage, json_attributes, mesh);
        }
        else
        {
            mesh.SetVertexType<BasicVertex>();
            mesh.ResizeVertexBuffer(num_vertices);
            ParseNonSkinnedPrimitiveAttributes(gltf_storage, json_attributes, mesh);
        }

        mesh.ResizeIndexBuffer(num_indices);
        gltf_storage.ReadAccessor(indices, mesh.IndexBuffer(), 0);
        mesh.SetMaterialAsset(IsValidIndex(mats, mat) ? mats[mat] : nullptr);
        mesh.RebuildBounds();
    }
}

static void ParseMeshes(
    const GltfBinaryDataStorage& gltf_storage,
    const VecSharedMaterial& mats,
    JsonProxy json_meshes, VecSharedMesh& meshes)
{
    const int num_meshes = json_meshes.ArraySize();
    meshes.assign(num_meshes, nullptr);

    for (int i = 0; i < num_meshes; ++i)
    {
        JsonProxy json_mesh = json_meshes.ChildObject(i);
        JsonProxy json_primitives = json_mesh.ChildArray("primitives");
        meshes[i] = std::make_shared<Mesh>();
        meshes[i]->SetName(json_mesh.ChildValue("name", ""));

        // we only support gltf meshes that have only a single primitive.
        if (json_primitives.ArraySize() == 1)
        {
            JsonProxy json_primitive = json_primitives.ChildObject(0);
            ParsePrimitive(gltf_storage, mats, json_primitive, *meshes[i]);
        }
    }
}

static void ParseSkins(
    const GltfBinaryDataStorage& gltf_storage,
    const GltfNodeSummary& gltf_node_summary,
    JsonProxy json_skins,
    VecSharedSkeleton& skeletons)
{
    const int num_skins = json_skins.ArraySize();
    skeletons.assign(num_skins, nullptr);

    for (int i = 0; i < num_skins; ++i)
    {
        JsonProxy json_skin = json_skins.ChildObject(i);
        JsonProxy json_joints = json_skin.ChildArray("joints");
        int num_joints = json_joints.ArraySize();
        int inverse_bind_matrices = json_skin.ChildValue("inverseBindMatrices", -1);

        auto skin = (skeletons[i] = std::make_shared<Skeleton>());
        skin->Initialize(num_joints);
        gltf_storage.ReadAccessor(inverse_bind_matrices, skin->inverse_bind_pose);
        skin->inverse_bind_pose.resize(num_joints, Matrix4f::Identity());

        for (int joint = 0; joint < num_joints; ++joint)
        {
            int node = json_joints.ChildValue(joint, -1);
            if (IsValidIndex(gltf_node_summary.joint_nodes, node))
            {
                int parent_node = gltf_node_summary.node_parents[node];
                int parent_joint = (parent_node < 0 ? -1 :
                    gltf_node_summary.joint_nodes[parent_node].joint);
                skin->parents[joint] = parent_joint;
                skin->ref_pose[joint] = gltf_node_summary.node_transforms[node];
            }
        }
    }
}

//==============================================================================
// Node & Camera & Light
//==============================================================================

static std::shared_ptr<Object3D> ParseNode(
    const VecSharedMesh& meshes,
    const VecSharedSkeleton& skeletons,
    JsonProxy json_node)
{
    const char* name = json_node.ChildValue("name", "");
    int mesh = json_node.ChildValue("mesh", -1);
    int skin = json_node.ChildValue("skin", -1);

    if (IsValidIndex(meshes, mesh))
    {
        if (IsValidIndex(skeletons, skin))
        {
            auto skeletal_obj = std::make_shared<SkeletalMeshInstance>();
            skeletal_obj->Initialize(skeletons[skin], Matrix4f::Identity());
            skeletal_obj->SetName(name);
            skeletal_obj->SetMeshAsset(meshes[mesh]);
            return skeletal_obj;
        }
        else
        {
            auto mesh_obj = std::make_shared<MeshInstance>();
            mesh_obj->SetName(name);
            mesh_obj->SetMeshAsset(meshes[mesh]);
            return mesh_obj;
        }
    }

    auto obj = std::make_shared<Object3D>();
    obj->SetName(name);
    return obj;
}

static void SolveNodeTransform(
    const GltfNodeSummary& gltf_node_summary,
    const VecSharedObject3D& objs, int node)
{
    const int parent = gltf_node_summary.node_parents[node];
    const Matrix m = gltf_node_summary.node_transforms[node].ToMatrix();
    int attach_parent = parent;
    int attach_socket = -1;
  
    // if parent is a scene node representing a joint,
    // redirect the attachment relasionship to its owner.
    if (parent >= 0 && gltf_node_summary.joint_nodes[parent].joint >= 0)
    {        
        int attach_skin = gltf_node_summary.joint_nodes[parent].skin;
        if (!gltf_node_summary.skin_owners[attach_skin].empty())
        {
            attach_parent = gltf_node_summary.skin_owners[attach_skin][0];
            attach_socket = gltf_node_summary.joint_nodes[parent].joint;
        }
    }
    
    if (attach_parent >= 0)
        objs[node]->AttachTo(objs[attach_parent], attach_socket, m);
    else
        objs[node]->SetWorldTransform(m);
}

static void ParseNodes(
    const GltfNodeSummary& gltf_node_summary,
    const VecSharedMesh& meshes,
    const VecSharedSkeleton& skeletons,
    JsonProxy json_nodes,
    VecSharedObject3D& objs)
{
    objs.assign(json_nodes.ArraySize(), nullptr);
    assert(objs.size() == gltf_node_summary.topological_nodes.size());

    for (int node : gltf_node_summary.topological_nodes)
    {
        // skip scene nodes representing a joint.
        if (gltf_node_summary.joint_nodes[node].joint < 0)
        {
            JsonProxy json_node = json_nodes.ChildObject(node);
            objs[node] = ParseNode(meshes, skeletons, json_node);
            SolveNodeTransform(gltf_node_summary, objs, node);
        }
    }
}

static void ParseCameras(
    const VecSharedObject3D& objs,
    JsonProxy json_nodes,
    JsonProxy json_cameras,
    std::vector<Camera>& cameras)
{
    cameras.clear();
    cameras.reserve(json_cameras.ArraySize());

    for (int node = 0; node < json_nodes.ArraySize(); ++node)
    {
        JsonProxy json_node = json_nodes.ChildObject(node);
        int referenced_camera = json_node.ChildValue("camera", -1);

        if (referenced_camera >= 0)
        {
            JsonProxy json_camera = json_cameras.ChildObject(referenced_camera);
            JsonProxy json_perspective = json_camera.ChildObject("perspective");
            float aspect_ratio = json_perspective.ChildValue("aspectRatio", 1.0f);
            float y_fov = json_perspective.ChildValue("yfov", 0.660593f);
            float z_far = json_perspective.ChildValue("zfar", 1000.0f);
            float z_near = json_perspective.ChildValue("znear", 0.001f);

            const Matrix4f& transform = objs[node]->WorldTransform();
            Vector3f pos = TransformPoint(transform, Vector3f::Zero());
            Vector3f look = TransformVector(transform, -Vector3f::UnitZ());
            Vector3f up = TransformVector(transform, Vector3f::UnitY());

            Camera camera;
            camera.SetLens(Math::Degrees(y_fov), aspect_ratio, z_near, z_far);
            camera.LookAlong(pos, look, up);
            cameras.push_back(camera);
        }
    }
}

static void ParseLights(
    const VecSharedObject3D& objs,
    JsonProxy json_nodes,
    JsonProxy json_lights,
    VecSharedLight& lights)
{
    lights.clear();
    lights.reserve(json_lights.ArraySize());

    for (int node = 0; node < json_nodes.ArraySize(); ++node)
    {
        JsonProxy json_node = json_nodes.ChildObject(node);
        int referenced_light = json_node.ChildObject("extensions").ChildObject(
            "KHR_lights_punctual").ChildValue("light", -1);

        if (referenced_light >= 0)
        {
            JsonProxy json_light = json_lights.ChildObject(referenced_light);
            std::string_view type = json_light.ChildValue("type", ""sv);
            Vector3f color = json_light.ChildValue("color", Vector3f::Zero());
            float intensity = json_light.ChildValue("intensity", 1.0f);
            const Matrix4f& transform = objs[node]->WorldTransform();

            if (type == "directional")
            {
                auto light = std::make_shared<DirectionalLight>();
                light->color = intensity * color;
                light->direction = TransformDirection(transform, -Vector3f::UnitZ());
                lights.push_back(light);
            }
            else if (type == "point")
            {
                auto light = std::make_shared<PointLight>();
                light->color = intensity * color;
                light->position = TransformPoint(transform, Vector3f::Zero());
                lights.push_back(light);
            }
        }
    }
}

//==============================================================================
// Animation
//==============================================================================

static void ParseAnimation(
    const GltfBinaryDataStorage& gltf_storage,
    JsonProxy json_anim,
    int num_nodes, Animation& anim)
{
    JsonProxy json_channels = json_anim.ChildArray("channels");
    JsonProxy json_samplers = json_anim.ChildArray("samplers");

    Animation anim_s;
    Animation anim_r;
    Animation anim_t;
    anim_s.Reset(num_nodes);
    anim_r.Reset(num_nodes);
    anim_t.Reset(num_nodes);

    for (JsonProxy json_channel : json_channels)
    {
        JsonProxy json_target = json_channel.ChildObject("target");
        int target_node = json_target.ChildValue("node", -1);
        std::string_view target_path = json_target.ChildValue("path", ""sv);

        int sampler = json_channel.ChildValue("sampler", -1);
        JsonProxy json_sampler = json_samplers.ChildObject(sampler);
        int sampler_input = json_sampler.ChildValue("input", -1);
        int sampler_output = json_sampler.ChildValue("output", -1);

        if (0 <= target_node && target_node < num_nodes)
        {
            if (target_path == "scale")
            {
                AnimationChannel& channel = anim_s.channels[target_node];
                gltf_storage.ReadAccessor(sampler_input, channel.timestamps);
                gltf_storage.ReadAccessor(sampler_output, channel.scales);
            }
            else if (target_path == "rotation")
            {
                AnimationChannel& channel = anim_r.channels[target_node];
                gltf_storage.ReadAccessor(sampler_input, channel.timestamps);
                gltf_storage.ReadAccessor(sampler_output, channel.rotations);
            }
            else if (target_path == "translation")
            {
                AnimationChannel& channel = anim_t.channels[target_node];
                gltf_storage.ReadAccessor(sampler_input, channel.timestamps);
                gltf_storage.ReadAccessor(sampler_output, channel.translations);
            }
        }
    }

    anim.Reset(num_nodes);
    anim.CombineAnimationSRT(anim_s, anim_r, anim_t);
    anim.name = json_anim.ChildValue("name", "");
}

static void SplitSkeletalAnimationFromMovie(
    const GltfNodeSummary& gltf_node_summary,
    Movie& movie, const VecSharedSkeleton& skeletons)
{
    // for animation channels of scene nodes representing a joint,
    // split them from movie, splice as skeletal animation.
    std::vector<int> splitted_anim_indices(skeletons.size(), -1);

    for (const GltfJointNode& joint_node : gltf_node_summary.joint_nodes)
    {
        if (joint_node.joint < 0) { continue; }
        std::size_t node = &joint_node - gltf_node_summary.joint_nodes.data();
        Skeleton& skeleton = *skeletons[joint_node.skin];
        int& anim_idx = splitted_anim_indices[joint_node.skin];

        if (anim_idx < 0)
        {
            anim_idx = static_cast<int>(skeleton.animations.size());
            Animation& anim = skeleton.animations.emplace_back();
            anim.Reset(skeleton.NumJoints());
            anim.name = movie.root_animation.name;
            for (int skin_owner : gltf_node_summary.skin_owners[joint_node.skin])
                movie.body_animations[skin_owner] = anim_idx;
        }

        skeleton.animations[anim_idx].channels[joint_node.joint] =
            std::move(movie.root_animation.channels[node]);
    }
}

static void ParseAnimations(
    const GltfBinaryDataStorage& gltf_storage,
    const GltfNodeSummary& gltf_node_summary,
    JsonProxy json_anims,
    std::vector<Movie>& movies,
    const VecSharedSkeleton& skeletons)
{
    const int num_nodes = static_cast<int>(gltf_node_summary.node_parents.size());
    Movie movie;
    for (JsonProxy json_anim : json_anims)
    {
        ParseAnimation(gltf_storage, json_anim, num_nodes, movie.root_animation);
        movie.body_animations.assign(num_nodes, -1);
        SplitSkeletalAnimationFromMovie(gltf_node_summary, movie, skeletons);
        if (!movie.IsEmpty())
            movies.push_back(std::move(movie));
    }
}

//==============================================================================
// PostProcess
//==============================================================================

static void PostProcessRemoveNullObject(AssetContent& assets)
{
    std::size_t new_num_objs = 0;

    // for each null object, remove it from object array and all movies.
    for (std::size_t idx = 0; idx < assets.objects.size(); ++idx)
    {
        if (assets.objects[idx] == nullptr) { continue; }
        if (new_num_objs++ == idx) { continue; }
        const std::size_t new_idx = new_num_objs - 1;
        assets.objects[new_idx] = assets.objects[idx];
        for (Movie& movie : assets.movies)
        {
            movie.root_animation.channels[new_idx] = 
                std::move(movie.root_animation.channels[idx]);
            movie.body_animations[new_idx] = movie.body_animations[idx];
        }
    }

    // for each movie, resize it to finish object removing.
    for (std::size_t idx = 0; idx < assets.movies.size(); ++idx)
    {
        Movie& movie = assets.movies[idx];
        movie.root_animation.channels.resize(new_num_objs);
        movie.body_animations.resize(new_num_objs);
    }

    assets.objects.resize(new_num_objs);
}

template <typename SrcTexelType, typename Operation>
void PostProcessTransformTexels(std::vector<std::uint8_t>& data, Operation op)
{
    using DestTexelType = std::invoke_result_t<Operation, SrcTexelType>;
    constexpr std::size_t kNumSrcComp = SrcTexelType::kSize;
    const std::size_t num_texels = data.size() / kNumSrcComp;

    if constexpr (sizeof(SrcTexelType) == sizeof(DestTexelType))
    {
        auto* src_texels = reinterpret_cast<const SrcTexelType*>(data.data());
        auto* dest_texels = reinterpret_cast<DestTexelType*>(data.data());
        std::transform(src_texels, src_texels + num_texels, dest_texels, op);
    }
    else
    {
        std::vector<std::uint8_t> buffer(num_texels * sizeof(DestTexelType));
        auto* src_texels = reinterpret_cast<const SrcTexelType*>(data.data());
        auto* dest_texels = reinterpret_cast<DestTexelType*>(buffer.data());
        std::transform(src_texels, src_texels + num_texels, dest_texels, op);
        data.swap(buffer);
    }
}

static void PostProcessImageSRGBToLinear(TextureMipMap& image)
{
    if (image.format == TexelFormat::UBYTE2)
    {
        image.format = TexelFormat::FLOAT4;
        PostProcessTransformTexels<Vector2b>(image.texels, [](Vector2b ga) {
            float grey = Color::SRGBToLinear(Color::UByteToFloat(ga[0]));
            float alpha = Color::UByteToFloat(ga[1]);
            return Vector4f(grey, grey, grey, alpha); });
    }
    else if (image.format == TexelFormat::UBYTE3)
    {
        image.format = TexelFormat::FLOAT3;
        PostProcessTransformTexels<Vector3b>(image.texels, [](Vector3b rgb) {
            return Color::SRGBToLinear(Color::UByteToFloat(rgb)); });
        
    }
    else if (image.format == TexelFormat::UBYTE4)
    {
        image.format = TexelFormat::FLOAT4;
        PostProcessTransformTexels<Vector4b>(image.texels, [](Vector4b rgba) {
            return Color::SRGBToLinear(Color::UByteToFloat(rgba)); });
    }
    else
    {
        // other TexelFormat is not supported yet.
        assert(false);
    }
}

static void PostProcessTextures(AssetContent& assets)
{
    std::unordered_set<const Texture*> srgb_texs;

    // summary all textures encoded with sRGB by glTF Specification.
    for (const std::shared_ptr<MaterialBase>& mat : assets.materials)
    {
        if (auto mr = DownCast<MaterialPbrMetallicRoughness>(mat))
        {
            srgb_texs.insert(mr->base_color_tex);
            srgb_texs.insert(mr->emissive_tex);
        }
        else if (auto sg = DownCast<MaterialPbrSpecularGlossiness>(mat))
        {
            srgb_texs.insert(sg->diffuse_tex);
            srgb_texs.insert(sg->specular_glossiness_tex);
            srgb_texs.insert(sg->emissive_tex);
        }
    }

    // convert sRGB textures to linear space and generate texture mipmaps.
    for (const std::shared_ptr<Texture>& tex : assets.textures)
    {
        if (srgb_texs.count(tex.get()))
            PostProcessImageSRGBToLinear(tex->MipMap0());
        tex->GenerateMipMaps();
    }
}

//==============================================================================
// LoadAssetFromGltf
//==============================================================================

static void LoadAssetFromGltf(
    const std::filesystem::path& asset_dir,
    JsonProxy json_doc,
    AssetContent& assets)
{
    GltfBinaryDataStorage gltf_storage;
    GltfNodeSummary gltf_node_summary;

    // BinaryDataStorage & NodeSummary
    {
        JsonProxy json_nodes = json_doc.ChildArray("nodes");
        JsonProxy json_skins = json_doc.ChildArray("skins");
        ParseBinaryDataStorage(asset_dir, json_doc, gltf_storage);
        CreateNodeSummary(json_nodes, json_skins, gltf_node_summary);
    }

    // Image & Material
    {
        JsonProxy json_images = json_doc.ChildArray("images");
        JsonProxy json_mats = json_doc.ChildArray("materials");
        ParseImages(asset_dir, json_images, assets.textures);
        ParseMaterials(assets.textures, json_mats, assets.materials);
    }

    // Mesh & Skin
    {
        JsonProxy json_meshes = json_doc.ChildArray("meshes");
        JsonProxy json_skins = json_doc.ChildArray("skins");
        ParseMeshes(gltf_storage, assets.materials, json_meshes, assets.meshes);
        ParseSkins(gltf_storage, gltf_node_summary, json_skins, assets.skeletons);
    }

    // Node
    {
        JsonProxy json_nodes = json_doc.ChildArray("nodes");
        ParseNodes(gltf_node_summary, assets.meshes, assets.skeletons,
            json_nodes, assets.objects);
    }

    // Camera & Light
    {
        JsonProxy json_nodes = json_doc.ChildArray("nodes");
        JsonProxy json_cameras = json_doc.ChildArray("cameras");
        JsonProxy json_lights = json_doc.ChildObject("extensions").
            ChildObject("KHR_lights_punctual").ChildArray("lights");
        ParseCameras(assets.objects, json_nodes, json_cameras, assets.cameras);
        ParseLights(assets.objects, json_nodes, json_lights, assets.lights);
    }

    // Animation
    {
        JsonProxy json_anims = json_doc.ChildArray("animations");
        ParseAnimations(gltf_storage, gltf_node_summary,
            json_anims, assets.movies, assets.skeletons);
    }

    // PostProcess
    {
        PostProcessRemoveNullObject(assets);
        PostProcessTextures(assets);
    }
}

}

bool LoadAssetFromGltf(const std::filesystem::path& filename, AssetContent& assets)
{
    std::ifstream ifs(filename);
    if (!ifs.is_open())
        return false;

    const nlohmann::json json_doc = nlohmann::json::parse(ifs, nullptr, false);
    if (!json_doc.is_object())
        return false;

    assets.Reset();
    GltfParser::LoadAssetFromGltf(filename.parent_path(), json_doc, assets);
    return true;
}

}