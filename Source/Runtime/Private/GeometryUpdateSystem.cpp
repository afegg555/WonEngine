#include "GeometryUpdateSystem.h"

#include "Scene.h"
#include "JobSystem.h"
#include "GeometryComponent.h"

namespace won::ecs
{
    void GeometryUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct GeometryBucket
        {
            UnorderedSet<resource::Mesh*> meshes;
            bool dirty = false;
        };

        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

        const uint32 job_count = static_cast<uint32>(geometry_array->GetSize());
        Vector<GeometryBucket> geometry_buckets(jobsystem::DispatchGroupCount(job_count, groupsize_light));

        jobsystem::Dispatch(sub_ctx, job_count, groupsize_light, [&](jobsystem::JobArgs args) {
            GeometryBucket& bucket = geometry_buckets[args.group_id];
            GeometryComponent& geometry_comp = geometry_array->data[args.job_index];

            if (geometry_comp.IsDirty())
            {
                bucket.dirty = true;
                geometry_comp.SetDirty(false);
            }

            if (geometry_comp.mesh)
                bucket.meshes.insert(geometry_comp.mesh.get());
        });

        jobsystem::Wait(sub_ctx);

        bool dirty = false;
        Vector<resource::Mesh*> unique_meshes;
        Size unique_submesh_sum = 0;
        {
            UnorderedSet<resource::Mesh*> merged;
            for (GeometryBucket& bucket : geometry_buckets)
            {
                dirty |= bucket.dirty;
                for (resource::Mesh* mesh : bucket.meshes)
                {
                    if (merged.insert(mesh).second)
                    {
                        mesh->geometry_offset = (uint32)unique_submesh_sum;
                        unique_meshes.push_back(mesh);
                        unique_submesh_sum += mesh->submeshes.size();
                    }
                }
            }
        }

        const bool geometry_structure_changed = render_data.shader_geometries.size() != unique_submesh_sum;
        if (!dirty && !geometry_structure_changed)
        {
            return;
        }

        render_data.shader_geometries.resize(unique_submesh_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)unique_meshes.size(), groupsize_light, [&](jobsystem::JobArgs args) {
            resource::Mesh* mesh = unique_meshes[args.job_index];
            const resource::Mesh::RenderData& mesh_render_data = mesh->render_data;

            for (Size i = 0; i < mesh->submeshes.size(); ++i)
            {
                ShaderGeometry& shader_geometry = render_data.shader_geometries[mesh->geometry_offset + i];
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

        scene.SetBVHDirty();
        jobsystem::Wait(sub_ctx);
    }
}
