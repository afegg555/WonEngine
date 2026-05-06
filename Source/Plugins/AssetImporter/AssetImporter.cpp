#include "AssetImporter.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

#include "Backlog.h"
#include "MathUtils.h"
#include "StringUtils.h"
#include "FileSystem.h"
#include "SceneComponents.h"
#include "RenderingUtils.h"
#include "EventHandler.h"
#include "JobSystem.h"
#include "ResourceLoader.h"

#include <algorithm>

namespace won::plugin
{
    class AssetImporter : public IPlugin
    {
        struct ImportedTextureData
        {
            uint32 material_index = 0;
            uint32 texture_slot = 0;
            std::shared_ptr<resource::Image> image;
        };

        struct ImportedAssetData
        {
            String name;
            Vector<ecs::MaterialSlot> material_slots;
            Vector<ImportedTextureData> textures;
            std::shared_ptr<resource::Mesh> mesh;
        };

    public:
        virtual const char* GetName() const override { return WON_IID_ASSET_IMPORTER; }
        virtual const char* GetVersion() const override { return WON_VID_ASSET_IMPORTER; }

        virtual void* QueryInterface(const char* iid, const char* version_id) const override
        {
            if (std::strcmp(iid, WON_IID_ASSET_IMPORTER) == 0 && std::strcmp(version_id, WON_VID_ASSET_IMPORTER) == 0)
                return (void*)&s_api;
            return nullptr;
        }
        virtual bool Initialize() override
        {
            return true;
        }
        virtual void Shutdown() override
        {
            return;
        }

        bool Import(const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in, ecs::Entity& root_entity_out)
        {
            if (file_path_in == nullptr || file_path_in[0] == '\0' || target_scene_in == nullptr)
            {
                return false;
            }

            String file_path = file_path_in;
            ImportedAssetData imported_data;
            if (!ImportAssetData(file_path, imported_data))
            {
                return false;
            }

            ecs::Entity root_entity = target_scene_in->CreateEntity();
            target_scene_in->AddComponent<ecs::NameComponent>(root_entity)->value = imported_data.name;
            target_scene_in->AddComponent<ecs::TransformComponent>(root_entity);

            ecs::MaterialComponent* material_comp = target_scene_in->AddComponent<ecs::MaterialComponent>(root_entity);
            if (material_comp)
            {
                material_comp->material_slots = imported_data.material_slots;

                if (device_in)
                {
                    for (const ImportedTextureData& texture_data : imported_data.textures)
                    {
                        if (texture_data.material_index >= material_comp->material_slots.size() ||
                            texture_data.texture_slot >= TEXTURESLOT_COUNT ||
                            !texture_data.image ||
                            !texture_data.image->IsValid())
                        {
                            continue;
                        }

                        ecs::MaterialSlot::TextureMap& texture_map = material_comp->material_slots[texture_data.material_index].textures[texture_data.texture_slot];

                        RHITextureDesc texture_desc = {};
                        texture_desc.width = static_cast<uint32>(texture_data.image->width);
                        texture_desc.height = static_cast<uint32>(texture_data.image->height);
                        texture_desc.depth = 1;
                        texture_desc.mip_levels = 1;
                        texture_desc.array_layers = 1;
                        texture_desc.sample_count = 1;
                        texture_desc.format = (texture_data.texture_slot == BASECOLORMAP || texture_data.texture_slot == EMISSIVEMAP || texture_data.texture_slot == SHEENCOLORMAP)
                            ? RHIFormat::R8G8B8A8UnormSrgb
                            : RHIFormat::R8G8B8A8Unorm;
                        texture_desc.usage = RHIResourceUsage::Default;
                        texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;

                        uint32 mip_width = texture_desc.width;
                        uint32 mip_height = texture_desc.height;
                        while (mip_width > 1 || mip_height > 1)
                        {
                            mip_width = std::max(1u, mip_width / 2u);
                            mip_height = std::max(1u, mip_height / 2u);
                            ++texture_desc.mip_levels;
                        }

                        texture_map.texture = device_in->CreateTexture(texture_desc, texture_data.image->pixels.data(), texture_data.image->pixels.size());
                        if (!texture_map.texture)
                        {
                            continue;
                        }
                        rendering::utils::EnqueueTextureMipGeneration(texture_map.texture);

                        RHISubresourceDesc texture_srv_desc = {};
                        texture_srv_desc.type = RHISubresourceType::ShaderResource;
                        texture_srv_desc.first_slice = 0;
                        texture_srv_desc.slice_count = 1;
                        texture_srv_desc.first_mip = 0;
                        texture_srv_desc.mip_count = texture_desc.mip_levels;

                        device_in->CreateSubresource(*texture_map.texture, texture_srv_desc, &texture_map.res_handle);
                    }
                }
            }

            ecs::GeometryComponent* geometry_comp = target_scene_in->AddComponent<ecs::GeometryComponent>(root_entity);
            if (geometry_comp)
            {
                geometry_comp->SetMesh(imported_data.mesh);
            }
            if (device_in && imported_data.mesh)
            {
                rendering::utils::CreateRenderData(*device_in, *imported_data.mesh);
            }

            target_scene_in->SetBVHDirty();
            root_entity_out = root_entity;
            backlog::Post("AssetImporter::Import succeeded: " + file_path, backlog::LogLevel::Default);
            return true;
        }

