#include "ResourceAsset.h"
#include "Animation.h"
#include "Backlog.h"
#include "BinaryArchive.h"
#include "FileSystem.h"
#include "Font.h"
#include "JobSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "RenderingUtils.h"
#include "Scene.h"
#include "SceneComponents.h"
#include "TransformUpdateSystem.h"
#include "NavMesh.h"
#include "StableHash.h"
#include "ShaderInterop_Renderer.h"
#include "Sound.h"
#include "StringUtils.h"
#include <cstring>
#include <fstream>
#include <mutex>

namespace won::resource
{
    namespace
    {
        constexpr uint32 mesh_binary_version = 2;
        constexpr uint32 mesh_binary_magic = 0x48534D57; // WMSH
        constexpr uint32 navmesh_binary_version = 1;
		constexpr uint32 navmesh_binary_magic = 0x56414E57; // WNAV
        constexpr uint32 material_binary_version = 5;
        constexpr uint32 dds_magic = 0x20534444; // DDS
        constexpr uint32 dds_fourcc_dx10 = 0x30315844; // DX10
        constexpr uint32 dds_resource_dimension_texture2d = 3;
        constexpr uint32 dds_header_flags_texture = 0x00001007;
        constexpr uint32 dds_header_flags_linear_size = 0x00080000;
        constexpr uint32 dds_header_flags_mipmap = 0x00020000;
        constexpr uint32 dds_pixel_format_flags_fourcc = 0x00000004;
        constexpr uint32 dds_caps_texture = 0x00001000;
        constexpr uint32 dds_caps_complex = 0x00000008;
        constexpr uint32 dds_caps_mipmap = 0x00400000;
        constexpr uint32 dds_pixel_format_flags_rgb = 0x00000040;
        constexpr uint32 dds_caps2_cubemap = 0x00000200;
        constexpr uint32 dds_misc_flag_texturecube = 0x00000004;
        constexpr uint32 dds_fourcc_a16b16g16r16f = 113; // legacy D3DFMT_A16B16G16R16F (RGBA16F)
        constexpr uint32 dds_fourcc_dxt1 = 0x31545844; // DXT1
        constexpr uint32 dds_fourcc_dxt3 = 0x33545844; // DXT3
        constexpr uint32 dds_fourcc_dxt5 = 0x35545844; // DXT5
        constexpr uint32 dds_fourcc_ati1 = 0x31495441; // ATI1
        constexpr uint32 dds_fourcc_bc4u = 0x55344342; // BC4U
        constexpr uint32 dds_fourcc_ati2 = 0x32495441; // ATI2
        constexpr uint32 dds_fourcc_bc5u = 0x55354342; // BC5U
        // DDS DX10 headers store DXGI_FORMAT numeric values, but resource code must not include platform headers.
        constexpr uint32 dds_dxgi_format_unknown = 0;
        constexpr uint32 dds_dxgi_format_r16g16b16a16_float = 10;
        constexpr uint32 dds_dxgi_format_r8g8b8a8_unorm = 28;
        constexpr uint32 dds_dxgi_format_r8g8b8a8_unorm_srgb = 29;
        constexpr uint32 dds_dxgi_format_bc1_unorm = 71;
        constexpr uint32 dds_dxgi_format_bc1_unorm_srgb = 72;
        constexpr uint32 dds_dxgi_format_bc2_unorm = 74;
        constexpr uint32 dds_dxgi_format_bc2_unorm_srgb = 75;
        constexpr uint32 dds_dxgi_format_bc3_unorm = 77;
        constexpr uint32 dds_dxgi_format_bc3_unorm_srgb = 78;
        constexpr uint32 dds_dxgi_format_bc4_unorm = 80;
        constexpr uint32 dds_dxgi_format_bc4_snorm = 81;
        constexpr uint32 dds_dxgi_format_bc5_unorm = 83;
        constexpr uint32 dds_dxgi_format_bc5_snorm = 84;
        constexpr uint32 dds_dxgi_format_b8g8r8a8_unorm = 87;
        constexpr uint32 dds_dxgi_format_b8g8r8a8_unorm_srgb = 91;
        constexpr uint32 dds_dxgi_format_bc6h_uf16 = 95;
        constexpr uint32 dds_dxgi_format_bc6h_sf16 = 96;
        constexpr uint32 dds_dxgi_format_bc7_unorm = 98;
        constexpr uint32 dds_dxgi_format_bc7_unorm_srgb = 99;

        struct DDSPixelFormat
        {
            uint32 size;
            uint32 flags;
            uint32 four_cc;
            uint32 rgb_bit_count;
            uint32 r_bit_mask;
            uint32 g_bit_mask;
            uint32 b_bit_mask;
            uint32 a_bit_mask;
        };

        struct DDSHeader
        {
            uint32 size;
            uint32 flags;
            uint32 height;
            uint32 width;
            uint32 pitch_or_linear_size;
            uint32 depth;
            uint32 mip_map_count;
            uint32 reserved1[11];
            DDSPixelFormat pixel_format;
            uint32 caps;
            uint32 caps2;
            uint32 caps3;
            uint32 caps4;
            uint32 reserved2;
        };

        struct DDSHeaderDXT10
        {
            uint32 dxgi_format;
            uint32 resource_dimension;
            uint32 misc_flag;
            uint32 array_size;
            uint32 misc_flags2;
        };

        static_assert(sizeof(DDSPixelFormat) == 32);
        static_assert(sizeof(DDSHeader) == 124);
        static_assert(sizeof(DDSHeaderDXT10) == 20);

        uint32 DXGIFormatFromRHIFormat(rendering::RHIFormat format)
        {
            switch (format)
            {
            case rendering::RHIFormat::R8G8B8A8Unorm: return dds_dxgi_format_r8g8b8a8_unorm;
            case rendering::RHIFormat::R8G8B8A8UnormSrgb: return dds_dxgi_format_r8g8b8a8_unorm_srgb;
            case rendering::RHIFormat::B8G8R8A8Unorm: return dds_dxgi_format_b8g8r8a8_unorm;
            case rendering::RHIFormat::B8G8R8A8UnormSrgb: return dds_dxgi_format_b8g8r8a8_unorm_srgb;
            case rendering::RHIFormat::BC1Unorm: return dds_dxgi_format_bc1_unorm;
            case rendering::RHIFormat::BC1UnormSrgb: return dds_dxgi_format_bc1_unorm_srgb;
            case rendering::RHIFormat::BC2Unorm: return dds_dxgi_format_bc2_unorm;
            case rendering::RHIFormat::BC2UnormSrgb: return dds_dxgi_format_bc2_unorm_srgb;
            case rendering::RHIFormat::BC3Unorm: return dds_dxgi_format_bc3_unorm;
            case rendering::RHIFormat::BC3UnormSrgb: return dds_dxgi_format_bc3_unorm_srgb;
            case rendering::RHIFormat::BC4Unorm: return dds_dxgi_format_bc4_unorm;
            case rendering::RHIFormat::BC4Snorm: return dds_dxgi_format_bc4_snorm;
            case rendering::RHIFormat::BC5Unorm: return dds_dxgi_format_bc5_unorm;
            case rendering::RHIFormat::BC5Snorm: return dds_dxgi_format_bc5_snorm;
            case rendering::RHIFormat::BC6HUf16: return dds_dxgi_format_bc6h_uf16;
            case rendering::RHIFormat::BC6HSf16: return dds_dxgi_format_bc6h_sf16;
            case rendering::RHIFormat::BC7Unorm: return dds_dxgi_format_bc7_unorm;
            case rendering::RHIFormat::BC7UnormSrgb: return dds_dxgi_format_bc7_unorm_srgb;
            default: return dds_dxgi_format_unknown;
            }
        }

