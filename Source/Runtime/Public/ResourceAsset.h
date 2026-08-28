#pragma once
#include "Entity.h"
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

    struct TextureImportInfo
    {
        uint32 width = 0;
        uint32 height = 0;
        uint32 mip_levels = 0;
        bool is_cube = false;
        String format;
        uint64 source_hash = 0;
        String import_error;
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
        TextureImportInfo texture_info;
        MeshImportSettings mesh;
    };

    WONENGINE_API String GetAssetMetaPath(const String& source_path);
    WONENGINE_API bool LoadAssetMeta(const String& meta_path, AssetMeta& out_meta);
    WONENGINE_API bool SaveAssetMeta(const String& meta_path, const AssetMeta& meta);
    WONENGINE_API bool SaveMeshBinary(const String& path, const Mesh& mesh);
    WONENGINE_API std::shared_ptr<Mesh> LoadMeshBinary(const String& path);

    WONENGINE_API rendering::RHIFormat RHIFormatFromDXGIFormat(uint32 dxgi_format);
    WONENGINE_API bool SaveTextureBinary(const String& path, uint32 width, uint32 height, uint32 mip_levels, rendering::RHIFormat format, const Vector<uint8>& pixels);
    WONENGINE_API std::shared_ptr<Image> LoadTextureBinary(const String& path);

    WONENGINE_API bool SaveMaterialBinary(const String& path, const Vector<MaterialSlot>& slots);
    // Saves the material and registers this exact instance as the path's cache entry, so subsequent
    // loads of the same path share it (keeping its already-resolved GPU texture handles).
    WONENGINE_API bool SaveMaterialBinary(const String& path, const std::shared_ptr<Material>& material);
    WONENGINE_API std::shared_ptr<Material> LoadMaterialBinary(const String& path);

    WONENGINE_API void LoadSceneResources(ecs::Scene& scene, const String& content_root, bool parallel = true);
    WONENGINE_API bool BuildSceneNavMesh(ecs::Scene& scene, const String& content_root);
    WONENGINE_API void LoadEntityResources(ecs::Scene& scene, const String& content_root, const Vector<ecs::Entity>& entities);
}
