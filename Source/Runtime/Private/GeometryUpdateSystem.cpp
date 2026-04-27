#include "GeometryUpdateSystem.h"

#include "Scene.h"
#include "JobSystem.h"
#include "GeometryComponent.h"

namespace won::ecs
{
    void GeometryUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

        bool dirty = false;
        // compute prefix sum
        Size submesh_sum = 0;
        for (Size i = 0; i < geometry_array->GetSize(); ++i)
        {
            GeometryComponent& geometry_comp = geometry_array->data[i];
            geometry_comp.geometry_offset = (uint32)submesh_sum;
            if (geometry_comp.mesh)
            {
                submesh_sum += geometry_comp.mesh->submeshes.size();
            }
            dirty |= geometry_comp.IsDirty();
        }

        if (!dirty)
            return;

        render_data.shader_geometries.resize(submesh_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)geometry_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            GeometryComponent& geometry_comp = geometry_array->data[args.job_index];
            if (!geometry_comp.mesh)
            {
                return;
            }

            const resource::Mesh::RenderData* mesh_render_data = geometry_comp.mesh->GetRenderData();

            for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
            {
                ShaderGeometry& shader_geometry = render_data.shader_geometries[geometry_comp.geometry_offset + i];
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
                    shader_geometry.first_index = geometry_comp.mesh->submeshes[i].first_index;
                }
            }

            geometry_comp.SetDirty(false);
        });

        scene.SetBVHDirty();
        jobsystem::Wait(sub_ctx);
    }
}
