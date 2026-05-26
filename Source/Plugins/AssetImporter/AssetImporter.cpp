#include "CustomFunctionExtension.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/anim.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace won::plugin
{
    namespace
    {
        constexpr const char* plugin_id = "AssetImporter";
        constexpr const char* plugin_version = "1.0.0";
        constexpr const char* import_function_id = "asset_importer.import";
        constexpr const char* get_result_info_function_id = "asset_importer.get_result_info";
        constexpr const char* get_stream_info_function_id = "asset_importer.get_stream_info";
        constexpr const char* copy_stream_function_id = "asset_importer.copy_stream";
        constexpr const char* get_struct_field_count_function_id = "asset_importer.get_struct_field_count";
        constexpr const char* get_struct_field_info_function_id = "asset_importer.get_struct_field_info";
        constexpr const char* get_material_info_function_id = "asset_importer.get_material_info";
        constexpr const char* get_material_texture_count_function_id = "asset_importer.get_material_texture_count";
        constexpr const char* get_material_texture_function_id = "asset_importer.get_material_texture";
        constexpr const char* get_embedded_texture_info_function_id = "asset_importer.get_embedded_texture_info";
        constexpr const char* copy_embedded_texture_function_id = "asset_importer.copy_embedded_texture";
        constexpr const char* get_bone_name_function_id = "asset_importer.get_bone_name";
        constexpr const char* get_animation_clip_name_function_id = "asset_importer.get_animation_clip_name";
        constexpr const char* release_result_function_id = "asset_importer.release_result";

        constexpr uint32_t texture_source_file = 0;
        constexpr uint32_t texture_source_embedded = 1;
        constexpr uint32_t asset_stream_count = 14;

        using int32 = int32_t;
        using uint8 = uint8_t;
        using uint32 = uint32_t;
        using uint64 = uint64_t;
        using Size = size_t;
        using String = std::string;
        template<typename T>
        using Vector = std::vector<T>;
        template<typename K, typename V>
        using UnorderedMap = std::unordered_map<K, V>;

        struct float2
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct float3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct float4
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        struct ImportedMatrix
        {
            float values[16] = {};
        };

        struct uint4
        {
            uint32 x = 0;
            uint32 y = 0;
            uint32 z = 0;
            uint32 w = 0;
        };

        struct ImportedBounds
        {
            float3 min;
            float3 max;

            void Invalidate()
            {
                const float min_value = (std::numeric_limits<float>::max)();
                const float max_value = (std::numeric_limits<float>::lowest)();
                min = { min_value, min_value, min_value };
                max = { max_value, max_value, max_value };
            }
        };

        enum ImportedTextureSlot : uint32
        {
            BaseColorMap,
            NormalMap,
            SurfaceMap,
            EmissiveMap,
            DisplacementMap,
            OcclusionMap,
            MetallicMap,
            RoughnessMap,
            SheenColorMap,
            SheenRoughnessMap,
            ClearcoatMap,
            ClearcoatRoughnessMap,
            ClearcoatNormalMap,
            AnisotropyMap,
            OpacityMap,
            ImportedTextureSlotCount
        };

        const char* s_texture_semantics[ImportedTextureSlotCount] = {
            "base_color",
            "normal",
            "surface",
            "emissive",
            "displacement",
            "occlusion",
            "metallic",
            "roughness",
            "sheen_color",
            "sheen_roughness",
            "clearcoat",
            "clearcoat_roughness",
            "clearcoat_normal",
            "anisotropy",
            "opacity"
        };

        struct ImportedMaterial
        {
            float4 base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
            float metallic = 0.3f;
            float roughness = 0.5f;
            float reflectance = 0.5f;
            float anisotropy = 0.0f;
            float3 sheen_color = { 1.0f, 1.0f, 1.0f };
            float sheen_roughness = 0.0f;
            float clearcoat = 0.0f;
            float clearcoat_roughness = 0.0f;
            String textures[ImportedTextureSlotCount];
        };

        struct ImportedTextureData
        {
            uint32 material_index = 0;
            uint32 texture_slot = 0;
            uint32 source_type = texture_source_file;
            String source;
        };

        struct ImportedEmbeddedTexture
        {
            String source;
            uint32 width = 0;
            uint32 height = 0;
            bool compressed = false;
            Vector<uint8> bytes;
        };

        struct ImportedSubmesh
        {
            uint32 first_index = 0;
            uint32 index_count = 0;
            uint32 first_vertex = 0;
            uint32 material_index = 0;
            float3 bounds_min = {};
            float3 bounds_max = {};
        };

        struct ImportedBone
        {
            int32 parent_index = -1;
            ImportedMatrix inverse_bind_matrix = {};
            ImportedMatrix bind_local_transform = {};
        };

        struct ImportedVec3Key
        {
            float time = 0.0f;
            float3 value = {};
        };

        struct ImportedQuatKey
        {
            float time = 0.0f;
            float4 value = {};
        };

        struct ImportedAnimationChannel
        {
            uint32 bone_index = 0;
            uint32 first_position_key = 0;
            uint32 position_key_count = 0;
            uint32 first_rotation_key = 0;
            uint32 rotation_key_count = 0;
            uint32 first_scale_key = 0;
            uint32 scale_key_count = 0;
        };

        struct ImportedAnimationClip
        {
            float duration = 0.0f;
            float ticks_per_second = 1.0f;
            uint32 first_channel = 0;
            uint32 channel_count = 0;
        };

        struct ImportedMesh
        {
            Vector<float3> positions;
            Vector<float3> normals;
            Vector<float4> tangents;
            Vector<float2> texcoords;
            Vector<uint4> bone_indices;
            Vector<float4> bone_weights;
            Vector<uint32> indices;
            Vector<ImportedSubmesh> submeshes;

            bool IsValid() const
            {
                return !positions.empty() && !indices.empty();
            }
        };

        static String ToLower(String value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        struct ImportedAssetData
        {
            String name;
            String cache_key;
            uint64 timestamp = 0;
            Vector<ImportedMaterial> materials;
            Vector<ImportedTextureData> textures;
            Vector<ImportedEmbeddedTexture> embedded_textures;
            Vector<String> bone_names;
            Vector<ImportedBone> bones;
            Vector<String> animation_clip_names;
            Vector<ImportedAnimationClip> animation_clips;
            Vector<ImportedAnimationChannel> animation_channels;
            Vector<ImportedVec3Key> animation_position_keys;
            Vector<ImportedQuatKey> animation_rotation_keys;
            Vector<ImportedVec3Key> animation_scale_keys;
            std::shared_ptr<ImportedMesh> mesh;
        };

        struct AssetImporterState
        {
            const WonPluginHostAPI* host_api = nullptr;
            std::mutex result_mutex;
            uint64 next_result_handle = 1;
            UnorderedMap<uint64, std::unique_ptr<ImportedAssetData>> results;
            std::mutex asset_cache_mutex;
            UnorderedMap<String, ImportedAssetData> asset_cache;
        };

        static void PostLog(const AssetImporterState& state, const String& message)
        {
            if (state.host_api && state.host_api->Log)
            {
                state.host_api->Log(message.c_str());
            }
        }

        static String MakeAssetCacheKey(const String& file_path)
        {
            String normalized_path = file_path;
            std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
            normalized_path = ToLower(normalized_path);
            return normalized_path;
        }

        static bool LoadCachedAssetData(AssetImporterState& state, const String& cache_key, uint64 timestamp, ImportedAssetData& imported_data)
        {
            std::lock_guard<std::mutex> lock(state.asset_cache_mutex);
            auto it = state.asset_cache.find(cache_key);
            if (it == state.asset_cache.end())
            {
                return false;
            }

            ImportedAssetData& cached = it->second;
            if (cached.timestamp != timestamp)
            {
                state.asset_cache.erase(it);
                return false;
            }

            imported_data = cached;
            return true;
        }

        static void StoreCachedAssetData(AssetImporterState& state, const ImportedAssetData& imported_data)
        {
            if (imported_data.cache_key.empty() || imported_data.timestamp == 0 || !imported_data.mesh)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(state.asset_cache_mutex);
            state.asset_cache[imported_data.cache_key] = imported_data;
        }

        static bool ImportAssetData(AssetImporterState& state, const String& file_path, ImportedAssetData& imported_data)
        {
            imported_data = {};
            uint64 timestamp = 0;
            std::error_code timestamp_error;
            const auto last_write_time = std::filesystem::last_write_time(file_path, timestamp_error);
            if (!timestamp_error)
            {
                timestamp = static_cast<uint64>(last_write_time.time_since_epoch().count());
                const String cache_key = MakeAssetCacheKey(file_path);
                if (LoadCachedAssetData(state, cache_key, timestamp, imported_data))
                {
                    PostLog(state, "AssetImporter cache hit: " + file_path);
                    return true;
                }
                imported_data.cache_key = cache_key;
                imported_data.timestamp = timestamp;
            }

            std::filesystem::path source_path(file_path);
            String ext = ToLower(source_path.extension().string());
            if (!ext.empty() && ext[0] == '.')
            {
                ext.erase(ext.begin());
            }
            String dir = source_path.parent_path().generic_string();
            imported_data.name = source_path.filename().string();
            if (ext == "obj" || ext == "gltf" || ext == "glb")
            {

            }
            else
            {
                PostLog(state, "AssetImporter::ImportAssetData : format(" + ext + ") not supported");
                return false;
            }

            Assimp::Importer importer;

            const unsigned flags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace |
                aiProcess_ImproveCacheLocality |
                aiProcess_MakeLeftHanded | // LHS
                aiProcess_FlipUVs | // upper left origin
                aiProcess_FlipWindingOrder; // use CW order

            const aiScene* aiscene = importer.ReadFile(file_path, flags);
            if (!aiscene || !aiscene->mRootNode)
            {
                PostLog(state, "AssetImporter::ImportAssetData failed: " + file_path);
                return false;
            }

            imported_data.materials.clear();
            imported_data.textures.clear();
            imported_data.embedded_textures.clear();
            imported_data.bone_names.clear();
            imported_data.bones.clear();
            imported_data.animation_clip_names.clear();
            imported_data.animation_clips.clear();
            imported_data.animation_channels.clear();
            imported_data.animation_position_keys.clear();
            imported_data.animation_rotation_keys.clear();
            imported_data.animation_scale_keys.clear();
            imported_data.mesh = nullptr;
            imported_data.materials.reserve(aiscene->mNumMaterials);

            imported_data.embedded_textures.reserve(aiscene->mNumTextures);
            for (uint32_t texture_index = 0; texture_index < aiscene->mNumTextures; ++texture_index)
            {
                const aiTexture* ai_texture = aiscene->mTextures[texture_index];
                if (!ai_texture || !ai_texture->pcData)
                {
                    continue;
                }

                ImportedEmbeddedTexture embedded_texture = {};
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

                imported_data.embedded_textures.push_back(std::move(embedded_texture));
            }

            auto identity_matrix = []()
            {
                ImportedMatrix matrix = {};
                matrix.values[0] = 1.0f;
                matrix.values[5] = 1.0f;
                matrix.values[10] = 1.0f;
                matrix.values[15] = 1.0f;
                return matrix;
            };

            auto to_stored_matrix = [](const aiMatrix4x4& matrix)
            {
                // a, b, c, d => each row
                // pre-multiplied form(row major & pre-multiplied)

                // transpose and store
                ImportedMatrix result = {};
                result.values[0] = matrix.a1; result.values[1] = matrix.b1; result.values[2] = matrix.c1; result.values[3] = matrix.d1;
                result.values[4] = matrix.a2; result.values[5] = matrix.b2; result.values[6] = matrix.c2; result.values[7] = matrix.d2;
                result.values[8] = matrix.a3; result.values[9] = matrix.b3; result.values[10] = matrix.c3; result.values[11] = matrix.d3;
                result.values[12] = matrix.a4; result.values[13] = matrix.b4; result.values[14] = matrix.c4; result.values[15] = matrix.d4;
                return result;
            };

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

            UnorderedMap<String, uint32> bone_name_to_index;
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
                        const uint32 bone_index = static_cast<uint32>(imported_data.bones.size());
                        bone_name_to_index[entry.node->mName.C_Str()] = bone_index;
                        imported_data.bone_names.push_back(entry.node->mName.C_Str());
                        ImportedBone& bone = imported_data.bones.emplace_back();
                        bone.parent_index = entry.parent_index;
                        bone.inverse_bind_matrix = identity_matrix();
                        bone.bind_local_transform = to_stored_matrix(entry.node->mTransformation);
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
                    // fallback
                    // could not found bone name in nodes, but keep this now
                    // fallback for bone names without scene node
                    if (bone_it == bone_name_to_index.end())
                    {
                        const uint32 fallback_bone_index = static_cast<uint32>(imported_data.bones.size());
                        bone_name_to_index[bone_name] = fallback_bone_index;
                        imported_data.bone_names.push_back(bone_name);
                        ImportedBone& bone = imported_data.bones.emplace_back();
                        bone.parent_index = -1;
                        bone.inverse_bind_matrix = to_stored_matrix(ai_bone->mOffsetMatrix);
                        bone.bind_local_transform = identity_matrix();
                    }
                    else
                    {
                        imported_data.bones[bone_it->second].inverse_bind_matrix = to_stored_matrix(ai_bone->mOffsetMatrix);
                    }
                }
            }

            // one animation clip: run, jump..
            for (uint32_t animation_index = 0; animation_index < aiscene->mNumAnimations; ++animation_index)
            {
                const aiAnimation* ai_animation = aiscene->mAnimations[animation_index];
                if (!ai_animation || ai_animation->mDuration <= 0.0)
                {
                    continue;
                }

                ImportedAnimationClip clip = {};
                clip.duration = static_cast<float>(ai_animation->mDuration);
                clip.ticks_per_second = ai_animation->mTicksPerSecond > 0.0 ? static_cast<float>(ai_animation->mTicksPerSecond) : 1.0f;
                clip.first_channel = static_cast<uint32>(imported_data.animation_channels.size());

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

                    ImportedAnimationChannel& channel = imported_data.animation_channels.emplace_back();
                    channel.bone_index = bone_it->second;
                    channel.first_position_key = static_cast<uint32>(imported_data.animation_position_keys.size());
                    channel.position_key_count = ai_channel->mNumPositionKeys;
                    for (uint32_t key_index = 0; key_index < ai_channel->mNumPositionKeys; ++key_index)
                    {
                        const aiVectorKey& key = ai_channel->mPositionKeys[key_index];
                        imported_data.animation_position_keys.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
                    }

                    channel.first_rotation_key = static_cast<uint32>(imported_data.animation_rotation_keys.size());
                    channel.rotation_key_count = ai_channel->mNumRotationKeys;
                    for (uint32_t key_index = 0; key_index < ai_channel->mNumRotationKeys; ++key_index)
                    {
                        const aiQuatKey& key = ai_channel->mRotationKeys[key_index];
                        imported_data.animation_rotation_keys.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w } });
                    }

                    channel.first_scale_key = static_cast<uint32>(imported_data.animation_scale_keys.size());
                    channel.scale_key_count = ai_channel->mNumScalingKeys;
                    for (uint32_t key_index = 0; key_index < ai_channel->mNumScalingKeys; ++key_index)
                    {
                        const aiVectorKey& key = ai_channel->mScalingKeys[key_index];
                        imported_data.animation_scale_keys.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
                    }
                }

                clip.channel_count = static_cast<uint32>(imported_data.animation_channels.size()) - clip.first_channel;
                if (clip.channel_count > 0)
                {
                    imported_data.animation_clip_names.push_back(ai_animation->mName.length > 0 ? ai_animation->mName.C_Str() : ("Animation" + std::to_string(animation_index)));
                    imported_data.animation_clips.push_back(clip);
                }
            }

            for (uint32_t i = 0; i < aiscene->mNumMaterials; ++i)
            {
                const aiMaterial* ai_mat = aiscene->mMaterials[i];
                const uint32 material_index = static_cast<uint32>(imported_data.materials.size());
                ImportedMaterial& material = imported_data.materials.emplace_back();
                aiColor4D c;
                float v = 0.f;

                // Metallic/Roughness Workflow
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_COLOR_DIFFUSE, &c) ||
                    aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_BASE_COLOR, &c))
                {
                    material.base_color = { c.r, c.g, c.b, c.a };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_METALLIC_FACTOR, &v))
                {
                    material.metallic = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ROUGHNESS_FACTOR, &v))
                {
                    material.roughness = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_ANISOTROPY_FACTOR, &v))
                {
                    material.anisotropy = v;
                }

                // sheen
                if (aiReturn_SUCCESS == aiGetMaterialColor(ai_mat, AI_MATKEY_SHEEN_COLOR_FACTOR, &c))
                {
                    material.sheen_color = { c.r, c.g, c.b };
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, &v))
                {
                    material.sheen_roughness = v;
                }

                // clearcoat
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_FACTOR, &v))
                {
                    material.clearcoat = v;
                }
                if (aiReturn_SUCCESS == aiGetMaterialFloat(ai_mat, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, &v))
                {
                    material.clearcoat_roughness = v;
                }

                aiString tex;
                if (ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == aiReturn_SUCCESS ||
                    ai_mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) == aiReturn_SUCCESS)
                {
                    material.textures[BaseColorMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_NORMALS, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[NormalMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_EMISSIVE, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[EmissiveMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_OPACITY, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[OpacityMap] = tex.C_Str();
                }

                if (ai_mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[DisplacementMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[OcclusionMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_SHEEN, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[SheenColorMap] = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material.textures[SheenRoughnessMap] = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_CLEARCOAT, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[ClearcoatMap] = tex.C_Str();
                }
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material.textures[ClearcoatRoughnessMap] = tex.C_Str();
                //}
                //if (ai_mat->GetTexture(, 0, &tex) == AI_SUCCESS)
                //{
                //    material.textures[ClearcoatNormalMap] = tex.C_Str();
                //}
                if (ai_mat->GetTexture(aiTextureType_ANISOTROPY, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[AnisotropyMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[RoughnessMap] = tex.C_Str();
                    material.textures[MetallicMap] = tex.C_Str();
                }
                else if (ai_mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[RoughnessMap] = tex.C_Str();
                }
                if (ai_mat->GetTexture(aiTextureType_METALNESS, 0, &tex) == AI_SUCCESS)
                {
                    material.textures[MetallicMap] = tex.C_Str();
                }

                for (uint32 texture_slot = 0; texture_slot < ImportedTextureSlotCount; ++texture_slot)
                {
                    String& texture_path = material.textures[texture_slot];
                    if (texture_path.empty())
                    {
                        continue;
                    }

                    ImportedTextureData texture_data = {};
                    texture_data.material_index = material_index;
                    texture_data.texture_slot = texture_slot;
                    if (!texture_path.empty() && texture_path[0] == '*')
                    {
                        texture_data.source_type = texture_source_embedded;
                        texture_data.source = texture_path;
                    }
                    else
                    {
                        texture_data.source_type = texture_source_file;
                        texture_data.source = (std::filesystem::path(dir) / texture_path).generic_string();
                    }
                    imported_data.textures.push_back(texture_data);
                }
            }

            imported_data.mesh = std::make_shared<ImportedMesh>();
            ImportedMesh& mesh = *imported_data.mesh;
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
                    ImportedSubmesh& submesh = mesh.submeshes.emplace_back();
                    ImportedBounds local_bounds = {};
                    local_bounds.Invalidate();
                    submesh.first_vertex = vertex_offset;
                    submesh.first_index = index_offset;
                    submesh.material_index = ai_mesh->mMaterialIndex < imported_data.materials.size() ? ai_mesh->mMaterialIndex : 0;

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

                        const float3 position = { transformed_position.x, transformed_position.y, transformed_position.z };
                        const float3 bound_position = { bounds_position.x, bounds_position.y, bounds_position.z };
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

                        local_bounds.min.x = std::min(local_bounds.min.x, bound_position.x);
                        local_bounds.min.y = std::min(local_bounds.min.y, bound_position.y);
                        local_bounds.min.z = std::min(local_bounds.min.z, bound_position.z);
                        local_bounds.max.x = std::max(local_bounds.max.x, bound_position.x);
                        local_bounds.max.y = std::max(local_bounds.max.y, bound_position.y);
                        local_bounds.max.z = std::max(local_bounds.max.z, bound_position.z);
                    }

                    if (!imported_data.bones.empty())
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

                    if (!imported_data.bones.empty())
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
                    submesh.bounds_min = local_bounds.min;
                    submesh.bounds_max = local_bounds.max;
                }

                for (uint32_t child_index = 0; child_index < node->mNumChildren; ++child_index)
                {
                    node_stack.push_back({ node->mChildren[child_index], node_transform });
                }
            }

            if (import_failed || !mesh.IsValid())
            {
                PostLog(state, "AssetImporter::ImportAssetData failed to build mesh: " + file_path);
                return false;
            }

            StoreCachedAssetData(state, imported_data);
            return true;
        }

        static bool WON_PLUGIN_CALL ImportAsset(void* self, const function::Call* call)
        {
            if (!call || !call->inputs || call->input_count != 1 || !call->outputs || call->output_capacity < 1 || !call->output_count || call->inputs[0].type != won::ValueType::String)
            {
                return false;
            }

            auto* state = static_cast<AssetImporterState*>(self);
            if (!state)
            {
                return false;
            }

            const char* file_path = call->inputs[0].string_value;
            if (!file_path || file_path[0] == '\0')
            {
                return false;
            }

            auto result = std::make_unique<ImportedAssetData>();
            if (!ImportAssetData(*state, file_path, *result))
            {
                return false;
            }

            uint64 result_handle = 0;
            {
                std::lock_guard<std::mutex> lock(state->result_mutex);
                result_handle = state->next_result_handle++;
                if (state->next_result_handle == 0)
                {
                    state->next_result_handle = 1;
                }
                state->results[result_handle] = std::move(result);
            }

            call->outputs[0].type = won::ValueType::UInt64;
            call->outputs[0].uint64_value = result_handle;
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetResultInfo(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!state || !call || !call->inputs || call->input_count != 1 || !call->outputs || call->output_capacity < 3 || !call->output_count || call->inputs[0].type != won::ValueType::UInt64)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            if (it == state->results.end() || !it->second)
            {
                return false;
            }

            const ImportedAssetData& result = *it->second;
            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = result.name.c_str();
            call->outputs[1].type = won::ValueType::UInt32;
            call->outputs[1].uint32_value = result.mesh ? asset_stream_count : 0u;
            call->outputs[2].type = won::ValueType::UInt32;
            call->outputs[2].uint32_value = static_cast<uint32>(result.materials.size());
            *call->output_count = 3;
            return true;
        }

        static bool WON_PLUGIN_CALL GetStreamInfo(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 5 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            if (it == state->results.end() || !it->second || !it->second->mesh)
            {
                return false;
            }

            const ImportedMesh& mesh = *it->second->mesh;
            const uint32 stream_index = call->inputs[1].uint32_value;
            const char* name = "";
            won::ValueType value_type = won::ValueType::Unknown;
            const char* type_name = "";
            uint32 element_size = 0;
            uint64 count = 0;
            switch (stream_index)
            {
            case 0:
                name = "positions";
                value_type = won::ValueType::Float32x3;
                element_size = sizeof(float3);
                count = static_cast<uint64>(mesh.positions.size());
                break;
            case 1:
                name = "normals";
                value_type = won::ValueType::Float32x3;
                element_size = sizeof(float3);
                count = static_cast<uint64>(mesh.normals.size());
                break;
            case 2:
                name = "tangents";
                value_type = won::ValueType::Float32x4;
                element_size = sizeof(float4);
                count = static_cast<uint64>(mesh.tangents.size());
                break;
            case 3:
                name = "texcoords";
                value_type = won::ValueType::Float32x2;
                element_size = sizeof(float2);
                count = static_cast<uint64>(mesh.texcoords.size());
                break;
            case 4:
                name = "indices";
                value_type = won::ValueType::UInt32;
                element_size = sizeof(uint32);
                count = static_cast<uint64>(mesh.indices.size());
                break;
            case 5:
                name = "submeshes";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.submesh";
                element_size = sizeof(ImportedSubmesh);
                count = static_cast<uint64>(mesh.submeshes.size());
                break;
            case 6:
                name = "bone_indices";
                value_type = won::ValueType::UInt32x4;
                element_size = sizeof(uint4);
                count = static_cast<uint64>(mesh.bone_indices.size());
                break;
            case 7:
                name = "bone_weights";
                value_type = won::ValueType::Float32x4;
                element_size = sizeof(float4);
                count = static_cast<uint64>(mesh.bone_weights.size());
                break;
            case 8:
                name = "bones";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.bone";
                element_size = sizeof(ImportedBone);
                count = static_cast<uint64>(it->second->bones.size());
                break;
            case 9:
                name = "animation_clips";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.animation_clip";
                element_size = sizeof(ImportedAnimationClip);
                count = static_cast<uint64>(it->second->animation_clips.size());
                break;
            case 10:
                name = "animation_channels";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.animation_channel";
                element_size = sizeof(ImportedAnimationChannel);
                count = static_cast<uint64>(it->second->animation_channels.size());
                break;
            case 11:
                name = "animation_position_keys";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.vec3_key";
                element_size = sizeof(ImportedVec3Key);
                count = static_cast<uint64>(it->second->animation_position_keys.size());
                break;
            case 12:
                name = "animation_rotation_keys";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.quat_key";
                element_size = sizeof(ImportedQuatKey);
                count = static_cast<uint64>(it->second->animation_rotation_keys.size());
                break;
            case 13:
                name = "animation_scale_keys";
                value_type = won::ValueType::CustomStruct;
                type_name = "asset_importer.vec3_key";
                element_size = sizeof(ImportedVec3Key);
                count = static_cast<uint64>(it->second->animation_scale_keys.size());
                break;
            default:
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = name;
            call->outputs[1].type = won::ValueType::UInt32;
            call->outputs[1].uint32_value = static_cast<uint32>(value_type);
            call->outputs[2].type = won::ValueType::String;
            call->outputs[2].string_value = type_name;
            call->outputs[3].type = won::ValueType::UInt32;
            call->outputs[3].uint32_value = element_size;
            call->outputs[4].type = won::ValueType::UInt64;
            call->outputs[4].uint64_value = count;
            *call->output_count = 5;
            return true;
        }

        static bool WON_PLUGIN_CALL CopyStream(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 4 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 ||
                call->inputs[2].type != won::ValueType::Pointer || call->inputs[3].type != won::ValueType::UInt64 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            if (it == state->results.end() || !it->second || !it->second->mesh)
            {
                return false;
            }

            const void* src = nullptr;
            uint64 byte_size = 0;
            const ImportedMesh& mesh = *it->second->mesh;
            switch (call->inputs[1].uint32_value)
            {
            case 0: src = mesh.positions.data(); byte_size = static_cast<uint64>(mesh.positions.size() * sizeof(float3)); break;
            case 1: src = mesh.normals.data(); byte_size = static_cast<uint64>(mesh.normals.size() * sizeof(float3)); break;
            case 2: src = mesh.tangents.data(); byte_size = static_cast<uint64>(mesh.tangents.size() * sizeof(float4)); break;
            case 3: src = mesh.texcoords.data(); byte_size = static_cast<uint64>(mesh.texcoords.size() * sizeof(float2)); break;
            case 4: src = mesh.indices.data(); byte_size = static_cast<uint64>(mesh.indices.size() * sizeof(uint32)); break;
            case 5: src = mesh.submeshes.data(); byte_size = static_cast<uint64>(mesh.submeshes.size() * sizeof(ImportedSubmesh)); break;
            case 6: src = mesh.bone_indices.data(); byte_size = static_cast<uint64>(mesh.bone_indices.size() * sizeof(uint4)); break;
            case 7: src = mesh.bone_weights.data(); byte_size = static_cast<uint64>(mesh.bone_weights.size() * sizeof(float4)); break;
            case 8: src = it->second->bones.data(); byte_size = static_cast<uint64>(it->second->bones.size() * sizeof(ImportedBone)); break;
            case 9: src = it->second->animation_clips.data(); byte_size = static_cast<uint64>(it->second->animation_clips.size() * sizeof(ImportedAnimationClip)); break;
            case 10: src = it->second->animation_channels.data(); byte_size = static_cast<uint64>(it->second->animation_channels.size() * sizeof(ImportedAnimationChannel)); break;
            case 11: src = it->second->animation_position_keys.data(); byte_size = static_cast<uint64>(it->second->animation_position_keys.size() * sizeof(ImportedVec3Key)); break;
            case 12: src = it->second->animation_rotation_keys.data(); byte_size = static_cast<uint64>(it->second->animation_rotation_keys.size() * sizeof(ImportedQuatKey)); break;
            case 13: src = it->second->animation_scale_keys.data(); byte_size = static_cast<uint64>(it->second->animation_scale_keys.size() * sizeof(ImportedVec3Key)); break;
            default: return false;
            }

            if (byte_size > 0)
            {
                if (!call->inputs[2].pointer_value || call->inputs[3].uint64_value < byte_size)
                {
                    return false;
                }
                std::memcpy(call->inputs[2].pointer_value, src, static_cast<Size>(byte_size));
            }

            call->outputs[0].type = won::ValueType::UInt64;
            call->outputs[0].uint64_value = byte_size;
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetMaterialInfo(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 9 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            const uint32 material_index = call->inputs[1].uint32_value;
            if (it == state->results.end() || !it->second || material_index >= it->second->materials.size())
            {
                return false;
            }

            const ImportedMaterial& material = it->second->materials[material_index];
            call->outputs[0].type = won::ValueType::Float32x4;
            call->outputs[0].float_values[0] = material.base_color.x;
            call->outputs[0].float_values[1] = material.base_color.y;
            call->outputs[0].float_values[2] = material.base_color.z;
            call->outputs[0].float_values[3] = material.base_color.w;
            call->outputs[1].type = won::ValueType::Float32;
            call->outputs[1].float_value = material.metallic;
            call->outputs[2].type = won::ValueType::Float32;
            call->outputs[2].float_value = material.roughness;
            call->outputs[3].type = won::ValueType::Float32;
            call->outputs[3].float_value = material.reflectance;
            call->outputs[4].type = won::ValueType::Float32;
            call->outputs[4].float_value = material.anisotropy;
            call->outputs[5].type = won::ValueType::Float32x3;
            call->outputs[5].float_values[0] = material.sheen_color.x;
            call->outputs[5].float_values[1] = material.sheen_color.y;
            call->outputs[5].float_values[2] = material.sheen_color.z;
            call->outputs[6].type = won::ValueType::Float32;
            call->outputs[6].float_value = material.sheen_roughness;
            call->outputs[7].type = won::ValueType::Float32;
            call->outputs[7].float_value = material.clearcoat;
            call->outputs[8].type = won::ValueType::Float32;
            call->outputs[8].float_value = material.clearcoat_roughness;
            *call->output_count = 9;
            return true;
        }

        static bool WON_PLUGIN_CALL GetMaterialTextureCount(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            const uint32 material_index = call->inputs[1].uint32_value;
            if (it == state->results.end() || !it->second || material_index >= it->second->materials.size())
            {
                return false;
            }

            uint32 texture_count = 0;
            for (const ImportedTextureData& texture : it->second->textures)
            {
                if (texture.material_index == material_index)
                {
                    ++texture_count;
                }
            }

            call->outputs[0].type = won::ValueType::UInt32;
            call->outputs[0].uint32_value = texture_count;
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetMaterialTexture(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 3 || !call->outputs || call->output_capacity < 3 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || call->inputs[2].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            const uint32 material_index = call->inputs[1].uint32_value;
            const uint32 texture_index = call->inputs[2].uint32_value;
            if (it == state->results.end() || !it->second || material_index >= it->second->materials.size())
            {
                return false;
            }

            uint32 current_texture_index = 0;
            const ImportedTextureData* selected_texture = nullptr;
            for (const ImportedTextureData& texture : it->second->textures)
            {
                if (texture.material_index != material_index)
                {
                    continue;
                }
                if (current_texture_index == texture_index)
                {
                    selected_texture = &texture;
                    break;
                }
                ++current_texture_index;
            }
            if (!selected_texture || selected_texture->texture_slot >= ImportedTextureSlotCount)
            {
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = s_texture_semantics[selected_texture->texture_slot];
            call->outputs[1].type = won::ValueType::UInt32;
            call->outputs[1].uint32_value = selected_texture->source_type;
            call->outputs[2].type = won::ValueType::String;
            call->outputs[2].string_value = selected_texture->source.c_str();
            *call->output_count = 3;
            return true;
        }

        static bool WON_PLUGIN_CALL GetEmbeddedTextureInfo(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 4 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::String || !state)
            {
                return false;
            }

            const char* source = call->inputs[1].string_value;
            if (!source)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            if (it == state->results.end() || !it->second)
            {
                return false;
            }

            for (const ImportedEmbeddedTexture& texture : it->second->embedded_textures)
            {
                if (texture.source != source)
                {
                    continue;
                }

                call->outputs[0].type = won::ValueType::UInt32;
                call->outputs[0].uint32_value = texture.width;
                call->outputs[1].type = won::ValueType::UInt32;
                call->outputs[1].uint32_value = texture.height;
                call->outputs[2].type = won::ValueType::Bool;
                call->outputs[2].bool_value = texture.compressed;
                call->outputs[3].type = won::ValueType::UInt64;
                call->outputs[3].uint64_value = static_cast<uint64>(texture.bytes.size());
                *call->output_count = 4;
                return true;
            }

            return false;
        }

        static bool WON_PLUGIN_CALL CopyEmbeddedTexture(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 4 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::String ||
                call->inputs[2].type != won::ValueType::Pointer || call->inputs[3].type != won::ValueType::UInt64 || !state)
            {
                return false;
            }

            const char* source = call->inputs[1].string_value;
            uint8* dst = static_cast<uint8*>(call->inputs[2].pointer_value);
            const uint64 capacity = call->inputs[3].uint64_value;
            if (!source || !dst)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            if (it == state->results.end() || !it->second)
            {
                return false;
            }

            for (const ImportedEmbeddedTexture& texture : it->second->embedded_textures)
            {
                if (texture.source != source || capacity < texture.bytes.size())
                {
                    continue;
                }

                std::memcpy(dst, texture.bytes.data(), texture.bytes.size());
                call->outputs[0].type = won::ValueType::UInt64;
                call->outputs[0].uint64_value = static_cast<uint64>(texture.bytes.size());
                *call->output_count = 1;
                return true;
            }

            return false;
        }

        static bool WON_PLUGIN_CALL GetBoneName(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            const uint32 bone_index = call->inputs[1].uint32_value;
            if (it == state->results.end() || !it->second || bone_index >= it->second->bone_names.size())
            {
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = it->second->bone_names[bone_index].c_str();
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetAnimationClipName(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::UInt64 || call->inputs[1].type != won::ValueType::UInt32 || !state)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(state->result_mutex);
            auto it = state->results.find(call->inputs[0].uint64_value);
            const uint32 clip_index = call->inputs[1].uint32_value;
            if (it == state->results.end() || !it->second || clip_index >= it->second->animation_clip_names.size())
            {
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = it->second->animation_clip_names[clip_index].c_str();
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetStructFieldCount(void* self, const function::Call* call)
        {
            (void)self;
            if (!call || !call->inputs || call->input_count != 1 || !call->outputs || call->output_capacity < 1 || !call->output_count ||
                call->inputs[0].type != won::ValueType::String)
            {
                return false;
            }

            const char* type_name = call->inputs[0].string_value;
            if (!type_name)
            {
                return false;
            }

            uint32 field_count = 0;
            if (std::strcmp(type_name, "asset_importer.submesh") == 0) { field_count = 6; }
            else if (std::strcmp(type_name, "asset_importer.bone") == 0) { field_count = 3; }
            else if (std::strcmp(type_name, "asset_importer.animation_clip") == 0) { field_count = 4; }
            else if (std::strcmp(type_name, "asset_importer.animation_channel") == 0) { field_count = 7; }
            else if (std::strcmp(type_name, "asset_importer.vec3_key") == 0) { field_count = 2; }
            else if (std::strcmp(type_name, "asset_importer.quat_key") == 0) { field_count = 2; }
            else { return false; }

            call->outputs[0].type = won::ValueType::UInt32;
            call->outputs[0].uint32_value = field_count;
            *call->output_count = 1;
            return true;
        }

        static bool WON_PLUGIN_CALL GetStructFieldInfo(void* self, const function::Call* call)
        {
            (void)self;
            if (!call || !call->inputs || call->input_count != 2 || !call->outputs || call->output_capacity < 5 || !call->output_count ||
                call->inputs[0].type != won::ValueType::String || call->inputs[1].type != won::ValueType::UInt32)
            {
                return false;
            }

            const char* type_name = call->inputs[0].string_value;
            if (!type_name)
            {
                return false;
            }

            const uint32 field_index = call->inputs[1].uint32_value;
            const char* field_name = "";
            won::ValueType field_value_type = won::ValueType::Unknown;
            uint32 offset = 0;
            uint32 size = 0;
            const char* field_type_name = "";
            if (std::strcmp(type_name, "asset_importer.submesh") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "first_index"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedSubmesh, first_index)); size = sizeof(uint32); break;
                case 1: field_name = "index_count"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedSubmesh, index_count)); size = sizeof(uint32); break;
                case 2: field_name = "first_vertex"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedSubmesh, first_vertex)); size = sizeof(uint32); break;
                case 3: field_name = "material_index"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedSubmesh, material_index)); size = sizeof(uint32); break;
                case 4: field_name = "bounds_min"; field_value_type = won::ValueType::Float32x3; offset = static_cast<uint32>(offsetof(ImportedSubmesh, bounds_min)); size = sizeof(float3); break;
                case 5: field_name = "bounds_max"; field_value_type = won::ValueType::Float32x3; offset = static_cast<uint32>(offsetof(ImportedSubmesh, bounds_max)); size = sizeof(float3); break;
                default: return false;
                }
            }
            else if (std::strcmp(type_name, "asset_importer.bone") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "parent_index"; field_value_type = won::ValueType::Int32; offset = static_cast<uint32>(offsetof(ImportedBone, parent_index)); size = sizeof(int32); break;
                case 1: field_name = "inverse_bind_matrix"; field_value_type = won::ValueType::CustomStruct; offset = static_cast<uint32>(offsetof(ImportedBone, inverse_bind_matrix)); size = sizeof(ImportedMatrix); field_type_name = "asset_importer.float4x4"; break;
                case 2: field_name = "bind_local_transform"; field_value_type = won::ValueType::CustomStruct; offset = static_cast<uint32>(offsetof(ImportedBone, bind_local_transform)); size = sizeof(ImportedMatrix); field_type_name = "asset_importer.float4x4"; break;
                default: return false;
                }
            }
            else if (std::strcmp(type_name, "asset_importer.animation_clip") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "duration"; field_value_type = won::ValueType::Float32; offset = static_cast<uint32>(offsetof(ImportedAnimationClip, duration)); size = sizeof(float); break;
                case 1: field_name = "ticks_per_second"; field_value_type = won::ValueType::Float32; offset = static_cast<uint32>(offsetof(ImportedAnimationClip, ticks_per_second)); size = sizeof(float); break;
                case 2: field_name = "first_channel"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationClip, first_channel)); size = sizeof(uint32); break;
                case 3: field_name = "channel_count"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationClip, channel_count)); size = sizeof(uint32); break;
                default: return false;
                }
            }
            else if (std::strcmp(type_name, "asset_importer.animation_channel") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "bone_index"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, bone_index)); size = sizeof(uint32); break;
                case 1: field_name = "first_position_key"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, first_position_key)); size = sizeof(uint32); break;
                case 2: field_name = "position_key_count"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, position_key_count)); size = sizeof(uint32); break;
                case 3: field_name = "first_rotation_key"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, first_rotation_key)); size = sizeof(uint32); break;
                case 4: field_name = "rotation_key_count"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, rotation_key_count)); size = sizeof(uint32); break;
                case 5: field_name = "first_scale_key"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, first_scale_key)); size = sizeof(uint32); break;
                case 6: field_name = "scale_key_count"; field_value_type = won::ValueType::UInt32; offset = static_cast<uint32>(offsetof(ImportedAnimationChannel, scale_key_count)); size = sizeof(uint32); break;
                default: return false;
                }
            }
            else if (std::strcmp(type_name, "asset_importer.vec3_key") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "time"; field_value_type = won::ValueType::Float32; offset = static_cast<uint32>(offsetof(ImportedVec3Key, time)); size = sizeof(float); break;
                case 1: field_name = "value"; field_value_type = won::ValueType::Float32x3; offset = static_cast<uint32>(offsetof(ImportedVec3Key, value)); size = sizeof(float3); break;
                default: return false;
                }
            }
            else if (std::strcmp(type_name, "asset_importer.quat_key") == 0)
            {
                switch (field_index)
                {
                case 0: field_name = "time"; field_value_type = won::ValueType::Float32; offset = static_cast<uint32>(offsetof(ImportedQuatKey, time)); size = sizeof(float); break;
                case 1: field_name = "value"; field_value_type = won::ValueType::Float32x4; offset = static_cast<uint32>(offsetof(ImportedQuatKey, value)); size = sizeof(float4); break;
                default: return false;
                }
            }
            else
            {
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = field_name;
            call->outputs[1].type = won::ValueType::UInt32;
            call->outputs[1].uint32_value = static_cast<uint32>(field_value_type);
            call->outputs[2].type = won::ValueType::UInt32;
            call->outputs[2].uint32_value = offset;
            call->outputs[3].type = won::ValueType::UInt32;
            call->outputs[3].uint32_value = size;
            call->outputs[4].type = won::ValueType::String;
            call->outputs[4].string_value = field_type_name;
            *call->output_count = 5;
            return true;
        }

        static bool WON_PLUGIN_CALL ReleaseResult(void* self, const function::Call* call)
        {
            auto* state = static_cast<AssetImporterState*>(self);
            if (!state || !call || !call->inputs || call->input_count != 1 || call->inputs[0].type != won::ValueType::UInt64)
            {
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(state->result_mutex);
                state->results.erase(call->inputs[0].uint64_value);
            }
            if (call->output_count)
            {
                *call->output_count = 0;
            }
            return true;
        }

        const function::ParamDesc s_import_inputs[] = {
            { "path", won::ValueType::String },
        };
        const function::ParamDesc s_import_outputs[] = {
            { "result", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_result_handle_input[] = {
            { "result", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_indexed_result_inputs[] = {
            { "result", won::ValueType::UInt64 },
            { "index", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_result_info_outputs[] = {
            { "name", won::ValueType::String },
            { "stream_count", won::ValueType::UInt32 },
            { "material_count", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_material_info_outputs[] = {
            { "base_color", won::ValueType::Float32x4 },
            { "metallic", won::ValueType::Float32 },
            { "roughness", won::ValueType::Float32 },
            { "reflectance", won::ValueType::Float32 },
            { "anisotropy", won::ValueType::Float32 },
            { "sheen_color", won::ValueType::Float32x3 },
            { "sheen_roughness", won::ValueType::Float32 },
            { "clearcoat", won::ValueType::Float32 },
            { "clearcoat_roughness", won::ValueType::Float32 },
        };
        const function::ParamDesc s_material_texture_inputs[] = {
            { "result", won::ValueType::UInt64 },
            { "material_index", won::ValueType::UInt32 },
            { "texture_index", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_material_texture_outputs[] = {
            { "semantic", won::ValueType::String },
            { "source_type", won::ValueType::UInt32 },
            { "source", won::ValueType::String },
        };
        const function::ParamDesc s_embedded_texture_inputs[] = {
            { "result", won::ValueType::UInt64 },
            { "source", won::ValueType::String },
        };
        const function::ParamDesc s_embedded_texture_info_outputs[] = {
            { "width", won::ValueType::UInt32 },
            { "height", won::ValueType::UInt32 },
            { "compressed", won::ValueType::Bool },
            { "byte_size", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_copy_embedded_texture_inputs[] = {
            { "result", won::ValueType::UInt64 },
            { "source", won::ValueType::String },
            { "dst", won::ValueType::Pointer },
            { "capacity", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_copy_embedded_texture_outputs[] = {
            { "copied_bytes", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_stream_info_outputs[] = {
            { "name", won::ValueType::String },
            { "value_type", won::ValueType::UInt32 },
            { "type_name", won::ValueType::String },
            { "element_size", won::ValueType::UInt32 },
            { "count", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_copy_stream_inputs[] = {
            { "result", won::ValueType::UInt64 },
            { "stream_index", won::ValueType::UInt32 },
            { "dst", won::ValueType::Pointer },
            { "capacity", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_copy_stream_outputs[] = {
            { "copied_bytes", won::ValueType::UInt64 },
        };
        const function::ParamDesc s_type_name_inputs[] = {
            { "type_name", won::ValueType::String },
        };
        const function::ParamDesc s_struct_field_info_inputs[] = {
            { "type_name", won::ValueType::String },
            { "field_index", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_struct_field_count_outputs[] = {
            { "field_count", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_struct_field_info_outputs[] = {
            { "name", won::ValueType::String },
            { "value_type", won::ValueType::UInt32 },
            { "offset", won::ValueType::UInt32 },
            { "size", won::ValueType::UInt32 },
            { "type_name", won::ValueType::String },
        };
        const function::ParamDesc s_material_texture_count_outputs[] = {
            { "texture_count", won::ValueType::UInt32 },
        };
        const function::ParamDesc s_name_outputs[] = {
            { "name", won::ValueType::String },
        };

        const function::Desc s_import_desc{ sizeof(function::Desc), "Import Asset", s_import_inputs, 1, s_import_outputs, 1, &ImportAsset };
        const function::Desc s_get_result_info_desc{ sizeof(function::Desc), "Get Result Info", s_result_handle_input, 1, s_result_info_outputs, 3, &GetResultInfo };
        const function::Desc s_get_stream_info_desc{ sizeof(function::Desc), "Get Stream Info", s_indexed_result_inputs, 2, s_stream_info_outputs, 5, &GetStreamInfo };
        const function::Desc s_copy_stream_desc{ sizeof(function::Desc), "Copy Stream", s_copy_stream_inputs, 4, s_copy_stream_outputs, 1, &CopyStream };
        const function::Desc s_get_struct_field_count_desc{ sizeof(function::Desc), "Get Struct Field Count", s_type_name_inputs, 1, s_struct_field_count_outputs, 1, &GetStructFieldCount };
        const function::Desc s_get_struct_field_info_desc{ sizeof(function::Desc), "Get Struct Field Info", s_struct_field_info_inputs, 2, s_struct_field_info_outputs, 5, &GetStructFieldInfo };
        const function::Desc s_get_material_info_desc{ sizeof(function::Desc), "Get Material Info", s_indexed_result_inputs, 2, s_material_info_outputs, 9, &GetMaterialInfo };
        const function::Desc s_get_material_texture_count_desc{ sizeof(function::Desc), "Get Material Texture Count", s_indexed_result_inputs, 2, s_material_texture_count_outputs, 1, &GetMaterialTextureCount };
        const function::Desc s_get_material_texture_desc{ sizeof(function::Desc), "Get Material Texture", s_material_texture_inputs, 3, s_material_texture_outputs, 3, &GetMaterialTexture };
        const function::Desc s_get_embedded_texture_info_desc{ sizeof(function::Desc), "Get Embedded Texture Info", s_embedded_texture_inputs, 2, s_embedded_texture_info_outputs, 4, &GetEmbeddedTextureInfo };
        const function::Desc s_copy_embedded_texture_desc{ sizeof(function::Desc), "Copy Embedded Texture", s_copy_embedded_texture_inputs, 4, s_copy_embedded_texture_outputs, 1, &CopyEmbeddedTexture };
        const function::Desc s_get_bone_name_desc{ sizeof(function::Desc), "Get Bone Name", s_indexed_result_inputs, 2, s_name_outputs, 1, &GetBoneName };
        const function::Desc s_get_animation_clip_name_desc{ sizeof(function::Desc), "Get Animation Clip Name", s_indexed_result_inputs, 2, s_name_outputs, 1, &GetAnimationClipName };
        const function::Desc s_release_result_desc{ sizeof(function::Desc), "Release Result", s_result_handle_input, 1, nullptr, 0, &ReleaseResult };

        const WonExtensionDesc s_extensions[] = {
            { sizeof(WonExtensionDesc), function::ExtensionType, import_function_id, &s_import_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_result_info_function_id, &s_get_result_info_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_stream_info_function_id, &s_get_stream_info_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, copy_stream_function_id, &s_copy_stream_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_struct_field_count_function_id, &s_get_struct_field_count_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_struct_field_info_function_id, &s_get_struct_field_info_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_material_info_function_id, &s_get_material_info_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_material_texture_count_function_id, &s_get_material_texture_count_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_material_texture_function_id, &s_get_material_texture_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_embedded_texture_info_function_id, &s_get_embedded_texture_info_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, copy_embedded_texture_function_id, &s_copy_embedded_texture_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_bone_name_function_id, &s_get_bone_name_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, get_animation_clip_name_function_id, &s_get_animation_clip_name_desc },
            { sizeof(WonExtensionDesc), function::ExtensionType, release_result_function_id, &s_release_result_desc },
        };
    }
}

WON_PLUGIN_EXPORT bool WON_PLUGIN_CALL WonPluginCreate(const WonPluginHostAPI* host_api, void** out_plugin, WonPluginAPI* out_api)
{
    if (!host_api || !out_plugin || !out_api || host_api->abi_version != WON_PLUGIN_ABI_VERSION)
    {
        return false;
    }

    auto* state = new won::plugin::AssetImporterState();
    state->host_api = host_api;
    *out_plugin = state;
    out_api->abi_version = WON_PLUGIN_ABI_VERSION;
    out_api->plugin_id = won::plugin::plugin_id;
    out_api->plugin_version = won::plugin::plugin_version;
    out_api->extensions = won::plugin::s_extensions;
    out_api->extension_count = static_cast<uint32_t>(sizeof(won::plugin::s_extensions) / sizeof(won::plugin::s_extensions[0]));
    return true;
}

WON_PLUGIN_EXPORT void WON_PLUGIN_CALL WonPluginDestroy(void* plugin)
{
    delete static_cast<won::plugin::AssetImporterState*>(plugin);
}