        void SerializeSubmeshes(serialize::BinaryArchive& archive, Vector<Submesh>& submeshes)
        {
            Size count = archive.IsWriteMode() ? submeshes.size() : 0;
            serialize::Serialize(archive, count);
            if (archive.IsReadMode())
            {
                submeshes.resize(count);
            }

            for (Submesh& submesh : submeshes)
            {
                serialize::Serialize(archive, submesh.first_index);
                serialize::Serialize(archive, submesh.index_count);
                serialize::Serialize(archive, submesh.first_vertex);
                serialize::Serialize(archive, submesh.material_slot);
                serialize::Serialize(archive, submesh.primitive_topology);
                serialize::Serialize(archive, submesh.local_bounds);
            }
        }

        void SerializeSkeleton(serialize::BinaryArchive& archive, std::shared_ptr<Skeleton>& skeleton)
        {
            bool has_skeleton = archive.IsWriteMode() ? skeleton && skeleton->IsValid() : false;
            serialize::Serialize(archive, has_skeleton);
            if (!has_skeleton)
            {
                if (archive.IsReadMode())
                {
                    skeleton.reset();
                }
                return;
            }

            if (archive.IsReadMode())
            {
                skeleton = std::make_shared<Skeleton>();
            }

            Size bone_count = archive.IsWriteMode() ? skeleton->bones.size() : 0;
            serialize::Serialize(archive, bone_count);
            if (archive.IsReadMode())
            {
                skeleton->bones.resize(bone_count);
            }

            for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
            {
                Bone& bone = skeleton->bones[bone_index];
                serialize::Serialize(archive, bone.name);
                serialize::Serialize(archive, bone.parent_index);
                serialize::Serialize(archive, bone.inverse_bind_matrix);
                serialize::Serialize(archive, bone.bind_local_transform);
                if (archive.IsReadMode() && !bone.name.empty())
                {
                    skeleton->bone_name_to_index[bone.name] = static_cast<uint32>(bone_index);
                }
            }
        }

        template <typename T>
        void SerializeKeyframes(serialize::BinaryArchive& archive, Vector<AnimationKeyframe<T>>& keyframes)
        {
            serialize::Serialize(archive, keyframes);
        }

        void SerializeAnimationClips(serialize::BinaryArchive& archive, Vector<std::shared_ptr<AnimationClip>>& clips, uint32 version)
        {
            Size clip_count = archive.IsWriteMode() ? clips.size() : 0;
            serialize::Serialize(archive, clip_count);
            if (archive.IsReadMode())
            {
                clips.resize(clip_count);
            }

            for (Size clip_index = 0; clip_index < clip_count; ++clip_index)
            {
                if (archive.IsReadMode())
                {
                    clips[clip_index] = std::make_shared<AnimationClip>();
                }
                AnimationClip& clip = *clips[clip_index];
                serialize::Serialize(archive, clip.name);
                serialize::Serialize(archive, clip.duration);
                serialize::Serialize(archive, clip.ticks_per_second);

                Size channel_count = archive.IsWriteMode() ? clip.channels.size() : 0;
                serialize::Serialize(archive, channel_count);
                if (archive.IsReadMode())
                {
                    clip.channels.resize(channel_count);
                }

                for (AnimationChannel& channel : clip.channels)
                {
                    serialize::Serialize(archive, channel.bone_index);
                    SerializeKeyframes(archive, channel.positions);
                    SerializeKeyframes(archive, channel.rotations);
                    SerializeKeyframes(archive, channel.scales);
                }

                if (version >= 2)
                {
                    Size event_count = archive.IsWriteMode() ? clip.events.size() : 0;
                    serialize::Serialize(archive, event_count);
                    if (archive.IsReadMode())
                    {
                        clip.events.resize(event_count);
                    }
                    for (AnimationEventMarker& clip_event : clip.events)
                    {
                        serialize::Serialize(archive, clip_event.time_seconds);
                        serialize::Serialize(archive, clip_event.name);
                    }
                }
            }
        }

        std::mutex mesh_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Mesh>> mesh_cache;

        std::mutex texture_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Image>> texture_cache;

        std::mutex material_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Material>> material_cache;
    }

    rendering::RHIFormat RHIFormatFromDXGIFormat(uint32 format)
    {
        switch (format)
        {
        case dds_dxgi_format_r16g16b16a16_float: return rendering::RHIFormat::R16G16B16A16Float;
        case dds_dxgi_format_r8g8b8a8_unorm: return rendering::RHIFormat::R8G8B8A8Unorm;
        case dds_dxgi_format_r8g8b8a8_unorm_srgb: return rendering::RHIFormat::R8G8B8A8UnormSrgb;
        case dds_dxgi_format_b8g8r8a8_unorm: return rendering::RHIFormat::B8G8R8A8Unorm;
        case dds_dxgi_format_b8g8r8a8_unorm_srgb: return rendering::RHIFormat::B8G8R8A8UnormSrgb;
        case dds_dxgi_format_bc1_unorm: return rendering::RHIFormat::BC1Unorm;
        case dds_dxgi_format_bc1_unorm_srgb: return rendering::RHIFormat::BC1UnormSrgb;
        case dds_dxgi_format_bc2_unorm: return rendering::RHIFormat::BC2Unorm;
        case dds_dxgi_format_bc2_unorm_srgb: return rendering::RHIFormat::BC2UnormSrgb;
        case dds_dxgi_format_bc3_unorm: return rendering::RHIFormat::BC3Unorm;
        case dds_dxgi_format_bc3_unorm_srgb: return rendering::RHIFormat::BC3UnormSrgb;
        case dds_dxgi_format_bc4_unorm: return rendering::RHIFormat::BC4Unorm;
        case dds_dxgi_format_bc4_snorm: return rendering::RHIFormat::BC4Snorm;
        case dds_dxgi_format_bc5_unorm: return rendering::RHIFormat::BC5Unorm;
        case dds_dxgi_format_bc5_snorm: return rendering::RHIFormat::BC5Snorm;
        case dds_dxgi_format_bc6h_uf16: return rendering::RHIFormat::BC6HUf16;
        case dds_dxgi_format_bc6h_sf16: return rendering::RHIFormat::BC6HSf16;
        case dds_dxgi_format_bc7_unorm: return rendering::RHIFormat::BC7Unorm;
        case dds_dxgi_format_bc7_unorm_srgb: return rendering::RHIFormat::BC7UnormSrgb;
        default: return rendering::RHIFormat::Unknown;
        }
    }

    String GetAssetMetaPath(const String& source_path)
    {
        return source_path.empty() ? String() : source_path + "." + asset_metadata_extension;
    }

