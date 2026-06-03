#include "BuiltinTypeReflection.h"

#include "BuiltinTypeMeta.h"
#include "Reflection.h"

namespace won::reflection
{
    void RegisterBuiltinTypes()
    {
        RegisterType(TypeMeta<bool>::Get());
        RegisterType(TypeMeta<int8>::Get());
        RegisterType(TypeMeta<uint8>::Get());
        RegisterType(TypeMeta<int16>::Get());
        RegisterType(TypeMeta<uint16>::Get());
        RegisterType(TypeMeta<int32>::Get());
        RegisterType(TypeMeta<uint32>::Get());
        RegisterType(TypeMeta<int64>::Get());
        RegisterType(TypeMeta<uint64>::Get());
        RegisterType(TypeMeta<float>::Get());
        RegisterType(TypeMeta<double>::Get());
        RegisterType(TypeMeta<void*>::Get());
        RegisterType(TypeMeta<String>::Get());

        RegisterType(TypeMeta<float2>::Get());
        RegisterType(TypeMeta<float3>::Get());
        RegisterType(TypeMeta<float4>::Get());
        RegisterType(TypeMeta<int2>::Get());
        RegisterType(TypeMeta<int3>::Get());
        RegisterType(TypeMeta<int4>::Get());
        RegisterType(TypeMeta<uint2>::Get());
        RegisterType(TypeMeta<uint3>::Get());
        RegisterType(TypeMeta<uint4>::Get());

        RegisterType(TypeMeta<ecs::LightComponent::LightType>::Get());
        RegisterType(TypeMeta<ecs::EnvironmentLightingComponent::GIMode>::Get());

        RegisterType(TypeMeta<ecs::NameComponent>::Get());
        RegisterType(TypeMeta<ecs::TransformComponent>::Get());
        RegisterType(TypeMeta<ecs::HierarchyComponent>::Get());
        RegisterType(TypeMeta<ecs::CameraComponent>::Get());
        RegisterType(TypeMeta<ecs::LightComponent>::Get());
        RegisterType(TypeMeta<ecs::SkyComponent>::Get());
        RegisterType(TypeMeta<ecs::FogVolumeComponent>::Get());
        RegisterType(TypeMeta<ecs::EnvironmentLightingComponent>::Get());
        RegisterType(TypeMeta<ecs::DDGIVolumeComponent>::Get());
        RegisterType(TypeMeta<ecs::GeometryComponent>::Get());
        RegisterType(TypeMeta<ecs::Sprite2DComponent>::Get());
        RegisterType(TypeMeta<ecs::Sprite3DComponent>::Get());
        RegisterType(TypeMeta<ecs::Text2DComponent>::Get());
        RegisterType(TypeMeta<ecs::Text3DComponent>::Get());
        RegisterType(TypeMeta<ecs::AnimationComponent>::Get());
        RegisterType(TypeMeta<ecs::MaterialSlot::TextureMap>::Get());
        RegisterType(TypeMeta<ecs::MaterialSlot>::Get());
        RegisterType(TypeMeta<ecs::MaterialComponent>::Get());
        RegisterType(TypeMeta<ecs::ScriptSlot>::Get());
        RegisterType(TypeMeta<ecs::ScriptComponent>::Get());
    }
}
