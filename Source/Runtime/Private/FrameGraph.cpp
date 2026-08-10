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
    }

    void FrameGraph::MarkOutput(FrameGraphResourceId resource)
    {
        if (resource < resources.size())
        {
            resources[resource].is_output = true;
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
            resource_needed[resource_index] = resources[resource_index].is_output;
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
