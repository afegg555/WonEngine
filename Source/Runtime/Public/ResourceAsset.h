#pragma once
#include "Image.h"
#include "Mesh.h"
#include "ResourceExtension.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::resource
{
    struct AssetMeta
    {
        uint32 version = 1;
        String asset_id;
        String source_asset_path; // original fbx/png/etc
        String asset_type;
        String binary_path; // generated runtime-loadable file
        uint64 source_timestamp = 0;
    };

    WONENGINE_API String GetAssetMetaPath(const String& source_path);
    WONENGINE_API bool LoadAssetMeta(const String& meta_path, AssetMeta& out_meta);
    WONENGINE_API bool SaveAssetMeta(const String& meta_path, const AssetMeta& meta);

    WONENGINE_API bool SaveMeshBinary(const String& path, const Mesh& mesh);
    WONENGINE_API std::shared_ptr<Mesh> LoadMeshBinary(const String& path);

    WONENGINE_API bool SaveTextureBinary(const String& path, uint32 width, uint32 height, uint32 mip_levels, rendering::RHIFormat format, const Vector<uint8>& pixels);
    WONENGINE_API std::shared_ptr<Image> LoadTextureBinary(const String& path);
}
