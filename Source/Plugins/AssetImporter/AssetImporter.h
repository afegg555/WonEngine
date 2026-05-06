#pragma once
#include "IPlugin.h"
#include "Scene.h"
#include "RHIDevice.h"

#include <atomic>
#include <memory>
#include <vector>

inline constexpr const char* WON_IID_ASSET_IMPORTER = "AssetImporter";
inline constexpr const char* WON_VID_ASSET_IMPORTER = "0.2.0";

namespace won::plugin
{
    // public APIs
    struct AssetImportTask
    {
        std::atomic_bool finished{ false };
        std::atomic_bool committed{ false };
        std::atomic_bool failed{ false };
        std::atomic<ecs::Entity> root_entity{ ecs::INVALID_ENTITY };
    };

    struct AssetImporterAPI
    {
        bool (*Import)(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in, ecs::Entity& root_entity_out);
        std::shared_ptr<AssetImportTask> (*ImportAsync)(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in);
    };
}