        std::shared_ptr<AssetImportTask> ImportAsync(const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in)
        {
            auto task = std::make_shared<AssetImportTask>();
            if (file_path_in == nullptr || file_path_in[0] == '\0' || target_scene_in == nullptr)
            {
                task->failed.store(true);
                task->finished.store(true);
                return task;
            }

            String file_path = file_path_in;
            auto job_context = std::make_shared<jobsystem::Context>();
            job_context->priority = jobsystem::Priority::Low;
            jobsystem::Execute(*job_context, [file_path, target_scene_in, device_in, task, job_context](jobsystem::JobArgs args)
            {
                (void)args;
                (void)job_context;

                if (jobsystem::IsShuttingDown())
                {
                    task->failed.store(true);
                    task->finished.store(true);
                    return;
                }

                auto imported_data = std::make_shared<ImportedAssetData>();
                if (!ImportAssetData(file_path, *imported_data))
                {
                    task->failed.store(true);
                    task->finished.store(true);
                    return;
                }

                eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [task, imported_data, target_scene_in, device_in, file_path](uint64 userdata)
                {
                    (void)userdata;

                    if (jobsystem::IsShuttingDown() || target_scene_in == nullptr)
                    {
                        task->failed.store(true);
                        task->finished.store(true);
                        return;
                    }

                    ecs::Entity root_entity = target_scene_in->CreateEntity();
                    target_scene_in->AddComponent<ecs::NameComponent>(root_entity)->value = imported_data->name;
                    target_scene_in->AddComponent<ecs::TransformComponent>(root_entity);

                    ecs::MaterialComponent* material_comp = target_scene_in->AddComponent<ecs::MaterialComponent>(root_entity);
                    if (material_comp)
                    {
                        material_comp->material_slots = imported_data->material_slots;

                        if (device_in)
                        {
                            for (const ImportedTextureData& texture_data : imported_data->textures)
                            {
                                if (texture_data.material_index >= material_comp->material_slots.size() ||
                                    texture_data.texture_slot >= TEXTURESLOT_COUNT ||
                                    !texture_data.image ||
                                    !texture_data.image->IsValid())
                                {
                                    continue;
                                }

                                ecs::MaterialSlot::TextureMap& texture_map = material_comp->material_slots[texture_data.material_index].textures[texture_data.texture_slot];

                                RHITextureDesc texture_desc = {};
                                texture_desc.width = static_cast<uint32>(texture_data.image->width);
                                texture_desc.height = static_cast<uint32>(texture_data.image->height);
                                texture_desc.depth = 1;
                                texture_desc.mip_levels = 1;
                                texture_desc.array_layers = 1;
                                texture_desc.sample_count = 1;
                                texture_desc.format = (texture_data.texture_slot == BASECOLORMAP || texture_data.texture_slot == EMISSIVEMAP || texture_data.texture_slot == SHEENCOLORMAP)
                                    ? RHIFormat::R8G8B8A8UnormSrgb
                                    : RHIFormat::R8G8B8A8Unorm;
                                texture_desc.usage = RHIResourceUsage::Default;
                                texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;

                                uint32 mip_width = texture_desc.width;
                                uint32 mip_height = texture_desc.height;
                                while (mip_width > 1 || mip_height > 1)
                                {
                                    mip_width = std::max(1u, mip_width / 2u);
                                    mip_height = std::max(1u, mip_height / 2u);
                                    ++texture_desc.mip_levels;
                                }

                                texture_map.texture = device_in->CreateTexture(texture_desc, texture_data.image->pixels.data(), texture_data.image->pixels.size());
                                if (!texture_map.texture)
                                {
                                    continue;
                                }
                                rendering::utils::EnqueueTextureMipGeneration(texture_map.texture);

                                RHISubresourceDesc texture_srv_desc = {};
                                texture_srv_desc.type = RHISubresourceType::ShaderResource;
                                texture_srv_desc.first_slice = 0;
                                texture_srv_desc.slice_count = 1;
                                texture_srv_desc.first_mip = 0;
                                texture_srv_desc.mip_count = texture_desc.mip_levels;

                                device_in->CreateSubresource(*texture_map.texture, texture_srv_desc, &texture_map.res_handle);
                            }
                        }
                    }

                    ecs::GeometryComponent* geometry_comp = target_scene_in->AddComponent<ecs::GeometryComponent>(root_entity);
                    if (geometry_comp)
                    {
                        geometry_comp->SetMesh(imported_data->mesh);
                    }
                    if (device_in && imported_data->mesh)
                    {
                        rendering::utils::CreateRenderData(*device_in, *imported_data->mesh);
                    }

                    target_scene_in->SetBVHDirty();
                    task->root_entity.store(root_entity);
                    task->committed.store(true);
                    task->finished.store(true);

                    backlog::Post("AssetImporter::ImportAsync committed: " + file_path, backlog::LogLevel::Default);
                });
            });

