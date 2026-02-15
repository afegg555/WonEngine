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
            Mesh::VBSubresource& out_subresource,
            Size& inout_offset)
        {
            out_subresource.offset = inout_offset;
            out_subresource.size = source.size() * sizeof(T);
            out_subresource.stride = sizeof(T);
            if (out_subresource.size == 0)
            {
                return;
            }

            std::memcpy(destination.data() + inout_offset, source.data(), out_subresource.size);
            inout_offset += out_subresource.size;
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
        PackBufferSubresource(positions, packed_data, new_render_data.positions, offset);
        PackBufferSubresource(normals, packed_data, new_render_data.normals, offset);
        PackBufferSubresource(texcoords, packed_data, new_render_data.texcoords, offset);
        PackBufferSubresource(indices, packed_data, new_render_data.indices, offset);

        rendering::RHIBufferDesc buffer_desc = {};
        buffer_desc.size = total_size;
        buffer_desc.usage = rendering::RHIResourceUsage::Upload;
        buffer_desc.bind_flags = rendering::RHIBindFlags::VertexBuffer | rendering::RHIBindFlags::IndexBuffer;
        new_render_data.buffer = device->CreateBuffer(buffer_desc, packed_data.data(), packed_data.size());
        if (!new_render_data.buffer)
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
