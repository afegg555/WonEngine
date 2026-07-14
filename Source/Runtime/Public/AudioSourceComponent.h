#pragma once
#include "Types.h"
#include "AudioMixer.h"
#include "Sound.h"

namespace won::ecs
{
    struct AudioSourceComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
            Playing = 1 << 2,
            Loop = 1 << 3,
            Spatial3D = 1 << 4,
            PlayOnStart = 1 << 5,
        };

        uint32 flags = Dirty | Enabled | PlayOnStart;
        String sound_asset_path = "";
        float volume = 1.0f;
		float pitch = 1.0f; // 1.0 = normal pitch, 2.0 = one octave up, 0.5 = one octave down
        float min_distance = 1.0f;
        float max_distance = 20.0f;
        String submix = "";

        std::shared_ptr<won::resource::Sound> sound;

		// internal use only, not serialized
        won::audio::VoiceHandle voice_handle = won::audio::invalid_voice_handle;

        inline void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        inline bool IsDirty() const { return (flags & Dirty) != 0; }
        
        inline void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        inline bool IsEnabled() const { return (flags & Enabled) != 0; }

        inline void SetPlaying(bool value = true) { if (IsPlaying() == value) { return; } if (value) { flags |= Playing; } else { flags &= ~Playing; } }
        inline bool IsPlaying() const { return (flags & Playing) != 0; }

        inline void SetLoop(bool value = true) { if (IsLoop() == value) { return; } if (value) { flags |= Loop; } else { flags &= ~Loop; } SetDirty(); }
        inline bool IsLoop() const { return (flags & Loop) != 0; }

        inline void Set3D(bool value = true) { if (Is3D() == value) { return; } if (value) { flags |= Spatial3D; } else { flags &= ~Spatial3D; } SetDirty(); }
        inline bool Is3D() const { return (flags & Spatial3D) != 0; }

        inline void SetPlayOnStart(bool value = true) { if (IsPlayOnStart() == value) { return; } if (value) { flags |= PlayOnStart; } else { flags &= ~PlayOnStart; } }
        inline bool IsPlayOnStart() const { return (flags & PlayOnStart) != 0; }
    };
}
