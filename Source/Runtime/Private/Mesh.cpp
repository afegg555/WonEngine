#include "Mesh.h"

#include "RHIDevice.h"

#include <cstring>
#include <utility>

namespace won::resource
{
    namespace
    {
        template <typename T>
        void PackBufferSubresource(const Vector<T>& source,
            Vector<uint8>& destination,
            Size& out_offset,
            const Size& in_size,
            Size& inout_offset)
        {
            out_offset = inout_offset;

            if (in_size == 0)
                return;

            std::memcpy(destination.data() + inout_offset, source.data(), in_size);
            inout_offset += in_size;
        }
    }

    bool Mesh::IsValid() const
    {
        return !positions.empty() && !indices.empty();
    }

    bool Mesh::CreateRenderData(const std::shared_ptr<rendering::RHIDevice>& device)
    {
        if (render_data.IsValid())
        {
            return true;
        }

        if (!device || !IsValid())
        {
            return false;
        }

        const Size positions_size = positions.size() * sizeof(float3);
        const Size normals_size = normals.size() * sizeof(float3);
        const Size texcoords_size = texcoords.size() * sizeof(float2);
        const Size indices_size = indices.size() * sizeof(uint32);
        const Size total_size = positions_size + normals_size + texcoords_size + indices_size;
        if (total_size == 0)
        {
            return false;
        }

        Vector<uint8> packed_data;
        packed_data.resize(total_size);

        RenderData new_render_data = {};
        Size offset = 0;

        Size positions_offset = 0;
        Size normals_offset = 0;
        Size texcoords_offset = 0;
        Size indices_offset = 0;

        PackBufferSubresource(positions, packed_data, positions_offset, positions_size, offset);
        PackBufferSubresource(normals, packed_data, normals_offset, normals_size, offset);
        PackBufferSubresource(texcoords, packed_data, texcoords_offset, texcoords_size, offset);
        PackBufferSubresource(indices, packed_data, indices_offset, indices_size, offset);

        rendering::RHIBufferDesc buffer_desc = {};
        buffer_desc.size = total_size;
        buffer_desc.usage = rendering::RHIResourceUsage::Default;
        buffer_desc.bind_flags = rendering::RHIBindFlags::VertexBuffer | rendering::RHIBindFlags::IndexBuffer | rendering::RHIBindFlags::ShaderResource;
        new_render_data.buffer = device->CreateBuffer(buffer_desc, packed_data.data(), packed_data.size());
        if (!new_render_data.buffer)
        {
            return false;
        }

        auto create_subresource = [&](rendering::RHISubresourceType type, Size buffer_offset, Size buffer_size, Size buffer_stride, VBSubresource& out_subresource) -> bool
        {
            if (buffer_size == 0)
            {
                return true;
            }

            rendering::RHISubresourceDesc subresource_desc = {};
            subresource_desc.type = type;
            subresource_desc.buffer_offset = buffer_offset;
            subresource_desc.buffer_size = buffer_size;
            subresource_desc.buffer_stride = buffer_stride;
            return device->CreateSubresource(*new_render_data.buffer, subresource_desc, &out_subresource.handle);
        };

        // for manual vertex pulling
        if (!create_subresource(rendering::RHISubresourceType::ShaderResource, positions_offset, positions_size, sizeof(float3), new_render_data.positions))
        {
            return false;
        }
        if (!create_subresource(rendering::RHISubresourceType::ShaderResource, normals_offset, normals_size, sizeof(float3), new_render_data.normals))
        {
            return false;
        }
        if (!create_subresource(rendering::RHISubresourceType::ShaderResource, texcoords_offset, texcoords_size, sizeof(float2), new_render_data.texcoords))
        {
            return false;
        }
        if (!create_subresource(rendering::RHISubresourceType::ShaderResource, indices_offset, indices_size, sizeof(uint32), new_render_data.indices))
        {
            return false;
        }

        render_data = std::move(new_render_data);
        return true;
    }

    const Mesh::RenderData* Mesh::GetRenderData() const
    {
        return render_data.IsValid() ? &render_data : nullptr;
    }

    void Mesh::ClearRenderData()
    {
        render_data = {};
    }
}