            return task;
        }
    private:
        static bool ImportAssetData(const String& file_path, ImportedAssetData& imported_data)
        {
            String ext = io::GetExtension(file_path);
            String dir = io::GetDirectoryFromPath(file_path);
            imported_data.name = io::GetFilename(file_path);
            if (ext == "obj" || ext == "gltf")
            {

            }
            else
            {
                backlog::Post("AssetImporter::ImportAssetData : format(" + ext + ") not supported", backlog::LogLevel::Warning);
                return false;
            }

            Assimp::Importer importer;

            const unsigned flags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace |
                aiProcess_ImproveCacheLocality |
                aiProcess_OptimizeMeshes |
                aiProcess_OptimizeGraph |
                aiProcess_MakeLeftHanded | // LHS
                aiProcess_FlipUVs | // upper left origin
                aiProcess_FlipWindingOrder; // use CW order

            const aiScene* aiscene = importer.ReadFile(file_path, flags);
            if (!aiscene || !aiscene->mRootNode)
            {
                backlog::Post("AssetImporter::ImportAssetData failed: " + file_path, backlog::LogLevel::Warning);
                return false;
            }

            imported_data.material_slots.clear();
            imported_data.textures.clear();
            imported_data.mesh = nullptr;
            imported_data.material_slots.reserve(aiscene->mNumMaterials);

            // TODO : check slot
            for (uint32_t i = 0; i < aiscene->mNumMaterials; ++i)
            {
                const aiMaterial* ai_mat = aiscene->mMaterials[i];
                const uint32 material_index = static_cast<uint32>(imported_data.material_slots.size());
                ecs::MaterialSlot& material_slot = imported_data.material_slots.emplace_back();
                aiColor4D c;
                float v = 0.f;

                // Metallic/Roughness Workflow
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_COLOR_DIFFUSE, &c) ||
                    aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_BASE_COLOR, &c))
                {
                    material_slot.base_color = { c.r, c.g, c.b, c.a };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_METALLIC_FACTOR, &v))
                {
                    material_slot.metallic = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ROUGHNESS_FACTOR, &v))
                {
                    material_slot.roughness = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ANISOTROPY_FACTOR, &v))
                {
                    material_slot.anisotropy = v;
                }

                // sheen
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_SHEEN_COLOR_FACTOR, &c))
                {
                    material_slot.sheen_color = { c.r, c.g, c.b };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, &v))
                {
                    material_slot.sheen_roughness = v;
                }

                // clearcoat
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_FACTOR, &v))
                {
                    material_slot.clearcoat = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, &v))
                {
                    material_slot.clearcoat_roughness = v;
                }

                aiString tex;
                if (ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == aiReturn_SUCCESS ||
                    ai_mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) == aiReturn_SUCCESS)
                {
                    material_slot.textures[BASECOLORMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_NORMALS, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[NORMALMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_EMISSIVE, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[EMISSIVEMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_OPACITY, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[OPACITYMAP].name = tex.C_Str();
                }

                if (ai_mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[DISPLACEMENTMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[OCCLUSIONMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_SHEEN, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[SHEENCOLORMAP].name = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material_slot.textures[SHEENROUGHNESSMAP].name = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_CLEARCOAT, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[CLEARCOATMAP].name = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material_slot.textures[CLEARCOATROUGHNESSMAP].name = tex.C_Str();
                //}
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material_slot.textures[CLEARCOATNORMALMAP].name = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_ANISOTROPY, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[ANISOTROPYMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[ROUGHNESSMAP].name = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_METALNESS, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[METALLICMAP].name = tex.C_Str();
                }

                for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
                {
                    ecs::MaterialSlot::TextureMap& texture_map = material_slot.textures[texture_slot];
                    if (texture_map.name.empty())
                    {
                        continue;
                    }

                    texture_map.name = dir + "/" + texture_map.name;
                    std::shared_ptr<resource::Image> image = resource::LoadImageFile(texture_map.name, 4);
                    if (!image || !image->IsValid())
                    {
                        continue;
                    }

                    ImportedTextureData texture_data = {};
                    texture_data.material_index = material_index;
                    texture_data.texture_slot = texture_slot;
                    texture_data.image = image;
                    imported_data.textures.push_back(texture_data);
                }
            }

            if (imported_data.material_slots.empty())
            {
                // default fallback
                imported_data.material_slots.emplace_back();
            }

            imported_data.mesh = std::make_shared<resource::Mesh>();
            resource::Mesh& mesh = *imported_data.mesh;
            bool import_failed = false;
            struct NodeImportEntry
            {
                const aiNode* node = nullptr;
                aiMatrix4x4 parent_transform;
            };

            Vector<NodeImportEntry> node_stack;
            node_stack.push_back({ aiscene->mRootNode, aiMatrix4x4() });

            while (!node_stack.empty() && !import_failed)
            {
                const NodeImportEntry entry = node_stack.back();
                node_stack.pop_back();

                const aiNode* node = entry.node;
                const aiMatrix4x4 node_transform = entry.parent_transform * node->mTransformation;
                aiMatrix3x3 tangent_matrix(node_transform);
                aiMatrix3x3 normal_matrix(node_transform);
                normal_matrix.Inverse().Transpose();

                for (uint32_t node_mesh_index = 0; node_mesh_index < node->mNumMeshes; ++node_mesh_index)
                {
                    const aiMesh* ai_mesh = aiscene->mMeshes[node->mMeshes[node_mesh_index]];
                    const bool has_uv = ai_mesh->HasTextureCoords(0);
                    const bool has_tb = ai_mesh->HasTangentsAndBitangents();
                    const uint32 vertex_offset = static_cast<uint32>(mesh.positions.size());
                    const uint32 index_offset = static_cast<uint32>(mesh.indices.size());
                    resource::Submesh& submesh = mesh.submeshes.emplace_back();
                    submesh.local_bounds.Invalidate();
                    submesh.first_vertex = vertex_offset;
                    submesh.first_index = index_offset;
                    submesh.material_slot = ai_mesh->mMaterialIndex < imported_data.material_slots.size() ? ai_mesh->mMaterialIndex : 0;

                    if (!(ai_mesh->HasPositions() && ai_mesh->HasNormals()))
                    {
                        import_failed = true;
                        break;
                    }

                    mesh.positions.reserve(mesh.positions.size() + ai_mesh->mNumVertices);
                    mesh.normals.reserve(mesh.normals.size() + ai_mesh->mNumVertices);

                    if (has_uv)
                    {
                        mesh.texcoords.reserve(mesh.texcoords.size() + ai_mesh->mNumVertices);
                    }
                    if (has_tb)
                    {
                        mesh.tangents.reserve(mesh.tangents.size() + ai_mesh->mNumVertices);
                    }

                    for (uint32_t vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index)
                    {
                        const aiVector3D transformed_position = node_transform * ai_mesh->mVertices[vertex_index];
                        aiVector3D transformed_normal = normal_matrix * ai_mesh->mNormals[vertex_index];
                        transformed_normal.NormalizeSafe();

                        const float3 position = { transformed_position.x, transformed_position.y, transformed_position.z };
                        mesh.positions.push_back(position);
                        mesh.normals.push_back({ transformed_normal.x, transformed_normal.y, transformed_normal.z });

                        if (has_uv)
                        {
                            mesh.texcoords.push_back({ ai_mesh->mTextureCoords[0][vertex_index].x, ai_mesh->mTextureCoords[0][vertex_index].y });
                        }
                        if (has_tb)
                        {
                            aiVector3D transformed_tangent = tangent_matrix * ai_mesh->mTangents[vertex_index];
                            aiVector3D transformed_bitangent = tangent_matrix * ai_mesh->mBitangents[vertex_index];
                            transformed_tangent.NormalizeSafe();
                            transformed_bitangent.NormalizeSafe();

                            aiVector3D computed_bitangent = aiVector3D(
                                transformed_tangent.y * transformed_normal.z - transformed_tangent.z * transformed_normal.y,
                                transformed_tangent.z * transformed_normal.x - transformed_tangent.x * transformed_normal.z,
                                transformed_tangent.x * transformed_normal.y - transformed_tangent.y * transformed_normal.x
                            );
                            const float tangent_sign =
                                (computed_bitangent.x * transformed_bitangent.x +
                                 computed_bitangent.y * transformed_bitangent.y +
                                 computed_bitangent.z * transformed_bitangent.z) < 0.f ? -1.f : 1.f;

                            mesh.tangents.push_back({ transformed_tangent.x, transformed_tangent.y, transformed_tangent.z, tangent_sign });
                        }

                        submesh.local_bounds.min.x = std::min(submesh.local_bounds.min.x, position.x);
                        submesh.local_bounds.min.y = std::min(submesh.local_bounds.min.y, position.y);
                        submesh.local_bounds.min.z = std::min(submesh.local_bounds.min.z, position.z);
                        submesh.local_bounds.max.x = std::max(submesh.local_bounds.max.x, position.x);
                        submesh.local_bounds.max.y = std::max(submesh.local_bounds.max.y, position.y);
                        submesh.local_bounds.max.z = std::max(submesh.local_bounds.max.z, position.z);
                    }

                    if (ai_mesh->HasFaces())
                    {
                        mesh.indices.reserve(mesh.indices.size() + ai_mesh->mNumFaces * 3);

                        for (uint32_t face_index = 0; face_index < ai_mesh->mNumFaces; ++face_index)
                        {
                            const aiFace& face = ai_mesh->mFaces[face_index];
                            for (uint32_t index_index = 0; index_index < face.mNumIndices; ++index_index)
                            {
                                mesh.indices.push_back(vertex_offset + face.mIndices[index_index]);
                            }
                        }
                    }

                    submesh.index_count = static_cast<uint32>(mesh.indices.size()) - index_offset;
                }

                for (uint32_t child_index = 0; child_index < node->mNumChildren; ++child_index)
                {
                    node_stack.push_back({ node->mChildren[child_index], node_transform });
                }
            }

            if (import_failed || !mesh.IsValid())
            {
                backlog::Post("AssetImporter::ImportAssetData failed to build mesh: " + file_path, backlog::LogLevel::Warning);
                return false;
            }

            return true;
        }

        static bool ImportThunk(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in, ecs::Entity& root_entity_out)
        {
            return static_cast<AssetImporter*>(self)->Import(file_path_in, target_scene_in, device_in, root_entity_out);
        }

        static std::shared_ptr<AssetImportTask> ImportAsyncThunk(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in)
        {
            return static_cast<AssetImporter*>(self)->ImportAsync(file_path_in, target_scene_in, device_in);
        }

        inline static AssetImporterAPI s_api{
            &ImportThunk,
            &ImportAsyncThunk
        };
    };

    IMPLEMENT_PLUGIN(AssetImporter, PAssetImporter);
}
