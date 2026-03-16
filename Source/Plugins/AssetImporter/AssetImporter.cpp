#include "AssetImporter.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

#include "Backlog.h"
#include "MathUtils.h"
#include "StringUtils.h"
#include "FileSystem.h"
#include "SceneComponents.h"

namespace won::plugin
{
    class AssetImporter : public IPlugin
    {
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
            
            std::string ext = io::GetExtension(file_path_in);
            std::string dir = io::GetDirectoryFromPath(file_path_in);
            std::string name = io::GetFilename(file_path_in);
            if (ext == "obj" || ext == "gltf")
            {

            }
            else
            {
                backlog::Post("AssetImporter::Import : format(" + ext + ") not supported", backlog::LogLevel::Warning);
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

            const aiScene* aiscene = importer.ReadFile(file_path_in, flags);
            if (!aiscene || !aiscene->mRootNode)
            {
                std::string log = "AssetImporter::Import failed: " + std::string(file_path_in);
                backlog::Post(log, backlog::LogLevel::Warning);
                return false;
            }

            ecs::Entity root_entity = target_scene_in->CreateEntity();
            target_scene_in->AddComponent<ecs::NameComponent>(root_entity)->value = name;
            target_scene_in->AddComponent<ecs::TransformComponent>(root_entity);

            ecs::MaterialComponent* material_comp = target_scene_in->AddComponent<ecs::MaterialComponent>(root_entity);
            material_comp->material_slots.clear();
            material_comp->material_slots.reserve(aiscene->mNumMaterials);

            // TODO : check slot
            for (uint32_t i = 0; i < aiscene->mNumMaterials; ++i)
            {
                const aiMaterial* ai_mat = aiscene->mMaterials[i];

                ecs::MaterialSlot& material_slot = material_comp->AddMaterialSlot();
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

                for (auto& x : material_slot.textures)
                {
                    if (!x.name.empty())
                    {
                        x.name = dir + "/" + x.name;

                        std::shared_ptr<resource::Image> image = resource::LoadImageFile(x.name, 4);
                        if (!image || !image->IsValid())
                        {
                            continue;
                        }

                        RHITextureDesc texture_desc = {};
                        texture_desc.width = static_cast<uint32>(image->width);
                        texture_desc.height = static_cast<uint32>(image->height);
                        texture_desc.depth = 1;
                        texture_desc.mip_levels = 1;
                        texture_desc.array_layers = 1;
                        texture_desc.sample_count = 1;
                        texture_desc.format = RHIFormat::R8G8B8A8Unorm;
                        texture_desc.usage = RHIResourceUsage::Default;
                        texture_desc.bind_flags = RHIBindFlags::ShaderResource;

                        x.texture =  device_in->CreateTexture(texture_desc, image->pixels.data(), image->pixels.size());

                        RHISubresourceDesc texture_srv_desc = {};
                        texture_srv_desc.type = RHISubresourceType::ShaderResource;
                        texture_srv_desc.first_slice = 0;
                        texture_srv_desc.slice_count = 1;
                        texture_srv_desc.first_mip = 0;
                        texture_srv_desc.mip_count = 1;

                        device_in->CreateSubresource(*x.texture, texture_srv_desc, &x.res_handle);
                    }
                }
            }

            if (material_comp->material_slots.empty())
            {
                // default fallback
                ecs::MaterialSlot& material_slot = material_comp->AddMaterialSlot();
            }

            ecs::GeometryComponent* geometry_comp = target_scene_in->AddComponent<ecs::GeometryComponent>(root_entity);
            geometry_comp->mesh = std::make_shared<resource::Mesh>();

            // Load objects, meshes:
            for (uint32_t mesh_index = 0; mesh_index < aiscene->mNumMeshes; ++mesh_index)
            {
                const aiMesh* ai_mesh = aiscene->mMeshes[mesh_index];
                const bool has_uv = ai_mesh->HasTextureCoords(0);
                const bool has_tb = ai_mesh->HasTangentsAndBitangents();
                const uint32 vertex_offset = static_cast<uint32>(geometry_comp->mesh->positions.size());
                const uint32 index_offset = static_cast<uint32>(geometry_comp->mesh->indices.size());
                resource::Submesh& submesh = geometry_comp->mesh->submeshes.emplace_back();
                submesh.first_vertex = vertex_offset;
                submesh.first_index = index_offset;
                submesh.material_slot = ai_mesh->mMaterialIndex < material_comp->GetMaterialSlotCount() ? ai_mesh->mMaterialIndex : 0;

                if (ai_mesh->HasPositions() && ai_mesh->HasNormals())
                {
                    geometry_comp->mesh->positions.reserve(geometry_comp->mesh->positions.size() + ai_mesh->mNumVertices);
                    geometry_comp->mesh->normals.reserve(geometry_comp->mesh->normals.size() + ai_mesh->mNumVertices);

                    if (has_uv)
                    {
                        geometry_comp->mesh->texcoords.reserve(geometry_comp->mesh->texcoords.size() + ai_mesh->mNumVertices);
                    }
                    if (has_tb)
                    {
                        geometry_comp->mesh->tangents.reserve(geometry_comp->mesh->tangents.size() + ai_mesh->mNumVertices);
                    }

                    for (unsigned int vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index)
                    {
                        const float3 position = { ai_mesh->mVertices[vertex_index].x, ai_mesh->mVertices[vertex_index].y, ai_mesh->mVertices[vertex_index].z };
                        geometry_comp->mesh->positions.push_back(position);
                        geometry_comp->mesh->normals.push_back({ ai_mesh->mNormals[vertex_index].x, ai_mesh->mNormals[vertex_index].y, ai_mesh->mNormals[vertex_index].z });

                        if (has_uv)
                        {
                            geometry_comp->mesh->texcoords.push_back(float2{ ai_mesh->mTextureCoords[0][vertex_index].x, ai_mesh->mTextureCoords[0][vertex_index].y });
                        }
                        if (has_tb)
                        {
                            geometry_comp->mesh->tangents.push_back(float4{ ai_mesh->mTangents[vertex_index].x, ai_mesh->mTangents[vertex_index].y, ai_mesh->mTangents[vertex_index].z, 1.f });
                        }

                        if (vertex_index == 0)
                        {
                            submesh.local_bounds.min = position;
                            submesh.local_bounds.max = position;
                        }
                        else
                        {
                            submesh.local_bounds.min.x = std::min(submesh.local_bounds.min.x, position.x);
                            submesh.local_bounds.min.y = std::min(submesh.local_bounds.min.y, position.y);
                            submesh.local_bounds.min.z = std::min(submesh.local_bounds.min.z, position.z);
                            submesh.local_bounds.max.x = std::max(submesh.local_bounds.max.x, position.x);
                            submesh.local_bounds.max.y = std::max(submesh.local_bounds.max.y, position.y);
                            submesh.local_bounds.max.z = std::max(submesh.local_bounds.max.z, position.z);
                        }
                    }
                }
                else
                {
                    assert(0);
                    return false;
                }

                if (ai_mesh->HasFaces())
                {
                    geometry_comp->mesh->indices.reserve(geometry_comp->mesh->indices.size() + ai_mesh->mNumFaces * 3);

                    for (unsigned int face_index = 0; face_index < ai_mesh->mNumFaces; ++face_index)
                    {
                        const aiFace& face = ai_mesh->mFaces[face_index];
                        for (unsigned int index_index = 0; index_index < face.mNumIndices; ++index_index)
                        {
                            geometry_comp->mesh->indices.push_back(vertex_offset + face.mIndices[index_index]);
                        }
                    }
                }

                submesh.index_count = static_cast<uint32>(geometry_comp->mesh->indices.size()) - index_offset;
                if (mesh_index == 0)
                {
                    geometry_comp->local_bounds = submesh.local_bounds;
                }
                else
                {
                    geometry_comp->local_bounds.min.x = std::min(geometry_comp->local_bounds.min.x, submesh.local_bounds.min.x);
                    geometry_comp->local_bounds.min.y = std::min(geometry_comp->local_bounds.min.y, submesh.local_bounds.min.y);
                    geometry_comp->local_bounds.min.z = std::min(geometry_comp->local_bounds.min.z, submesh.local_bounds.min.z);
                    geometry_comp->local_bounds.max.x = std::max(geometry_comp->local_bounds.max.x, submesh.local_bounds.max.x);
                    geometry_comp->local_bounds.max.y = std::max(geometry_comp->local_bounds.max.y, submesh.local_bounds.max.y);
                    geometry_comp->local_bounds.max.z = std::max(geometry_comp->local_bounds.max.z, submesh.local_bounds.max.z);
                }
            }

            geometry_comp->mesh->CreateRenderData(device_in);
            std::string log = "AssetImporter::Import succeeded: " + std::string(file_path_in);
            backlog::Post(log, backlog::LogLevel::Default);

            root_entity_out = root_entity;
            return true;
        }
    private:
        static bool ImportThunk(IPlugin* self, const char* file_path_in, ecs::Scene* target_scene_in, RHIDevice* device_in, ecs::Entity& root_entity_out)
        {
            return static_cast<AssetImporter*>(self)->Import(file_path_in, target_scene_in, device_in, root_entity_out);
        }

        inline static AssetImporterAPI s_api{
            &ImportThunk
        };
    };

    IMPLEMENT_PLUGIN(AssetImporter, PAssetImporter);
}
