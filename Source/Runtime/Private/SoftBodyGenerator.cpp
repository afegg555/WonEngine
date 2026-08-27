#include "SoftBodyGenerator.h"

#include <algorithm>

namespace won::ecs
{
    static constexpr float soft_body_rest_wrinkle = 0.13f;

    std::shared_ptr<resource::Mesh> GenerateSoftBodyMesh(const SoftBodyComponent& soft_body)
    {
        const uint32 vertices_x = (std::max)(1u, soft_body.divisions_x) + 1u;
        const uint32 vertices_y = (std::max)(1u, soft_body.divisions_y) + 1u;
        const Size vertex_count = static_cast<Size>(vertices_x) * static_cast<Size>(vertices_y);

        const float width = (std::max)(0.001f, soft_body.size_x);
        const float height = (std::max)(0.001f, soft_body.size_y);
        const float step_x = width / static_cast<float>(vertices_x - 1u);
        const float step_y = height / static_cast<float>(vertices_y - 1u);

        // a perfectly flat sheet has no reason to buckle either way, so break the symmetry.
        const float wrinkle = soft_body.bend_type == SoftBodyComponent::BendType::None ? soft_body_rest_wrinkle : 0.0f;

        auto mesh = std::make_shared<resource::Mesh>();
        mesh->dynamic_vertex_streams = true;
        mesh->positions.resize(vertex_count);
        mesh->normals.assign(vertex_count, float3(0.0f, 0.0f, 1.0f));
        mesh->texcoords.resize(vertex_count);
        for (uint32 y = 0; y < vertices_y; ++y)
        {
            for (uint32 x = 0; x < vertices_x; ++x)
            {
                const Size index = static_cast<Size>(y) * vertices_x + x;
                mesh->positions[index] = {
                    -width * 0.5f + step_x * static_cast<float>(x),
                    -step_y * static_cast<float>(y),
                    ((y & 1u) ? 1.0f : -1.0f) * wrinkle * step_y
                };
                mesh->texcoords[index] = {
                    static_cast<float>(x) / static_cast<float>(vertices_x - 1u),
                    static_cast<float>(y) / static_cast<float>(vertices_y - 1u)
                };
            }
        }

        mesh->indices.reserve(static_cast<Size>(vertices_x - 1u) * (vertices_y - 1u) * 6u);
        for (uint32 y = 0; y + 1u < vertices_y; ++y)
        {
            for (uint32 x = 0; x + 1u < vertices_x; ++x)
            {
                const uint32 top_left = y * vertices_x + x;
                const uint32 top_right = top_left + 1u;
                const uint32 bottom_left = top_left + vertices_x;
                const uint32 bottom_right = bottom_left + 1u;

                mesh->indices.push_back(top_left);
                mesh->indices.push_back(bottom_left);
                mesh->indices.push_back(top_right);

                mesh->indices.push_back(top_right);
                mesh->indices.push_back(bottom_left);
                mesh->indices.push_back(bottom_right);
            }
        }

        math::AABB bounds = {};
        bounds.min = { -width * 0.5f - height, -height - height, -height };
        bounds.max = { width * 0.5f + height, height, height };

        resource::Submesh submesh = {};
        submesh.first_index = 0;
        submesh.index_count = static_cast<uint32>(mesh->indices.size());
        submesh.first_vertex = 0;
        submesh.material_slot = 0;
        submesh.primitive_topology = resource::PrimitiveTopology::TriangleList;
        submesh.local_bounds = bounds;
        mesh->submeshes.push_back(submesh);

        return mesh;
    }
}
