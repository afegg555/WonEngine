#include "RenderDataUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include "TransformComponent.h"
#include "GeometryComponent.h"
#include "MaterialComponent.h"

using namespace won::math;
namespace won::ecs
{
    void RenderDataUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

        // compute prefix sum
        Size submesh_sum = 0;
        for (Size i = 0; i < geometry_array->GetSize(); ++i)
        {
            GeometryComponent& geometry_comp = geometry_array->data[i];
            geometry_comp.geometry_offset = (uint32)submesh_sum;
            submesh_sum += geometry_comp.mesh->submeshes.size();
        }
        render_data.shader_geometry.resize(submesh_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)geometry_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const GeometryComponent& geometry_comp = geometry_array->data[args.job_index];
            const resource::Mesh::RenderData* mesh_render_data = geometry_comp.mesh->GetRenderData();
            for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
            {
                ShaderGeometry& shader_geometry = render_data.shader_geometry[geometry_comp.geometry_offset + i];
                shader_geometry.Init();
                shader_geometry.bounds_min = geometry_comp.mesh->submeshes[i].local_bounds.min;
                shader_geometry.bounds_max = geometry_comp.mesh->submeshes[i].local_bounds.max;
                //shader_geometry.flags = geometry_comp.flags;

                if (mesh_render_data)
                {
                    shader_geometry.position_buffer_descriptor = mesh_render_data->positions.handle.descriptor_index;
                    shader_geometry.color_buffer_descriptor = mesh_render_data->colors.handle.descriptor_index;
                    shader_geometry.normal_buffer_descriptor = mesh_render_data->normals.handle.descriptor_index;
                    shader_geometry.texcoord_buffer_descriptor = mesh_render_data->texcoords.handle.descriptor_index;
                    shader_geometry.tangent_buffer_descriptor = mesh_render_data->tangents.handle.descriptor_index;
                    shader_geometry.index_buffer_descriptor = mesh_render_data->indices.handle.descriptor_index;
                    shader_geometry.index_count = geometry_comp.mesh->submeshes[i].index_count;
                }
            }
            });

        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        // compute prefix sum
        Size material_slot_sum = 0;
        for (Size i = 0; i < material_array->GetSize(); ++i)
        {
            MaterialComponent& material_comp = material_array->data[i];
            material_comp.material_offset = (uint32)material_slot_sum;
            material_slot_sum += material_array->data[i].GetMaterialSlotCount();
        }
        render_data.shader_material.resize(material_slot_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)material_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const MaterialComponent& material_comp = material_array->data[args.job_index];
            for (size_t i = 0; i < material_comp.GetMaterialSlotCount(); ++i)
            {
                const MaterialSlot& material_slot = material_comp.material_slots[i];
                ShaderMaterial& shader_material = render_data.shader_material[material_comp.material_offset + i];
                shader_material.Init();
                shader_material.base_color = pack_half4(material_slot.base_color);
                shader_material.emissive_color_metallic = pack_half4(0.f, 0.f, 0.f, material_slot.metallic);
                shader_material.roughness_reflectance_refraction_padding = pack_half4(material_slot.roughness, material_slot.reflectance, 0.f, 0.f);
                shader_material.anisotropy_sheenroughness_clearcoat_clearcoatroughness = pack_half4(material_slot.anisotropy, material_slot.sheen_roughness, material_slot.clearcoat, material_slot.clearcoat_roughness);
                shader_material.sheencolor_padding = pack_half4(material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z, 0.f);
                shader_material.flags = material_slot.flags;

                for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
                {
                    if (material_slot.textures[texture_slot].IsValid())
                    {
                        shader_material.textures[texture_slot].texture_descriptor = material_slot.textures[texture_slot].res_handle.descriptor_index;
                    }
                    
                }
            }
            });

        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        render_data.shader_instance.resize(transform_array->GetSize());

        render_data.renderables.resize(submesh_sum);
        std::atomic<uint32> renderable_count{ 0 };

        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {

            const TransformComponent& transform = transform_array->data[args.job_index];
            ShaderInstance& shader_instance = render_data.shader_instance[args.job_index];
            shader_instance.Init();
            shader_instance.world_transform = transform.world_transform;

            XMMATRIX x_normal_mat = XMLoadFloat4x4(&transform.world_transform);
            x_normal_mat.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
            x_normal_mat = XMMatrixInverse(nullptr, x_normal_mat);
            x_normal_mat = XMMatrixTranspose(x_normal_mat);
            XMStoreFloat3x3(&shader_instance.normal_transform, x_normal_mat);

            Entity entity = transform_array->index_to_entity[args.job_index];
            if (geometry_array->HasData(entity) && material_array->HasData(entity))
            {
                const GeometryComponent& geometry_comp = geometry_array->GetData(entity);
                const MaterialComponent& material_comp = material_array->GetData(entity);
                const resource::Mesh::RenderData* mesh_render_data = geometry_comp.mesh->GetRenderData();
                if (!mesh_render_data || !mesh_render_data->buffer)
                {
                    return;
                }

                const uint32 submesh_count = static_cast<uint32>(geometry_comp.mesh->submeshes.size());
                uint32 index = renderable_count.fetch_add(submesh_count);
                for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                {
                    const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                    const MaterialSlot& material_slot = material_comp.material_slots[submesh.material_slot];
                    Scene::RenderData::Renderable& renderable = render_data.renderables[index + i];
                    ObjectPushConstants& push_constants = renderable.push_constants;
                    push_constants.Init();
                    push_constants.geometry_index = geometry_comp.geometry_offset + (uint)i;
                    push_constants.material_index = material_comp.material_offset + submesh.material_slot;
                    push_constants.instance_index = (uint)transform_array->entity_to_index[entity];

                    renderable.index_buffer = mesh_render_data->buffer;
                    renderable.index_offset = mesh_render_data->indices.offset + submesh.first_index * sizeof(uint32);
                    renderable.index_count = submesh.index_count;
                    renderable.flags = Scene::RenderData::Renderable::None;
                    if (geometry_comp.IsCastShadow())
                    {
                        renderable.flags |= Scene::RenderData::Renderable::CastShadow;
                    }
                    if ((material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0)
                    {
                        renderable.flags |= Scene::RenderData::Renderable::Transparent;
                    }
                }
            }

            });
        jobsystem::Wait(sub_ctx);

        render_data.renderables.resize(renderable_count.load());
    }
}
