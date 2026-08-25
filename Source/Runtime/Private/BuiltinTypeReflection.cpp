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
        RegisterType(TypeMeta<ecs::EnvironmentComponent::DiffuseGIMode>::Get());
        RegisterType(TypeMeta<ecs::EnvironmentComponent::ReflectionMode>::Get());
        RegisterType(TypeMeta<ecs::EnvironmentComponent::SkyType>::Get());
        RegisterType(TypeMeta<resource::MaterialType>::Get());
        RegisterType(TypeMeta<resource::MaterialBlendMode>::Get());
        RegisterType(TypeMeta<ecs::Collider3DComponent::ShapeType>::Get());
        RegisterType(TypeMeta<ecs::UIScaleMode>::Get());
        RegisterType(TypeMeta<ecs::Canvas2DComponent::RenderMode>::Get());
        RegisterType(TypeMeta<ecs::Rigidbody3DComponent::MotionType>::Get());
        RegisterType(TypeMeta<ecs::JointComponent::JointType>::Get());
        RegisterType(TypeMeta<ecs::AnimationParameter::Type>::Get());
        RegisterType(TypeMeta<ecs::TransitionCondition::Op>::Get());
        RegisterType(TypeMeta<ecs::SequenceTrackType>::Get());
        RegisterType(TypeMeta<ecs::LayoutComponent::Type>::Get());
        RegisterType(TypeMeta<ecs::LayoutComponent::CrossAlign>::Get());
        RegisterType(TypeMeta<rendering::AntiAliasingMode>::Get());
        RegisterType(TypeMeta<rendering::TonemapMode>::Get());
        RegisterType(TypeMeta<rendering::ViewMode>::Get());

        RegisterType(TypeMeta<ecs::NameComponent>::Get());
        RegisterType(TypeMeta<ecs::TransformComponent>::Get());
        RegisterType(TypeMeta<ecs::HierarchyComponent>::Get());
        RegisterType(TypeMeta<ecs::CameraComponent>::Get());
        RegisterType(TypeMeta<ecs::LightComponent>::Get());
        RegisterType(TypeMeta<ecs::EnvironmentComponent>::Get());
        RegisterType(TypeMeta<ecs::FogVolumeComponent>::Get());
        RegisterType(TypeMeta<ecs::DDGIVolumeComponent>::Get());
        RegisterType(TypeMeta<ecs::ReflectionProbeComponent>::Get());
        RegisterType(TypeMeta<ecs::WaterBodyComponent::Type>::Get());
        RegisterType(TypeMeta<ecs::WaterBodyComponent>::Get());
        RegisterType(TypeMeta<ecs::WaterZoneComponent>::Get());
        RegisterType(TypeMeta<ecs::GeometryComponent>::Get());
        RegisterType(TypeMeta<ecs::Sprite2DComponent>::Get());
        RegisterType(TypeMeta<ecs::Canvas2DComponent>::Get());
        RegisterType(TypeMeta<ecs::RectTransform2DComponent>::Get());
        RegisterType(TypeMeta<ecs::ButtonComponent>::Get());
        RegisterType(TypeMeta<ecs::LayoutComponent>::Get());
        RegisterType(TypeMeta<ecs::Sprite3DComponent>::Get());
        RegisterType(TypeMeta<ecs::Text2DComponent>::Get());
        RegisterType(TypeMeta<ecs::Text3DComponent>::Get());
        RegisterType(TypeMeta<ecs::AnimationComponent>::Get());
        RegisterType(TypeMeta<ecs::AnimationState>::Get());
        RegisterType(TypeMeta<ecs::AnimationParameter>::Get());
        RegisterType(TypeMeta<ecs::TransitionCondition>::Get());
        RegisterType(TypeMeta<ecs::AnimationTransition>::Get());
        RegisterType(TypeMeta<ecs::AnimationStateMachineComponent>::Get());
        RegisterType(TypeMeta<ecs::Collider3DComponent>::Get());
        RegisterType(TypeMeta<ecs::Rigidbody3DComponent>::Get());
        RegisterType(TypeMeta<ecs::JointComponent>::Get());
        RegisterType(TypeMeta<ecs::BehaviorTreeComponent>::Get());
        RegisterType(TypeMeta<ecs::NavAgentComponent>::Get());
        RegisterType(TypeMeta<ecs::SequenceKey>::Get());
        RegisterType(TypeMeta<ecs::SequenceTrack>::Get());
        RegisterType(TypeMeta<ecs::SequenceComponent>::Get());
        RegisterType(TypeMeta<ecs::AudioSourceComponent>::Get());
        RegisterType(TypeMeta<ecs::AudioListenerComponent>::Get());
        RegisterType(TypeMeta<ecs::VisibilityLayerComponent>::Get());
        RegisterType(TypeMeta<ecs::CollisionLayerComponent>::Get());
        RegisterType(TypeMeta<ecs::TerrainComponent>::Get());
        RegisterType(TypeMeta<ecs::NavMeshComponent>::Get());
        RegisterType(TypeMeta<ecs::ParticleEmitter3DComponent>::Get());
        RegisterType(TypeMeta<ecs::DecalComponent>::Get());
        RegisterType(TypeMeta<resource::MaterialSlot::TextureMap>::Get());
        RegisterType(TypeMeta<resource::MaterialSlot>::Get());
        RegisterType(TypeMeta<ecs::MaterialComponent>::Get());
        RegisterType(TypeMeta<ecs::ScriptSlot>::Get());
        RegisterType(TypeMeta<ecs::ScriptComponent>::Get());
    }
}
