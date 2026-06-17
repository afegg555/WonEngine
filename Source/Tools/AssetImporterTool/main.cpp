#include "Animation.h"
#include "FileSystem.h"
#include "Image.h"
#include "MaterialComponent.h"
#include "Mesh.h"
#include "ProjectSettings.h"
#include "ResourceAsset.h"
#include "StringUtils.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/anim.h"

#if defined(WON_TEXTURE_COMPRESS_GPU)
// GPU path: include rendering headers and initialize device in Run()
#else
#include <DirectXTex.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <objbase.h>

using namespace won;

struct COMInitializer
{
    COMInitializer() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~COMInitializer() { CoUninitialize(); }
};
    namespace
    {
        constexpr const char* generated_asset_directory = "Generated";
        constexpr uint32 texture_source_file = 0;
        constexpr uint32 texture_source_embedded = 1;

        struct TextureData
        {
            uint32 material_index = 0;
            uint32 texture_slot = 0;
            uint32 source_type = texture_source_file;
            String source;
        };

        struct EmbeddedTexture
        {
            String source;
            uint32 width = 0;
            uint32 height = 0;
            bool compressed = false;
            Vector<uint8> bytes;
        };

        struct AssetData
        {
            String name;
            uint64 timestamp = 0;
            std::shared_ptr<resource::Mesh> mesh;
            Vector<ecs::MaterialSlot> materials;
            Vector<TextureData> textures;
            Vector<EmbeddedTexture> embedded_textures;
        };

        static float4x4 ToFloat4x4(const aiMatrix4x4& matrix)
        {
            // aiMatrix4x4 rows are a, b, c, d; columns are 1, 2, 3, 4
            // transpose into row-major pre-multiply form used by float4x4

            float4x4 result = {};
            result._11 = matrix.a1; result._12 = matrix.b1; result._13 = matrix.c1; result._14 = matrix.d1;
            result._21 = matrix.a2; result._22 = matrix.b2; result._23 = matrix.c2; result._24 = matrix.d2;
            result._31 = matrix.a3; result._32 = matrix.b3; result._33 = matrix.c3; result._34 = matrix.d3;
            result._41 = matrix.a4; result._42 = matrix.b4; result._43 = matrix.c4; result._44 = matrix.d4;
            return result;
        }

        static bool ImportAssetData(const String& file_path, float scale, bool import_skeleton, bool import_normals, bool import_tangents, bool import_animations, AssetData& asset_data)
        {
            asset_data = {};
            io::GetLastTimestamp(file_path, &asset_data.timestamp);

            String ext = utils::ToLower(io::GetExtension(file_path));
            String dir = io::GetDirectoryFromPath(file_path);
            asset_data.name = io::GetFilename(file_path);
            if (ext == "obj" || ext == "gltf" || ext == "glb")
            {

            }
            else
            {
                std::cerr << "AssetImporter::ImportAssetData : format(" << ext << ") not supported\n";
                return false;
            }

            Assimp::Importer importer;

            unsigned flags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality |
                aiProcess_MakeLeftHanded | // LHS
                aiProcess_FlipUVs | // upper left origin
                aiProcess_FlipWindingOrder; // use CW order
            if (import_normals)  flags |= aiProcess_GenSmoothNormals;
            if (import_tangents) flags |= aiProcess_CalcTangentSpace;

            const aiScene* aiscene = importer.ReadFile(file_path, flags);
            if (!aiscene || !aiscene->mRootNode)
            {
                std::cerr << "AssetImporter::ImportAssetData failed: " << file_path << "\n";
                return false;
            }

            asset_data.embedded_textures.reserve(aiscene->mNumTextures);
            for (uint32_t texture_index = 0; texture_index < aiscene->mNumTextures; ++texture_index)
            {
                const aiTexture* ai_texture = aiscene->mTextures[texture_index];
                if (!ai_texture || !ai_texture->pcData)
                {
                    continue;
                }

                EmbeddedTexture embedded_texture = {};
                embedded_texture.source = "*" + std::to_string(texture_index);
                embedded_texture.compressed = ai_texture->mHeight == 0;
                if (embedded_texture.compressed)
                {
                    const Size byte_size = static_cast<Size>(ai_texture->mWidth);
                    const uint8* bytes = reinterpret_cast<const uint8*>(ai_texture->pcData);
                    embedded_texture.bytes.assign(bytes, bytes + byte_size);
                }
                else
                {
                    embedded_texture.width = ai_texture->mWidth;
                    embedded_texture.height = ai_texture->mHeight;
                    const Size pixel_count = static_cast<Size>(ai_texture->mWidth) * static_cast<Size>(ai_texture->mHeight);
                    embedded_texture.bytes.resize(pixel_count * 4);
                    const aiTexel* texels = ai_texture->pcData;
                    for (Size i = 0; i < pixel_count; ++i)
                    {
                        embedded_texture.bytes[i * 4 + 0] = texels[i].r;
                        embedded_texture.bytes[i * 4 + 1] = texels[i].g;
                        embedded_texture.bytes[i * 4 + 2] = texels[i].b;
                        embedded_texture.bytes[i * 4 + 3] = texels[i].a;
                    }
                }

                asset_data.embedded_textures.push_back(std::move(embedded_texture));
            }

            asset_data.mesh = std::make_shared<resource::Mesh>();
            resource::Mesh& mesh = *asset_data.mesh;

            auto skeleton = std::make_shared<resource::Skeleton>();
            UnorderedMap<String, uint32> bone_name_to_index;
            if (import_skeleton)
            {
                UnorderedMap<String, bool> required_node_names; // nodes directly referenced by bones or animation channels
                for (uint32_t mesh_index = 0; mesh_index < aiscene->mNumMeshes; ++mesh_index)
                {
                    const aiMesh* ai_mesh = aiscene->mMeshes[mesh_index];
                    if (!ai_mesh)
                    {
                        continue;
                    }

                    for (uint32_t bone_index = 0; bone_index < ai_mesh->mNumBones; ++bone_index)
                    {
                        const aiBone* ai_bone = ai_mesh->mBones[bone_index];
                        if (ai_bone)
                        {
                            required_node_names[ai_bone->mName.C_Str()] = true;
                        }
                    }
                }
                for (uint32_t animation_index = 0; animation_index < aiscene->mNumAnimations; ++animation_index)
                {
                    const aiAnimation* ai_animation = aiscene->mAnimations[animation_index];
                    if (!ai_animation)
                    {
                        continue;
                    }

                    for (uint32_t channel_index = 0; channel_index < ai_animation->mNumChannels; ++channel_index)
                    {
                        const aiNodeAnim* ai_channel = ai_animation->mChannels[channel_index];
                        if (ai_channel)
                        {
                            required_node_names[ai_channel->mNodeName.C_Str()] = true;
                        }
                    }
                }

                UnorderedMap<String, bool> included_node_names; // upper required nodes + parent nodes kept in hierarchy
                auto mark_required_nodes = [&](auto&& self, const aiNode* node) -> bool
                {
                    if (!node)
                    {
                        return false;
                    }

                    bool include_node = required_node_names.find(node->mName.C_Str()) != required_node_names.end();
                    for (uint32_t child_index = 0; child_index < node->mNumChildren; ++child_index)
                    {
                        include_node = self(self, node->mChildren[child_index]) || include_node;
                    }
                    if (include_node)
                    {
                        included_node_names[node->mName.C_Str()] = true;
                    }
                    return include_node;
                };
                mark_required_nodes(mark_required_nodes, aiscene->mRootNode);

                if (!included_node_names.empty())
                {
                    struct BoneNodeEntry
                    {
                        const aiNode* node = nullptr;
                        int32 parent_index = -1;
                    };

                    // traverse nodes in hierarchy
                    Vector<BoneNodeEntry> bone_node_stack;
                    bone_node_stack.push_back({ aiscene->mRootNode, -1 });
                    while (!bone_node_stack.empty())
                    {
                        const BoneNodeEntry entry = bone_node_stack.back();
                        bone_node_stack.pop_back();

                        const bool include_node = entry.node && included_node_names.find(entry.node->mName.C_Str()) != included_node_names.end();
                        int32 parent_index = entry.parent_index;
                        if (include_node)
                        {
                            const uint32 bone_index = static_cast<uint32>(skeleton->bones.size());
                            bone_name_to_index[entry.node->mName.C_Str()] = bone_index;
                            resource::Bone& bone = skeleton->bones.emplace_back();
                            bone.name = entry.node->mName.C_Str();
                            bone.parent_index = entry.parent_index;
                            bone.inverse_bind_matrix = math::IDENTITY_MATRIX;
                            bone.bind_local_transform = ToFloat4x4(entry.node->mTransformation);
                            skeleton->bone_name_to_index[bone.name] = bone_index;
                            parent_index = static_cast<int32>(bone_index);
                        }

                        for (uint32_t child_index = entry.node->mNumChildren; child_index > 0; --child_index)
                        {
                            bone_node_stack.push_back({ entry.node->mChildren[child_index - 1], parent_index });
                        }
                    }
                }

                for (uint32_t mesh_index = 0; mesh_index < aiscene->mNumMeshes; ++mesh_index)
                {
                    const aiMesh* ai_mesh = aiscene->mMeshes[mesh_index];
                    if (!ai_mesh)
                    {
                        continue;
                    }

                    for (uint32_t bone_index = 0; bone_index < ai_mesh->mNumBones; ++bone_index)
                    {
                        const aiBone* ai_bone = ai_mesh->mBones[bone_index];
                        if (!ai_bone)
                        {
                            continue;
                        }

                        const String bone_name = ai_bone->mName.C_Str();
                        auto bone_it = bone_name_to_index.find(bone_name);
                        // fallback for bone names without scene node
                        if (bone_it == bone_name_to_index.end())
                        {
                            const uint32 fallback_bone_index = static_cast<uint32>(skeleton->bones.size());
                            bone_name_to_index[bone_name] = fallback_bone_index;
                            resource::Bone& bone = skeleton->bones.emplace_back();
                            bone.name = bone_name;
                            bone.parent_index = -1;
                            bone.inverse_bind_matrix = ToFloat4x4(ai_bone->mOffsetMatrix);
                            bone.bind_local_transform = math::IDENTITY_MATRIX;
                            skeleton->bone_name_to_index[bone.name] = fallback_bone_index;
                        }
                        else
                        {
                            skeleton->bones[bone_it->second].inverse_bind_matrix = ToFloat4x4(ai_bone->mOffsetMatrix);
                        }
                    }
                }

                if (scale != 1.0f)
                {
                    for (resource::Bone& bone : skeleton->bones)
                    {
                        bone.bind_local_transform._41 *= scale;
                        bone.bind_local_transform._42 *= scale;
                        bone.bind_local_transform._43 *= scale;
                        bone.inverse_bind_matrix._41 *= scale;
                        bone.inverse_bind_matrix._42 *= scale;
                        bone.inverse_bind_matrix._43 *= scale;
                    }
                }

                if (!skeleton->bones.empty())
                {
                    mesh.skeleton = skeleton;
                }
            }

            // one animation clip: run, jump..
            if (import_skeleton && import_animations)
            {
                for (uint32_t animation_index = 0; animation_index < aiscene->mNumAnimations; ++animation_index)
                {
                    const aiAnimation* ai_animation = aiscene->mAnimations[animation_index];
                    if (!ai_animation || ai_animation->mDuration <= 0.0)
                    {
                        continue;
                    }

                    auto clip = std::make_shared<resource::AnimationClip>();
                    clip->name = ai_animation->mName.length > 0 ? ai_animation->mName.C_Str() : ("Animation" + std::to_string(animation_index));
                    clip->duration = static_cast<float>(ai_animation->mDuration);
                    clip->ticks_per_second = ai_animation->mTicksPerSecond > 0.0 ? static_cast<float>(ai_animation->mTicksPerSecond) : 1.0f;

                    for (uint32_t channel_index = 0; channel_index < ai_animation->mNumChannels; ++channel_index)
                    {
                        const aiNodeAnim* ai_channel = ai_animation->mChannels[channel_index];
                        if (!ai_channel)
                        {
                            continue;
                        }

                        auto bone_it = bone_name_to_index.find(ai_channel->mNodeName.C_Str());
                        if (bone_it == bone_name_to_index.end())
                        {
                            continue;
                        }

                        resource::AnimationChannel channel = {};
                        channel.bone_index = bone_it->second;

                        channel.positions.reserve(ai_channel->mNumPositionKeys);
                        for (uint32_t key_index = 0; key_index < ai_channel->mNumPositionKeys; ++key_index)
                        {
                            const aiVectorKey& key = ai_channel->mPositionKeys[key_index];
                            channel.positions.push_back({ static_cast<float>(key.mTime), { key.mValue.x * scale, key.mValue.y * scale, key.mValue.z * scale } });
                        }

                        channel.rotations.reserve(ai_channel->mNumRotationKeys);
                        for (uint32_t key_index = 0; key_index < ai_channel->mNumRotationKeys; ++key_index)
                        {
                            const aiQuatKey& key = ai_channel->mRotationKeys[key_index];
                            channel.rotations.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w } });
                        }

                        channel.scales.reserve(ai_channel->mNumScalingKeys);
                        for (uint32_t key_index = 0; key_index < ai_channel->mNumScalingKeys; ++key_index)
                        {
                            const aiVectorKey& key = ai_channel->mScalingKeys[key_index];
                            channel.scales.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
                        }

                        if (channel.IsValid())
                        {
                            clip->channels.push_back(std::move(channel));
                        }
                    }

                    if (clip->IsValid())
                    {
                        mesh.animation_clips.push_back(std::move(clip));
                    }
                }
            }

            asset_data.materials.reserve(aiscene->mNumMaterials);
            for (uint32_t i = 0; i < aiscene->mNumMaterials; ++i)
            {
                const aiMaterial* ai_mat = aiscene->mMaterials[i];
                const uint32 material_index = static_cast<uint32>(asset_data.materials.size());
                ecs::MaterialSlot& slot = asset_data.materials.emplace_back();
                aiColor4D c;
                float v = 0.f;

                // Metallic/Roughness Workflow
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_COLOR_DIFFUSE, &c) ||
                    aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_BASE_COLOR, &c))
                {
                    slot.base_color = { c.r, c.g, c.b, c.a };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_METALLIC_FACTOR, &v))
                {
                    slot.metallic = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ROUGHNESS_FACTOR, &v))
                {
                    slot.roughness = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ANISOTROPY_FACTOR, &v))
                {
                    slot.anisotropy = v;
                }

                // sheen
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_SHEEN_COLOR_FACTOR, &c))
                {
                    slot.sheen_color = { c.r, c.g, c.b };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, &v))
                {
                    slot.sheen_roughness = v;
                }

                // clearcoat
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_FACTOR, &v))
                {
                    slot.clearcoat = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, &v))
                {
                    slot.clearcoat_roughness = v;
                }

                String tex_paths[TEXTURESLOT_COUNT] = {};
                aiString tex;
                if (ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == aiReturn_SUCCESS ||
                    ai_mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) == aiReturn_SUCCESS)
                {
                    tex_paths[BASECOLORMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_NORMALS, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[NORMALMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_EMISSIVE, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[EMISSIVEMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_OPACITY, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[OPACITYMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[DISPLACEMENTMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[OCCLUSIONMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_SHEEN, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[SHEENCOLORMAP] = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    tex_paths[SHEENROUGHNESSMAP] = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_CLEARCOAT, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[CLEARCOATMAP] = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    tex_paths[CLEARCOATROUGHNESSMAP] = tex.C_Str();
                //}
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    tex_paths[CLEARCOATNORMALMAP] = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_ANISOTROPY, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[ANISOTROPYMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[ROUGHNESSMAP] = tex.C_Str();
                    tex_paths[METALLICMAP] = tex.C_Str();
                }
                else if (ai_mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[ROUGHNESSMAP] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_METALNESS, 0, &tex) == AI_SUCCESS)
                {
                    tex_paths[METALLICMAP] = tex.C_Str();
                }

                for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
                {
                    if (tex_paths[texture_slot].empty())
                    {
                        continue;
                    }

                    TextureData texture_data = {};
                    texture_data.material_index = material_index;
                    texture_data.texture_slot = texture_slot;
                    if (tex_paths[texture_slot][0] == '*')
                    {
                        texture_data.source_type = texture_source_embedded;
                        texture_data.source = tex_paths[texture_slot];
                    }
                    else
                    {
                        texture_data.source_type = texture_source_file;
                        texture_data.source = io::CombinePath(dir, tex_paths[texture_slot]);
                    }
                    asset_data.textures.push_back(texture_data);
                }
            }

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
                    const bool use_skinning_mesh_space = ai_mesh->HasBones();
                    const uint32 vertex_offset = static_cast<uint32>(mesh.positions.size());
                    const uint32 index_offset = static_cast<uint32>(mesh.indices.size());
                    resource::Submesh& submesh = mesh.submeshes.emplace_back();
                    math::AABB local_bounds = {};
                    local_bounds.Invalidate();
                    submesh.first_vertex = vertex_offset;
                    submesh.first_index = index_offset;
                    submesh.material_slot = ai_mesh->mMaterialIndex < asset_data.materials.size() ? ai_mesh->mMaterialIndex : 0;

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
                        const aiVector3D transformed_position = use_skinning_mesh_space ? ai_mesh->mVertices[vertex_index] : node_transform * ai_mesh->mVertices[vertex_index];
                        const aiVector3D bounds_position = use_skinning_mesh_space ? node_transform * ai_mesh->mVertices[vertex_index] : transformed_position;
                        aiVector3D transformed_normal = use_skinning_mesh_space ? ai_mesh->mNormals[vertex_index] : normal_matrix * ai_mesh->mNormals[vertex_index];
                        transformed_normal.NormalizeSafe();

                        const float3 position = { transformed_position.x * scale, transformed_position.y * scale, transformed_position.z * scale };
                        const float3 bound_position = { bounds_position.x * scale, bounds_position.y * scale, bounds_position.z * scale };
                        mesh.positions.push_back(position);
                        mesh.normals.push_back({ transformed_normal.x, transformed_normal.y, transformed_normal.z });
                        if (has_uv)
                        {
                            mesh.texcoords.push_back({ ai_mesh->mTextureCoords[0][vertex_index].x, ai_mesh->mTextureCoords[0][vertex_index].y });
                        }
                        if (has_tb)
                        {
                            aiVector3D transformed_tangent = use_skinning_mesh_space ? ai_mesh->mTangents[vertex_index] : tangent_matrix * ai_mesh->mTangents[vertex_index];
                            aiVector3D transformed_bitangent = use_skinning_mesh_space ? ai_mesh->mBitangents[vertex_index] : tangent_matrix * ai_mesh->mBitangents[vertex_index];
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

                        local_bounds.min.x = (std::min)(local_bounds.min.x, bound_position.x);
                        local_bounds.min.y = (std::min)(local_bounds.min.y, bound_position.y);
                        local_bounds.min.z = (std::min)(local_bounds.min.z, bound_position.z);
                        local_bounds.max.x = (std::max)(local_bounds.max.x, bound_position.x);
                        local_bounds.max.y = (std::max)(local_bounds.max.y, bound_position.y);
                        local_bounds.max.z = (std::max)(local_bounds.max.z, bound_position.z);
                    }

                    if (!skeleton->bones.empty())
                    {
                        mesh.bone_indices.resize(mesh.positions.size());
                        mesh.bone_weights.resize(mesh.positions.size());
                    }

                    for (uint32_t bone_index = 0; bone_index < ai_mesh->mNumBones; ++bone_index)
                    {
                        const aiBone* ai_bone = ai_mesh->mBones[bone_index];
                        if (!ai_bone)
                        {
                            continue;
                        }

                        auto bone_it = bone_name_to_index.find(ai_bone->mName.C_Str());
                        if (bone_it == bone_name_to_index.end())
                        {
                            continue;
                        }

                        for (uint32_t weight_index = 0; weight_index < ai_bone->mNumWeights; ++weight_index)
                        {
                            const aiVertexWeight& weight = ai_bone->mWeights[weight_index];
                            const uint32 vertex_index = vertex_offset + weight.mVertexId;
                            if (vertex_index < mesh.bone_indices.size() && vertex_index < mesh.bone_weights.size())
                            {
                                add_bone_weight(mesh.bone_indices[vertex_index], mesh.bone_weights[vertex_index], bone_it->second, weight.mWeight);
                            }
                        }
                    }

                    if (!skeleton->bones.empty())
                    {
                        for (uint32 vertex_index = vertex_offset; vertex_index < vertex_offset + ai_mesh->mNumVertices; ++vertex_index)
                        {
                            float4& weights = mesh.bone_weights[vertex_index];
                            const float weight_sum = weights.x + weights.y + weights.z + weights.w;
                            if (weight_sum > 0.0f)
                            {
                                weights.x /= weight_sum;
                                weights.y /= weight_sum;
                                weights.z /= weight_sum;
                                weights.w /= weight_sum;
                            }
                            else
                            {
                                mesh.bone_indices[vertex_index] = { 0, 0, 0, 0 };
                                weights = { 1.0f, 0.0f, 0.0f, 0.0f };
                            }
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
                    submesh.local_bounds = local_bounds;
                }

                for (uint32_t child_index = 0; child_index < node->mNumChildren; ++child_index)
                {
                    node_stack.push_back({ node->mChildren[child_index], node_transform });
                }
            }

            if (import_failed || !mesh.IsValid())
            {
                std::cerr << "AssetImporter::ImportAssetData failed to build mesh: " << file_path << "\n";
                return false;
            }

            return true;
        }

        static bool CompressTexture(const resource::Image& src, rendering::RHIFormat dst_format, bool generate_mipmaps,
                                    Vector<uint8>& out_pixels, uint32& out_mip_levels)
        {
#if defined(WON_TEXTURE_COMPRESS_GPU)
            // Not implemented yet -- define WON_TEXTURE_COMPRESS_GPU only when GPU init is wired up
            return false;
#else
            const DXGI_FORMAT src_dxgi = (src.format == rendering::RHIFormat::R8G8B8A8UnormSrgb)
                ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            const DXGI_FORMAT dst_dxgi = [dst_format]() -> DXGI_FORMAT {
                switch (dst_format)
                {
                case rendering::RHIFormat::BC1Unorm:     return DXGI_FORMAT_BC1_UNORM;
                case rendering::RHIFormat::BC1UnormSrgb: return DXGI_FORMAT_BC1_UNORM_SRGB;
                case rendering::RHIFormat::BC3Unorm:     return DXGI_FORMAT_BC3_UNORM;
                case rendering::RHIFormat::BC3UnormSrgb: return DXGI_FORMAT_BC3_UNORM_SRGB;
                default:                                 return DXGI_FORMAT_UNKNOWN;
                }
            }();

            std::cout << "DEBUG: CompressTexture formats: src.format=" << (int)src.format
                      << " dst_format=" << (int)dst_format
                      << " src_dxgi=" << src_dxgi
                      << " dst_dxgi=" << dst_dxgi << "\n";

            if (dst_dxgi == DXGI_FORMAT_UNKNOWN)
            {
                std::cout << "DEBUG:   dst_dxgi is unknown!\n";
                return false;
            }

            DirectX::Image dxtex = {};
            dxtex.width     = static_cast<size_t>(src.width);
            dxtex.height    = static_cast<size_t>(src.height);
            dxtex.format    = src_dxgi;
            dxtex.rowPitch  = static_cast<size_t>(src.width) * 4;
            dxtex.slicePitch = dxtex.rowPitch * static_cast<size_t>(src.height);
            dxtex.pixels    = const_cast<uint8_t*>(src.pixels.data());

            DirectX::ScratchImage result;
            if (generate_mipmaps)
            {
                DirectX::ScratchImage mipChain;
                HRESULT hr_mips = DirectX::GenerateMipMaps(dxtex, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
                std::cout << "DEBUG:   GenerateMipMaps hr=" << std::hex << hr_mips << std::dec << "\n";
                if (FAILED(hr_mips))
                {
                    return false;
                }
                HRESULT hr_comp = DirectX::Compress(mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), dst_dxgi, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, result);
                std::cout << "DEBUG:   Compress (mips) hr=" << std::hex << hr_comp << std::dec << "\n";
                if (FAILED(hr_comp))
                {
                    return false;
                }
                out_mip_levels = static_cast<uint32>(result.GetMetadata().mipLevels);
            }
            else
            {
                HRESULT hr_comp = DirectX::Compress(dxtex, dst_dxgi, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, result);
                std::cout << "DEBUG:   Compress (no mips) hr=" << std::hex << hr_comp << std::dec << "\n";
                if (FAILED(hr_comp))
                {
                    return false;
                }
                out_mip_levels = 1;
            }

            const size_t pixel_size = result.GetPixelsSize();
            out_pixels.resize(pixel_size);
            std::memcpy(out_pixels.data(), result.GetPixels(), pixel_size);
            return true;
#endif
        }

        bool SaveImportedAsset(const String& content_root, const String& source_asset_path,
                               const AssetData& data, const resource::MeshImportSettings& mesh_settings,
                               String& out_error)
        {
            out_error.clear();
            if (!data.mesh || !data.mesh->IsValid())
            {
                out_error = "Imported mesh is not valid.";
                return false;
            }

            if (!io::CreateDirectories(io::CombinePath(content_root, generated_asset_directory)))
            {
                out_error = "Failed to create Generated directory.";
                return false;
            }

            String source_mesh_asset_path = io::GetRelativePath(content_root, source_asset_path);
            if (source_mesh_asset_path.empty())
            {
                source_mesh_asset_path = source_asset_path;
            }

            const String asset_id = std::to_string(utils::Hash(source_mesh_asset_path));

            const String mesh_binary_path     = String(generated_asset_directory) + "/" + asset_id + "." + resource::mesh_binary_extension;
            const String mesh_binary_full_path = io::CombinePath(content_root, mesh_binary_path);
            if (!resource::SaveMeshBinary(mesh_binary_full_path, *data.mesh))
            {
                out_error = "Failed to save mesh binary.";
                return false;
            }

            // Copy material slots so texture paths can be filled in without modifying the original
            Vector<ecs::MaterialSlot> material_slots = data.materials;
            if (material_slots.empty())
            {
                material_slots.emplace_back();
            }

            const String embedded_texture_directory = String(generated_asset_directory) + "/" + asset_id + "_Textures";
            UnorderedMap<String, String> saved_texture_paths;

            auto SaveTexture = [&](const resource::Image& image, const String& texture_full_path, const resource::TextureImportSettings& settings) -> bool
            {
                std::cout << "DEBUG: SaveTexture to: " << texture_full_path << "\n";
                bool has_alpha = false;
                for (Size pixel_index = 3; pixel_index < image.pixels.size(); pixel_index += 4)
                {
                    if (image.pixels[pixel_index] < 255) { has_alpha = true; break; }
                }
                const rendering::RHIFormat bc_format = has_alpha
                    ? (settings.is_srgb ? rendering::RHIFormat::BC3UnormSrgb : rendering::RHIFormat::BC3Unorm)
                    : (settings.is_srgb ? rendering::RHIFormat::BC1UnormSrgb : rendering::RHIFormat::BC1Unorm);
                Vector<uint8> compressed_pixels;
                uint32 mip_levels = 1;
                bool dirs_ok = io::CreateDirectories(io::GetDirectoryFromPath(texture_full_path));
                std::cout << "DEBUG:   CreateDirectories=" << dirs_ok << "\n";
                bool compress_ok = CompressTexture(image, bc_format, settings.generate_mipmaps, compressed_pixels, mip_levels);
                std::cout << "DEBUG:   CompressTexture=" << compress_ok << " mips=" << mip_levels << "\n";
                bool save_ok = resource::SaveTextureBinary(texture_full_path,
                    static_cast<uint32>(image.width), static_cast<uint32>(image.height),
                    mip_levels, bc_format, compressed_pixels);
                std::cout << "DEBUG:   SaveTextureBinary=" << save_ok << "\n";
                return dirs_ok && compress_ok && save_ok;
            };

            for (const TextureData& tex_data : data.textures)
            {
                if (tex_data.material_index >= material_slots.size() || tex_data.texture_slot >= TEXTURESLOT_COUNT)
                {
                    continue;
                }

                std::cout << "DEBUG: Texture binding: material=" << tex_data.material_index
                          << " slot=" << tex_data.texture_slot
                          << " type=" << tex_data.source_type
                          << " source=" << tex_data.source << "\n";

                const bool color_texture = tex_data.texture_slot == BASECOLORMAP || tex_data.texture_slot == EMISSIVEMAP || tex_data.texture_slot == SHEENCOLORMAP;
                String texture_asset_path;

                if (tex_data.source_type == texture_source_file)
                {
                    const String source_texture_full_path = project::ResolveProjectContentPath(content_root, tex_data.source);
                    const String texture_asset_key = io::NormalizePath(source_texture_full_path);
                    auto texture_it = saved_texture_paths.find(texture_asset_key);
                    if (texture_it != saved_texture_paths.end())
                    {
                        texture_asset_path = texture_it->second;
                    }
                    else
                    {
                        std::shared_ptr<resource::Image> image = resource::LoadImageFile(source_texture_full_path, 4);
                        if (image && image->IsValid())
                        {
                            const String texture_file_name = std::to_string(utils::Hash(texture_asset_key)) + "." + resource::texture_binary_extension;
                            texture_asset_path = embedded_texture_directory + "/" + texture_file_name;
                            const String texture_full_path = io::CombinePath(content_root, texture_asset_path);

                            resource::TextureImportSettings settings;
                            settings.is_srgb = color_texture;
                            settings.generate_mipmaps = true;

                            resource::AssetMeta texture_meta;
                            if (resource::LoadAssetMeta(resource::GetAssetMetaPath(source_texture_full_path), texture_meta))
                            {
                                settings = texture_meta.texture;
                            }

                            if (!SaveTexture(*image, texture_full_path, settings))
                            {
                                texture_asset_path.clear();
                            }
                            else
                            {
                                saved_texture_paths[texture_asset_key] = texture_asset_path;
                            }
                        }
                    }
                }
                else // embedded
                {
                    std::cout << "DEBUG: Searching embedded: " << tex_data.source << " (total embedded count=" << data.embedded_textures.size() << ")\n";
                    for (const auto& e : data.embedded_textures)
                    {
                        std::cout << "  Available embedded: " << e.source << " size=" << e.bytes.size() << " compressed=" << e.compressed << "\n";
                    }

                    auto emb_it = std::find_if(data.embedded_textures.begin(), data.embedded_textures.end(),
                        [&](const EmbeddedTexture& e) { return e.source == tex_data.source; });
                    if (emb_it != data.embedded_textures.end() && !emb_it->bytes.empty())
                    {
                        std::cout << "DEBUG: Found embedded texture in data.embedded_textures\n";
                        const String texture_file_name = std::to_string(tex_data.material_index) + "_" + std::to_string(tex_data.texture_slot) + "." + resource::texture_binary_extension;
                        texture_asset_path = embedded_texture_directory + "/" + texture_file_name;
                        const String texture_full_path = io::CombinePath(content_root, texture_asset_path);
                        std::shared_ptr<resource::Image> image = nullptr;
                        if (emb_it->compressed)
                        {
                            std::cout << "DEBUG: Decoding compressed image memory... size=" << emb_it->bytes.size() << "\n";
                            image = resource::LoadImageMemory(emb_it->bytes.data(), emb_it->bytes.size(), 4);
                        }
                        else if (emb_it->width > 0 && emb_it->height > 0 && emb_it->bytes.size() == static_cast<Size>(emb_it->width) * static_cast<Size>(emb_it->height) * 4)
                        {
                            std::cout << "DEBUG: Constructing raw image " << emb_it->width << "x" << emb_it->height << "\n";
                            image = std::make_shared<resource::Image>();
                            image->width    = static_cast<int32>(emb_it->width);
                            image->height   = static_cast<int32>(emb_it->height);
                            image->channels = 4;
                            image->pixels   = emb_it->bytes;
                        }

                        if (image)
                        {
                            std::cout << "DEBUG: Image decoded. IsValid=" << image->IsValid() << " width=" << image->width << " height=" << image->height << "\n";
                        }
                        else
                        {
                            std::cout << "DEBUG: Image decoding FAILED (image is null)\n";
                        }

                        resource::TextureImportSettings settings;
                        settings.is_srgb = color_texture;
                        settings.generate_mipmaps = true;

                        if (!image || !image->IsValid() || !SaveTexture(*image, texture_full_path, settings))
                        {
                            std::cout << "DEBUG: Skipping texture save due to validation failure\n";
                            texture_asset_path.clear();
                        }
                    }
                }

                if (!texture_asset_path.empty())
                {
                    material_slots[tex_data.material_index].textures[tex_data.texture_slot].texture_asset_path = texture_asset_path;
                }
            }

            const String material_binary_path     = String(generated_asset_directory) + "/" + asset_id + "." + resource::material_binary_extension;
            const String material_binary_full_path = io::CombinePath(content_root, material_binary_path);
            if (!resource::SaveMaterialBinary(material_binary_full_path, material_slots))
            {
                out_error = "Failed to save material binary.";
                return false;
            }

            resource::AssetMeta meta = {};
            meta.asset_id          = asset_id;
            meta.asset_name        = data.name;
            meta.source_asset_path = source_mesh_asset_path;
            meta.asset_type        = "mesh";
            meta.binary_path       = mesh_binary_path;
            meta.source_timestamp  = data.timestamp;
            meta.mesh              = mesh_settings;
            if (!resource::SaveAssetMeta(resource::GetAssetMetaPath(source_asset_path), meta))
            {
                out_error = "Failed to save asset metadata.";
                return false;
            }

            std::cout << "Imported: " << source_asset_path << "\n";
            std::cout << "Name: " << data.name << "\n";
            std::cout << "Asset id: " << asset_id << "\n";
            std::cout << "Mesh: " << mesh_binary_full_path << "\n";
            std::cout << "Materials: " << material_binary_full_path << "\n";
            std::cout << "Meta: " << resource::GetAssetMetaPath(source_asset_path) << "\n";
            std::cout << "Vertices: " << data.mesh->positions.size() << "\n";
            std::cout << "Indices: " << data.mesh->indices.size() << "\n";
            std::cout << "Submeshes: " << data.mesh->submeshes.size() << "\n";
            std::cout << "Material slots: " << material_slots.size() << "\n";
            std::cout << "Texture bindings: " << data.textures.size() << "\n";
            if (data.mesh->skeleton)
            {
                std::cout << "Bones: " << data.mesh->skeleton->bones.size() << "\n";
                std::cout << "Animation clips: " << data.mesh->animation_clips.size() << "\n";
            }
            return true;
        }

        String ResolveProjectSettingsPath(const String& path)
        {
            const String project_path = io::GetAbsolutePath(path);
            String settings_path = project_path;
            String project_root = io::GetDirectoryFromPath(settings_path);
            if (!io::IsFile(settings_path))
            {
                project_root = project_path;
                settings_path = io::CombinePath(project_root, io::ReplaceExtension(io::GetFilename(project_root), project::project_file_extension));
                if (!io::IsFile(settings_path))
                {
                    settings_path = io::CombinePath(project_root, project::default_project_file_name);
                }
            }
            return settings_path;
        }
    }

int main(int argc, char** argv)
{
    COMInitializer com_init;
    if (argc != 3)
    {
        std::cout << "Usage: AssetImporterTool <project_settings_path> <asset_path>\n";
        return 1;
    }

    const String project_settings_path = ResolveProjectSettingsPath(argv[1] ? argv[1] : "");
    if (!io::IsFile(project_settings_path))
    {
        std::cout << "Project settings not found: " << project_settings_path << "\n";
        return 1;
    }

    project::ProjectSettings project_settings = {};
    if (!project::LoadSettings(project_settings_path, project_settings))
    {
        std::cout << "Failed to load project settings: " << project_settings_path << "\n";
        return 1;
    }

    const String content_root = project::GetContentRoot(project_settings);
    const String source_asset_path = project::ResolveProjectContentPath(content_root, argv[2] ? argv[2] : "");
    if (!io::IsFile(source_asset_path))
    {
        std::cout << "Source asset not found: " << source_asset_path << "\n";
        return 1;
    }

    resource::MeshImportSettings mesh_settings;
    {
        resource::AssetMeta existing_meta;
        if (resource::LoadAssetMeta(resource::GetAssetMetaPath(source_asset_path), existing_meta))
        {
            mesh_settings = existing_meta.mesh;
        }
    }

    AssetData imported_data;
    if (!ImportAssetData(source_asset_path, mesh_settings.scale, mesh_settings.import_skeleton,
                         mesh_settings.import_normals, mesh_settings.import_tangents,
                         mesh_settings.import_animations, imported_data))
    {
        std::cout << "Failed to import asset: " << source_asset_path << "\n";
        return 1;
    }

    String error;
    if (!SaveImportedAsset(content_root, source_asset_path, imported_data, mesh_settings, error))
    {
        std::cout << error << "\n";
        return 1;
    }
    return 0;
}