    bool LoadAssetMeta(const String& meta_path, AssetMeta& out_meta)
    {
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(meta_path) || !archive.BeginObject())
        {
            return false;
        }

        archive.Field("version", out_meta.version);
        archive.Field("asset_id", out_meta.asset_id);
        archive.Field("source_asset_path", out_meta.source_asset_path);
        archive.Field("asset_type", out_meta.asset_type);
        archive.Field("binary_path", out_meta.binary_path);
        archive.Field("source_timestamp", out_meta.source_timestamp);

        if (out_meta.version >= 2)
        {
            archive.Field("asset_name", out_meta.asset_name);
            if (archive.BeginObject("texture"))
            {
                archive.Field("is_srgb", out_meta.texture.is_srgb);
                archive.Field("generate_mipmaps", out_meta.texture.generate_mipmaps);
                archive.EndObject();
            }
            if (archive.BeginObject("texture_info"))
            {
                archive.Field("width", out_meta.texture_info.width);
                archive.Field("height", out_meta.texture_info.height);
                archive.Field("mip_levels", out_meta.texture_info.mip_levels);
                archive.Field("is_cube", out_meta.texture_info.is_cube);
                archive.Field("format", out_meta.texture_info.format);
                archive.Field("source_hash", out_meta.texture_info.source_hash);
                archive.Field("import_error", out_meta.texture_info.import_error);
                archive.EndObject();
            }
            if (archive.BeginObject("mesh"))
            {
                archive.Field("scale", out_meta.mesh.scale);
                archive.Field("import_animations", out_meta.mesh.import_animations);
                archive.Field("import_normals", out_meta.mesh.import_normals);
                archive.Field("import_tangents", out_meta.mesh.import_tangents);
                archive.Field("import_skeleton", out_meta.mesh.import_skeleton);
                archive.EndObject();
            }
        }

