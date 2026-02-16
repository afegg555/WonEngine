#pragma once

#include "Primitives.h"
#include "ResourceLoader.h"
#include "RHIResource.h"
#include "Types.h"

#include <memory>

namespace won::rendering
{
    class RHIDevice;
}

namespace won::resource
{
    struct Submesh
    {
        uint32 first_index = 0;
        uint32 index_count = 0;
        uint32 first_vertex = 0;
        uint32 material_slot = 0;
        math::Aabb local_bounds = {};
    };

    struct WONENGINE_API Mesh : public Resource
    {
        Vector<float3> positions;
        Vector<float3> normals;
        Vector<float2> texcoords;
        Vector<uint32> indices;
        Vector<Submesh> submeshes;

        bool IsValid() const override;

        struct VBSubresource
        {
            rendering::RHISubresourceHandle handle = {};

            bool IsValid() const
            {
                return handle.IsValid();
            }
        };

        struct RenderData
        {
            std::shared_ptr<rendering::RHIResource> buffer;
            VBSubresource positions = {};
            VBSubresource normals = {};
            VBSubresource texcoords = {};
            VBSubresource indices = {};

            bool IsValid() const
            {
                return buffer != nullptr && positions.IsValid() && indices.IsValid();
            }
        };

        bool CreateRenderData(const std::shared_ptr<rendering::RHIDevice>& device) override;
        const RenderData* GetRenderData() const;
        void ClearRenderData();

    private:
        RenderData render_data = {};
    };
}
