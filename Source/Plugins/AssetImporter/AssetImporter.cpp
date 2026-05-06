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
#include "Animation.h"
#include "Image.h"

#include <algorithm>
#include <mutex>

namespace won::plugin
{
    class AssetImporter : public IPlugin
    {
        struct ImportedTextureData
        {
            uint32 material_index = 0;
            uint32 texture_slot = 0;
            std::shared_ptr<resource::Image> image;
            std::weak_ptr<RHIResource> texture;
            RHISubresourceHandle res_handle = {};
        };

        struct ImportedAssetData
        {
            String name;
            String cache_key;
            uint64 timestamp = 0;
            Vector<ecs::MaterialSlot> material_slots;
            Vector<ImportedTextureData> textures;
            std::shared_ptr<resource::Mesh> mesh;
            std::weak_ptr<resource::Mesh> cached_mesh;
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
            if (!ImportAssetData(file_path, device_in, imported_data))
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
            if (device_in && imported_data.mesh && !imported_data.mesh->render_data.IsValid())
            {
                rendering::utils::CreateRenderData(*device_in, *imported_data.mesh);
            }
            StoreCachedAssetData(imported_data, material_comp);

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
                if (!ImportAssetData(file_path, device_in, *imported_data))
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
                    if (device_in && imported_data->mesh && !imported_data->mesh->render_data.IsValid())
                    {
                        rendering::utils::CreateRenderData(*device_in, *imported_data->mesh);
                    }
                    StoreCachedAssetData(*imported_data, material_comp);

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
        inline static std::mutex s_asset_cache_mutex;
        inline static UnorderedMap<String, ImportedAssetData> s_asset_cache;

        static String MakeAssetCacheKey(const String& file_path, RHIDevice* device_in)
        {
            String normalized_path = file_path;
            std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
#if defined(_WIN32)
            normalized_path = utils::ToLower(normalized_path);
#endif
            return normalized_path + "|" + std::to_string(reinterpret_cast<uintptr_t>(device_in));
        }

        static bool LoadCachedAssetData(const String& cache_key, uint64 timestamp, ImportedAssetData& imported_data)
        {
            std::lock_guard<std::mutex> lock(s_asset_cache_mutex);
            auto it = s_asset_cache.find(cache_key);
            if (it == s_asset_cache.end())
            {
                return false;
            }

            ImportedAssetData& cached = it->second;
            if (cached.timestamp != timestamp)
            {
                s_asset_cache.erase(it);
                return false;
            }

            std::shared_ptr<resource::Mesh> mesh = cached.cached_mesh.lock();
            if (!mesh || !mesh->IsValid())
            {
                s_asset_cache.erase(it);
                return false;
            }

            ImportedAssetData cached_data = {};
            cached_data.name = cached.name;
            cached_data.cache_key = cache_key;
            cached_data.timestamp = timestamp;
            cached_data.material_slots = cached.material_slots;
            cached_data.mesh = mesh;

            for (const ImportedTextureData& cached_texture : cached.textures)
            {
                if (cached_texture.material_index >= cached_data.material_slots.size() || cached_texture.texture_slot >= TEXTURESLOT_COUNT)
                {
                    s_asset_cache.erase(it);
                    return false;
                }

                std::shared_ptr<RHIResource> texture = cached_texture.texture.lock();
                if (!texture || !cached_texture.res_handle.IsValid())
                {
                    s_asset_cache.erase(it);
                    return false;
                }

                ecs::MaterialSlot::TextureMap& texture_map = cached_data.material_slots[cached_texture.material_index].textures[cached_texture.texture_slot];
                texture_map.texture = texture;
                texture_map.res_handle = cached_texture.res_handle;
            }

            imported_data = cached_data;
            return true;
        }

