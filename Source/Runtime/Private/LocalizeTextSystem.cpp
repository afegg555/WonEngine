#include "LocalizeTextSystem.h"

#include "Localization.h"
#include "Scene.h"
#include "JobSystem.h"

namespace won::ecs
{
    void LocalizeTextSystem::Update(Scene& scene, float delta_time)
    {
        const uint32 revision = locale::GetRevision();
        jobsystem::Context sub_ctx;

        auto text_2d_array = scene.GetComponentArray<Text2DComponent>().get();
        jobsystem::Dispatch(sub_ctx, static_cast<uint32>(text_2d_array->GetSize()), jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            Text2DComponent& text = text_2d_array->data[args.job_index];
            if (text.text_key.empty())
            {
                text.locale_revision = revision;
                text.resolved_text = text.text;
                return;
            }
            if (text.locale_revision == revision)
            {
                return;
            }
            text.locale_revision = revision;
            text.resolved_text = locale::GetText(text.text_key);
        });

        auto text_3d_array = scene.GetComponentArray<Text3DComponent>().get();
        jobsystem::Dispatch(sub_ctx, static_cast<uint32>(text_3d_array->GetSize()), jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            Text3DComponent& text = text_3d_array->data[args.job_index];
            if (text.text_key.empty())
            {
                text.locale_revision = revision;
                text.resolved_text = text.text;
                return;
            }
            if (text.locale_revision == revision)
            {
                return;
            }
            text.locale_revision = revision;
            text.resolved_text = locale::GetText(text.text_key);
        });

        jobsystem::Wait(sub_ctx);
    }
}
