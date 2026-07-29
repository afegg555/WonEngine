#pragma once

#include <type_traits>

#include "GeometryComponent.h"
#include "MaterialComponent.h"
#include "NameComponent.h"
#include "TransformComponent.h"
#include "HierarchyComponent.h"
#include "Sprite2DComponent.h"
#include "Sprite3DComponent.h"
#include "Text2DComponent.h"
#include "Text3DComponent.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "EnvironmentComponent.h"
#include "FogVolumeComponent.h"
#include "DDGIVolumeComponent.h"
#include "ReflectionProbeComponent.h"
#include "AnimationComponent.h"
#include "AnimationStateMachineComponent.h"
#include "ScriptComponent.h"
#include "Collider3DComponent.h"
#include "Rigidbody3DComponent.h"
#include "JointComponent.h"
#include "AudioSourceComponent.h"
#include "AudioListenerComponent.h"
#include "VisibilityLayerComponent.h"
#include "CollisionLayerComponent.h"
#include "TerrainComponent.h"
#include "NavMeshComponent.h"
#include "ParticleEmitter3DComponent.h"
#include "DecalComponent.h"
#include "Canvas2DComponent.h"
#include "RectTransform2DComponent.h"
#include "ButtonComponent.h"
#include "LayoutComponent.h"
#include "BehaviorTreeComponent.h"

namespace won::ecs
{
#ifdef AudioSource
#undef AudioSource
#endif
#ifdef AudioListener
#undef AudioListener
#endif

    using ComponentMask = uint64;

    enum class SceneComponentBit : uint32
    {
        Transform,
        Hierarchy,
        Name,
        Geometry,
        Material,
        Sprite3D,
        Text3D,
        Camera,
        Light,
        Environment,
        FogVolume,
        DDGIVolume,
        Animation,
        Sprite2D,
        Text2D,
        Script,
        Collider3D,
        Rigidbody3D,
        AudioSource,
        AudioListener,
        Layer,
        Terrain,
        ParticleEmitter3D,
        Decal,
        Button,
        Canvas2D,
        RectTransform2D,
        CollisionLayer,
        Layout,
        AnimationStateMachine,
        ReflectionProbe,
        Joint,
        BehaviorTree
    };

    constexpr ComponentMask ComponentMaskFromBit(SceneComponentBit bit)
    {
        return static_cast<ComponentMask>(1ull << static_cast<uint32>(bit));
    }

