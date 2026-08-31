#pragma once
#include "Animation.h"
#include "MathUtils.h"
#include "Primitives.h"
#include "BVH.h"
#include "Backlog.h"
#include "Resource.h"
#include "RHIResource.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    struct Skeleton;
    struct AnimationClip;

    enum class PrimitiveTopology : uint8
    {
        TriangleList,
        LineList,
        PointList,
    };

    struct Submesh
    {
        uint32 first_index = 0;
        uint32 index_count = 0;
        uint32 first_vertex = 0;
        uint32 material_slot = 0;
        PrimitiveTopology primitive_topology = PrimitiveTopology::TriangleList;
        math::AABB local_bounds = {};
    };

    struct Mesh : public Resource
    {
        struct GPUBVH
        {
            std::shared_ptr<rendering::RHIResource> node_buffer;
            std::shared_ptr<rendering::RHIResource> primitive_buffer;
            rendering::RHISubresourceHandle node_srv = {};
            rendering::RHISubresourceHandle node_uav = {};
            rendering::RHISubresourceHandle primitive_srv = {};
            rendering::RHISubresourceHandle primitive_uav = {};
            uint32 node_count = 0;
            uint32 primitive_count = 0;
            bool dirty = true;

            bool IsValid() const
            {
                return node_buffer != nullptr && primitive_buffer != nullptr && node_srv.IsValid() && primitive_srv.IsValid() && node_count > 0 && primitive_count > 0;
            }
        };

        struct VBSubresource
        {
            rendering::RHISubresourceHandle srv = {};
            rendering::RHISubresourceHandle uav = {};
            uint32 size = 0;
            uint32 offset = 0;

            bool IsValid() const
            {
                return srv.IsValid();
            }
        };

        struct RenderData
        {
            std::shared_ptr<rendering::RHIResource> buffer;
            VBSubresource positions = {};
            VBSubresource colors = {};
            VBSubresource normals = {};
            VBSubresource tangents = {};
            VBSubresource texcoords = {};
            VBSubresource bone_indices = {};
            VBSubresource bone_weights = {};
            VBSubresource indices = {};
            // built for dynamic meshes so normals can be recomputed on the gpu
            VBSubresource adjacency_ranges = {};
            VBSubresource adjacency_triangles = {};

            bool IsValid() const
            {
                return buffer != nullptr && positions.IsValid() && indices.IsValid();
            }
        };

        Vector<float3> positions;
        Vector<float4> colors;
        Vector<float3> normals;
        Vector<float4> tangents;
        Vector<float2> texcoords;
        Vector<uint4> bone_indices;
        Vector<float4> bone_weights;
        Vector<uint32> indices;
        Vector<Submesh> submeshes;
        std::shared_ptr<Skeleton> skeleton;
        Vector<math::AABB> bone_bounds;
        Vector<std::shared_ptr<AnimationClip>> animation_clips; // run, jump...
        bool dynamic_vertex_streams = false; // vertex streams are rewritten every frame, so the gpu copies are ring buffered per frame in flight
        math::bvh::BVH cpu_bvh; // local space bvh
        GPUBVH gpu_bvh = {}; // BLAS
        RenderData render_data = {};

        bool IsValid() const override
        {
            return !positions.empty() && !indices.empty();
        }

        void BuildBVH()
        {
            Vector<math::bvh::BVHPrimitive> primitives;
            primitives.reserve(indices.size() / 3);
            for (const Submesh& submesh : submeshes)
            {
                if (submesh.primitive_topology != PrimitiveTopology::TriangleList)
                {
                    continue;
                }

                const uint32 end_index = submesh.first_index + submesh.index_count;
                for (uint32 index = submesh.first_index; index + 2 < end_index && index + 2 < indices.size(); index += 3)
                {
                    const uint32 i0 = indices[index];
                    const uint32 i1 = indices[index + 1];
                    const uint32 i2 = indices[index + 2];
                    if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    {
                        continue;
                    }

                    math::AABB bounds = {};
                    bounds.Invalidate();
                    math::AABB vertex_bounds = {};
                    vertex_bounds.min = positions[i0];
                    vertex_bounds.max = positions[i0];
                    bounds.Merge(vertex_bounds);
                    vertex_bounds.min = positions[i1];
                    vertex_bounds.max = positions[i1];
                    bounds.Merge(vertex_bounds);
                    vertex_bounds.min = positions[i2];
                    vertex_bounds.max = positions[i2];
                    bounds.Merge(vertex_bounds);

                    primitives.push_back(math::bvh::MakePrimitive(bounds, index / 3));
                }
            }

            cpu_bvh.Build(primitives);
            backlog::Post("Mesh local BVH built: " + std::to_string(primitives.size()) + " triangles");
        }

        void BuildBoneBounds()
        {
            bone_bounds.clear();
            if (!skeleton || bone_indices.size() != positions.size() || bone_weights.size() != positions.size())
            {
                return;
            }

            bone_bounds.resize(skeleton->bones.size());
            for (math::AABB& bounds : bone_bounds)
            {
                bounds.Invalidate();
            }

            for (Size vertex = 0; vertex < positions.size(); ++vertex)
            {
                const uint4& influences = bone_indices[vertex];
                const float4& weights = bone_weights[vertex];
                const uint32 influence_indices[4] = { influences.x, influences.y, influences.z, influences.w };
                const float influence_weights[4] = { weights.x, weights.y, weights.z, weights.w };

                math::AABB vertex_bounds = {};
                vertex_bounds.min = positions[vertex];
                vertex_bounds.max = positions[vertex];

                for (uint32 influence = 0; influence < 4; ++influence)
                {
                    if (influence_weights[influence] <= 0.0f || influence_indices[influence] >= bone_bounds.size())
                    {
                        continue;
                    }

                    bone_bounds[influence_indices[influence]].Merge(vertex_bounds);
                }
            }
        }

        void ClearRenderData()
        {
            render_data = {};
        }

        void ClearGPUBVH()
        {
            gpu_bvh = {};
        }
    };
}
