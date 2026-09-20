#include "RHIDevice.h"
#include "Renderer.h"
#include "RenderingUtils.h"
#include "ResourceAsset.h"
#include "Mesh.h"
#include "Material.h"
#include "Image.h"
#include "Impostor.h"
#include "Configuration.h"
#include "FileSystem.h"
#include "JobSystem.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace won;

namespace
{
    String ResolveTexturePath(const String& content_root, const String& asset_path)
    {
        if (asset_path.empty() || io::Exists(asset_path))
        {
            return asset_path;
        }
        if (!content_root.empty())
        {
            const String combined = io::CombinePath(content_root, asset_path);
            if (io::Exists(combined))
            {
                return combined;
            }
        }
        return asset_path;
    }
}

int main(int argc, char** argv)
{
    config::Configuration arguments;
    arguments.LoadFromCommandLine(argc, argv);

    const char* mesh_path = arguments.GetString("0");
    const char* material_path = arguments.GetString("1");
    const char* output_directory = arguments.GetString("2");
    if (mesh_path == nullptr || material_path == nullptr || output_directory == nullptr)
    {
        std::cout << "Usage: ImpostorBakeTool <mesh.wonmesh> <material.wonmat> <output_dir> [content_root] [shaders_dir] [grid] [tile] [--full]\n";
        return 1;
    }

    const char* content_arg = arguments.GetString("3");
    const char* shaders_arg = arguments.GetString("4");
    const char* grid_arg = arguments.GetString("5");
    const char* tile_arg = arguments.GetString("6");
    const int grid_size = grid_arg != nullptr ? std::atoi(grid_arg) : 12;
    const int tile_resolution = tile_arg != nullptr ? std::atoi(tile_arg) : 128;
    const impostor::ImpostorLayout layout = arguments.HasKey("--full")
        ? impostor::ImpostorLayout::Full
        : impostor::ImpostorLayout::Hemi;

    jobsystem::Initialize();

    rendering::RHIDeviceDesc device_desc;
    device_desc.enable_debug_layer = true;
    std::unique_ptr<rendering::RHIDevice> device = rendering::CreateRHIDevice(device_desc);
    if (!device)
    {
        std::cout << "ImpostorBakeTool: failed to create RHI device\n";
        return 1;
    }

    rendering::RendererDesc renderer_desc;
    renderer_desc.device = device.get();
    renderer_desc.shader_bin_root_path = shaders_arg != nullptr
        ? io::NormalizePath(shaders_arg)
        : io::NormalizePath(io::CombinePath(io::GetExecutableDirectory(), "CompiledShaders"));
    std::unique_ptr<rendering::Renderer> renderer = rendering::CreateRenderer(renderer_desc);
    if (!renderer)
    {
        std::cout << "ImpostorBakeTool: failed to create renderer (check --shaders path: " << renderer_desc.shader_bin_root_path << ")\n";
        return 1;
    }

    std::shared_ptr<resource::Mesh> mesh = resource::LoadMeshBinary(mesh_path);
    if (!mesh)
    {
        std::cout << "ImpostorBakeTool: failed to load mesh " << mesh_path << "\n";
        return 1;
    }
    if (!rendering::utils::CreateRenderData(*device, *mesh))
    {
        std::cout << "ImpostorBakeTool: failed to create mesh render data\n";
        return 1;
    }

    std::shared_ptr<resource::Material> material = resource::LoadMaterialBinary(material_path);
    if (!material)
    {
        std::cout << "ImpostorBakeTool: failed to load material " << material_path << "\n";
        return 1;
    }

    const String content_root = content_arg != nullptr ? String(content_arg) : String();
    for (resource::MaterialSlot& slot : material->slots)
    {
        for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
        {
            resource::MaterialTextureMap& texture = slot.attributes.textures[texture_slot];
            if (texture.texture_asset_path.empty())
            {
                continue;
            }
            const String resolved = ResolveTexturePath(content_root, texture.texture_asset_path);
            std::shared_ptr<resource::Image> image = resource::LoadTextureBinary(resolved);
            if (!image)
            {
                std::cout << "ImpostorBakeTool: warning, failed to load texture " << resolved << "\n";
                continue;
            }
            rendering::utils::CreateRenderData(*device, *image, image->format);
            texture.image = image;
        }
    }

    if (!rendering::utils::BakeImpostor(*device, *renderer, *mesh, *material, static_cast<uint32>(grid_size), static_cast<uint32>(tile_resolution), layout))
    {
        std::cout << "ImpostorBakeTool: bake failed\n";
        return 1;
    }

    io::CreateDirectories(output_directory);
    const bool saved_albedo = resource::SaveImageFile(*mesh->impostor.albedo, io::CombinePath(output_directory, "impostor_albedo.png"));
    const bool saved_normal = resource::SaveImageFile(*mesh->impostor.normal, io::CombinePath(output_directory, "impostor_normal.png"));
    const bool saved_depth = resource::SaveImageFile(*mesh->impostor.depth, io::CombinePath(output_directory, "impostor_depth.png"));
    if (!saved_albedo || !saved_normal || !saved_depth)
    {
        std::cout << "ImpostorBakeTool: failed to write one or more atlas images\n";
        return 1;
    }

    std::cout << "ImpostorBakeTool: baked " << grid_size << "x" << grid_size << " impostor (tile " << tile_resolution << ") to " << output_directory << "\n";
    return 0;
}