        archive.EndObject();
        return !archive.HasError() && out_meta.version <= asset_format_version;
    }

    bool SaveAssetMeta(const String& meta_path, const AssetMeta& meta)
    {
        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        uint32 version = asset_format_version;
        String asset_id = meta.asset_id;
        String asset_name = meta.asset_name;
        String source_asset_path = meta.source_asset_path;
        String asset_type = meta.asset_type;
        String binary_path = meta.binary_path;
        uint64 source_timestamp = meta.source_timestamp;
        bool is_srgb = meta.texture.is_srgb;
        bool generate_mipmaps = meta.texture.generate_mipmaps;
        uint32 texture_width = meta.texture_info.width;
        uint32 texture_height = meta.texture_info.height;
        uint32 texture_mip_levels = meta.texture_info.mip_levels;
        bool texture_is_cube = meta.texture_info.is_cube;
        String texture_format = meta.texture_info.format;
        uint64 texture_source_hash = meta.texture_info.source_hash;
        String texture_import_error = meta.texture_info.import_error;
        float mesh_scale = meta.mesh.scale;
        bool import_animations = meta.mesh.import_animations;
        bool import_normals = meta.mesh.import_normals;
        bool import_tangents = meta.mesh.import_tangents;
        bool import_skeleton = meta.mesh.import_skeleton;
        archive.Field("version", version);
        archive.Field("asset_id", asset_id);
        archive.Field("source_asset_path", source_asset_path);
        archive.Field("asset_type", asset_type);
        archive.Field("binary_path", binary_path);
        archive.Field("source_timestamp", source_timestamp);
        archive.Field("asset_name", asset_name);
        archive.BeginObject("texture");
        archive.Field("is_srgb", is_srgb);
        archive.Field("generate_mipmaps", generate_mipmaps);
        archive.EndObject();
        archive.BeginObject("texture_info");
        archive.Field("width", texture_width);
        archive.Field("height", texture_height);
        archive.Field("mip_levels", texture_mip_levels);
        archive.Field("is_cube", texture_is_cube);
        archive.Field("format", texture_format);
        archive.Field("source_hash", texture_source_hash);
        archive.Field("import_error", texture_import_error);
        archive.EndObject();
        archive.BeginObject("mesh");
        archive.Field("scale", mesh_scale);
        archive.Field("import_animations", import_animations);
        archive.Field("import_normals", import_normals);
        archive.Field("import_tangents", import_tangents);
        archive.Field("import_skeleton", import_skeleton);
        archive.EndObject();
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(meta_path);
    }

    bool SaveMeshBinary(const String& path, const Mesh& mesh)
    {
        if (path.empty() || !mesh.IsValid())
        {
            return false;
        }

        Mesh copy = mesh;
        copy.ClearRenderData();
        copy.ClearGPUBVH();
        serialize::BinaryArchive archive(path, serialize::ArchiveMode::Write);
        uint32 magic = mesh_binary_magic;
        uint32 version = mesh_binary_version;
        serialize::Serialize(archive, magic);
        serialize::Serialize(archive, version);
        serialize::Serialize(archive, copy.positions);
        serialize::Serialize(archive, copy.colors);
        serialize::Serialize(archive, copy.normals);
        serialize::Serialize(archive, copy.tangents);
        serialize::Serialize(archive, copy.texcoords);
        serialize::Serialize(archive, copy.bone_indices);
        serialize::Serialize(archive, copy.bone_weights);
        serialize::Serialize(archive, copy.indices);
        SerializeSubmeshes(archive, copy.submeshes);
        SerializeSkeleton(archive, copy.skeleton);
        SerializeAnimationClips(archive, copy.animation_clips, version);
        return true;
    }

    std::shared_ptr<Mesh> LoadMeshBinary(const String& path)
    {
        if (path.empty() || !io::Exists(path))
        {
            return nullptr;
        }
        if (utils::ToLower(io::GetExtension(path)) != mesh_binary_extension)
        {
            backlog::Post("[LoadResources] mesh load rejected, expected ." + String(mesh_binary_extension) + ": " + path, backlog::LogLevel::Warning);
            return nullptr;
        }

        const String key = io::GetAbsolutePath(path);

        {
            std::lock_guard<std::mutex> lock(mesh_cache_mutex);
            auto it = mesh_cache.find(key);
            if (it != mesh_cache.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }
            }
        }

        serialize::BinaryArchive archive(path, serialize::ArchiveMode::Read);
        uint32 magic = 0;
        uint32 version = 0;
        serialize::Serialize(archive, magic);
        serialize::Serialize(archive, version);
        if (magic != mesh_binary_magic || version < 1 || version > mesh_binary_version)
        {
            return nullptr;
        }

        auto mesh = std::make_shared<Mesh>();
        mesh->name = path;
        serialize::Serialize(archive, mesh->positions);
        serialize::Serialize(archive, mesh->colors);
        serialize::Serialize(archive, mesh->normals);
        serialize::Serialize(archive, mesh->tangents);
        serialize::Serialize(archive, mesh->texcoords);
        serialize::Serialize(archive, mesh->bone_indices);
        serialize::Serialize(archive, mesh->bone_weights);
        serialize::Serialize(archive, mesh->indices);
        SerializeSubmeshes(archive, mesh->submeshes);
        SerializeSkeleton(archive, mesh->skeleton);
        SerializeAnimationClips(archive, mesh->animation_clips, version);
        if (!mesh->IsValid())
        {
            return nullptr;
        }

        mesh->BuildBoneBounds();

        {
            std::lock_guard<std::mutex> lock(mesh_cache_mutex);
            auto it = mesh_cache.find(key);
            if (it != mesh_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            mesh_cache[key] = mesh;
        }

        return mesh;
    }

    bool SaveTextureBinary(const String& path, uint32 width, uint32 height, uint32 mip_levels, rendering::RHIFormat format, const Vector<uint8>& pixels)
    {
        if (path.empty() || width == 0 || height == 0 || mip_levels == 0 || pixels.empty())
        {
            return false;
        }

        const uint32 dxgi_format = DXGIFormatFromRHIFormat(format);
        if (dxgi_format == dds_dxgi_format_unknown)
        {
            return false;
        }

        DDSHeader header = {};
        header.size = sizeof(DDSHeader);
        header.flags = dds_header_flags_texture | dds_header_flags_linear_size;
        if (mip_levels > 1)
        {
            header.flags |= dds_header_flags_mipmap;
        }
        header.height = height;
        header.width = width;
        header.pitch_or_linear_size = static_cast<uint32>(pixels.size());
        header.depth = 0;
        header.mip_map_count = mip_levels;
        header.pixel_format.size = sizeof(DDSPixelFormat);
        header.pixel_format.flags = dds_pixel_format_flags_fourcc;
        header.pixel_format.four_cc = dds_fourcc_dx10;
        header.caps = dds_caps_texture;
        if (mip_levels > 1)
        {
            header.caps |= dds_caps_complex | dds_caps_mipmap;
        }

        DDSHeaderDXT10 header_dxt10 = {};
        header_dxt10.dxgi_format = dxgi_format;
        header_dxt10.resource_dimension = dds_resource_dimension_texture2d;
        header_dxt10.array_size = 1;

        const Size header_size = sizeof(uint32) + sizeof(DDSHeader) + sizeof(DDSHeaderDXT10);
        Vector<uint8> bytes;
        bytes.resize(header_size + pixels.size());
        uint8* dst = bytes.data();
        uint32 magic = dds_magic;
        std::memcpy(dst, &magic, sizeof(magic));
        dst += sizeof(magic);
        std::memcpy(dst, &header, sizeof(header));
        dst += sizeof(header);
        std::memcpy(dst, &header_dxt10, sizeof(header_dxt10));
        dst += sizeof(header_dxt10);
        std::memcpy(dst, pixels.data(), pixels.size());
        return io::WriteAllBytes(path, bytes.data(), bytes.size());
    }

    std::shared_ptr<Image> LoadTextureBinary(const String& path)
    {
        if (path.empty() || !io::Exists(path))
        {
            return nullptr;
        }
        const String extension = utils::ToLower(io::GetExtension(path));
        if (extension != texture_binary_extension && extension != "dds") // support dds
        {
            backlog::Post("[LoadResources] texture load rejected, expected ." + String(texture_binary_extension) + ": " + path, backlog::LogLevel::Warning);
            return nullptr;
        }

        const String key = io::GetAbsolutePath(path);

        {
            std::lock_guard<std::mutex> lock(texture_cache_mutex);
            auto it = texture_cache.find(key);
            if (it != texture_cache.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }
            }
        }

        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return nullptr;
        }
        const std::streamoff file_size = stream.tellg();
        if (file_size < static_cast<std::streamoff>(sizeof(uint32) + sizeof(DDSHeader)))
        {
            return nullptr;
        }
        stream.seekg(0);

        uint32 magic = 0;
        DDSHeader header = {};
        stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream || magic != dds_magic || header.size != sizeof(DDSHeader) || header.pixel_format.size != sizeof(DDSPixelFormat))
        {
            return nullptr;
        }

        const bool has_dx10 = (header.pixel_format.flags & dds_pixel_format_flags_fourcc) != 0 && header.pixel_format.four_cc == dds_fourcc_dx10;
        rendering::RHIFormat format = rendering::RHIFormat::Unknown;
        bool is_cube = false;
        Size header_bytes = sizeof(uint32) + sizeof(DDSHeader);

        if (has_dx10)
        {
            DDSHeaderDXT10 header_dxt10 = {};
            stream.read(reinterpret_cast<char*>(&header_dxt10), sizeof(header_dxt10));
            if (!stream)
            {
                return nullptr;
            }
            header_bytes += sizeof(DDSHeaderDXT10);
            format = RHIFormatFromDXGIFormat(header_dxt10.dxgi_format);
            is_cube = (header_dxt10.misc_flag & dds_misc_flag_texturecube) != 0;
        }
        else
        {
            // Legacy header: FourCC extended formats, or uncompressed RGBA/BGRA by channel masks; cube flagged in caps2.
            if ((header.pixel_format.flags & dds_pixel_format_flags_fourcc) != 0)
            {
                switch (header.pixel_format.four_cc)
                {
                case dds_fourcc_a16b16g16r16f: format = rendering::RHIFormat::R16G16B16A16Float; break;
                case dds_fourcc_dxt1: format = rendering::RHIFormat::BC1Unorm; break;
                case dds_fourcc_dxt3: format = rendering::RHIFormat::BC2Unorm; break;
                case dds_fourcc_dxt5: format = rendering::RHIFormat::BC3Unorm; break;
                case dds_fourcc_ati1:
                case dds_fourcc_bc4u: format = rendering::RHIFormat::BC4Unorm; break;
                case dds_fourcc_ati2:
                case dds_fourcc_bc5u: format = rendering::RHIFormat::BC5Unorm; break;
                default: break;
                }
            }

            if (format == rendering::RHIFormat::Unknown
                && (header.pixel_format.flags & dds_pixel_format_flags_rgb) != 0 && header.pixel_format.rgb_bit_count == 32)
            {
                const uint32 r = header.pixel_format.r_bit_mask;
                const uint32 g = header.pixel_format.g_bit_mask;
                const uint32 b = header.pixel_format.b_bit_mask;
                if (r == 0x00ff0000 && g == 0x0000ff00 && b == 0x000000ff)
                {
                    format = rendering::RHIFormat::B8G8R8A8Unorm;
                }
                else if (r == 0x000000ff && g == 0x0000ff00 && b == 0x00ff0000)
                {
                    format = rendering::RHIFormat::R8G8B8A8Unorm;
                }
            }
            is_cube = (header.caps2 & dds_caps2_cubemap) != 0;
        }

        if (format == rendering::RHIFormat::Unknown)
        {
            return nullptr;
        }

        const Size payload_size = static_cast<Size>(file_size) - header_bytes;
        if (payload_size == 0)
        {
            return nullptr;
        }

        auto image = std::make_shared<Image>();
        image->name = path;
        image->width = static_cast<int32>(header.width);
        image->height = static_cast<int32>(header.height);
        image->channels = 4;
        image->mip_levels = header.mip_map_count > 0 ? header.mip_map_count : 1;
        image->is_cube = is_cube;
        image->format = format;
        image->pixels.resize(payload_size);
        stream.seekg(static_cast<std::streamoff>(header_bytes));
        stream.read(reinterpret_cast<char*>(image->pixels.data()), static_cast<std::streamsize>(image->pixels.size()));
        if (!stream || !image->IsValid())
        {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(texture_cache_mutex);
            auto it = texture_cache.find(key);
            if (it != texture_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            texture_cache[key] = image;
        }

        return image;
    }

    bool SaveMaterialBinary(const String& path, const Vector<MaterialSlot>& slots)
    {
        if (path.empty())
        {
            return false;
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        uint32 version = material_binary_version;
        archive.Field("version", version);
        archive.BeginArray("material_slots");
        for (const MaterialSlot& slot : slots)
        {
            archive.BeginItem();
            archive.BeginObject();
            uint32 material_type_value = static_cast<uint32>(slot.material_type);
            uint32 blend_mode_value = static_cast<uint32>(slot.blend_mode);
            archive.Field("material_type", material_type_value);
            archive.Field("blend_mode", blend_mode_value);
            archive.Field("alpha_cutoff", slot.alpha_cutoff);
            archive.Field("double_sided", slot.double_sided);
            archive.Field("use_vertex_colors", slot.use_vertex_colors);
            archive.Field("receive_shadow", slot.receive_shadow);
            archive.Field("base_color", slot.base_color);
            archive.Field("emissive_color", slot.emissive_color);
            archive.Field("emissive_intensity", slot.emissive_intensity);
            archive.Field("metallic", slot.metallic);
            archive.Field("roughness", slot.roughness);
            archive.Field("reflectance", slot.reflectance);
            archive.Field("anisotropy", slot.anisotropy);
            archive.Field("sheen_color", slot.sheen_color);
            archive.Field("sheen_roughness", slot.sheen_roughness);
            archive.Field("clearcoat", slot.clearcoat);
            archive.Field("clearcoat_roughness", slot.clearcoat_roughness);
            archive.BeginArray("textures");
            for (uint32 i = 0; i < TEXTURESLOT_COUNT; ++i)
            {
                archive.Item(slot.textures[i].texture_asset_path);
            }
            archive.EndArray();
            archive.EndObject();
            archive.EndItem();
        }
        archive.EndArray();
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(path);
    }

    bool SaveMaterialBinary(const String& path, const std::shared_ptr<Material>& material)
    {
        if (!material || !SaveMaterialBinary(path, material->slots))
        {
            return false;
        }

        // Register the just-written instance as the canonical cache entry for this path.
        // file == instance is guaranteed, and the live GPU texture handles are preserved.
        const String key = io::GetAbsolutePath(path);
        std::lock_guard<std::mutex> lock(material_cache_mutex);
        material_cache[key] = material;
        return true;
    }

    std::shared_ptr<Material> LoadMaterialBinary(const String& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        const String key = io::GetAbsolutePath(path);

        {
            std::lock_guard<std::mutex> lock(material_cache_mutex);
            auto it = material_cache.find(key);
            if (it != material_cache.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }
            }
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path) || !archive.BeginObject())
        {
            return nullptr;
        }

        // Missing version reads as 0; versions below 2 use the legacy flags/shader_type slot layout.
        // Only a newer-than-known version is rejected.
        uint32 version = 0;
        archive.Field("version", version);
        if (version > material_binary_version)
        {
            backlog::Post("[LoadMaterialBinary] unsupported material version " + std::to_string(version) + ": " + path, backlog::LogLevel::Warning);
            return nullptr;
        }

        auto material = std::make_shared<Material>();
        if (archive.BeginArray("material_slots"))
        {
            const Size count = archive.GetArraySize();
            for (Size slot_index = 0; slot_index < count; ++slot_index)
            {
                if (!archive.BeginItem())
                {
                    continue;
                }
                if (archive.BeginObject())
                {
                    MaterialSlot slot = {};
                    if (version < 2)
                    {
                        uint32 flags = 0;
                        uint32 shader_type = static_cast<uint32>(MaterialType::PBR);
                        archive.Field("flags", flags);
                        archive.Field("shader_type", shader_type);
                        slot.material_type = static_cast<MaterialType>(shader_type);
                        slot.blend_mode = (flags & (1u << 1)) ? MaterialBlendMode::Transparent : MaterialBlendMode::Opaque;
                        slot.double_sided = (flags & SHADER_MATERIAL_FLAG_DOUBLE_SIDED) != 0;
                        slot.use_vertex_colors = (flags & SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS) != 0;
                        slot.receive_shadow = (flags & SHADER_MATERIAL_FLAG_RECEIVE_SHADOW) != 0;
                    }
                    else
                    {
                        uint32 material_type_value = static_cast<uint32>(slot.material_type);
                        uint32 blend_mode_value = static_cast<uint32>(slot.blend_mode);
                        archive.Field("material_type", material_type_value);
                        archive.Field("blend_mode", blend_mode_value);
                        slot.material_type = static_cast<MaterialType>(material_type_value);
                        // Masked was inserted at index 1 in version 3, shifting the blended modes up by one.
                        if (version < 3 && blend_mode_value > 0)
                        {
                            ++blend_mode_value;
                        }
                        slot.blend_mode = static_cast<MaterialBlendMode>(blend_mode_value);
                        archive.Field("alpha_cutoff", slot.alpha_cutoff);
                        archive.Field("double_sided", slot.double_sided);
                        archive.Field("use_vertex_colors", slot.use_vertex_colors);
                        archive.Field("receive_shadow", slot.receive_shadow);
                    }
                    archive.Field("base_color", slot.base_color);
                    if (version >= 4)
                    {
                        archive.Field("emissive_color", slot.emissive_color);
                    }
                    if (version >= 5)
                    {
                        archive.Field("emissive_intensity", slot.emissive_intensity);
                    }
                    archive.Field("metallic", slot.metallic);
                    archive.Field("roughness", slot.roughness);
                    archive.Field("reflectance", slot.reflectance);
                    archive.Field("anisotropy", slot.anisotropy);
                    archive.Field("sheen_color", slot.sheen_color);
                    archive.Field("sheen_roughness", slot.sheen_roughness);
                    archive.Field("clearcoat", slot.clearcoat);
                    archive.Field("clearcoat_roughness", slot.clearcoat_roughness);
                    if (archive.BeginArray("textures"))
                    {
                        const Size texture_count = archive.GetArraySize();
                        for (Size i = 0; i < texture_count && i < TEXTURESLOT_COUNT; ++i)
                        {
                            archive.Item(slot.textures[i].texture_asset_path);
                        }
                        archive.EndArray();
                    }
                    archive.EndObject();
                    material->slots.push_back(std::move(slot));
                }
                archive.EndItem();
            }
            archive.EndArray();
        }

        archive.EndObject();
        if (archive.HasError() || !material->IsValid())
        {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(material_cache_mutex);
            auto it = material_cache.find(key);
            if (it != material_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            material_cache[key] = material;
        }

        return material;
    }

    static void LoadMeshResource(ecs::GeometryComponent& geometry, const String& content_root)
    {
        if (geometry.mesh_asset_path.empty())
            return;
        const String mesh_path = project::ResolveProjectContentPath(content_root, geometry.mesh_asset_path);
        String binary_path = mesh_path;
        if (utils::ToLower(io::GetExtension(mesh_path)) != mesh_binary_extension)
        {
            AssetMeta meta = {};
            if (LoadAssetMeta(GetAssetMetaPath(mesh_path), meta) && !meta.binary_path.empty())
                binary_path = project::ResolveProjectContentPath(content_root, meta.binary_path);
        }
        auto mesh = LoadMeshBinary(binary_path);
        if (mesh)
        {
            geometry.SetMesh(mesh);
            rendering::utils::EnqueueResourceUpload(mesh);
        }
        else
            backlog::Post("[LoadResources] mesh load failed: " + binary_path, backlog::LogLevel::Warning);
    }

    static void LoadTextureMap(MaterialSlot::TextureMap& texture_map, uint32 slot, const String& content_root)
    {
        if (texture_map.texture_asset_path.empty())
            return;
        const String texture_path = project::ResolveProjectContentPath(content_root, texture_map.texture_asset_path);
        std::shared_ptr<Image> image;
        if (utils::ToLower(io::GetExtension(texture_path)) == texture_binary_extension)
        {
            image = LoadTextureBinary(texture_path);
        }
        else
        {
            AssetMeta meta = {};
            if (LoadAssetMeta(GetAssetMetaPath(texture_path), meta) && !meta.binary_path.empty())
                image = LoadTextureBinary(project::ResolveProjectContentPath(content_root, meta.binary_path));
            if (!image)
                image = LoadImageFile(texture_path, 4);
        }
        if (image && image->IsValid())
        {
            const bool color_texture = slot == BASECOLORMAP || slot == EMISSIVEMAP || slot == SHEENCOLORMAP;
            const rendering::RHIFormat fmt = color_texture ? rendering::RHIFormat::R8G8B8A8UnormSrgb : rendering::RHIFormat::R8G8B8A8Unorm;
            texture_map.image = image;
            rendering::utils::EnqueueResourceUpload(image, fmt);
        }
        else
        {
            backlog::Post("[LoadResources] texture load failed: " + texture_path, backlog::LogLevel::Warning);
        }
    }

    static void LoadMaterialTextures(Material& material, const String& content_root)
    {
        for (MaterialSlot& material_slot : material.slots)
            for (uint32 slot = 0; slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++slot)
                LoadTextureMap(material_slot.textures[slot], slot, content_root);
    }

    template <typename TextComponent>
    static void LoadFontResource(TextComponent& text, const String& content_root)
    {
        if (text.font_asset_path.empty())
            return;
        text.font = LoadFontFile(project::ResolveProjectContentPath(content_root, text.font_asset_path));
        if (text.font)
        {
            rendering::utils::EnqueueResourceUpload(text.font);
            text.SetDirty();
        }
        else
        {
            backlog::Post("[LoadResources] font load failed: " + text.font_asset_path, backlog::LogLevel::Warning);
        }
    }

    static String ResolveTextureBinaryPath(const String& content_root, const String& asset_path)
    {
        const String full_path = project::ResolveProjectContentPath(content_root, asset_path);
        AssetMeta meta = {};
        if (LoadAssetMeta(GetAssetMetaPath(full_path), meta) && !meta.binary_path.empty())
            return project::ResolveProjectContentPath(content_root, meta.binary_path);
        return full_path;
    }

    static void LoadReflectionProbeResource(ecs::ReflectionProbeComponent& probe, const String& content_root)
    {
        if (probe.cubemap_asset_path.empty())
            return;
        const String path = ResolveTextureBinaryPath(content_root, probe.cubemap_asset_path);
        std::shared_ptr<Image> image = LoadTextureBinary(path);
        if (image && image->IsValid())
        {
            probe.cubemap = image;
            rendering::utils::EnqueueResourceUpload(image, image->format);
        }
        else
        {
            backlog::Post("[LoadResources] cubemap load failed: " + path, backlog::LogLevel::Warning);
        }
    }

    static void LoadEnvironmentResource(ecs::EnvironmentComponent& environment, const String& content_root)
    {
        auto load_cube = [&content_root](const String& asset_path, std::shared_ptr<Image>& out_cube)
        {
            if (asset_path.empty())
                return;
            const String path = ResolveTextureBinaryPath(content_root, asset_path);
            std::shared_ptr<Image> image = LoadTextureBinary(path);
            if (image && image->IsValid())
            {
                out_cube = image;
                rendering::utils::EnqueueResourceUpload(image, image->format);
            }
            else
            {
                backlog::Post("[LoadResources] environment cubemap load failed: " + path, backlog::LogLevel::Warning);
            }
        };
        load_cube(environment.sky_cubemap_asset_path, environment.sky_cubemap);
        load_cube(environment.irradiance_cubemap_asset_path, environment.irradiance_cubemap);
        load_cube(environment.specular_cubemap_asset_path, environment.specular_cubemap);
    }

    static void LoadSoundResource(ecs::AudioSourceComponent& source, const String& content_root)
    {
        if (source.sound_asset_path.empty())
            return;
        source.sound = LoadSoundFile(project::ResolveProjectContentPath(content_root, source.sound_asset_path));
        if (source.sound)
            source.SetDirty();
        else
            backlog::Post("[LoadResources] sound load failed: " + source.sound_asset_path, backlog::LogLevel::Warning);
    }

    static void ResolveScriptPaths(ecs::ScriptComponent& script, const String& content_root)
    {
        for (ecs::ScriptSlot& script_slot : script.scripts)
            script_slot.script_path = project::ResolveProjectContentPath(content_root, script_slot.script_path);
    }

    static void BindAnimationClips(ecs::Scene& scene, ecs::Entity entity)
    {
        ecs::AnimationComponent* animation = scene.GetComponent<ecs::AnimationComponent>(entity);
        if (!animation || !animation->clips.empty())
        {
            return;
        }
        ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity);
        if (geometry && geometry->mesh && geometry->mesh->skeleton && geometry->mesh->skeleton->IsValid() && !geometry->mesh->animation_clips.empty())
        {
            animation->clips = geometry->mesh->animation_clips;
            animation->event_scan_time = animation->time;
        }
    }

    static void DispatchLoadJobs(bool parallel, jobsystem::Context& ctx, uint32 job_count, const jobsystem::job_function_type& task)
    {
        if (parallel)
        {
            jobsystem::Dispatch(ctx, job_count, 1, task);
            return;
        }
        for (uint32 i = 0; i < job_count; ++i)
        {
            jobsystem::JobArgs args = {};
            args.job_index = i;
            task(args);
        }
    }

    static bool SaveNavMeshBinary(const String& path, const nav::NavMesh& nav_mesh, uint64 source_hash)
    {
        const uint8* data = nav_mesh.GetData();
        const uint32 data_size = nav_mesh.GetDataSize();
        if (!data || data_size == 0)
        {
            return false;
        }

        const Size header_size = sizeof(uint32) * 2 + sizeof(uint64) + sizeof(uint32);
        Vector<uint8> bytes(header_size + data_size);
        uint8* cursor = bytes.data();
        const uint32 magic = navmesh_binary_magic;
        const uint32 version = navmesh_binary_version;
        std::memcpy(cursor, &magic, sizeof(magic)); cursor += sizeof(magic);
        std::memcpy(cursor, &version, sizeof(version)); cursor += sizeof(version);
        std::memcpy(cursor, &source_hash, sizeof(source_hash)); cursor += sizeof(source_hash);
        std::memcpy(cursor, &data_size, sizeof(data_size)); cursor += sizeof(data_size);
        std::memcpy(cursor, data, data_size);

        return io::WriteAllBytes(path, bytes.data(), bytes.size());
    }

    static bool LoadNavMeshBinary(const String& path, uint64 expected_source_hash, nav::NavMesh& out_nav_mesh)
    {
        io::FileData file = {};
        if (!io::ReadAllBytes(path, &file))
        {
            return false;
        }

        const Size header_size = sizeof(uint32) * 2 + sizeof(uint64) + sizeof(uint32);
        if (file.bytes.size() < header_size)
        {
            return false;
        }

        const uint8* cursor = file.bytes.data();
        uint32 magic = 0;
        uint32 version = 0;
        uint64 stored_hash = 0;
        uint32 data_size = 0;
        std::memcpy(&magic, cursor, sizeof(magic)); cursor += sizeof(magic);
        std::memcpy(&version, cursor, sizeof(version)); cursor += sizeof(version);
        std::memcpy(&stored_hash, cursor, sizeof(stored_hash)); cursor += sizeof(stored_hash);
        std::memcpy(&data_size, cursor, sizeof(data_size)); cursor += sizeof(data_size);

        if (magic != navmesh_binary_magic || version != navmesh_binary_version)
        {
            return false;
        }
        if (data_size == 0 || file.bytes.size() < header_size + data_size)
        {
            return false;
        }
        if (stored_hash != expected_source_hash)
        {
            backlog::Post("[NavMesh] asset stale (source changed), rebaking: " + path, backlog::LogLevel::Warning);
            return false;
        }

        return out_nav_mesh.InitFromData(cursor, data_size);
    }

    bool BuildSceneNavMesh(ecs::Scene& scene, const String& content_root)
    {
        auto nav_array = scene.GetComponentArray<ecs::NavMeshComponent>();
        if (!nav_array || nav_array->GetSize() == 0)
        {
            return false;
        }
        const ecs::NavMeshComponent& config = nav_array->data[0];

		ecs::TransformUpdateSystem transform_update_system; // we use this to ensure that all transforms are up-to-date before baking the navmesh
        transform_update_system.Update(scene, 0.0f);

        Vector<float3> positions;
        Vector<uint32> indices;

        if (auto collider_array = scene.GetComponentArray<ecs::Collider3DComponent>())
        {
            static const float3 unit_box_positions[8] = {
                { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f },
                { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { -1.0f, 1.0f, 1.0f },
            };
            static const uint32 unit_box_indices[36] = {
                0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
                0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
            };
            for (Size i = 0; i < collider_array->GetSize(); ++i)
            {
                const ecs::Entity entity = collider_array->index_to_entity[i];
                const ecs::Collider3DComponent& collider = collider_array->data[i];
                if (!collider.IsEnabled() || collider.IsTrigger())
                {
                    continue;
                }
                const ecs::Rigidbody3DComponent* rb = scene.GetComponent<ecs::Rigidbody3DComponent>(entity);
                if (rb && rb->motion_type != ecs::Rigidbody3DComponent::MotionType::Static)
                {
                    continue;
                }
                const ecs::CollisionLayerComponent* collision_layer = scene.GetComponent<ecs::CollisionLayerComponent>(entity);
                const uint32 layer = collision_layer ? collision_layer->layer : 0;
                if ((config.include_layers & (1u << layer)) == 0)
                {
                    continue;
                }
                ecs::TransformComponent* transform = scene.GetComponent<ecs::TransformComponent>(entity);
                if (!transform)
                {
                    continue;
                }

                const float3* src_positions = nullptr;
                Size src_position_count = 0;
                const uint32* src_indices = nullptr;
                Size src_index_count = 0;
                DirectX::XMMATRIX world = transform->GetWorldTransform();
                if (collider.shape_type == ecs::Collider3DComponent::ShapeType::HeightField)
                {
                    ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity);
                    if (!geometry || !geometry->mesh)
                    {
                        continue;
                    }
                    src_positions = geometry->mesh->positions.data();
                    src_position_count = geometry->mesh->positions.size();
                    src_indices = geometry->mesh->indices.data();
                    src_index_count = geometry->mesh->indices.size();
                }
                else if (collider.shape_type == ecs::Collider3DComponent::ShapeType::Sphere)
                {
					// approximated as a box for navmesh baking
                    world = DirectX::XMMatrixScaling(collider.radius, collider.radius, collider.radius) *
                        DirectX::XMMatrixTranslation(collider.offset.x, collider.offset.y, collider.offset.z) *
                        world;
                    src_positions = unit_box_positions;
                    src_position_count = 8;
                    src_indices = unit_box_indices;
                    src_index_count = 36;
                }
                else
                {
                    world = DirectX::XMMatrixScaling(collider.half_extent.x, collider.half_extent.y, collider.half_extent.z) *
                        DirectX::XMMatrixTranslation(collider.offset.x, collider.offset.y, collider.offset.z) *
                        world;
                    src_positions = unit_box_positions;
                    src_position_count = 8;
                    src_indices = unit_box_indices;
                    src_index_count = 36;
                }

                const uint32 base = static_cast<uint32>(positions.size());
                for (Size v = 0; v < src_position_count; ++v)
                {
                    float3 world_position = {};
                    DirectX::XMStoreFloat3(&world_position, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&src_positions[v]), world));
                    positions.push_back(world_position);
                }
                for (Size k = 0; k < src_index_count; ++k)
                {
                    indices.push_back(base + src_indices[k]);
                }
            }
        }

        if (positions.empty() || indices.size() < 3)
        {
            return false;
        }

        const float hash_params[] = {
            config.agent_radius, config.agent_height, config.agent_max_climb, config.agent_max_slope,
            config.use_bounds ? 1.0f : 0.0f,
            config.bounds_center.x, config.bounds_center.y, config.bounds_center.z,
            config.bounds_extent.x, config.bounds_extent.y, config.bounds_extent.z
        };
        const uint64 hash_parts[3] = {
            won::StableHash(reinterpret_cast<const char*>(positions.data()), positions.size() * sizeof(float3)),
            won::StableHash(reinterpret_cast<const char*>(indices.data()), indices.size() * sizeof(uint32)),
            won::StableHash(reinterpret_cast<const char*>(hash_params), sizeof(hash_params))
        };
        const uint64 source_hash = won::StableHash(reinterpret_cast<const char*>(hash_parts), sizeof(hash_parts));
        const bool has_asset = !config.navmesh_asset_path.empty();
        const String asset_full_path = has_asset ? project::ResolveProjectContentPath(content_root, config.navmesh_asset_path) : String();

        if (has_asset)
        {
            auto loaded = std::make_unique<nav::NavMesh>();
            if (LoadNavMeshBinary(asset_full_path, source_hash, *loaded))
            {
                backlog::Post("[NavMesh] loaded from asset: " + config.navmesh_asset_path);
                scene.SetNavMesh(std::move(loaded));
                return true;
            }
        }

        nav::NavMeshBuildDesc desc = {};
        desc.agent_radius = config.agent_radius;
        desc.agent_height = config.agent_height;
        desc.agent_max_climb = config.agent_max_climb;
        desc.agent_max_slope = config.agent_max_slope;
        desc.use_bounds = config.use_bounds;
        desc.bounds_min = { config.bounds_center.x - config.bounds_extent.x, config.bounds_center.y - config.bounds_extent.y, config.bounds_center.z - config.bounds_extent.z };
        desc.bounds_max = { config.bounds_center.x + config.bounds_extent.x, config.bounds_center.y + config.bounds_extent.y, config.bounds_center.z + config.bounds_extent.z };

        auto nav_mesh = std::make_unique<nav::NavMesh>();
		// TODO: parallelize navmesh building(per tile)
        if (!nav_mesh->Build(positions.data(), static_cast<uint32>(positions.size()), indices.data(), static_cast<uint32>(indices.size()), desc))
        {
            return false;
        }
        if (has_asset)
        {
            io::CreateDirectories(io::GetDirectoryFromPath(asset_full_path));
            if (SaveNavMeshBinary(asset_full_path, *nav_mesh, source_hash))
            {
                backlog::Post("[NavMesh] cached to asset: " + config.navmesh_asset_path);
            }
        }
        scene.SetNavMesh(std::move(nav_mesh));
        return true;
    }

    void LoadSceneResources(ecs::Scene& scene, const String& content_root, bool parallel)
    {
        jobsystem::Context ctx;
        jobsystem::Context mesh_ctx;

        BuildSceneNavMesh(scene, content_root);

        if (auto geometry_array = scene.GetComponentArray<ecs::GeometryComponent>())
        {
            DispatchLoadJobs(parallel, mesh_ctx, static_cast<uint32>(geometry_array->GetSize()), [geometry_array, &scene, &content_root](jobsystem::JobArgs args)
            {
                LoadMeshResource(geometry_array->data[args.job_index], content_root);
            });
        }

        if (auto material_array = scene.GetComponentArray<ecs::MaterialComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(material_array->GetSize()), [material_array, &content_root](jobsystem::JobArgs args)
            {
                ecs::MaterialComponent& material_comp = material_array->data[args.job_index];
                if (!material_comp.material_asset_path.empty())
                    material_comp.SetMaterial(LoadMaterialBinary(project::ResolveProjectContentPath(content_root, material_comp.material_asset_path)));
            });
            jobsystem::Wait(ctx);

            struct TextureJob { MaterialSlot::TextureMap* map; uint32 slot; };
            Vector<TextureJob> texture_jobs;
            UnorderedSet<Material*> seen_materials;
            for (Size i = 0; i < material_array->GetSize(); ++i)
            {
                Material* material = material_array->data[i].material.get();
                if (!material || !seen_materials.insert(material).second)
                    continue;
                for (MaterialSlot& material_slot : material->slots)
                    for (uint32 slot = 0; slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++slot)
                        if (!material_slot.textures[slot].texture_asset_path.empty())
                            texture_jobs.push_back({ &material_slot.textures[slot], slot });
            }
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(texture_jobs.size()), [&texture_jobs, &content_root](jobsystem::JobArgs args)
            {
                const TextureJob& texture_job = texture_jobs[args.job_index];
                LoadTextureMap(*texture_job.map, texture_job.slot, content_root);
            });
            jobsystem::Wait(ctx);
        }

        if (auto text2d_array = scene.GetComponentArray<ecs::Text2DComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(text2d_array->GetSize()), [text2d_array, &content_root](jobsystem::JobArgs args)
            {
                LoadFontResource(text2d_array->data[args.job_index], content_root);
            });
        }
        if (auto text3d_array = scene.GetComponentArray<ecs::Text3DComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(text3d_array->GetSize()), [text3d_array, &content_root](jobsystem::JobArgs args)
            {
                LoadFontResource(text3d_array->data[args.job_index], content_root);
            });
        }
        if (auto audio_array = scene.GetComponentArray<ecs::AudioSourceComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(audio_array->GetSize()), [audio_array, &content_root](jobsystem::JobArgs args)
            {
                LoadSoundResource(audio_array->data[args.job_index], content_root);
            });
        }
        if (auto reflection_probe_array = scene.GetComponentArray<ecs::ReflectionProbeComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(reflection_probe_array->GetSize()), [reflection_probe_array, &content_root](jobsystem::JobArgs args)
            {
                LoadReflectionProbeResource(reflection_probe_array->data[args.job_index], content_root);
            });
        }
        if (auto environment_array = scene.GetComponentArray<ecs::EnvironmentComponent>())
        {
            DispatchLoadJobs(parallel, ctx, static_cast<uint32>(environment_array->GetSize()), [environment_array, &content_root](jobsystem::JobArgs args)
            {
                LoadEnvironmentResource(environment_array->data[args.job_index], content_root);
            });
        }

        jobsystem::Wait(ctx);
        jobsystem::Wait(mesh_ctx);

        if (auto animation_array = scene.GetComponentArray<ecs::AnimationComponent>())
        {
            for (Size i = 0; i < animation_array->GetSize(); ++i)
            {
                BindAnimationClips(scene, animation_array->index_to_entity[i]);
            }
        }

        if (auto script_array = scene.GetComponentArray<ecs::ScriptComponent>())
        {
            for (Size i = 0; i < script_array->GetSize(); ++i)
                ResolveScriptPaths(script_array->data[i], content_root);
        }
    }

    void LoadEntityResources(ecs::Scene& scene, const String& content_root, const Vector<ecs::Entity>& entities)
    {
        UnorderedSet<Material*> seen_materials;
        for (ecs::Entity entity : entities)
        {
            if (ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity))
            {
                LoadMeshResource(*geometry, content_root);
            }

            BindAnimationClips(scene, entity);

            if (ecs::MaterialComponent* material_comp = scene.GetComponent<ecs::MaterialComponent>(entity))
            {
                if (!material_comp->material_asset_path.empty())
                    material_comp->SetMaterial(LoadMaterialBinary(project::ResolveProjectContentPath(content_root, material_comp->material_asset_path)));
                if (material_comp->material && seen_materials.insert(material_comp->material.get()).second)
                    LoadMaterialTextures(*material_comp->material, content_root);
            }

            if (ecs::Text2DComponent* text = scene.GetComponent<ecs::Text2DComponent>(entity))
                LoadFontResource(*text, content_root);
            if (ecs::Text3DComponent* text = scene.GetComponent<ecs::Text3DComponent>(entity))
                LoadFontResource(*text, content_root);
            if (ecs::AudioSourceComponent* audio = scene.GetComponent<ecs::AudioSourceComponent>(entity))
                LoadSoundResource(*audio, content_root);
            if (ecs::ScriptComponent* script = scene.GetComponent<ecs::ScriptComponent>(entity))
                ResolveScriptPaths(*script, content_root);
        }
    }
}
