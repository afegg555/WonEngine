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
#include "meshoptimizer.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace won;

namespace
{
    void GenerateMeshLods(resource::Mesh& mesh, const Vector<float>& ratios, const Vector<float>& screen_sizes)
    {
        if (mesh.lods.empty() || mesh.positions.empty() || mesh.lods[0].indices.empty() || mesh.lods[0].submeshes.empty())
        {
            return;
        }
        mesh.lods.resize(1);
        mesh.lods.reserve(1 + ratios.size());
        const Vector<uint32>& base_indices = mesh.lods[0].indices;
        const Vector<resource::Submesh>& base_submeshes = mesh.lods[0].submeshes;
        for (Size level = 0; level < ratios.size(); ++level)
        {
            resource::Mesh::Lod lod;
            lod.screen_size_threshold = screen_sizes[level];
            for (const resource::Submesh& submesh : base_submeshes)
            {
                const unsigned int* source = base_indices.data() + submesh.first_index;
                const Size source_count = submesh.index_count;
                Size target = static_cast<Size>(source_count * ratios[level]);
                target -= target % 3;
                if (target < 3)
                {
                    target = source_count >= 3 ? 3 : 0;
                }
                Vector<unsigned int> destination(source_count);
                float error = 0.0f;
                const Size result = meshopt_simplify(destination.data(), source, source_count,
                    reinterpret_cast<const float*>(mesh.positions.data()), mesh.positions.size(), sizeof(float3),
                    target, 0.05f, 0, &error);

                resource::Submesh lod_submesh = submesh;
                lod_submesh.first_index = static_cast<uint32>(lod.indices.size());
                lod_submesh.index_count = static_cast<uint32>(result);
                lod.submeshes.push_back(lod_submesh);
                lod.indices.insert(lod.indices.end(), destination.begin(), destination.begin() + result);
            }
            mesh.lods.push_back(std::move(lod));
        }
    }

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
        std::cout << "Usage: MeshLodBakeTool <mesh.wonmesh> <material.wonmat> <output_dir> [content_root] [shaders_dir] [grid] [tile] [--full]\n";
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
        std::cout << "MeshLodBakeTool: failed to create RHI device\n";
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
        std::cout << "MeshLodBakeTool: failed to create renderer (check --shaders path: " << renderer_desc.shader_bin_root_path << ")\n";
        return 1;
    }

    std::shared_ptr<resource::Mesh> mesh = resource::LoadMeshBinary(mesh_path);
    if (!mesh)
    {
        std::cout << "MeshLodBakeTool: failed to load mesh " << mesh_path << "\n";
        return 1;
    }
    if (!rendering::utils::CreateRenderData(*device, *mesh))
    {
        std::cout << "MeshLodBakeTool: failed to create mesh render data\n";
        return 1;
    }

    if (!arguments.HasKey("--nolods"))
    {
        const Vector<float> lod_ratios = { 0.5f, 0.2f };
        const Vector<float> lod_screen_sizes = { 0.3f, 0.1f };
        GenerateMeshLods(*mesh, lod_ratios, lod_screen_sizes);
        std::cout << "MeshLodBakeTool: generated " << mesh->lods.size() << " mesh LODs\n";
    }

    std::shared_ptr<resource::Material> material = resource::LoadMaterialBinary(material_path);
    if (!material)
    {
        std::cout << "MeshLodBakeTool: failed to load material " << material_path << "\n";
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
                std::cout << "MeshLodBakeTool: warning, failed to load texture " << resolved << "\n";
                continue;
            }
            rendering::utils::CreateRenderData(*device, *image, image->format);
            texture.image = image;
        }
    }

    if (!rendering::utils::BakeImpostor(*device, *renderer, *mesh, *material, static_cast<uint32>(grid_size), static_cast<uint32>(tile_resolution), layout))
    {
        std::cout << "MeshLodBakeTool: bake failed\n";
        return 1;
    }

    if (arguments.HasKey("--write"))
    {
        if (resource::SaveMeshBinary(mesh_path, *mesh))
        {
            std::cout << "MeshLodBakeTool: wrote impostor into " << mesh_path << "\n";
        }
        else
        {
            std::cout << "MeshLodBakeTool: failed to write mesh " << mesh_path << "\n";
            return 1;
        }
    }

    io::CreateDirectories(output_directory);
    const bool saved_albedo = resource::SaveImageFile(*mesh->impostor.albedo, io::CombinePath(output_directory, "impostor_albedo.png"));
    const bool saved_normal = resource::SaveImageFile(*mesh->impostor.normal, io::CombinePath(output_directory, "impostor_normal.png"));
    const bool saved_depth = resource::SaveImageFile(*mesh->impostor.depth, io::CombinePath(output_directory, "impostor_depth.png"));
    if (!saved_albedo || !saved_normal || !saved_depth)
    {
        std::cout << "MeshLodBakeTool: failed to write one or more atlas images\n";
        return 1;
    }

    std::cout << "MeshLodBakeTool: baked " << grid_size << "x" << grid_size << " impostor (tile " << tile_resolution << ") to " << output_directory << "\n";
    return 0;
}
