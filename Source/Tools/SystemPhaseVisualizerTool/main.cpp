// SystemPhaseVisualizerTool
// Introspects the REAL ECS system schedule (Scene::BuildSystemSchedule) and prints it
// to the console: phases in forced order, and within each phase the execution batches
// (same batch = parallel, next batch depends on previous) derived from component R/W masks.

#include "PhysicsWorld.h"
#include "Scene.h"
#include "SceneComponents.h"
#include "System.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <typeinfo>

using namespace won::ecs;

namespace
{
    struct MaskEntry
    {
        ComponentMask mask;
        const char* name;
    };

    const MaskEntry mask_table[] = {
        { transform_component_mask, "Transform" },
        { hierarchy_component_mask, "Hierarchy" },
        { name_component_mask, "Name" },
        { geometry_component_mask, "Geometry" },
        { material_component_mask, "Material" },
        { sprite_2d_component_mask, "Sprite2D" },
        { sprite_3d_component_mask, "Sprite3D" },
        { text_2d_component_mask, "Text2D" },
        { text_3d_component_mask, "Text3D" },
        { camera_component_mask, "Camera" },
        { light_component_mask, "Light" },
        { environment_component_mask, "Environment" },
        { fog_volume_component_mask, "FogVolume" },
        { ddgi_volume_component_mask, "DDGIVolume" },
        { animation_component_mask, "Animation" },
        { script_component_mask, "Script" },
        { collider_3d_component_mask, "Collider3D" },
        { rigidbody_3d_component_mask, "Rigidbody3D" },
        { audio_source_component_mask, "AudioSource" },
        { audio_listener_component_mask, "AudioListener" },
        { layer_component_mask, "Layer" },
        { terrain_component_mask, "Terrain" },
        { particle_emitter_3d_component_mask, "ParticleEmitter3D" },
        { decal_component_mask, "Decal" },
        { button_component_mask, "Button" },
        { canvas_2d_component_mask, "Canvas2D" },
        { rect_transform_2d_component_mask, "RectTransform2D" },
        { collision_layer_component_mask, "CollisionLayer" },
        { layout_component_mask, "Layout" },
    };

    std::string MaskToString(ComponentMask m)
    {
        if (m == 0)
        {
            return "-";
        }
        std::string s;
        for (const MaskEntry& e : mask_table)
        {
            if ((m & e.mask) != 0)
            {
                if (!s.empty())
                {
                    s += ",";
                }
                s += e.name;
            }
        }
        return s.empty() ? "-" : s;
    }

    const char* PhaseName(SystemPhase p)
    {
        switch (p)
        {
        case SystemPhase::PreSimulation:  return "PreSimulation";
        case SystemPhase::Simulation:     return "Simulation";
        case SystemPhase::PostSimulation: return "PostSimulation";
        default:                          return "?";
        }
    }

    std::string SystemName(const System& s)
    {
        std::string n = typeid(s).name(); // MSVC: "class won::ecs::XxxSystem"
        const std::string::size_type pos = n.rfind(':');
        if (pos != std::string::npos)
        {
            n = n.substr(pos + 1);
        }
        return n;
    }
}

int main()
{
    won::physics::Initialize(); // Jolt globals; a Scene constructs a PhysicsWorld.

    SceneDesc desc;
    desc.enable_simulation = true;
    desc.script_runtime = reinterpret_cast<won::script::ScriptRuntime*>(1);
    desc.audio_mixer = nullptr;

    Scene scene(desc);
    scene.BuildSystemSchedule();

    const auto& systems = scene.GetSystems();
    const auto& batches = scene.GetSystemExecutionBatches();

    std::printf("\n============================================================\n");
    std::printf(" WonEngine System Schedule  (enable_simulation=true, script_runtime=present)\n");
    std::printf("============================================================\n");
    std::printf(" rules:\n");
    std::printf("  - Phase order FORCED:  PreSimulation -> Simulation -> PostSimulation\n");
    std::printf("  - Within a phase: split into BATCHES by component R/W conflict\n");
    std::printf("       RAW (writer before reader) / WAW (write-write, registration order)\n");
    std::printf("  - Same batch = run in PARALLEL (NO order guarantee); next batch depends on prev\n");
    std::printf("  - [sync] main-thread / [job] parallel job\n");
    std::printf("  - Tracks COMPONENT R/W only. Scene-level state (event queues) is NOT ordered.\n");

    int prev_phase = -1;
    int batch_in_phase = 0;
    for (const auto& batch : batches)
    {
        if (batch.empty())
        {
            continue;
        }
        const int phase = static_cast<int>(systems[batch[0]]->GetPhase());
        if (phase != prev_phase)
        {
            std::printf("\n------- %s -------\n", PhaseName(static_cast<SystemPhase>(phase)));
            prev_phase = phase;
            batch_in_phase = 0;
        }
        std::printf("  Batch %d%s:\n", batch_in_phase, batch.size() > 1 ? "  (parallel)" : "");
        ++batch_in_phase;
        for (auto idx : batch)
        {
            const System& s = *systems[idx];
            const char* pol = (s.GetExecutionPolicy() == SystemExecutionPolicy::Synchronous) ? "[sync]" : "[job] ";
            std::printf("      %-27s %s  W:{%s}  R:{%s}\n",
                SystemName(s).c_str(), pol,
                MaskToString(s.GetWriteMask()).c_str(),
                MaskToString(s.GetReadOnlyMask()).c_str());
        }
    }
    std::printf("\n");

    won::physics::Shutdown();
    return 0;
}
