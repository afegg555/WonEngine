#include "FrameGraph.h"

#include "RHICommandList.h"
#include "RHIDevice.h"
#include "JobSystem.h"

namespace won::rendering
{
    void FrameGraph::Reset(RHIDevice& in_device, FrameContext& in_frame_context)
    {
        device = &in_device;
        frame_context = &in_frame_context;
        resources.clear();
        resource_ids.clear();
        passes.clear();
        buffer_uploads.clear();
        upload_pass_index = 0;
        has_upload_pass = false;
    }

    void FrameGraph::MarkNoCull(FrameGraphResourceId resource)
    {
        if (resource < resources.size())
        {
            resources[resource].no_cull = true;
        }
    }

    FrameGraphResourceId FrameGraph::Import(RHIResource& resource)
    {
        auto found = resource_ids.find(&resource);
        if (found != resource_ids.end())
        {
            return found->second;
        }

        const FrameGraphResourceId id = static_cast<FrameGraphResourceId>(resources.size());
        FrameGraphResource& graph_resource = resources.emplace_back();
        graph_resource.resource = &resource;
        resource_ids.emplace(&resource, id);
        return id;
    }


    bool FrameGraph::QueueBufferUpload(FrameGraphResourceId destination, const void* data, Size size, RHIResourceState final_state, Size destination_offset)
    {
        RHIResource* destination_resource = destination < resources.size() ? resources[destination].resource : nullptr;
        if (!destination_resource || size == 0)
        {
            return false;
        }

        FrameUploadAllocation allocation = {};
        const Size alignment = device->GetMinOffsetAlignment(destination_resource->GetDesc().buffer_desc);
        if (!frame_context->AllocateFrameUpload(*device, size, alignment, allocation))
        {
            return false;
        }
        std::memcpy(allocation.mapped_data, data, size);

        if (!has_upload_pass)
        {
            has_upload_pass = true;
            upload_pass_index = passes.size();
            AddPass("Upload Buffers", {}, [this](RHICommandList& command_list)
            {
                for (const BufferUpload& upload : buffer_uploads)
                {
                    command_list.TransitionResource(*upload.destination, RHIResourceState::CopyDest);
                    command_list.CopyBuffer(*upload.destination, upload.destination_offset, *upload.source, upload.source_offset, upload.size);
                    command_list.TransitionResource(*upload.destination, upload.final_state);
                }
            });
        }

        passes[upload_pass_index].accesses.push_back({ destination, RHIResourceState::CopyDest, FrameGraphAccessType::Write });
        BufferUpload& upload = buffer_uploads.emplace_back();
        upload.destination = destination_resource;
        upload.destination_offset = destination_offset;
        upload.source = allocation.buffer;
        upload.source_offset = allocation.buffer_offset;
        upload.size = size;
        upload.final_state = final_state;
        return true;
    }

    void FrameGraph::AddPass(const char* name, Vector<FrameGraphAccess> accesses, std::function<void(RHICommandList&)> execute)
    {
        Pass& pass = passes.emplace_back();
        pass.name = name;
        pass.accesses = std::move(accesses);
        pass.execute = std::move(execute);
    }

    void FrameGraph::Compile()
    {
        Vector<bool> pass_alive(passes.size(), false);
        Vector<bool> resource_needed(resources.size(), false);

        for (Size resource_index = 0; resource_index < resources.size(); ++resource_index)
        {
            resource_needed[resource_index] = resources[resource_index].no_cull;
        }

        // !! backwards
        for (Size pass_index = passes.size(); pass_index > 0; --pass_index)
        {
            const Size index = pass_index - 1;
            const Pass& pass = passes[index];

            for (const FrameGraphAccess& access : pass.accesses)
            {
                if (access.type == FrameGraphAccessType::Read)
                {
                    continue;
                }
                if (access.resource < resource_needed.size() && resource_needed[access.resource])
                {
                    pass_alive[index] = true;
                    break;
                }
            }

            if (!pass_alive[index])
            {
                continue;
            }

			// all read accesses of this pass are needed
            for (const FrameGraphAccess& access : pass.accesses)
            {
                if (access.type != FrameGraphAccessType::Write && access.resource < resource_needed.size())
                {
                    resource_needed[access.resource] = true;
                }
            }
        }

        for (Size pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            if (!pass_alive[pass_index])
            {
                continue;
            }

            Pass& pass = passes[pass_index];
            RHICommandList* pass_command_list = frame_context->BeginCommandList(*device);
            if (!pass_command_list)
            {
                return;
            }
            pass.command_list = pass_command_list;

            pass_command_list->BeginEvent(pass.name.c_str());
            for (const FrameGraphAccess& access : pass.accesses)
            {
                if (access.resource >= resources.size() || !resources[access.resource].resource)
                {
                    continue;
                }
                if (resources[access.resource].state == access.state)
                {
                    continue;
                }

                pass_command_list->TransitionResource(*resources[access.resource].resource, access.state);
                resources[access.resource].state = access.state;
            }
        }
    }

    void FrameGraph::Dispatch(jobsystem::Context& context)
    {
        for (Pass& pass : passes)
        {
            if (!pass.command_list)
            {
                continue;
            }

            jobsystem::Execute(context, [pass = &pass](jobsystem::JobArgs)
            {
                if (pass->execute)
                {
                    pass->execute(*pass->command_list);
                }
                pass->command_list->EndEvent();
            });
        }
    }
}
