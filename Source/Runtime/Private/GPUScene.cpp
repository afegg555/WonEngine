#include "GPUScene.h"
#include "ShaderInterop_PostProcess.h"

#include "Scene.h"
#include "LightComponent.h"
#include "GeometryComponent.h"
#include "MaterialComponent.h"
#include "AnimationComponent.h"
#include "RHIDevice.h"
#include "RHICommandList.h"
#include "Backlog.h"
#include "JobSystem.h"
#include "MathUtils.h"
#include "Profiler.h"
#include "StringUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace won::rendering
{
    namespace
    {
        using namespace won::ecs;

        bool UploadBuffer(GPUBuffer& target, Vector<std::unique_ptr<RHIResource>>& retire,
            const void* data, Size size, Size stride,
            RHIDevice& device, RHICommandList& command_list, uint32 frame_slot)
        {
            if (size == 0)
            {
                if (target.buffer)
                {
                    retire.push_back(std::move(target.buffer));
                }
                target.buffer = nullptr;
                target.srv = {};
                return true;
            }

            const Size current_buffer_size = target.buffer ? target.buffer->GetDesc().buffer_desc.size : 0;
            if (!target.buffer || current_buffer_size < size)
            {
                if (target.buffer)
                {
                    retire.push_back(std::move(target.buffer));
                }

                RHIBufferDesc buffer_desc = {};
                buffer_desc.size = size;
                buffer_desc.usage = RHIResourceUsage::Default;
                buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                target.buffer = device.CreateBuffer(buffer_desc);
                if (!target.buffer)
                {
                    backlog::Post("GPUScene: failed to create default buffer", backlog::LogLevel::Error);
                    return false;
                }

                target.srv = {};
                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.buffer_offset = 0;
                srv_desc.buffer_size = target.buffer->GetDesc().buffer_desc.size;
                srv_desc.buffer_stride = stride;
                if (!device.CreateSubresource(*target.buffer, srv_desc, &target.srv))
                {
                    backlog::Post("GPUScene: failed to create buffer subresource", backlog::LogLevel::Error);
                    target.buffer = nullptr;
                    return false;
                }
            }

            std::unique_ptr<RHIResource>& staging = target.staging[frame_slot];
            const Size current_staging_size = staging ? staging->GetDesc().buffer_desc.size : 0;
            if (!staging || current_staging_size < size)
            {
                RHIBufferDesc staging_desc = {};
                staging_desc.size = size;
                staging_desc.usage = RHIResourceUsage::Upload;
                staging_desc.bind_flags = RHIBindFlags::None;
                staging = device.CreateBuffer(staging_desc);
                if (!staging)
                {
                    backlog::Post("GPUScene: failed to create staging buffer", backlog::LogLevel::Error);
                    return false;
                }
            }

            void* mapped = staging->GetMappedData();
            if (!mapped)
            {
                backlog::Post("GPUScene: failed to map staging buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped, data, size);

            command_list.TransitionResource(*target.buffer, RHIResourceState::CopyDest);
            command_list.CopyBuffer(*target.buffer, 0, *staging, 0, size);
            command_list.TransitionResource(*target.buffer, RHIResourceState::ShaderRead);
            return true;
        }

        void ExtractLights(const ecs::Scene& scene, Vector<ShaderLight>& shader_lights, Vector<math::AABB>& light_bounds, uint32& directional_count)
        {
            auto light_array = scene.GetComponentArray<ecs::LightComponent>().get();
            shader_lights.clear();
            light_bounds.clear();
            directional_count = 0;

            if (!light_array)
            {
                return;
            }

            const Size total = light_array->GetSize();
            shader_lights.reserve(total);
            light_bounds.reserve(total);

            auto emit = [&](const ecs::LightComponent& light)
            {
                ShaderLight shader_light;
                shader_light.Init();
                shader_light.position = light.position;
                shader_light.SetType(static_cast<uint32>(light.type));
                shader_light.SetDirection(light.direction);

				float radiance_intensity = light.intensity; // in directional light, intensity is illuminance
                float effective_range = light.range;
                if (light.type == LightComponent::LightType::Point || light.type == LightComponent::LightType::Spot)
                {
					radiance_intensity = light.intensity / (4.0f * math::PI); // luminous power to luminous intensity

                    shader_light.SetOuterConeAngleCos(std::cos(light.outer_cone_angle));
                    shader_light.SetInnerConeAngleCos(std::cos(light.inner_cone_angle));
                }
                else if (light.type == LightComponent::LightType::Rect)
                {
                    const float area = std::max(light.area_size.x * light.area_size.y, 0.0001f);
					radiance_intensity = light.intensity / (math::PI * area); // luminous power to luminance

                    shader_light.SetRight(light.right);
                    shader_light.SetHalfExtents({ light.area_size.x * 0.5f, light.area_size.y * 0.5f });
                    const float half_diagonal = 0.5f * std::sqrt(light.area_size.x * light.area_size.x + light.area_size.y * light.area_size.y);
                    effective_range = light.range + half_diagonal;
                }

                shader_light.SetColor({ light.color.x * radiance_intensity, light.color.y * radiance_intensity, light.color.z * radiance_intensity, radiance_intensity });
                shader_light.SetRange(effective_range);

                if (!light.IsDynamic())
                {
                    shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_STATIC);
                }
                if (light.IsCastShadow())
                {
                    shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_CASTING_SHADOW);
                }
                if (light.IsTwoSided())
                {
                    shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_TWO_SIDED);
                }
                shader_lights.push_back(shader_light);
                light_bounds.push_back(light.aabb);
            };

            for (Size i = 0; i < total; ++i)
            {
                const ecs::LightComponent& light = light_array->data[i];
                if (light.IsActive() && light.type == ecs::LightComponent::LightType::Directional)
                {
                    emit(light);
                }
            }
            directional_count = static_cast<uint32>(shader_lights.size());
            for (Size i = 0; i < total; ++i)
            {
                const ecs::LightComponent& light = light_array->data[i];
                if (light.IsActive() && light.type != ecs::LightComponent::LightType::Directional)
                {
                    emit(light);
                }
            }
        }

        void ExtractGeometries(ecs::Scene& scene, Vector<ShaderGeometry>& shader_geometries)
        {
            jobsystem::Context sub_ctx;

            const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

            Vector<std::pair<resource::Mesh*, uint32>> unique_meshes;
            Size unique_submesh_sum = 0;
            {
                UnorderedMap<resource::Mesh*, uint32> offsets;
                for (Size i = 0; i < geometry_array->GetSize(); ++i)
                {
                    GeometryComponent& geometry_comp = geometry_array->data[i];
                    if (!geometry_comp.mesh)
                    {
                        continue;
                    }

                    resource::Mesh* mesh = geometry_comp.mesh.get();
                    const uint32 offset = (uint32)unique_submesh_sum;
                    auto [it, inserted] = offsets.try_emplace(mesh, offset);
                    if (inserted)
                    {
                        unique_meshes.push_back({ mesh, offset });
                        unique_submesh_sum += mesh->submeshes.size();
                    }
                    geometry_comp.geometry_offset = it->second;
                }
            }

            shader_geometries.resize(unique_submesh_sum);

            jobsystem::Dispatch(sub_ctx, (uint32_t)unique_meshes.size(), jobsystem::groupsize_light, [&](jobsystem::JobArgs args) {
                auto [mesh, geometry_offset] = unique_meshes[args.job_index];
                const resource::Mesh::RenderData& mesh_render_data = mesh->render_data;

                for (Size i = 0; i < mesh->submeshes.size(); ++i)
                {
                    ShaderGeometry& shader_geometry = shader_geometries[geometry_offset + i];
                    shader_geometry.Init();
                    shader_geometry.bounds_min = mesh->submeshes[i].local_bounds.min;
                    shader_geometry.bounds_max = mesh->submeshes[i].local_bounds.max;

                    if (mesh_render_data.IsValid())
                    {
                        shader_geometry.position_buffer_descriptor = mesh_render_data.positions.handle.descriptor_index;
                        shader_geometry.color_buffer_descriptor = mesh_render_data.colors.handle.descriptor_index;
                        shader_geometry.normal_buffer_descriptor = mesh_render_data.normals.handle.descriptor_index;
                        shader_geometry.texcoord_buffer_descriptor = mesh_render_data.texcoords.handle.descriptor_index;
                        shader_geometry.tangent_buffer_descriptor = mesh_render_data.tangents.handle.descriptor_index;
                        shader_geometry.index_buffer_descriptor = mesh_render_data.indices.handle.descriptor_index;
                        shader_geometry.index_count = mesh->submeshes[i].index_count;
                        shader_geometry.first_index = mesh->submeshes[i].first_index;

                        if (mesh_render_data.bone_indices.IsValid() && mesh_render_data.bone_weights.IsValid())
                        {
                            shader_geometry.bone_indices_buffer_descriptor = mesh_render_data.bone_indices.handle.descriptor_index;
                            shader_geometry.bone_weights_buffer_descriptor = mesh_render_data.bone_weights.handle.descriptor_index;
                            shader_geometry.flags |= SHADER_GEOMETRY_FLAG_SKINNED;
                        }
                    }
                }
            });

            jobsystem::Wait(sub_ctx);
        }

        void ExtractMaterials(ecs::Scene& scene, Vector<ShaderMaterial>& shader_materials)
        {
            jobsystem::Context sub_ctx;

            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

            Vector<std::pair<resource::Material*, uint32>> unique_materials;
            Size unique_slot_sum = 0;
            {
                UnorderedMap<resource::Material*, uint32> offsets;
                for (Size i = 0; i < material_array->GetSize(); ++i)
                {
                    MaterialComponent& material_comp = material_array->data[i];
                    if (!material_comp.material)
                    {
                        continue;
                    }

                    resource::Material* material = material_comp.material.get();
                    const uint32 offset = (uint32)unique_slot_sum;
					auto [it, inserted] = offsets.try_emplace(material, offset);
					if (inserted) // newly inserted, so this is a unique material
                    {
                        unique_materials.push_back({ material, offset });
                        unique_slot_sum += material->slots.size();
                    }
                    material_comp.material_offset = it->second;
                }
            }

            shader_materials.resize(unique_slot_sum);

            jobsystem::Dispatch(sub_ctx, (uint32_t)unique_materials.size(), jobsystem::groupsize_light, [&](jobsystem::JobArgs args) {
                auto [material, material_offset] = unique_materials[args.job_index];

                for (Size i = 0; i < material->slots.size(); ++i)
                {
                    const resource::MaterialSlot& material_slot = material->slots[i];
                    ShaderMaterial& shader_material = shader_materials[material_offset + i];
                    shader_material.Init();
                    shader_material.base_color = math::PackHalf4(material_slot.base_color);
                    shader_material.emissive_color_metallic = math::PackHalf4(
                        material_slot.emissive_color.x * material_slot.emissive_intensity,
                        material_slot.emissive_color.y * material_slot.emissive_intensity,
                        material_slot.emissive_color.z * material_slot.emissive_intensity,
                        material_slot.metallic);
                    shader_material.roughness_reflectance_refraction_padding = math::PackHalf4(material_slot.roughness, material_slot.reflectance, 0.f, 0.f);
                    shader_material.anisotropy_sheenroughness_clearcoat_clearcoatroughness = math::PackHalf4(material_slot.anisotropy, material_slot.sheen_roughness, material_slot.clearcoat, material_slot.clearcoat_roughness);
                    shader_material.sheencolor_alphacutoff = math::PackHalf4(material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z, material_slot.alpha_cutoff);
                    uint32 gpu_flags = SHADER_MATERIAL_FLAG_NONE;
                    if (material_slot.double_sided) { gpu_flags |= SHADER_MATERIAL_FLAG_DOUBLE_SIDED; }
                    if (material_slot.use_vertex_colors) { gpu_flags |= SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; }
                    if (material_slot.receive_shadow) { gpu_flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; }
                    shader_material.flags = gpu_flags;

                    for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
                    {
                        if (material_slot.textures[texture_slot].IsValid())
                        {
                            shader_material.textures[texture_slot].texture_descriptor = material_slot.textures[texture_slot].image->render_data.srv.descriptor_index;
                        }
                    }
                }
            });

            jobsystem::Wait(sub_ctx);
        }

        void ExtractBones(const ecs::Scene& scene, Vector<float4>& shader_bone_matrices)
        {
            const auto animation_array = scene.GetComponentArray<AnimationComponent>().get();
            shader_bone_matrices.clear();
            if (!animation_array)
            {
                return;
            }

            const Size anim_count = animation_array->GetSize();
            Size total_bone_count = 0;
            for (Size i = 0; i < anim_count; ++i)
            {
                total_bone_count += animation_array->data[i].bone_matrices.size();
            }
            if (total_bone_count == 0)
            {
                return;
            }

            shader_bone_matrices.resize(total_bone_count * 4);

            for (Size i = 0; i < anim_count; ++i)
            {
                const AnimationComponent& animation = animation_array->data[i];
                const Size bone_count = animation.bone_matrices.size();
                for (Size b = 0; b < bone_count; ++b)
                {
                    const float4x4& bone_matrix = animation.bone_matrices[b];
                    const Size idx = static_cast<Size>(animation.bone_matrix_offset + b) * 4;
                    shader_bone_matrices[idx + 0] = { bone_matrix._11, bone_matrix._21, bone_matrix._31, bone_matrix._41 };
                    shader_bone_matrices[idx + 1] = { bone_matrix._12, bone_matrix._22, bone_matrix._32, bone_matrix._42 };
                    shader_bone_matrices[idx + 2] = { bone_matrix._13, bone_matrix._23, bone_matrix._33, bone_matrix._43 };
                    shader_bone_matrices[idx + 3] = { bone_matrix._14, bone_matrix._24, bone_matrix._34, bone_matrix._44 };
                }
            }
        }

        void ExtractRenderables(const ecs::Scene& scene, Vector<ShaderInstance>& shader_instances,
            Vector<Renderable>& opaque_renderables, Vector<Renderable>& transparent_renderables,
            Vector<Renderable>& line_renderables, Vector<Renderable>& point_renderables,
            math::AABB& shadow_caster_world_bound)
        {
            struct RenderableBucket
            {
                Vector<Renderable> opaque;
                Vector<Renderable> transparent;
                Vector<Renderable> line;
                Vector<Renderable> point;
                math::AABB caster_bound;
            };

            jobsystem::Context sub_ctx;

            const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
            const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
            const auto animation_array = scene.GetComponentArray<AnimationComponent>().get();
            const auto layer_array = scene.GetComponentArray<VisibilityLayerComponent>().get();

            shader_instances.resize(transform_array->GetSize());

            opaque_renderables.clear();
            transparent_renderables.clear();
            line_renderables.clear();
            point_renderables.clear();

            const uint32 job_count = static_cast<uint32>(transform_array->GetSize());
            Vector<RenderableBucket> renderable_buckets(jobsystem::DispatchGroupCount(job_count, jobsystem::groupsize));
            for (RenderableBucket& bucket : renderable_buckets)
            {
                bucket.caster_bound.Invalidate();
            }

            jobsystem::Dispatch(sub_ctx, job_count, jobsystem::groupsize, [&](jobsystem::JobArgs args) {
                RenderableBucket& bucket = renderable_buckets[args.group_id];

                const TransformComponent& transform = transform_array->data[args.job_index];
                ShaderInstance& shader_instance = shader_instances[args.job_index];
                shader_instance.Init();
                shader_instance.world_transform = transform.world_transform;

                XMMATRIX x_normal_mat = XMLoadFloat4x4(&transform.world_transform);
                x_normal_mat.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                x_normal_mat = XMMatrixInverse(nullptr, x_normal_mat);
                x_normal_mat = XMMatrixTranspose(x_normal_mat);
                XMFLOAT3X3 normal_mat_3x3;
                XMStoreFloat3x3(&normal_mat_3x3, x_normal_mat);
                shader_instance.normal_transform_row0 = { normal_mat_3x3._11, normal_mat_3x3._12, normal_mat_3x3._13 };
                shader_instance.normal_transform_row1 = { normal_mat_3x3._21, normal_mat_3x3._22, normal_mat_3x3._23 };
                shader_instance.normal_transform_row2 = { normal_mat_3x3._31, normal_mat_3x3._32, normal_mat_3x3._33 };

                Entity entity = transform_array->index_to_entity[args.job_index];
                if (animation_array && animation_array->HasData(entity))
                {
                    const AnimationComponent& animation = animation_array->GetData(entity);
                    if (!animation.bone_matrices.empty())
                    {
                        shader_instance.bone_matrix_offset = animation.bone_matrix_offset;
                        shader_instance.bone_count = static_cast<uint32>(animation.bone_matrices.size());
                    }
                }

                if (geometry_array->HasData(entity) && material_array->HasData(entity))
                {
                    const GeometryComponent& geometry_comp = geometry_array->GetData(entity);
                    const MaterialComponent& material_comp = material_array->GetData(entity);
                    if (!geometry_comp.mesh || !material_comp.material)
                    {
                        return;
                    }

                    const resource::Mesh::RenderData& mesh_render_data = geometry_comp.mesh->render_data;
                    if (!mesh_render_data.IsValid())
                    {
                        return;
                    }

                    const float3 world_position = math::GetPosition(transform.world_transform);
                    math::AABB world_aabb;
                    world_aabb.Invalidate();
                    if (geometry_comp.local_bounds.IsValid())
                    {
                        world_aabb = geometry_comp.local_bounds.TransformAABB(transform.world_transform);
                    }

                    if (geometry_comp.IsCastShadow() && world_aabb.IsValid())
                    {
                        bucket.caster_bound.Merge(world_aabb);
                    }

                    for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                    {
                        const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                        if (submesh.material_slot >= material_comp.material->slots.size())
                        {
                            continue;
                        }

                        const resource::MaterialSlot& material_slot = material_comp.material->slots[submesh.material_slot];
                        Renderable renderable = {};
                        ObjectPushConstants& push_constants = renderable.push_constants;
                        push_constants.Init();
                        push_constants.geometry_index = geometry_comp.geometry_offset + (uint)i;
                        push_constants.material_index = material_comp.material_offset + submesh.material_slot;
                        push_constants.draw_offset = (uint)args.job_index;

                        renderable.index_buffer = mesh_render_data.buffer.get();
                        renderable.index_offset = mesh_render_data.indices.offset + submesh.first_index * sizeof(uint32);
                        renderable.index_count = submesh.index_count;
                        renderable.world_position = world_position;
                        renderable.aabb = submesh.local_bounds.IsValid()
                            ? submesh.local_bounds.TransformAABB(transform.world_transform)
                            : world_aabb;
                        renderable.primitive_topology = submesh.primitive_topology;
                        renderable.shader_type = static_cast<uint32>(material_slot.material_type);
                        renderable.blend_mode = material_slot.blend_mode;
                        renderable.flags = Renderable::None;
                        renderable.layer_mask = (layer_array && layer_array->HasData(entity)) ? layer_array->GetData(entity).layer_mask : 0xFFFFFFFF;
                        if (geometry_comp.IsCastShadow())
                        {
                            renderable.flags |= Renderable::CastShadow;
                        }
                        if (material_slot.double_sided)
                        {
                            renderable.flags |= Renderable::DoubleSided;
                        }

                        if (submesh.primitive_topology == resource::PrimitiveTopology::LineList)
                        {
                            bucket.line.push_back(renderable);
                        }
                        else if (submesh.primitive_topology == resource::PrimitiveTopology::PointList)
                        {
                            bucket.point.push_back(renderable);
                        }
                        else if (material_slot.IsTransparent())
                        {
                            bucket.transparent.push_back(renderable);
                        }
                        else
                        {
                            bucket.opaque.push_back(renderable);
                        }
                    }
                }
            });
            jobsystem::Wait(sub_ctx);

            Size opaque_count = 0;
            Size transparent_count = 0;
            Size line_count = 0;
            Size point_count = 0;
            for (const RenderableBucket& bucket : renderable_buckets)
            {
                opaque_count += bucket.opaque.size();
                transparent_count += bucket.transparent.size();
                line_count += bucket.line.size();
                point_count += bucket.point.size();
            }

            opaque_renderables.reserve(opaque_count);
            transparent_renderables.reserve(transparent_count);
            line_renderables.reserve(line_count);
            point_renderables.reserve(point_count);
            shadow_caster_world_bound.Invalidate();
            for (RenderableBucket& bucket : renderable_buckets)
            {
                if (bucket.caster_bound.IsValid())
                {
                    shadow_caster_world_bound.Merge(bucket.caster_bound);
                }
                opaque_renderables.insert(opaque_renderables.end(), std::make_move_iterator(bucket.opaque.begin()), std::make_move_iterator(bucket.opaque.end()));
                transparent_renderables.insert(transparent_renderables.end(), std::make_move_iterator(bucket.transparent.begin()), std::make_move_iterator(bucket.transparent.end()));
                line_renderables.insert(line_renderables.end(), std::make_move_iterator(bucket.line.begin()), std::make_move_iterator(bucket.line.end()));
                point_renderables.insert(point_renderables.end(), std::make_move_iterator(bucket.point.begin()), std::make_move_iterator(bucket.point.end()));
            }
        }

        void ExtractSprites(const ecs::Scene& scene, Vector<Sprite2DRenderable>& sprite_2d_renderables, Vector<Sprite3DRenderable>& sprite_3d_renderables)
        {
            struct SpriteBucket
            {
                Vector<Sprite2DRenderable> sprite_2d_renderables;
                Vector<Sprite3DRenderable> sprite_3d_renderables;
            };

            jobsystem::Context sub_ctx;

            const auto sprite_2d_array = scene.GetComponentArray<Sprite2DComponent>().get();
            const auto sprite_3d_array = scene.GetComponentArray<Sprite3DComponent>().get();
            const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
            const auto layer_array = scene.GetComponentArray<VisibilityLayerComponent>().get();
            const auto rect_transform_array = scene.GetComponentArray<RectTransform2DComponent>().get();
            const auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

            sprite_2d_renderables.clear();
            sprite_3d_renderables.clear();
            if (!material_array)
            {
                return;
            }

            Vector<SpriteBucket> sprite_2d_buckets;
            Vector<SpriteBucket> sprite_3d_buckets;
            if (sprite_2d_array)
            {
                const uint32 sprite_2d_job_count = static_cast<uint32>(sprite_2d_array->GetSize());
                sprite_2d_buckets.resize(jobsystem::DispatchGroupCount(sprite_2d_job_count, jobsystem::groupsize_light));
                jobsystem::Dispatch(sub_ctx, sprite_2d_job_count, jobsystem::groupsize_light, [&](jobsystem::JobArgs args) {
                    SpriteBucket& bucket = sprite_2d_buckets[args.group_id];

                    const Entity entity = sprite_2d_array->index_to_entity[args.job_index];
                    if (!material_array->HasData(entity))
                    {
                        return;
                    }

                    const Sprite2DComponent& sprite = sprite_2d_array->data[args.job_index];
                    const MaterialComponent& material = material_array->GetData(entity);
                    if (material.GetMaterialSlotCount() == 0)
                    {
                        return;
                    }

                    if (!rect_transform_array || !rect_transform_array->HasData(entity))
                    {
                        return;
                    }
                    if (!hierarchy_array || !hierarchy_array->HasData(entity))
                    {
                        return;
                    }
                    const RectTransform2DComponent& rect = rect_transform_array->GetData(entity);

                    Sprite2DRenderable renderable = {};
                    renderable.material_index = material.material_offset;
                    renderable.anchor = { 0.0f, 0.0f };
                    renderable.position = rect.resolved_position;
                    renderable.size = rect.resolved_size;
                    renderable.pivot = { 0.0f, 0.0f };
                    renderable.reference_resolution = rect.reference_resolution;
                    renderable.uv_rect = sprite.uv_rect;
                    renderable.layer = sprite.layer;
                    renderable.layer_mask = rect.layer_mask;
                    renderable.match = rect.match;
                    bucket.sprite_2d_renderables.push_back(renderable);
                });
            }

            if (sprite_3d_array && transform_array)
            {
                const uint32 sprite_3d_job_count = static_cast<uint32>(sprite_3d_array->GetSize());
                sprite_3d_buckets.resize(jobsystem::DispatchGroupCount(sprite_3d_job_count, jobsystem::groupsize_light));
                jobsystem::Dispatch(sub_ctx, sprite_3d_job_count, jobsystem::groupsize_light, [&](jobsystem::JobArgs args) {
                    SpriteBucket& bucket = sprite_3d_buckets[args.group_id];

                    const Entity entity = sprite_3d_array->index_to_entity[args.job_index];
                    if (!transform_array->HasData(entity) || !material_array->HasData(entity))
                    {
                        return;
                    }

                    const Sprite3DComponent& sprite = sprite_3d_array->data[args.job_index];
                    const MaterialComponent& material = material_array->GetData(entity);
                    if (material.GetMaterialSlotCount() == 0)
                    {
                        return;
                    }
                    const resource::MaterialSlot& material_slot = material.material->slots[0];

                    Sprite3DRenderable renderable = {};
                    renderable.instance_index = static_cast<uint32>(transform_array->entity_to_index.at(entity));
                    renderable.material_index = material.material_offset;
                    renderable.world_position = math::GetPosition(transform_array->GetData(entity).world_transform);
                    renderable.size = sprite.size;
                    renderable.pivot = sprite.pivot;
                    renderable.uv_rect = sprite.uv_rect;
                    if (sprite.IsBillboard())
                    {
                        renderable.flags |= Sprite3DRenderable::Billboard;
                        const float r = std::max(sprite.size.x, sprite.size.y) * 0.5f;
                        renderable.aabb.min = { renderable.world_position.x - r, renderable.world_position.y - r, renderable.world_position.z - r };
                        renderable.aabb.max = { renderable.world_position.x + r, renderable.world_position.y + r, renderable.world_position.z + r };
                    }
                    else
                    {
                        const float lx = -sprite.pivot.x * sprite.size.x;
                        const float ly = -sprite.pivot.y * sprite.size.y;
                        const float3 local_corners[4] = {
                            { lx,                  ly,                  0.0f },
                            { lx + sprite.size.x,  ly,                  0.0f },
                            { lx,                  ly + sprite.size.y,  0.0f },
                            { lx + sprite.size.x,  ly + sprite.size.y,  0.0f },
                        };
                        const XMMATRIX world = XMLoadFloat4x4(&transform_array->GetData(entity).world_transform);
                        renderable.aabb.Invalidate();
                        for (const float3& c : local_corners)
                        {
                            float3 wc = {};
                            XMStoreFloat3(&wc, XMVector3TransformCoord(XMLoadFloat3(&c), world));
                            renderable.aabb.min.x = std::min(renderable.aabb.min.x, wc.x);
                            renderable.aabb.min.y = std::min(renderable.aabb.min.y, wc.y);
                            renderable.aabb.min.z = std::min(renderable.aabb.min.z, wc.z);
                            renderable.aabb.max.x = std::max(renderable.aabb.max.x, wc.x);
                            renderable.aabb.max.y = std::max(renderable.aabb.max.y, wc.y);
                            renderable.aabb.max.z = std::max(renderable.aabb.max.z, wc.z);
                        }
                    }
                    renderable.blend_mode = material_slot.blend_mode;
                    if (material_slot.IsTransparent())
                    {
                        renderable.flags |= Sprite3DRenderable::Transparent;
                    }
                    renderable.layer_mask = (layer_array && layer_array->HasData(entity)) ? layer_array->GetData(entity).layer_mask : 0xFFFFFFFF;

                    bucket.sprite_3d_renderables.push_back(renderable);
                });
            }
            jobsystem::Wait(sub_ctx);

            Size sprite_2d_renderable_count = 0;
            Size sprite_3d_renderable_count = 0;
            for (const SpriteBucket& bucket : sprite_2d_buckets)
            {
                sprite_2d_renderable_count += bucket.sprite_2d_renderables.size();
            }
            for (const SpriteBucket& bucket : sprite_3d_buckets)
            {
                sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
            }

            sprite_2d_renderables.reserve(sprite_2d_renderable_count);
            sprite_3d_renderables.reserve(sprite_3d_renderable_count);
            for (SpriteBucket& bucket : sprite_2d_buckets)
            {
                sprite_2d_renderables.insert(sprite_2d_renderables.end(), std::make_move_iterator(bucket.sprite_2d_renderables.begin()), std::make_move_iterator(bucket.sprite_2d_renderables.end()));
            }
            for (SpriteBucket& bucket : sprite_3d_buckets)
            {
                sprite_3d_renderables.insert(sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
            }
        }

        struct GlyphRequest
        {
            resource::Font* font = nullptr;
            uint32 codepoint = 0;
            uint32 pixel_height = 0;
        };

        void ExtractText(const ecs::Scene& scene, Vector<Sprite2DRenderable>& sprite_2d_renderables, Vector<Sprite3DRenderable>& sprite_3d_renderables, Vector<GlyphRequest>& glyph_requests)
        {
            struct TextBucket
            {
                Vector<GlyphRequest> glyph_requests;
                Vector<Sprite2DRenderable> sprite_2d_renderables;
                Vector<Sprite3DRenderable> sprite_3d_renderables;
            };

            const auto text_2d_array = scene.GetComponentArray<Text2DComponent>().get();
            const auto text_3d_array = scene.GetComponentArray<Text3DComponent>().get();
            const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
            const auto layer_array = scene.GetComponentArray<VisibilityLayerComponent>().get();
            const auto rect_transform_array = scene.GetComponentArray<RectTransform2DComponent>().get();
            const auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

            sprite_2d_renderables.clear();
            sprite_3d_renderables.clear();
            glyph_requests.clear();
            if (!material_array)
            {
                return;
            }

            jobsystem::Context sub_ctx;
            Vector<TextBucket> text_2d_buckets;
            Vector<TextBucket> text_3d_buckets;
            if (text_2d_array)
            {
                const uint32 text_2d_job_count = static_cast<uint32>(text_2d_array->GetSize());
                text_2d_buckets.resize(jobsystem::DispatchGroupCount(text_2d_job_count, jobsystem::groupsize));
                jobsystem::Dispatch(sub_ctx, text_2d_job_count, jobsystem::groupsize, [&](jobsystem::JobArgs args) {
                struct GlyphLayout
                {
                    const resource::Font::Glyph* glyph = nullptr;
                    uint32 codepoint = 0;
                    uint32 line_index = 0;
                    float pen_x = 0.0f;
                };

                TextBucket& bucket = text_2d_buckets[args.group_id];
                const Text2DComponent& text = text_2d_array->data[args.job_index];

                if (!text.font || !text.font->IsValid() || text.pixel_height == 0)
                {
                    return;
                }

                Vector<GlyphLayout> glyph_layouts;
                Vector<float> line_widths;
                line_widths.push_back(0.0f);
                const WString decoded_text = utils::DecodeUtf8(text.resolved_text);
                uint32 line_index = 0;
                float pen_x = 0.0f;
                for (Size char_index = 0; char_index < decoded_text.size(); ++char_index)
                {
                    uint32 codepoint = static_cast<uint32>(decoded_text[char_index]);
                    if constexpr (sizeof(wchar_t) < 4)
                    {
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && char_index + 1 < decoded_text.size())
                        {
                            const uint32 low_surrogate = static_cast<uint32>(decoded_text[char_index + 1]);
                            if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF)
                            {
                                codepoint = (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00)) + 0x10000;
                                ++char_index;
                            }
                        }
                    }
                    if (codepoint == '\r' || codepoint == '\n')
                    {
                        if (codepoint == '\r' && char_index + 1 < decoded_text.size() && decoded_text[char_index + 1] == '\n')
                        {
                            ++char_index;
                        }
                        line_widths[line_index] = pen_x;
                        line_widths.push_back(0.0f);
                        ++line_index;
                        pen_x = 0.0f;
                        continue;
                    }

                    const resource::Font::Glyph* glyph = text.font->atlas.FindGlyph(codepoint, text.pixel_height);
                    if (!glyph)
                    {
                        bucket.glyph_requests.push_back({ text.font.get(), codepoint, text.pixel_height });
                        continue;
                    }

                    glyph_layouts.push_back({ glyph, codepoint, line_index, pen_x });
                    pen_x += glyph->advance;
                }
                line_widths[line_index] = pen_x;

                const Entity entity = text_2d_array->index_to_entity[args.job_index];
                if (!material_array->HasData(entity) || material_array->GetData(entity).GetMaterialSlotCount() == 0)
                {
                    return;
                }

                if (!rect_transform_array || !rect_transform_array->HasData(entity))
                {
                    return;
                }
                if (!hierarchy_array || !hierarchy_array->HasData(entity))
                {
                    return;
                }
                const RectTransform2DComponent& rect = rect_transform_array->GetData(entity);
                const float2 text_anchor_point = {
                    rect.resolved_position.x + rect.pivot.x * rect.resolved_size.x,
                    rect.resolved_position.y + rect.pivot.y * rect.resolved_size.y
                };

                const float font_metric_height = static_cast<float>(text.font->ascent - text.font->descent);
                const float font_metric_scale = font_metric_height > 0.0f ? static_cast<float>(text.pixel_height) / font_metric_height : 1.0f;
                const float line_advance = static_cast<float>(text.font->ascent - text.font->descent + text.font->line_gap) * font_metric_scale;
                float visible_min_y = (std::numeric_limits<float>::max)();
                float visible_max_y = (std::numeric_limits<float>::lowest)();
                for (const GlyphLayout& layout : glyph_layouts)
                {
                    const resource::Font::Glyph* glyph = layout.glyph;
                    if (!glyph)
                    {
                        continue;
                    }
                    const float baseline_y = line_advance * static_cast<float>(layout.line_index);
                    const float glyph_top_y = baseline_y + glyph->offset.y;
                    visible_min_y = (std::min)(visible_min_y, glyph_top_y);
                    visible_max_y = (std::max)(visible_max_y, glyph_top_y + glyph->size.y);
                }
                const float text_visible_height = visible_max_y > visible_min_y ? visible_max_y - visible_min_y : 0.0f;
                const float pivot_y_offset = text_visible_height > 0.0f ? -(visible_min_y + text_visible_height * rect.pivot.y) : 0.0f;
                for (const GlyphLayout& layout : glyph_layouts)
                {
                    const resource::Font::Glyph* glyph = layout.glyph;
                    if (!glyph)
                    {
                        continue;
                    }

                    const float line_x = -line_widths[layout.line_index] * rect.pivot.x;
                    const float glyph_visual_x = line_x + layout.pen_x + glyph->offset.x;
                    const float baseline_y = pivot_y_offset + line_advance * static_cast<float>(layout.line_index);
                    const float glyph_top_y = baseline_y + glyph->offset.y;
                    Sprite2DRenderable renderable = {};
                    renderable.flags |= Sprite2DRenderable::Text;
                    renderable.material_index = material_array->GetData(entity).material_offset;
                    renderable.font = text.font.get();
                    renderable.anchor = { 0.0f, 0.0f };
                    renderable.position = { text_anchor_point.x + glyph_visual_x, text_anchor_point.y + glyph_top_y };
                    renderable.size = glyph->size;
                    renderable.pivot = { 0.0f, 0.0f };
                    renderable.reference_resolution = rect.reference_resolution;
                    renderable.uv_rect = { glyph->uv_min.x, glyph->uv_min.y, glyph->uv_max.x, glyph->uv_max.y };
                    renderable.layer = text.layer;
                    renderable.layer_mask = rect.layer_mask;
                    renderable.match = rect.match;
                    bucket.sprite_2d_renderables.push_back(renderable);
                }
                });
            }

            if (text_3d_array && transform_array)
            {
                const uint32 text_3d_job_count = static_cast<uint32>(text_3d_array->GetSize());
                text_3d_buckets.resize(jobsystem::DispatchGroupCount(text_3d_job_count, jobsystem::groupsize));
                jobsystem::Dispatch(sub_ctx, text_3d_job_count, jobsystem::groupsize, [&](jobsystem::JobArgs args) {
                struct GlyphLayout
                {
                    const resource::Font::Glyph* glyph = nullptr;
                    uint32 codepoint = 0;
                    uint32 line_index = 0;
                    float pen_x = 0.0f;
                };

                TextBucket& bucket = text_3d_buckets[args.group_id];
                const Text3DComponent& text = text_3d_array->data[args.job_index];

                if (!text.font || !text.font->IsValid() || text.pixel_height == 0)
                {
                    return;
                }

                Vector<GlyphLayout> glyph_layouts;
                Vector<float> line_widths;
                line_widths.push_back(0.0f);
                const WString decoded_text = utils::DecodeUtf8(text.resolved_text);
                const float glyph_world_scale = text.height / static_cast<float>(text.pixel_height);
                uint32 line_index = 0;
                float pen_x = 0.0f;
                for (Size char_index = 0; char_index < decoded_text.size(); ++char_index)
                {
                    uint32 codepoint = static_cast<uint32>(decoded_text[char_index]);
                    if constexpr (sizeof(wchar_t) < 4)
                    {
                        // use 2 * wchar_t (Emoji, etc.)
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && char_index + 1 < decoded_text.size())
                        {
                            const uint32 low_surrogate = static_cast<uint32>(decoded_text[char_index + 1]);
                            if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF)
                            {
                                codepoint = (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00)) + 0x10000;
                                ++char_index;
                            }
                        }
                    }
                    if (codepoint == '\r' || codepoint == '\n')
                    {
                        if (codepoint == '\r' && char_index + 1 < decoded_text.size() && decoded_text[char_index + 1] == '\n')
                        {
                            ++char_index;
                        }
                        line_widths[line_index] = pen_x;
                        line_widths.push_back(0.0f);
                        ++line_index;
                        pen_x = 0.0f;
                        continue;
                    }

                    const resource::Font::Glyph* glyph = text.font->atlas.FindGlyph(codepoint, text.pixel_height);
                    if (!glyph)
                    {
                        bucket.glyph_requests.push_back({ text.font.get(), codepoint, text.pixel_height });
                        continue;
                    }

                    glyph_layouts.push_back({ glyph, codepoint, line_index, pen_x });
                    pen_x += glyph->advance * glyph_world_scale;
                }
                line_widths[line_index] = pen_x;

                const Entity entity = text_3d_array->index_to_entity[args.job_index];
                if (transform_array && transform_array->HasData(entity))
                {
                    const float font_metric_height = static_cast<float>(text.font->ascent - text.font->descent);
                    const float font_metric_scale = font_metric_height > 0.0f ? static_cast<float>(text.pixel_height) / font_metric_height : 1.0f;
                    const float line_advance = static_cast<float>(text.font->ascent - text.font->descent + text.font->line_gap) * font_metric_scale * glyph_world_scale;
                    float visible_min_y = (std::numeric_limits<float>::max)();
                    float visible_max_y = (std::numeric_limits<float>::lowest)();
                    for (const GlyphLayout& layout : glyph_layouts)
                    {
                        const resource::Font::Glyph* glyph = layout.glyph;
                        if (!glyph)
                        {
                            continue;
                        }
                        const float glyph_height = glyph->size.y * glyph_world_scale;
                        const float baseline_y = -line_advance * static_cast<float>(layout.line_index);
                        const float glyph_top_y = baseline_y - glyph->offset.y * glyph_world_scale;
                        visible_min_y = (std::min)(visible_min_y, glyph_top_y - glyph_height);
                        visible_max_y = (std::max)(visible_max_y, glyph_top_y);
                    }
                    const float text_visible_height = visible_max_y > visible_min_y ? visible_max_y - visible_min_y : 0.0f;
                    const float pivot_y_offset = text_visible_height > 0.0f ? -(visible_min_y + text_visible_height * text.pivot.y) : 0.0f;
                    for (const GlyphLayout& layout : glyph_layouts)
                    {
                        const resource::Font::Glyph* glyph = layout.glyph;
                        if (!glyph)
                        {
                            continue;
                        }

                        Sprite3DRenderable renderable = {};
                        renderable.flags |= Sprite3DRenderable::Text;
                        renderable.instance_index = static_cast<uint32>(transform_array->entity_to_index.at(entity));
                        renderable.world_position = math::GetPosition(transform_array->GetData(entity).world_transform);
                        if (material_array && material_array->HasData(entity) && material_array->GetData(entity).GetMaterialSlotCount() > 0)
                        {
                            renderable.material_index = material_array->GetData(entity).material_offset;
                        }
                        const float2 glyph_size = { glyph->size.x * glyph_world_scale, glyph->size.y * glyph_world_scale };
                        const float line_x = -line_widths[layout.line_index] * text.pivot.x;
                        const float glyph_visual_x = line_x + layout.pen_x + glyph->offset.x * glyph_world_scale;
                        const float baseline_y = pivot_y_offset - line_advance * static_cast<float>(layout.line_index);
                        const float glyph_top_y = baseline_y - glyph->offset.y * glyph_world_scale;
                        renderable.font = text.font.get();
                        renderable.pivot = { (glyph_visual_x + glyph_size.x) / glyph_size.x, (glyph_size.y - glyph_top_y) / glyph_size.y };
                        renderable.size = glyph_size;
                        renderable.uv_rect = { glyph->uv_min.x, glyph->uv_min.y, glyph->uv_max.x, glyph->uv_max.y };
                        if (text.IsBillboard())
                        {
                            renderable.flags |= Sprite3DRenderable::Billboard;
                        }
                        renderable.layer_mask = (layer_array && layer_array->HasData(entity)) ? layer_array->GetData(entity).layer_mask : 0xFFFFFFFF;
                        bucket.sprite_3d_renderables.push_back(renderable);
                    }
                }
                });
            }
            jobsystem::Wait(sub_ctx);

            Size sprite_2d_renderable_count = 0;
            Size sprite_3d_renderable_count = 0;
            Size glyph_request_count = 0;
            for (const TextBucket& bucket : text_2d_buckets)
            {
                sprite_2d_renderable_count += bucket.sprite_2d_renderables.size();
                glyph_request_count += bucket.glyph_requests.size();
            }
            for (const TextBucket& bucket : text_3d_buckets)
            {
                sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
                glyph_request_count += bucket.glyph_requests.size();
            }

            sprite_2d_renderables.reserve(sprite_2d_renderable_count);
            sprite_3d_renderables.reserve(sprite_3d_renderable_count);
            glyph_requests.reserve(glyph_request_count);
            for (TextBucket& bucket : text_2d_buckets)
            {
                sprite_2d_renderables.insert(sprite_2d_renderables.end(), std::make_move_iterator(bucket.sprite_2d_renderables.begin()), std::make_move_iterator(bucket.sprite_2d_renderables.end()));
                glyph_requests.insert(glyph_requests.end(), std::make_move_iterator(bucket.glyph_requests.begin()), std::make_move_iterator(bucket.glyph_requests.end()));
            }
            for (TextBucket& bucket : text_3d_buckets)
            {
                sprite_3d_renderables.insert(sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
                glyph_requests.insert(glyph_requests.end(), std::make_move_iterator(bucket.glyph_requests.begin()), std::make_move_iterator(bucket.glyph_requests.end()));
            }
        }

        void ExtractParticles(const ecs::Scene& scene, Vector<float4>& particle_instances, Vector<Sprite3DRenderable>& sprite_3d_renderables)
        {
            struct ParticleBucket
            {
                Vector<float4> particle_instances;
                Vector<Sprite3DRenderable> sprite_3d_renderables;
            };

            particle_instances.clear();
            sprite_3d_renderables.clear();

            const auto emitter_array = scene.GetComponentArray<ParticleEmitter3DComponent>().get();
            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
            if (!emitter_array || !material_array)
            {
                return;
            }

            jobsystem::Context sub_ctx;
            const uint32 emitter_count = static_cast<uint32>(emitter_array->GetSize());
            Vector<ParticleBucket> particle_buckets(jobsystem::DispatchGroupCount(emitter_count, jobsystem::groupsize_heavy));
            jobsystem::Dispatch(sub_ctx, emitter_count, jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args) {
                ParticleBucket& bucket = particle_buckets[args.group_id];

                const Entity entity = emitter_array->index_to_entity[args.job_index];
                const ParticleEmitter3DComponent& emitter = emitter_array->data[args.job_index];
                if (!material_array->HasData(entity))
                {
                    return;
                }
                const MaterialComponent& material = material_array->GetData(entity);
                if (material.GetMaterialSlotCount() == 0)
                {
                    return;
                }

                const float lifetime = emitter.lifetime > 0.0001f ? emitter.lifetime : 0.0001f;
                const uint32 material_index = material.material_offset;
                const resource::MaterialBlendMode blend_mode = material.material->slots[0].blend_mode;

                for (const ParticleEmitter3DComponent::Particle& particle : emitter.particles)
                {
                    const float t = particle.age / lifetime;
                    const float size = math::Lerp(emitter.start_size, emitter.end_size, t);
                    const float4 color = math::Lerp(emitter.start_color, emitter.end_color, t);

                    const uint32 buffer_index = static_cast<uint32>(bucket.particle_instances.size() / 2);
                    bucket.particle_instances.push_back({ particle.position.x, particle.position.y, particle.position.z, 0.0f });
                    bucket.particle_instances.push_back(color);

                    Sprite3DRenderable renderable = {};
                    renderable.instance_index = buffer_index;
                    renderable.material_index = material_index;
                    renderable.blend_mode = blend_mode;
                    renderable.world_position = particle.position;
                    renderable.size = { size, size };
                    // Particles are always alpha-blended (color fades out), so flag them transparent
                    // for back-to-front sorting regardless of the material.
                    renderable.flags = Sprite3DRenderable::Particle
                        | Sprite3DRenderable::Billboard
                        | Sprite3DRenderable::Transparent;
                    const float half_extent = size * 0.5f;
                    renderable.aabb.min = { particle.position.x - half_extent, particle.position.y - half_extent, particle.position.z - half_extent };
                    renderable.aabb.max = { particle.position.x + half_extent, particle.position.y + half_extent, particle.position.z + half_extent };
                    bucket.sprite_3d_renderables.push_back(renderable);
                }
            });
            jobsystem::Wait(sub_ctx);

            Size particle_instance_count = 0;
            Size sprite_3d_renderable_count = 0;
            for (const ParticleBucket& bucket : particle_buckets)
            {
                particle_instance_count += bucket.particle_instances.size();
                sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
            }

            particle_instances.reserve(particle_instance_count);
            sprite_3d_renderables.reserve(sprite_3d_renderable_count);

            uint32 particle_base_index = 0;
            for (ParticleBucket& bucket : particle_buckets)
            {
                for (Sprite3DRenderable& renderable : bucket.sprite_3d_renderables)
                {
                    renderable.instance_index += particle_base_index;
                }

                particle_instances.insert(particle_instances.end(), std::make_move_iterator(bucket.particle_instances.begin()), std::make_move_iterator(bucket.particle_instances.end()));
                sprite_3d_renderables.insert(sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
                particle_base_index += static_cast<uint32>(bucket.particle_instances.size() / 2);
            }
        }

        void ExtractDecals(const ecs::Scene& scene, Vector<ShaderDecal>& shader_decals)
        {
            shader_decals.clear();

            const auto decal_array = scene.GetComponentArray<DecalComponent>().get();
            if (!decal_array)
            {
                return;
            }
            const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
            const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
            if (!transform_array || !material_array)
            {
                return;
            }

            for (Size i = 0; i < decal_array->GetSize(); ++i)
            {
                const DecalComponent& decal = decal_array->data[i];
                if (!decal.IsActive())
                {
                    continue;
                }

                const Entity entity = decal_array->index_to_entity[i];
                if (!transform_array->HasData(entity) || !material_array->HasData(entity))
                {
                    continue;
                }

                const MaterialComponent& material = material_array->GetData(entity);
                if (!material.material || material.material->slots.empty())
                {
                    continue;
                }

                const TransformComponent& transform = transform_array->GetData(entity);
                const XMMATRIX world = XMLoadFloat4x4(&transform.world_transform);

                ShaderDecal shader_decal = {};
                shader_decal.Init();
                XMStoreFloat4x4(&shader_decal.inv_world, XMMatrixInverse(nullptr, world));
                shader_decal.instance_index = static_cast<uint32>(transform_array->entity_to_index.at(entity));
                shader_decal.material_index = material.material_offset;
                shader_decals.push_back(shader_decal);
            }
        }

        void ExtractEnvironment(const ecs::Scene& scene, ShaderEnvironment& shader_environment, ShaderDDGIVolume& shader_ddgi_volume, ShaderReflectionProbe& shader_reflection_probe, Entity& ddgi_volume_entity, ShaderLight& derived_sun, bool& has_derived_sun, bool& direct_sun_cast_shadow, uint32& direct_sun_shadow_resolution, uint32& direct_sun_cascade_count, float& direct_sun_cascade_lambda, float& direct_sun_cascade_blend)
        {
            shader_environment.Init();
            shader_ddgi_volume.Init();
            shader_reflection_probe.Init();
            ddgi_volume_entity = INVALID_ENTITY;
            derived_sun.Init();
            has_derived_sun = false;
            direct_sun_cast_shadow = false;

            const auto environment_array = scene.GetComponentArray<EnvironmentComponent>().get();
            const auto transform_array = scene.GetComponentArray<TransformComponent>().get();

            if (environment_array)
            {
                for (Size i = 0; i < environment_array->GetSize(); ++i)
                {
                    const EnvironmentComponent& environment = environment_array->data[i];
                    if (!environment.IsActive())
                        continue;

                    shader_environment.sky_type = static_cast<uint32>(environment.sky_type);
                    if (environment.HasSky())
                    {
                        shader_environment.SetSunDirection(environment.sun_direction);
                        shader_environment.SetSunColorIntensity(environment.sun_color, environment.sun_intensity);
                        shader_environment.SetSunParams(environment.sun_angular_radius, environment.sun_glow_intensity, environment.sun_glow_falloff);
                        shader_environment.SetSkyHorizonColorIntensity(environment.sky_horizon_color, environment.sky_intensity);
                        shader_environment.SetSkyZenithColorFalloff(environment.sky_zenith_color, environment.sky_horizon_falloff);
                        shader_environment.SetGroundHorizonColorIntensity(environment.ground_horizon_color, environment.ground_intensity);
                        shader_environment.SetGroundColorFalloff(environment.ground_color, environment.ground_falloff);
                        shader_environment.SetAtmosphere(environment.turbidity, environment.mie_eccentricity, environment.rayleigh_coefficient, environment.mie_coefficient);
                        shader_environment.SetCloud(environment.cloud_coverage, environment.cloud_density, environment.cloud_color, environment.cloud_frequency, environment.cloud_direction, environment.cloud_speed);
                    }

                    if (environment.sky_type == EnvironmentComponent::SkyType::PhysicallyBased
                        && environment.direct_sun_active
                        && environment.sun_direction.y > 0.0f
                        && environment.sun_intensity > 0.0f)
                    {
                        derived_sun.SetType(SHADER_LIGHT_TYPE_DIRECTIONAL);
                        derived_sun.SetDirection({ -environment.sun_direction.x, -environment.sun_direction.y, -environment.sun_direction.z });
                        derived_sun.SetColor({
                            environment.sun_color.x * environment.sun_intensity,
                            environment.sun_color.y * environment.sun_intensity,
                            environment.sun_color.z * environment.sun_intensity,
                            environment.sun_intensity
                        });
                        if (environment.direct_sun_cast_shadow)
                        {
                            derived_sun.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_CASTING_SHADOW);
                        }
                        has_derived_sun = true;
                        direct_sun_cast_shadow = environment.direct_sun_cast_shadow;
                        direct_sun_shadow_resolution = environment.direct_sun_shadow_resolution;
                        direct_sun_cascade_count = environment.direct_sun_cascade_count;
                        direct_sun_cascade_lambda = environment.direct_sun_cascade_lambda;
                        direct_sun_cascade_blend = environment.direct_sun_cascade_blend;
                    }

                    shader_environment.diffuse_gi_mode = static_cast<uint32>(environment.diffuse_gi_mode);
                    shader_environment.reflection_mode = static_cast<uint32>(environment.reflection_mode);
                    shader_environment.SetAmbientColorIntensity(environment.ambient_color, environment.ambient_intensity);
                    shader_environment.SetIndirectScale(environment.indirect_diffuse_scale, environment.indirect_specular_scale);

                    const bool has_sky_cube = environment.sky_cubemap && environment.sky_cubemap->render_data.IsValid();
                    const bool has_irradiance_cube = environment.irradiance_cubemap && environment.irradiance_cubemap->render_data.IsValid();
                    const bool has_specular_cube = environment.specular_cubemap && environment.specular_cubemap->render_data.IsValid();
                    shader_environment.sky_cubemap = has_sky_cube ? environment.sky_cubemap->render_data.srv.descriptor_index : -1;
                    shader_environment.irradiance_cubemap = has_irradiance_cube ? environment.irradiance_cubemap->render_data.srv.descriptor_index : -1;
                    shader_environment.specular_cubemap = has_specular_cube ? environment.specular_cubemap->render_data.srv.descriptor_index : -1;
                    shader_environment.specular_mip_count = has_specular_cube ? static_cast<float>(environment.specular_cubemap->mip_levels) : 0.0f;
                    break;
                }
            }

            const auto ddgi_volume_array = scene.GetComponentArray<DDGIVolumeComponent>().get();
            if (ddgi_volume_array && ddgi_volume_array->GetSize() > 0)
            {
                const DDGIVolumeComponent* selected_ddgi_volume = nullptr;
                Entity selected_ddgi_volume_entity = INVALID_ENTITY;
                float3 selected_volume_center = { 0.0f, 0.0f, 0.0f };

                for (Size i = 0; i < ddgi_volume_array->GetSize(); ++i)
                {
                    const DDGIVolumeComponent& ddgi_volume = ddgi_volume_array->data[i];
                    if (!ddgi_volume.IsActive())
                    {
                        continue;
                    }

                    if (selected_ddgi_volume && ddgi_volume.priority < selected_ddgi_volume->priority)
                    {
                        continue;
                    }

                    Entity entity = ddgi_volume_array->index_to_entity[i];
                    float3 volume_center = ddgi_volume.volume_offset;
                    if (transform_array && transform_array->HasData(entity))
                    {
                        const TransformComponent& transform = transform_array->GetData(entity);
                        const float3 transform_position = math::GetPosition(transform.world_transform);
                        volume_center.x += transform_position.x;
                        volume_center.y += transform_position.y;
                        volume_center.z += transform_position.z;
                    }

                    selected_ddgi_volume = &ddgi_volume;
                    selected_ddgi_volume_entity = entity;
                    selected_volume_center = volume_center;
                }

                if (selected_ddgi_volume)
                {
                    const float3 probe_span = {
                        static_cast<float>((selected_ddgi_volume->probe_counts.x > 0 ? selected_ddgi_volume->probe_counts.x - 1 : 0)) * selected_ddgi_volume->probe_spacing.x,
                        static_cast<float>((selected_ddgi_volume->probe_counts.y > 0 ? selected_ddgi_volume->probe_counts.y - 1 : 0)) * selected_ddgi_volume->probe_spacing.y,
                        static_cast<float>((selected_ddgi_volume->probe_counts.z > 0 ? selected_ddgi_volume->probe_counts.z - 1 : 0)) * selected_ddgi_volume->probe_spacing.z
                    };

                    shader_ddgi_volume.flags = SHADER_DDGI_FLAG_ACTIVE;
                    ddgi_volume_entity = selected_ddgi_volume_entity;
                    shader_ddgi_volume.probe_counts = selected_ddgi_volume->probe_counts;
                    shader_ddgi_volume.total_probe_count = selected_ddgi_volume->probe_counts.x * selected_ddgi_volume->probe_counts.y * selected_ddgi_volume->probe_counts.z;
                    shader_ddgi_volume.probes_per_frame = selected_ddgi_volume->probes_per_frame;
                    shader_ddgi_volume.hysteresis = selected_ddgi_volume->hysteresis;
                    shader_ddgi_volume.normal_bias = selected_ddgi_volume->normal_bias;
                    shader_ddgi_volume.view_bias = selected_ddgi_volume->view_bias;
                    shader_ddgi_volume.max_distance = selected_ddgi_volume->max_distance;
                    shader_ddgi_volume.probe_spacing = selected_ddgi_volume->probe_spacing;
                    shader_ddgi_volume.volume_min = {
                        selected_volume_center.x - probe_span.x * 0.5f,
                        selected_volume_center.y - probe_span.y * 0.5f,
                        selected_volume_center.z - probe_span.z * 0.5f
                    };
                }
            }

            const auto reflection_probe_array = scene.GetComponentArray<ReflectionProbeComponent>().get();
            if (reflection_probe_array)
            {
                for (Size i = 0; i < reflection_probe_array->GetSize(); ++i)
                {
                    const ReflectionProbeComponent& probe = reflection_probe_array->data[i];
                    if (!probe.IsActive())
                    {
                        continue;
                    }

                    float3 probe_position = { 0.0f, 0.0f, 0.0f };
                    const Entity entity = reflection_probe_array->index_to_entity[i];
                    if (transform_array && transform_array->HasData(entity))
                    {
                        probe_position = math::GetPosition(transform_array->GetData(entity).world_transform);
                    }

                    shader_reflection_probe.flags = SHADER_REFLECTION_PROBE_FLAG_ACTIVE;
                    shader_reflection_probe.intensity = probe.intensity_multiplier;
                    shader_reflection_probe.influence_radius = probe.influence_radius;
                    shader_reflection_probe.position = probe_position;
                    const bool has_cubemap = probe.cubemap && probe.cubemap->render_data.IsValid();
                    shader_reflection_probe.cubemap_texture = has_cubemap ? probe.cubemap->render_data.srv.descriptor_index : -1;
                    shader_reflection_probe.cubemap_mip_count = has_cubemap ? static_cast<float>(probe.cubemap->mip_levels) : 0.0f;
                    break;
                }
            }
        }
    }

    void GPUScene::RetireResource(std::unique_ptr<RHIResource>& resource, uint32 frame_slot)
    {
        if (resource)
        {
            retired[frame_slot].push_back(std::move(resource));
        }
    }

    void GPUScene::ReleaseDDGIResources(uint32 frame_slot)
    {
        RetireResource(ddgi.irradiance_texture, frame_slot);
        RetireResource(ddgi.irradiance_history_texture, frame_slot);
        RetireResource(ddgi.visibility_texture, frame_slot);
        RetireResource(ddgi.visibility_history_texture, frame_slot);
        RetireResource(ddgi.probe_data_buffer, frame_slot);
        RetireResource(ddgi.probe_data_history_buffer, frame_slot);
        ddgi.irradiance_texture_srv = {};
        ddgi.irradiance_texture_uav = {};
        ddgi.irradiance_history_texture_srv = {};
        ddgi.visibility_texture_srv = {};
        ddgi.visibility_texture_uav = {};
        ddgi.visibility_history_texture_srv = {};
        ddgi.probe_data_buffer_srv = {};
        ddgi.probe_data_buffer_uav = {};
        ddgi.probe_data_history_buffer_srv = {};
        ddgi.probe_counts = { 0, 0, 0 };
        ddgi.probe_spacing = { 0.0f, 0.0f, 0.0f };
        ddgi.volume_min = { 0.0f, 0.0f, 0.0f };
        ddgi.max_distance = 0.0f;
        ddgi.probe_update_offset = 0;
        ddgi.history_valid = false;
    }

    bool GPUScene::CreateSkyLightingResources(RHIDevice& device)
    {
        const bool diffuse_from_sky = shader_environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_SKY;
        const bool specular_from_sky = shader_environment.reflection_mode == SHADER_REFLECTION_MODE_SKY;
        if (!diffuse_from_sky && !specular_from_sky)
        {
            return false;
        }

        if (!sky_lighting.capture_texture)
        {
            RHITextureDesc desc = {};
            desc.width = sky_capture_resolution;
            desc.height = sky_capture_resolution;
            desc.depth = 1;
            desc.mip_levels = 1;
            desc.array_layers = 6;
            desc.is_cube = true;
            desc.sample_count = 1;
            desc.format = RHIFormat::R16G16B16A16Float;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
            sky_lighting.capture_texture = device.CreateTexture(desc);
            if (!sky_lighting.capture_texture)
            {
                return false;
            }
            sky_lighting.capture_texture->SetName("Sky Capture Cubemap");

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = desc.format;
            srv_desc.first_mip = 0;
            srv_desc.mip_count = 1;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 6;
            device.CreateSubresource(*sky_lighting.capture_texture, srv_desc, &sky_lighting.capture_srv);

            RHISubresourceDesc uav_desc = {};
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            uav_desc.format = desc.format;
            uav_desc.first_mip = 0;
            uav_desc.mip_count = 1;
            uav_desc.first_slice = 0;
            uav_desc.slice_count = 6;
            device.CreateSubresource(*sky_lighting.capture_texture, uav_desc, &sky_lighting.capture_uav);
        }

        if (diffuse_from_sky && !sky_lighting.irradiance_texture)
        {
            RHITextureDesc desc = {};
            desc.width = sky_irradiance_resolution;
            desc.height = sky_irradiance_resolution;
            desc.depth = 1;
            desc.mip_levels = 1;
            desc.array_layers = 6;
            desc.is_cube = true;
            desc.sample_count = 1;
            desc.format = RHIFormat::R16G16B16A16Float;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
            sky_lighting.irradiance_texture = device.CreateTexture(desc);
            if (!sky_lighting.irradiance_texture)
            {
                return false;
            }
            sky_lighting.irradiance_texture->SetName("Sky Irradiance Cubemap");

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = desc.format;
            srv_desc.first_mip = 0;
            srv_desc.mip_count = 1;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 6;
            device.CreateSubresource(*sky_lighting.irradiance_texture, srv_desc, &sky_lighting.irradiance_srv);

            RHISubresourceDesc uav_desc = {};
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            uav_desc.format = desc.format;
            uav_desc.first_mip = 0;
            uav_desc.mip_count = 1;
            uav_desc.first_slice = 0;
            uav_desc.slice_count = 6;
            device.CreateSubresource(*sky_lighting.irradiance_texture, uav_desc, &sky_lighting.irradiance_uav);
        }

        if (specular_from_sky && !sky_lighting.specular_texture)
        {
            RHITextureDesc desc = {};
            desc.width = sky_specular_resolution;
            desc.height = sky_specular_resolution;
            desc.depth = 1;
            desc.mip_levels = sky_specular_mip_count;
            desc.array_layers = 6;
            desc.is_cube = true;
            desc.sample_count = 1;
            desc.format = RHIFormat::R16G16B16A16Float;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
            sky_lighting.specular_texture = device.CreateTexture(desc);
            if (!sky_lighting.specular_texture)
            {
                return false;
            }
            sky_lighting.specular_texture->SetName("Sky Specular Cubemap");

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = desc.format;
            srv_desc.first_mip = 0;
            srv_desc.mip_count = sky_specular_mip_count;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 6;
            device.CreateSubresource(*sky_lighting.specular_texture, srv_desc, &sky_lighting.specular_srv);

            for (uint32 mip = 0; mip < sky_specular_mip_count; ++mip)
            {
                RHISubresourceDesc uav_desc = {};
                uav_desc.type = RHISubresourceType::UnorderedAccess;
                uav_desc.format = desc.format;
                uav_desc.first_mip = mip;
                uav_desc.mip_count = 1;
                uav_desc.first_slice = 0;
                uav_desc.slice_count = 6;
                device.CreateSubresource(*sky_lighting.specular_texture, uav_desc, &sky_lighting.specular_mip_uav[mip]);
            }
        }

        return true;
    }

    void GPUScene::ReleaseSkyLightingResources(uint32 frame_slot)
    {
        RetireResource(sky_lighting.capture_texture, frame_slot);
        RetireResource(sky_lighting.irradiance_texture, frame_slot);
        RetireResource(sky_lighting.specular_texture, frame_slot);
        sky_lighting.capture_srv = {};
        sky_lighting.capture_uav = {};
        sky_lighting.irradiance_srv = {};
        sky_lighting.irradiance_uav = {};
        sky_lighting.specular_srv = {};
        for (uint32 mip = 0; mip < sky_specular_mip_count; ++mip)
        {
            sky_lighting.specular_mip_uav[mip] = {};
        }
        sky_lighting.signature = {};
        sky_lighting.pending_irradiance_face = -1;
        sky_lighting.pending_specular_mip = -1;
        sky_lighting.valid = false;
    }

    bool GPUScene::CreateDDGIResources(RHIDevice& device, uint32 frame_slot)
    {
        if ((shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) == 0)
        {
            ReleaseDDGIResources(frame_slot);
            return true;
        }

        const bool recreate_ddgi_texture =
            !ddgi.irradiance_texture ||
            !ddgi.irradiance_texture_srv.IsValid() ||
            !ddgi.irradiance_texture_uav.IsValid() ||
            !ddgi.irradiance_history_texture ||
            !ddgi.irradiance_history_texture_srv.IsValid() ||
            !ddgi.visibility_texture ||
            !ddgi.visibility_texture_srv.IsValid() ||
            !ddgi.visibility_texture_uav.IsValid() ||
            !ddgi.visibility_history_texture ||
            !ddgi.visibility_history_texture_srv.IsValid() ||
            !ddgi.probe_data_buffer ||
            !ddgi.probe_data_buffer_srv.IsValid() ||
            !ddgi.probe_data_buffer_uav.IsValid() ||
            !ddgi.probe_data_history_buffer ||
            !ddgi.probe_data_history_buffer_srv.IsValid() ||
            ddgi.probe_counts.x != shader_ddgi_volume.probe_counts.x ||
            ddgi.probe_counts.y != shader_ddgi_volume.probe_counts.y ||
            ddgi.probe_counts.z != shader_ddgi_volume.probe_counts.z;

        if (!recreate_ddgi_texture)
        {
            const bool reset_ddgi_history =
                ddgi.probe_spacing.x != shader_ddgi_volume.probe_spacing.x ||
                ddgi.probe_spacing.y != shader_ddgi_volume.probe_spacing.y ||
                ddgi.probe_spacing.z != shader_ddgi_volume.probe_spacing.z ||
                ddgi.volume_min.x != shader_ddgi_volume.volume_min.x ||
                ddgi.volume_min.y != shader_ddgi_volume.volume_min.y ||
                ddgi.volume_min.z != shader_ddgi_volume.volume_min.z ||
                ddgi.max_distance != shader_ddgi_volume.max_distance;
            if (reset_ddgi_history)
            {
                ddgi.probe_spacing = shader_ddgi_volume.probe_spacing;
                ddgi.volume_min = shader_ddgi_volume.volume_min;
                ddgi.max_distance = shader_ddgi_volume.max_distance;
                ddgi.probe_update_offset = 0;
                ddgi.history_valid = false;
            }
            return true;
        }

        ReleaseDDGIResources(frame_slot);

        RHITextureDesc ddgi_irradiance_texture_desc = {};
        ddgi_irradiance_texture_desc.width = (std::max)(shader_ddgi_volume.probe_counts.x, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
        ddgi_irradiance_texture_desc.height = (std::max)(shader_ddgi_volume.probe_counts.y, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
        ddgi_irradiance_texture_desc.depth = 1;
        ddgi_irradiance_texture_desc.mip_levels = 1;
        ddgi_irradiance_texture_desc.array_layers = (std::max)(shader_ddgi_volume.probe_counts.z, 1u);
        ddgi_irradiance_texture_desc.sample_count = 1;
        ddgi_irradiance_texture_desc.format = RHIFormat::R16G16B16A16Float;
        ddgi_irradiance_texture_desc.usage = RHIResourceUsage::Default;
        ddgi_irradiance_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi.irradiance_texture = device.CreateTexture(ddgi_irradiance_texture_desc);
        if (!ddgi.irradiance_texture)
        {
            backlog::Post("failed to create ddgi irradiance texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi.irradiance_texture->SetName("DDGI Irradiance Texture");

        RHISubresourceDesc ddgi_irradiance_srv_desc = {};
        ddgi_irradiance_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_irradiance_srv_desc.format = ddgi_irradiance_texture_desc.format;
        ddgi_irradiance_srv_desc.first_slice = 0;
        ddgi_irradiance_srv_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
        ddgi_irradiance_srv_desc.first_mip = 0;
        ddgi_irradiance_srv_desc.mip_count = 1;
        if (!device.CreateSubresource(*ddgi.irradiance_texture, ddgi_irradiance_srv_desc, &ddgi.irradiance_texture_srv))
        {
            backlog::Post("failed to create ddgi irradiance srv", backlog::LogLevel::Error);
            ddgi.irradiance_texture = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_irradiance_uav_desc = {};
        ddgi_irradiance_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_irradiance_uav_desc.format = ddgi_irradiance_texture_desc.format;
        ddgi_irradiance_uav_desc.first_mip = 0;
        ddgi_irradiance_uav_desc.mip_count = 1;
        ddgi_irradiance_uav_desc.first_slice = 0;
        ddgi_irradiance_uav_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
        if (!device.CreateSubresource(*ddgi.irradiance_texture, ddgi_irradiance_uav_desc, &ddgi.irradiance_texture_uav))
        {
            backlog::Post("failed to create ddgi irradiance uav", backlog::LogLevel::Error);
            ddgi.irradiance_texture = nullptr;
            return false;
        }

        ddgi.irradiance_history_texture = device.CreateTexture(ddgi_irradiance_texture_desc);
        if (!ddgi.irradiance_history_texture)
        {
            backlog::Post("failed to create ddgi irradiance history texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi.irradiance_history_texture->SetName("DDGI Irradiance History Texture");
        if (!device.CreateSubresource(*ddgi.irradiance_history_texture, ddgi_irradiance_srv_desc, &ddgi.irradiance_history_texture_srv))
        {
            backlog::Post("failed to create ddgi irradiance history srv", backlog::LogLevel::Error);
            ddgi.irradiance_history_texture = nullptr;
            return false;
        }

        RHITextureDesc ddgi_visibility_texture_desc = {};
        ddgi_visibility_texture_desc.width = (std::max)(shader_ddgi_volume.probe_counts.x, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
        ddgi_visibility_texture_desc.height = (std::max)(shader_ddgi_volume.probe_counts.y, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
        ddgi_visibility_texture_desc.depth = 1;
        ddgi_visibility_texture_desc.mip_levels = 1;
        ddgi_visibility_texture_desc.array_layers = (std::max)(shader_ddgi_volume.probe_counts.z, 1u);
        ddgi_visibility_texture_desc.sample_count = 1;
        ddgi_visibility_texture_desc.format = RHIFormat::R16G16B16A16Float;
        ddgi_visibility_texture_desc.usage = RHIResourceUsage::Default;
        ddgi_visibility_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi.visibility_texture = device.CreateTexture(ddgi_visibility_texture_desc);
        if (!ddgi.visibility_texture)
        {
            backlog::Post("failed to create ddgi visibility texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi.visibility_texture->SetName("DDGI Visibility Texture");

        RHISubresourceDesc ddgi_visibility_srv_desc = {};
        ddgi_visibility_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_visibility_srv_desc.format = ddgi_visibility_texture_desc.format;
        ddgi_visibility_srv_desc.first_slice = 0;
        ddgi_visibility_srv_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
        ddgi_visibility_srv_desc.first_mip = 0;
        ddgi_visibility_srv_desc.mip_count = 1;
        if (!device.CreateSubresource(*ddgi.visibility_texture, ddgi_visibility_srv_desc, &ddgi.visibility_texture_srv))
        {
            backlog::Post("failed to create ddgi visibility srv", backlog::LogLevel::Error);
            ddgi.visibility_texture = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_visibility_uav_desc = {};
        ddgi_visibility_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_visibility_uav_desc.format = ddgi_visibility_texture_desc.format;
        ddgi_visibility_uav_desc.first_mip = 0;
        ddgi_visibility_uav_desc.mip_count = 1;
        ddgi_visibility_uav_desc.first_slice = 0;
        ddgi_visibility_uav_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
        if (!device.CreateSubresource(*ddgi.visibility_texture, ddgi_visibility_uav_desc, &ddgi.visibility_texture_uav))
        {
            backlog::Post("failed to create ddgi visibility uav", backlog::LogLevel::Error);
            ddgi.visibility_texture = nullptr;
            return false;
        }

        ddgi.visibility_history_texture = device.CreateTexture(ddgi_visibility_texture_desc);
        if (!ddgi.visibility_history_texture)
        {
            backlog::Post("failed to create ddgi visibility history texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi.visibility_history_texture->SetName("DDGI Visibility History Texture");
        if (!device.CreateSubresource(*ddgi.visibility_history_texture, ddgi_visibility_srv_desc, &ddgi.visibility_history_texture_srv))
        {
            backlog::Post("failed to create ddgi visibility history srv", backlog::LogLevel::Error);
            ddgi.visibility_history_texture = nullptr;
            return false;
        }

        const uint32 total_probe_count = (std::max)(shader_ddgi_volume.total_probe_count, 1u);
        RHIBufferDesc ddgi_probe_data_buffer_desc = {};
        ddgi_probe_data_buffer_desc.size = sizeof(float4) * total_probe_count;
        ddgi_probe_data_buffer_desc.usage = RHIResourceUsage::Default;
        ddgi_probe_data_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi.probe_data_buffer = device.CreateBuffer(ddgi_probe_data_buffer_desc);
        if (!ddgi.probe_data_buffer)
        {
            backlog::Post("failed to create ddgi probe data buffer", backlog::LogLevel::Error);
            return false;
        }
        ddgi.probe_data_buffer->SetName("DDGI Probe Data Buffer");

        RHISubresourceDesc ddgi_probe_data_srv_desc = {};
        ddgi_probe_data_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_probe_data_srv_desc.buffer_offset = 0;
        ddgi_probe_data_srv_desc.buffer_size = ddgi.probe_data_buffer->GetDesc().buffer_desc.size;
        ddgi_probe_data_srv_desc.buffer_stride = sizeof(float4);
        if (!device.CreateSubresource(*ddgi.probe_data_buffer, ddgi_probe_data_srv_desc, &ddgi.probe_data_buffer_srv))
        {
            backlog::Post("failed to create ddgi probe data srv", backlog::LogLevel::Error);
            ddgi.probe_data_buffer = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_probe_data_uav_desc = {};
        ddgi_probe_data_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_probe_data_uav_desc.buffer_offset = 0;
        ddgi_probe_data_uav_desc.buffer_size = ddgi.probe_data_buffer->GetDesc().buffer_desc.size;
        ddgi_probe_data_uav_desc.buffer_stride = sizeof(float4);
        if (!device.CreateSubresource(*ddgi.probe_data_buffer, ddgi_probe_data_uav_desc, &ddgi.probe_data_buffer_uav))
        {
            backlog::Post("failed to create ddgi probe data uav", backlog::LogLevel::Error);
            ddgi.probe_data_buffer = nullptr;
            return false;
        }

        ddgi.probe_data_history_buffer = device.CreateBuffer(ddgi_probe_data_buffer_desc);
        if (!ddgi.probe_data_history_buffer)
        {
            backlog::Post("failed to create ddgi probe data history buffer", backlog::LogLevel::Error);
            return false;
        }
        ddgi.probe_data_history_buffer->SetName("DDGI Probe Data History Buffer");
        if (!device.CreateSubresource(*ddgi.probe_data_history_buffer, ddgi_probe_data_srv_desc, &ddgi.probe_data_history_buffer_srv))
        {
            backlog::Post("failed to create ddgi probe data history srv", backlog::LogLevel::Error);
            ddgi.probe_data_history_buffer = nullptr;
            return false;
        }

        ddgi.probe_counts = shader_ddgi_volume.probe_counts;
        ddgi.probe_spacing = shader_ddgi_volume.probe_spacing;
        ddgi.volume_min = shader_ddgi_volume.volume_min;
        ddgi.max_distance = shader_ddgi_volume.max_distance;
        ddgi.probe_update_offset = 0;
        return true;
    }

    void GPUScene::Update(ecs::Scene& scene, RHIDevice& device, RHICommandList& command_list, uint32 frame_slot)
    {
        auto cpu_range = profiler::ScopedRangeCPU("Update GPU Scene");
        auto gpu_range = profiler::ScopedRangeGPU("Update GPU Scene", command_list);

        retired[frame_slot].clear();

        if (has_derived_sun)
        {
            shader_lights.pop_back();
            light_bounds.pop_back();
            has_derived_sun = false;
        }
        derived_sun_index = std::numeric_limits<uint32>::max();

        const ComponentMask dirty = scene.GetGpuDirtySnapshot();
        const bool light_dirty = (dirty & ecs::light_component_mask) != 0;
        const bool geometry_dirty = (dirty & ecs::geometry_component_mask) != 0;
        const bool material_dirty = (dirty & ecs::material_component_mask) != 0;
        const bool animation_dirty = (dirty & ecs::animation_component_mask) != 0;

        jobsystem::Context extract_ctx;
        if (light_dirty)
        {
            jobsystem::Execute(extract_ctx, [&](jobsystem::JobArgs) { ExtractLights(scene, shader_lights, light_bounds, directional_count); });
        }
        if (animation_dirty)
        {
            jobsystem::Execute(extract_ctx, [&](jobsystem::JobArgs) { ExtractBones(scene, shader_bone_matrices); });
        }
        if (geometry_dirty)
        {
            jobsystem::Execute(extract_ctx, [&](jobsystem::JobArgs) { ExtractGeometries(scene, shader_geometries); });
        }
        if (material_dirty)
        {
            jobsystem::Execute(extract_ctx, [&](jobsystem::JobArgs) { ExtractMaterials(scene, shader_materials); });
        }
        jobsystem::Wait(extract_ctx);

        Vector<Sprite2DRenderable> text_sprite_2d;
        Vector<Sprite3DRenderable> text_sprite_3d;
        Vector<GlyphRequest> glyph_requests;
        Vector<Sprite3DRenderable> particle_sprite_3d;

        jobsystem::Context project_ctx;
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractRenderables(scene, shader_instances, opaque_renderables, transparent_renderables, line_renderables, point_renderables, shadow_caster_world_bound); });
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractSprites(scene, sprite_2d_renderables, sprite_3d_renderables); });
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractText(scene, text_sprite_2d, text_sprite_3d, glyph_requests); });
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractParticles(scene, particle_instances, particle_sprite_3d); });
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractDecals(scene, shader_decals); });
        jobsystem::Execute(project_ctx, [&](jobsystem::JobArgs) { ExtractEnvironment(scene, shader_environment, shader_ddgi_volume, shader_reflection_probe, ddgi_volume_entity, derived_sun, has_derived_sun, direct_sun_cast_shadow, direct_sun_shadow_resolution, direct_sun_cascade_count, direct_sun_cascade_lambda, direct_sun_cascade_blend); });
        jobsystem::Wait(project_ctx);

        if (has_derived_sun)
        {
            derived_sun_index = static_cast<uint32>(shader_lights.size());
            shader_lights.push_back(derived_sun);
            light_bounds.push_back({});
        }
        shader_environment.derived_sun_index = derived_sun_index;

        sprite_2d_renderables.insert(sprite_2d_renderables.end(), std::make_move_iterator(text_sprite_2d.begin()), std::make_move_iterator(text_sprite_2d.end()));
        sprite_3d_renderables.insert(sprite_3d_renderables.end(), std::make_move_iterator(text_sprite_3d.begin()), std::make_move_iterator(text_sprite_3d.end()));
        sprite_3d_renderables.insert(sprite_3d_renderables.end(), std::make_move_iterator(particle_sprite_3d.begin()), std::make_move_iterator(particle_sprite_3d.end()));

        Vector<resource::Font*> dirty_fonts;
        for (const GlyphRequest& request : glyph_requests)
        {
            if (!request.font || !request.font->atlas.RequestGlyph(request.codepoint, request.pixel_height))
            {
                continue;
            }
            if (std::find(dirty_fonts.begin(), dirty_fonts.end(), request.font) == dirty_fonts.end())
            {
                dirty_fonts.push_back(request.font);
            }
        }
        for (resource::Font* font : dirty_fonts)
        {
            resource::UpdateGlyphAtlas(*font);
        }
        // Newly packed glyphs are picked up on the next frame.

        UploadBuffer(light_buffer, retired[frame_slot], shader_lights.data(), shader_lights.size() * sizeof(ShaderLight), sizeof(ShaderLight), device, command_list, frame_slot);
        UploadBuffer(geometry_buffer, retired[frame_slot], shader_geometries.data(), shader_geometries.size() * sizeof(ShaderGeometry), sizeof(ShaderGeometry), device, command_list, frame_slot);
        UploadBuffer(material_buffer, retired[frame_slot], shader_materials.data(), shader_materials.size() * sizeof(ShaderMaterial), sizeof(ShaderMaterial), device, command_list, frame_slot);
        UploadBuffer(bone_buffer, retired[frame_slot], shader_bone_matrices.data(), shader_bone_matrices.size() * sizeof(float4), sizeof(float4), device, command_list, frame_slot);
        UploadBuffer(instance_buffer, retired[frame_slot], shader_instances.data(), shader_instances.size() * sizeof(ShaderInstance), sizeof(ShaderInstance), device, command_list, frame_slot);
        UploadBuffer(particle_buffer, retired[frame_slot], particle_instances.data(), particle_instances.size() * sizeof(float4), sizeof(float4), device, command_list, frame_slot);
        UploadBuffer(decal_buffer, retired[frame_slot], shader_decals.data(), shader_decals.size() * sizeof(ShaderDecal), sizeof(ShaderDecal), device, command_list, frame_slot);
        UploadBuffer(bvh_node_buffer, retired[frame_slot], shader_bvh_nodes.data(), shader_bvh_nodes.size() * sizeof(ShaderBVHNode), sizeof(ShaderBVHNode), device, command_list, frame_slot);
        UploadBuffer(bvh_instance_buffer, retired[frame_slot], shader_bvh_instances.data(), shader_bvh_instances.size() * sizeof(ShaderBVHInstance), sizeof(ShaderBVHInstance), device, command_list, frame_slot);

        const bool uses_sky_lighting = shader_environment.sky_type != SHADER_SKY_TYPE_NONE
            && (shader_environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_SKY
                || shader_environment.reflection_mode == SHADER_REFLECTION_MODE_SKY);
        if (uses_sky_lighting)
        {
            CreateSkyLightingResources(device);
        }
        else if (sky_lighting.capture_texture)
        {
            ReleaseSkyLightingResources(frame_slot);
        }
        CreateDDGIResources(device, frame_slot);
    }
}
