#pragma once
#include "Image.h"
#include "MaterialComponent.h"
#include "Mesh.h"
#include "ResourceExtension.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::ecs { class Scene; }
namespace won::rendering { class RHIDevice; }

namespace won::resource
{
    inline constexpr uint32 asset_format_version = 2;

    struct TextureImportSettings
    {
        bool is_srgb = false;
        bool generate_mipmaps = true;
    };

    struct MeshImportSettings
    {
        float scale = 1.0f;
        bool import_animations = true;
        bool import_normals = true;
        bool import_tangents = true;
        bool import_skeleton = true;
    };

    struct AssetMeta
    {
        uint32 version = asset_format_version;
        String asset_id;
        String asset_name;
        String source_asset_path; // original fbx/png/etc
        String asset_type;
        String binary_path; // generated runtime-loadable file
        uint64 source_timestamp = 0;
        TextureImportSettings texture;
        MeshImportSettings mesh;
    };

    WONENGINE_API String GetAssetMetaPath(const String& source_path);
    WONENGINE_API bool LoadAssetMeta(const String& meta_path, AssetMeta& out_meta);
    WONENGINE_API bool SaveAssetMeta(const String& meta_path, const AssetMeta& meta);
    WONENGINE_API bool SaveMeshBinary(const String& path, const Mesh& mesh);
    WONENGINE_API std::shared_ptr<Mesh> LoadMeshBinary(const String& path);

    WONENGINE_API bool SaveTextureBinary(const String& path, uint32 width, uint32 height, uint32 mip_levels, rendering::RHIFormat format, const Vector<uint8>& pixels);
    WONENGINE_API std::shared_ptr<Image> LoadTextureBinary(const String& path);

    WONENGINE_API bool SaveMaterialBinary(const String& path, const Vector<ecs::MaterialSlot>& slots);
    WONENGINE_API bool LoadMaterialBinary(const String& path, Vector<ecs::MaterialSlot>& out_slots);

    WONENGINE_API void LoadSceneResources(ecs::Scene& scene, rendering::RHIDevice& device, const String& content_root);
}
