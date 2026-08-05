#pragma once
#include "Entity.h"
#include "Types.h"

namespace won::ecs
{
    enum class SequenceTrackType
    {
        Position,
        Rotation,
        CameraFov,
        CameraSwitch,
        Event,
    };

    struct SequenceKey
    {
        float time = 0.0f;
        float4 value = { 0.0f, 0.0f, 0.0f, 1.0f };
        String event_name;
        Entity camera = INVALID_ENTITY;
    };

    struct SequenceTrack
    {
        Entity target = INVALID_ENTITY;
        SequenceTrackType type = SequenceTrackType::Position;
        Vector<SequenceKey> keys;
    };

    struct SequenceComponent
    {
        enum Flags
        {
            Empty = 0,
            Enabled = 1 << 0,
            Playing = 1 << 1,
            Loop = 1 << 2,
            PlayOnStart = 1 << 3,
        };

        uint32 flags = Enabled;
        Vector<SequenceTrack> tracks;
        float time = 0.0f;
        float duration = 0.0f;

        float event_scan_time = -1.0f;
        bool started = false;
        Entity cut_camera = INVALID_ENTITY;

        constexpr void SetEnabled(bool value = true) { if (value) { flags |= Enabled; } else { flags &= ~Enabled; } }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }

        constexpr void SetPlaying(bool value = true) { if (value) { flags |= Playing; } else { flags &= ~Playing; } }
        constexpr bool IsPlaying() const { return (flags & Playing) != 0; }

        constexpr void SetLoop(bool value = true) { if (value) { flags |= Loop; } else { flags &= ~Loop; } }
        constexpr bool IsLoop() const { return (flags & Loop) != 0; }

        constexpr void SetPlayOnStart(bool value = true) { if (value) { flags |= PlayOnStart; } else { flags &= ~PlayOnStart; } }
        constexpr bool IsPlayOnStart() const { return (flags & PlayOnStart) != 0; }
    };
}
