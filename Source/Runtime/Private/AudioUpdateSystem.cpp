#include "AudioUpdateSystem.h"
#include "Scene.h"
#include "Sound.h"
#include "JobSystem.h"

using namespace DirectX;

namespace won::ecs
{
    AudioUpdateSystem::AudioUpdateSystem(won::audio::AudioMixer* mixer)
        : audio_mixer(mixer)
    {
    }

    AudioUpdateSystem::~AudioUpdateSystem()
    {
        // audio_mixer outlives this system (owned by Application); voices still playing would
        // otherwise hold dangling resource::Sound pointers once AudioSourceComponent::sound is destroyed.
        if (audio_mixer)
            audio_mixer->StopAll();
    }

    void AudioUpdateSystem::Update(Scene& scene, float delta_time)
    {
        if (!audio_mixer)
            return;

        auto audio_source_array = scene.GetComponentArray<AudioSourceComponent>().get();
        auto audio_listener_array = scene.GetComponentArray<AudioListenerComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        if (!audio_source_array || !transform_array)
            return;

		// set listener
        if (audio_listener_array)
        {
            for (Size i = 0; i < audio_listener_array->GetSize(); ++i)
            {
                if (!audio_listener_array->data[i].enabled)
                    continue;

                const Entity listener_entity = audio_listener_array->index_to_entity[i];
                if (!transform_array->HasData(listener_entity))
                    break;

                const TransformComponent& t = transform_array->GetData(listener_entity);
                const XMMATRIX world = t.GetWorldTransform();

                XMVECTOR S, R, T;
                XMMatrixDecompose(&S, &R, &T, world);

                won::audio::ListenerState listener;
                XMStoreFloat3(&listener.position, T);
                XMStoreFloat4(&listener.rotation, R);
                audio_mixer->SetListener(listener);
                break;
            }
        }

        jobsystem::Context ctx;
        jobsystem::Dispatch(ctx, (uint32_t)audio_source_array->GetSize(), jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            const Entity entity = audio_source_array->index_to_entity[args.job_index];
            AudioSourceComponent& source = audio_source_array->data[args.job_index];

            // stop and skip disabled sources
            if (!source.IsEnabled())
            {
                if (source.voice_handle != won::audio::invalid_voice_handle)
                {
                    audio_mixer->Stop(source.voice_handle);
                    source.voice_handle = won::audio::invalid_voice_handle;
                }
                return;
            }

            // Check if voice finished
            if (source.voice_handle != won::audio::invalid_voice_handle && !audio_mixer->IsPlaying(source.voice_handle))
            {
                source.voice_handle = won::audio::invalid_voice_handle;
                source.SetPlaying(false);
            }

            // PlayOnStart trigger
            if (source.voice_handle == won::audio::invalid_voice_handle && source.IsPlayOnStart())
            {
                source.SetPlaying(true);
                source.SetPlayOnStart(false);
            }

            // Dirty: restart voice with new params
            if (source.voice_handle != won::audio::invalid_voice_handle && source.IsDirty())
            {
                audio_mixer->Stop(source.voice_handle);
                source.voice_handle = won::audio::invalid_voice_handle;
                source.SetDirty(false);
            }

            won::audio::VoiceParams params;
            params.volume = source.volume;
            params.pitch = source.pitch;
            params.loop = source.IsLoop();
            params.spatial_3d = source.Is3D();
            params.min_distance = source.min_distance;
            params.max_distance = source.max_distance;
            params.submix = source.submix;
            if (transform_array->HasData(entity))
            {
                const XMMATRIX world = transform_array->GetData(entity).GetWorldTransform();
                XMStoreFloat3(&params.position, world.r[3]);
            }

            // Start playing
            if (source.IsPlaying() && source.voice_handle == won::audio::invalid_voice_handle)
            {
                if (!source.sound)
                    source.sound = won::resource::LoadSoundFile(source.sound_asset_path);

                if (source.sound)
                {
                    source.voice_handle = audio_mixer->Play(*source.sound, params);
                    if (source.voice_handle == won::audio::invalid_voice_handle)
                        source.SetPlaying(false);
                }
                else
                {
                    source.SetPlaying(false);
                }
                return;
            }

            // Update params for active voice
            if (source.voice_handle != won::audio::invalid_voice_handle)
            {
                audio_mixer->UpdateParams(source.voice_handle, params);
            }
        });
        jobsystem::Wait(ctx);
    }
}
