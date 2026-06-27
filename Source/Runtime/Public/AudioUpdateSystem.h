#pragma once
#include "System.h"
#include "RuntimeExport.h"
#include "AudioMixer.h"

#include <memory>

namespace won::ecs
{
    class Scene;

    class WONENGINE_API AudioUpdateSystem final : public System
    {
    public:
        explicit AudioUpdateSystem(won::audio::AudioMixer* mixer);
        ~AudioUpdateSystem() override;

        ComponentMask GetReadOnlyMask() const override { return transform_component_mask | audio_listener_component_mask; }
        ComponentMask GetWriteMask() const override { return audio_source_component_mask; }
        SystemExecutionPolicy GetExecutionPolicy() const override { return SystemExecutionPolicy::Synchronous; }

        void Update(Scene& scene, float delta_time) override;

    private:
        won::audio::AudioMixer* audio_mixer = nullptr;
    };
}