    inline constexpr ComponentMask none_component_mask = 0;
    inline constexpr ComponentMask transform_component_mask = ComponentMaskFromBit(SceneComponentBit::Transform);
    inline constexpr ComponentMask hierarchy_component_mask = ComponentMaskFromBit(SceneComponentBit::Hierarchy);
    inline constexpr ComponentMask name_component_mask = ComponentMaskFromBit(SceneComponentBit::Name);
    inline constexpr ComponentMask geometry_component_mask = ComponentMaskFromBit(SceneComponentBit::Geometry);
    inline constexpr ComponentMask material_component_mask = ComponentMaskFromBit(SceneComponentBit::Material);
    inline constexpr ComponentMask sprite_2d_component_mask = ComponentMaskFromBit(SceneComponentBit::Sprite2D);
    inline constexpr ComponentMask sprite_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::Sprite3D);
    inline constexpr ComponentMask text_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::Text3D);
    inline constexpr ComponentMask camera_component_mask = ComponentMaskFromBit(SceneComponentBit::Camera);
    inline constexpr ComponentMask light_component_mask = ComponentMaskFromBit(SceneComponentBit::Light);
    inline constexpr ComponentMask environment_component_mask = ComponentMaskFromBit(SceneComponentBit::Environment);
    inline constexpr ComponentMask fog_volume_component_mask = ComponentMaskFromBit(SceneComponentBit::FogVolume);
    inline constexpr ComponentMask ddgi_volume_component_mask = ComponentMaskFromBit(SceneComponentBit::DDGIVolume);
    inline constexpr ComponentMask reflection_probe_component_mask = ComponentMaskFromBit(SceneComponentBit::ReflectionProbe);
    inline constexpr ComponentMask animation_component_mask = ComponentMaskFromBit(SceneComponentBit::Animation);
    inline constexpr ComponentMask text_2d_component_mask = ComponentMaskFromBit(SceneComponentBit::Text2D);
    inline constexpr ComponentMask script_component_mask = ComponentMaskFromBit(SceneComponentBit::Script);
    inline constexpr ComponentMask collider_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::Collider3D);
    inline constexpr ComponentMask rigidbody_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::Rigidbody3D);
    inline constexpr ComponentMask audio_source_component_mask = ComponentMaskFromBit(SceneComponentBit::AudioSource);
    inline constexpr ComponentMask audio_listener_component_mask = ComponentMaskFromBit(SceneComponentBit::AudioListener);
    inline constexpr ComponentMask layer_component_mask = ComponentMaskFromBit(SceneComponentBit::Layer);
    inline constexpr ComponentMask terrain_component_mask = ComponentMaskFromBit(SceneComponentBit::Terrain);
    inline constexpr ComponentMask particle_emitter_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::ParticleEmitter3D);
    inline constexpr ComponentMask decal_component_mask = ComponentMaskFromBit(SceneComponentBit::Decal);
    inline constexpr ComponentMask button_component_mask = ComponentMaskFromBit(SceneComponentBit::Button);
    inline constexpr ComponentMask canvas_2d_component_mask = ComponentMaskFromBit(SceneComponentBit::Canvas2D);
    inline constexpr ComponentMask rect_transform_2d_component_mask = ComponentMaskFromBit(SceneComponentBit::RectTransform2D);
    inline constexpr ComponentMask collision_layer_component_mask = ComponentMaskFromBit(SceneComponentBit::CollisionLayer);
    inline constexpr ComponentMask layout_component_mask = ComponentMaskFromBit(SceneComponentBit::Layout);
    inline constexpr ComponentMask animation_state_machine_component_mask = ComponentMaskFromBit(SceneComponentBit::AnimationStateMachine);
    inline constexpr ComponentMask joint_component_mask = ComponentMaskFromBit(SceneComponentBit::Joint);
    inline constexpr ComponentMask behavior_tree_component_mask = ComponentMaskFromBit(SceneComponentBit::BehaviorTree);

    template <typename Component>
    constexpr ComponentMask ComponentMaskFromType()
    {
        if constexpr (std::is_same_v<Component, TransformComponent>) { return transform_component_mask; }
        else if constexpr (std::is_same_v<Component, HierarchyComponent>) { return hierarchy_component_mask; }
        else if constexpr (std::is_same_v<Component, NameComponent>) { return name_component_mask; }
        else if constexpr (std::is_same_v<Component, GeometryComponent>) { return geometry_component_mask; }
        else if constexpr (std::is_same_v<Component, MaterialComponent>) { return material_component_mask; }
        else if constexpr (std::is_same_v<Component, Sprite2DComponent>) { return sprite_2d_component_mask; }
        else if constexpr (std::is_same_v<Component, Sprite3DComponent>) { return sprite_3d_component_mask; }
        else if constexpr (std::is_same_v<Component, Text2DComponent>) { return text_2d_component_mask; }
        else if constexpr (std::is_same_v<Component, Text3DComponent>) { return text_3d_component_mask; }
        else if constexpr (std::is_same_v<Component, CameraComponent>) { return camera_component_mask; }
        else if constexpr (std::is_same_v<Component, LightComponent>) { return light_component_mask; }
        else if constexpr (std::is_same_v<Component, EnvironmentComponent>) { return environment_component_mask; }
        else if constexpr (std::is_same_v<Component, FogVolumeComponent>) { return fog_volume_component_mask; }
        else if constexpr (std::is_same_v<Component, DDGIVolumeComponent>) { return ddgi_volume_component_mask; }
        else if constexpr (std::is_same_v<Component, ReflectionProbeComponent>) { return reflection_probe_component_mask; }
        else if constexpr (std::is_same_v<Component, AnimationComponent>) { return animation_component_mask; }
        else if constexpr (std::is_same_v<Component, AnimationStateMachineComponent>) { return animation_state_machine_component_mask; }
        else if constexpr (std::is_same_v<Component, ScriptComponent>) { return script_component_mask; }
        else if constexpr (std::is_same_v<Component, Collider3DComponent>) { return collider_3d_component_mask; }
        else if constexpr (std::is_same_v<Component, Rigidbody3DComponent>) { return rigidbody_3d_component_mask; }
        else if constexpr (std::is_same_v<Component, JointComponent>) { return joint_component_mask; }
        else if constexpr (std::is_same_v<Component, AudioSourceComponent>) { return audio_source_component_mask; }
        else if constexpr (std::is_same_v<Component, AudioListenerComponent>) { return audio_listener_component_mask; }
        else if constexpr (std::is_same_v<Component, VisibilityLayerComponent>) { return layer_component_mask; }
        else if constexpr (std::is_same_v<Component, CollisionLayerComponent>) { return collision_layer_component_mask; }
        else if constexpr (std::is_same_v<Component, TerrainComponent>) { return terrain_component_mask; }
        else if constexpr (std::is_same_v<Component, ParticleEmitter3DComponent>) { return particle_emitter_3d_component_mask; }
        else if constexpr (std::is_same_v<Component, DecalComponent>) { return decal_component_mask; }
        else if constexpr (std::is_same_v<Component, ButtonComponent>) { return button_component_mask; }
        else if constexpr (std::is_same_v<Component, Canvas2DComponent>) { return canvas_2d_component_mask; }
        else if constexpr (std::is_same_v<Component, RectTransform2DComponent>) { return rect_transform_2d_component_mask; }
        else if constexpr (std::is_same_v<Component, LayoutComponent>) { return layout_component_mask; }
        else if constexpr (std::is_same_v<Component, BehaviorTreeComponent>) { return behavior_tree_component_mask; }
        else { return none_component_mask; }
    }
}
