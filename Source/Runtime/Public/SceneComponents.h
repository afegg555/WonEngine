#pragma once

#include "GeometryComponent.h"
#include "MaterialComponent.h"
#include "NameComponent.h"
#include "TransformComponent.h"
#include "HierarchyComponent.h"
#include "CameraComponent.h"

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
        Camera
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
    inline constexpr ComponentMask camera_component_mask = ComponentMaskFromBit(SceneComponentBit::Camera);
}