        static void StoreCachedAssetData(const ImportedAssetData& imported_data, const ecs::MaterialComponent* material_comp)
        {
            if (imported_data.cache_key.empty() || imported_data.timestamp == 0 || !imported_data.mesh)
            {
                return;
            }

            ImportedAssetData cached = {};
            cached.name = imported_data.name;
            cached.cache_key = imported_data.cache_key;
            cached.timestamp = imported_data.timestamp;
            cached.mesh = nullptr;
            cached.cached_mesh = imported_data.mesh;
            cached.material_slots = material_comp ? material_comp->material_slots : imported_data.material_slots;

            for (uint32 material_index = 0; material_index < cached.material_slots.size(); ++material_index)
            {
                ecs::MaterialSlot& material_slot = cached.material_slots[material_index];
                for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
                {
                    ecs::MaterialSlot::TextureMap& texture_map = material_slot.textures[texture_slot];
                    if (texture_map.texture && texture_map.res_handle.IsValid())
                    {
                        ImportedTextureData cached_texture = {};
                        cached_texture.material_index = material_index;
                        cached_texture.texture_slot = texture_slot;
                        cached_texture.texture = texture_map.texture;
                        cached_texture.res_handle = texture_map.res_handle;
                        cached.textures.push_back(cached_texture);
                    }

                    texture_map.texture = nullptr;
                    texture_map.res_handle = {};
                }
            }

            std::lock_guard<std::mutex> lock(s_asset_cache_mutex);
            s_asset_cache[imported_data.cache_key] = cached;
        }

