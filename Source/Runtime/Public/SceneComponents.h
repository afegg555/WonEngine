#pragma once

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
#include "SkyComponent.h"
#include "FogVolumeComponent.h"
#include "EnvironmentLightingComponent.h"
#include "DDGIVolumeComponent.h"
#include "AnimationComponent.h"
#include "ScriptComponent.h"
#include "Collider3DComponent.h"

namespace won::ecs
{
    using ComponentMask = uint64;

    enum class SceneComponentBit : uint32
    {
        None = 0,
        Transform,
        Hierarchy,
        Name,
        Geometry,
        Material,
        Sprite3D,
        Text3D,
        Camera,
        Light,
        Sky,
        FogVolume,
        EnvironmentLighting,
        DDGIVolume,
        Animation,
        Sprite2D,
        Text2D,
        Script,
        Collider3D
    };

    constexpr ComponentMask ComponentMaskFromBit(SceneComponentBit bit)
    {
        return static_cast<ComponentMask>(1ull << static_cast<uint32>(bit));
    }

    inline constexpr ComponentMask none_component_mask = ComponentMaskFromBit(SceneComponentBit::None);
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
    inline constexpr ComponentMask sky_component_mask = ComponentMaskFromBit(SceneComponentBit::Sky);
    inline constexpr ComponentMask fog_volume_component_mask = ComponentMaskFromBit(SceneComponentBit::FogVolume);
    inline constexpr ComponentMask environment_lighting_component_mask = ComponentMaskFromBit(SceneComponentBit::EnvironmentLighting);
    inline constexpr ComponentMask ddgi_volume_component_mask = ComponentMaskFromBit(SceneComponentBit::DDGIVolume);
    inline constexpr ComponentMask animation_component_mask = ComponentMaskFromBit(SceneComponentBit::Animation);
    inline constexpr ComponentMask text_2d_component_mask = ComponentMaskFromBit(SceneComponentBit::Text2D);
    inline constexpr ComponentMask script_component_mask = ComponentMaskFromBit(SceneComponentBit::Script);
    inline constexpr ComponentMask collider_3d_component_mask = ComponentMaskFromBit(SceneComponentBit::Collider3D);
}
