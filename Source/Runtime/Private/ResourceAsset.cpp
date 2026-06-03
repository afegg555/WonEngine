#include "ResourceAsset.h"
#include "Animation.h"
#include "BinaryArchive.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include <cstring>
#include <dxgiformat.h>
#include <fstream>

namespace won::resource
{
    namespace
    {
        constexpr uint32 asset_meta_version = 1;
        constexpr uint32 mesh_binary_version = 1;
        constexpr uint32 mesh_binary_magic = 0x48534D57; // WMSH
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

        rendering::RHIFormat RHIFormatFromDXGIFormat(uint32 format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM: return rendering::RHIFormat::R8G8B8A8Unorm;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return rendering::RHIFormat::R8G8B8A8UnormSrgb;
            case DXGI_FORMAT_B8G8R8A8_UNORM: return rendering::RHIFormat::B8G8R8A8Unorm;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return rendering::RHIFormat::B8G8R8A8UnormSrgb;
            case DXGI_FORMAT_BC1_UNORM: return rendering::RHIFormat::BC1Unorm;
            case DXGI_FORMAT_BC1_UNORM_SRGB: return rendering::RHIFormat::BC1UnormSrgb;
            case DXGI_FORMAT_BC2_UNORM: return rendering::RHIFormat::BC2Unorm;
            case DXGI_FORMAT_BC2_UNORM_SRGB: return rendering::RHIFormat::BC2UnormSrgb;
            case DXGI_FORMAT_BC3_UNORM: return rendering::RHIFormat::BC3Unorm;
            case DXGI_FORMAT_BC3_UNORM_SRGB: return rendering::RHIFormat::BC3UnormSrgb;
            case DXGI_FORMAT_BC4_UNORM: return rendering::RHIFormat::BC4Unorm;
            case DXGI_FORMAT_BC4_SNORM: return rendering::RHIFormat::BC4Snorm;
            case DXGI_FORMAT_BC5_UNORM: return rendering::RHIFormat::BC5Unorm;
            case DXGI_FORMAT_BC5_SNORM: return rendering::RHIFormat::BC5Snorm;
            case DXGI_FORMAT_BC6H_UF16: return rendering::RHIFormat::BC6HUf16;
            case DXGI_FORMAT_BC6H_SF16: return rendering::RHIFormat::BC6HSf16;
            case DXGI_FORMAT_BC7_UNORM: return rendering::RHIFormat::BC7Unorm;
            case DXGI_FORMAT_BC7_UNORM_SRGB: return rendering::RHIFormat::BC7UnormSrgb;
            default: return rendering::RHIFormat::Unknown;
            }
        }

        uint32 DXGIFormatFromRHIFormat(rendering::RHIFormat format)
        {
            switch (format)
            {
            case rendering::RHIFormat::R8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case rendering::RHIFormat::R8G8B8A8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case rendering::RHIFormat::B8G8R8A8Unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case rendering::RHIFormat::B8G8R8A8UnormSrgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case rendering::RHIFormat::BC1Unorm: return DXGI_FORMAT_BC1_UNORM;
            case rendering::RHIFormat::BC1UnormSrgb: return DXGI_FORMAT_BC1_UNORM_SRGB;
            case rendering::RHIFormat::BC2Unorm: return DXGI_FORMAT_BC2_UNORM;
            case rendering::RHIFormat::BC2UnormSrgb: return DXGI_FORMAT_BC2_UNORM_SRGB;
            case rendering::RHIFormat::BC3Unorm: return DXGI_FORMAT_BC3_UNORM;
            case rendering::RHIFormat::BC3UnormSrgb: return DXGI_FORMAT_BC3_UNORM_SRGB;
            case rendering::RHIFormat::BC4Unorm: return DXGI_FORMAT_BC4_UNORM;
            case rendering::RHIFormat::BC4Snorm: return DXGI_FORMAT_BC4_SNORM;
            case rendering::RHIFormat::BC5Unorm: return DXGI_FORMAT_BC5_UNORM;
            case rendering::RHIFormat::BC5Snorm: return DXGI_FORMAT_BC5_SNORM;
            case rendering::RHIFormat::BC6HUf16: return DXGI_FORMAT_BC6H_UF16;
            case rendering::RHIFormat::BC6HSf16: return DXGI_FORMAT_BC6H_SF16;
            case rendering::RHIFormat::BC7Unorm: return DXGI_FORMAT_BC7_UNORM;
            case rendering::RHIFormat::BC7UnormSrgb: return DXGI_FORMAT_BC7_UNORM_SRGB;
            default: return DXGI_FORMAT_UNKNOWN;
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

        void SerializeAnimationClips(serialize::BinaryArchive& archive, Vector<std::shared_ptr<AnimationClip>>& clips)
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
            }
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
        archive.EndObject();
        return !archive.HasError() && out_meta.version == asset_meta_version;
    }