        static bool ImportAssetData(const String& file_path, RHIDevice* device_in, ImportedAssetData& imported_data)
        {
            imported_data = {};
            uint64 timestamp = 0;
            if (io::GetLastTimestamp(file_path, &timestamp))
            {
                const String cache_key = MakeAssetCacheKey(file_path, device_in);
                if (LoadCachedAssetData(cache_key, timestamp, imported_data))
                {
                    backlog::Post("AssetImporter cache hit: " + file_path, backlog::LogLevel::Default);
                    return true;
                }
                imported_data.cache_key = cache_key;
                imported_data.timestamp = timestamp;
            }

            String ext = utils::ToLower(io::GetExtension(file_path));
            String dir = io::GetDirectoryFromPath(file_path);
            imported_data.name = io::GetFilename(file_path);
            if (ext == "obj" || ext == "gltf" || ext == "glb")
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

            auto to_float4x4 = [](const aiMatrix4x4& matrix) -> float4x4
            {
                return float4x4(
                    matrix.a1, matrix.a2, matrix.a3, matrix.a4,
                    matrix.b1, matrix.b2, matrix.b3, matrix.b4,
                    matrix.c1, matrix.c2, matrix.c3, matrix.c4,
                    matrix.d1, matrix.d2, matrix.d3, matrix.d4);
            };

            // collect name of nodes that should be in skeleton
            UnorderedMap<String, const aiNode*> nodes_by_name;
            Vector<const aiNode*> node_stack_for_names;
            node_stack_for_names.push_back(aiscene->mRootNode);
            while (!node_stack_for_names.empty())
            {
                const aiNode* node = node_stack_for_names.back();
                node_stack_for_names.pop_back();
                nodes_by_name[node->mName.C_Str()] = node;
                for (uint32 child_index = 0; child_index < node->mNumChildren; ++child_index)
                {
                    node_stack_for_names.push_back(node->mChildren[child_index]);
                }
            }

            UnorderedMap<String, aiMatrix4x4> inverse_bind_matrices;
            UnorderedMap<String, bool> skeleton_node_names;
            for (uint32 mesh_index = 0; mesh_index < aiscene->mNumMeshes; ++mesh_index)
            {
                const aiMesh* ai_mesh = aiscene->mMeshes[mesh_index];
                if (!ai_mesh || !ai_mesh->HasBones())
                {
                    continue;
                }

                for (uint32 bone_index = 0; bone_index < ai_mesh->mNumBones; ++bone_index)
                {
                    const aiBone* ai_bone = ai_mesh->mBones[bone_index];
                    if (!ai_bone)
                    {
                        continue;
                    }

                    const String bone_name = ai_bone->mName.C_Str();
                    inverse_bind_matrices[bone_name] = ai_bone->mOffsetMatrix;

                    auto node_it = nodes_by_name.find(bone_name);
                    if (node_it == nodes_by_name.end())
                    {
                        // fallback
                        // could not found bone name in nodes, but keep this now
                        skeleton_node_names[bone_name] = true;
                        continue;
                    }

                    const aiNode* bone_node = node_it->second;
                    while (bone_node)
                    {
                        skeleton_node_names[bone_node->mName.C_Str()] = true;
                        bone_node = bone_node->mParent;
                    }
                }
            }

            std::shared_ptr<resource::Skeleton> skeleton = nullptr;
            if (!inverse_bind_matrices.empty())
            {
                skeleton = std::make_shared<resource::Skeleton>();
                struct SkeletonNodeEntry
                {
                    const aiNode* node = nullptr;
                    int32 parent_index = -1;
                };

                Vector<SkeletonNodeEntry> skeleton_node_stack;
                skeleton_node_stack.push_back({ aiscene->mRootNode, -1 });
                while (!skeleton_node_stack.empty())
                {
                    const SkeletonNodeEntry entry = skeleton_node_stack.back();
                    skeleton_node_stack.pop_back();

                    const aiNode* node = entry.node;
                    const String node_name = node->mName.C_Str();
                    int32 current_parent_index = entry.parent_index;
                    if (skeleton_node_names.find(node_name) != skeleton_node_names.end())
                    {
                        resource::Bone bone = {};
                        bone.name = node_name;
                        bone.parent_index = entry.parent_index;
                        bone.bind_local_transform = to_float4x4(node->mTransformation);

                        auto inverse_bind_it = inverse_bind_matrices.find(node_name);
                        if (inverse_bind_it != inverse_bind_matrices.end())
                        {
                            bone.inverse_bind_matrix = to_float4x4(inverse_bind_it->second);
                        }

                        const uint32 bone_index = static_cast<uint32>(skeleton->bones.size());
                        skeleton->bone_name_to_index[bone.name] = bone_index;
                        skeleton->bones.push_back(bone);
                        current_parent_index = static_cast<int32>(bone_index);
                    }

                    for (uint32 child_index = node->mNumChildren; child_index > 0; --child_index)
                    {
                        skeleton_node_stack.push_back({ node->mChildren[child_index - 1], current_parent_index });
                    }
                }

                // fallback for bone names without scene node
                for (const auto& entry : inverse_bind_matrices)
                {
                    if (skeleton->bone_name_to_index.find(entry.first) != skeleton->bone_name_to_index.end())
                    {
                        continue;
                    }

                    resource::Bone bone = {};
                    bone.name = entry.first;
                    bone.inverse_bind_matrix = to_float4x4(entry.second);
                    const uint32 bone_index = static_cast<uint32>(skeleton->bones.size());
                    skeleton->bone_name_to_index[bone.name] = bone_index;
                    skeleton->bones.push_back(bone);
                }

                if (!skeleton->IsValid())
                {
                    skeleton = nullptr;
                }
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
                if (ai_mat->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    material_slot.textures[ROUGHNESSMAP].name = tex.C_Str();
                    material_slot.textures[METALLICMAP].name = tex.C_Str();
                }
                else if (ai_mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex) == AI_SUCCESS)
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
            mesh.skeleton = skeleton;
            bool import_failed = false;
            struct NodeImportEntry
            {
                const aiNode* node = nullptr;
                aiMatrix4x4 parent_transform;
            };

            Vector<NodeImportEntry> node_stack;
            node_stack.push_back({ aiscene->mRootNode, aiMatrix4x4() });

            auto add_bone_weight = [](uint4& bone_indices, float4& bone_weights, uint32 bone_index, float weight)
            {
                if (weight <= 0.0f)
                {
                    return;
                }

                uint32 indices[4] = { bone_indices.x, bone_indices.y, bone_indices.z, bone_indices.w };
                float weights[4] = { bone_weights.x, bone_weights.y, bone_weights.z, bone_weights.w };

                // find top 4 weights
                uint32 target_index = 0;
                for (uint32 influence_index = 0; influence_index < 4; ++influence_index)
                {
                    if (weights[influence_index] == 0.0f)
                    {
                        target_index = influence_index;
                        break;
                    }
                    if (weights[influence_index] < weights[target_index])
                    {
                        target_index = influence_index;
                    }
                }

                if (weights[target_index] > weight)
                {
                    return;
                }

                indices[target_index] = bone_index;
                weights[target_index] = weight;
                bone_indices = { indices[0], indices[1], indices[2], indices[3] };
                bone_weights = { weights[0], weights[1], weights[2], weights[3] };
            };

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
                    if (mesh.skeleton)
                    {
                        mesh.bone_indices.reserve(mesh.bone_indices.size() + ai_mesh->mNumVertices);
                        mesh.bone_weights.reserve(mesh.bone_weights.size() + ai_mesh->mNumVertices);
                    }

                    for (uint32_t vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index)
                    {
                        const aiVector3D transformed_position = node_transform * ai_mesh->mVertices[vertex_index];
                        aiVector3D transformed_normal = normal_matrix * ai_mesh->mNormals[vertex_index];
                        transformed_normal.NormalizeSafe();

                        const float3 position = { transformed_position.x, transformed_position.y, transformed_position.z };
                        mesh.positions.push_back(position);
                        mesh.normals.push_back({ transformed_normal.x, transformed_normal.y, transformed_normal.z });
                        if (mesh.skeleton)
                        {
                            mesh.bone_indices.push_back({ 0, 0, 0, 0 });
                            mesh.bone_weights.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
                        }

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

                    if (mesh.skeleton && ai_mesh->HasBones())
                    {
                        for (uint32_t bone_index = 0; bone_index < ai_mesh->mNumBones; ++bone_index)
                        {
                            const aiBone* ai_bone = ai_mesh->mBones[bone_index];
                            if (!ai_bone)
                            {
                                continue;
                            }

                            auto skeleton_bone_it = mesh.skeleton->bone_name_to_index.find(ai_bone->mName.C_Str()); // all bone names, including those not associated with a scene node
                            if (skeleton_bone_it == mesh.skeleton->bone_name_to_index.end())
                            {
                                continue;
                            }

                            const uint32 skeleton_bone_index = skeleton_bone_it->second;
                            for (uint32_t weight_index = 0; weight_index < ai_bone->mNumWeights; ++weight_index)
                            {
                                const aiVertexWeight& vertex_weight = ai_bone->mWeights[weight_index];
                                if (vertex_weight.mVertexId >= ai_mesh->mNumVertices)
                                {
                                    continue;
                                }

                                const uint32 mesh_vertex_index = vertex_offset + vertex_weight.mVertexId;
                                // add if this weight is in top4
                                add_bone_weight(mesh.bone_indices[mesh_vertex_index], mesh.bone_weights[mesh_vertex_index], skeleton_bone_index, vertex_weight.mWeight);
                            }
                        }

                        const uint32 vertex_end = vertex_offset + ai_mesh->mNumVertices;
                        for (uint32 vertex_index = vertex_offset; vertex_index < vertex_end; ++vertex_index)
                        {
                            float4& bone_weights = mesh.bone_weights[vertex_index];
                            const float weight_sum = bone_weights.x + bone_weights.y + bone_weights.z + bone_weights.w;
                            if (weight_sum <= 0.0f)
                            {
                                continue;
                            }

                            bone_weights.x /= weight_sum;
                            bone_weights.y /= weight_sum;
                            bone_weights.z /= weight_sum;
                            bone_weights.w /= weight_sum;
                        }
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
