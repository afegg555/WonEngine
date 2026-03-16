#pragma once
#include "IPlugin.h"
#include "Scene.h"
#include "RHIDevice.h"

#include <vector>

inline constexpr const char* WON_IID_ASSET_IMPORTER = "AssetImporter";
inline constexpr const char* WON_VID_ASSET_IMPORTER = "0.1.0";

namespace won::plugin
{
    // public APIs
    struct AssetImporterAPI
    {
        bool (*Import)(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in, ecs::Entity& root_entity_out);
    };
}