    bool SaveAssetMeta(const String& meta_path, const AssetMeta& meta)
    {
        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        uint32 version = asset_meta_version;
        String asset_id = meta.asset_id;
        String source_asset_path = meta.source_asset_path;
        String asset_type = meta.asset_type;
        String binary_path = meta.binary_path;
        uint64 source_timestamp = meta.source_timestamp;
        archive.Field("version", version);
        archive.Field("asset_id", asset_id);
        archive.Field("source_asset_path", source_asset_path);
        archive.Field("asset_type", asset_type);
        archive.Field("binary_path", binary_path);
        archive.Field("source_timestamp", source_timestamp);
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
        SerializeAnimationClips(archive, copy.animation_clips);
        return true;
    }

    std::shared_ptr<Mesh> LoadMeshBinary(const String& path)
    {
        if (path.empty() || !io::Exists(path))
        {
            return nullptr;
        }

        serialize::BinaryArchive archive(path, serialize::ArchiveMode::Read);
        uint32 magic = 0;
        uint32 version = 0;
        serialize::Serialize(archive, magic);
        serialize::Serialize(archive, version);
        if (magic != mesh_binary_magic || version != mesh_binary_version)
        {
            return nullptr;
        }

        auto mesh = std::make_shared<Mesh>();
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
        SerializeAnimationClips(archive, mesh->animation_clips);
        return mesh->IsValid() ? mesh : nullptr;
    }

    bool SaveTextureBinary(const String& path, uint32 width, uint32 height, uint32 mip_levels, rendering::RHIFormat format, const Vector<uint8>& pixels)
    {
        if (path.empty() || width == 0 || height == 0 || mip_levels == 0 || pixels.empty())
        {
            return false;
        }

        const uint32 dxgi_format = DXGIFormatFromRHIFormat(format);
        if (dxgi_format == DXGI_FORMAT_UNKNOWN)
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

        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return nullptr;
        }

        const std::streamoff file_size = stream.tellg();
        const Size header_size = sizeof(uint32) + sizeof(DDSHeader) + sizeof(DDSHeaderDXT10);
        if (file_size < static_cast<std::streamoff>(header_size))
        {
            return nullptr;
        }
        stream.seekg(0);

        uint32 magic = 0;
        DDSHeader header = {};
        DDSHeaderDXT10 header_dxt10 = {};
        stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        stream.read(reinterpret_cast<char*>(&header_dxt10), sizeof(header_dxt10));
        if (!stream || magic != dds_magic || header.size != sizeof(DDSHeader) || header.pixel_format.size != sizeof(DDSPixelFormat) || header.pixel_format.four_cc != dds_fourcc_dx10)
        {
            return nullptr;
        }
        if (header_dxt10.resource_dimension != dds_resource_dimension_texture2d || header_dxt10.array_size != 1)
        {
            return nullptr;
        }

        const rendering::RHIFormat format = RHIFormatFromDXGIFormat(header_dxt10.dxgi_format);
        if (format == rendering::RHIFormat::Unknown)
        {
            return nullptr;
        }

        const Size payload_size = static_cast<Size>(file_size) - header_size;
        if (payload_size == 0)
        {
            return nullptr;
        }

        auto image = std::make_shared<Image>();
        image->width = static_cast<int32>(header.width);
        image->height = static_cast<int32>(header.height);
        image->channels = 4;
        image->mip_levels = header.mip_map_count > 0 ? header.mip_map_count : 1;
        image->format = format;
        image->pixels.resize(payload_size);
        stream.read(reinterpret_cast<char*>(image->pixels.data()), static_cast<std::streamsize>(image->pixels.size()));
        if (!stream)
        {
            return nullptr;
        }
        return image->IsValid() ? image : nullptr;
    }
}
