#include "BuiltinTypeReflection.h"

#include "Reflection.h"
#include "SceneComponents.h"

namespace won::reflection
{
    WON_REFLECT_SCALAR(bool, "bool", won::ValueType::Bool)

    WON_REFLECT_SCALAR(won::int8, "int8", won::ValueType::Int8)
    WON_REFLECT_SCALAR(won::uint8, "uint8", won::ValueType::UInt8)
    WON_REFLECT_SCALAR(won::int16, "int16", won::ValueType::Int16)
    WON_REFLECT_SCALAR(won::uint16, "uint16", won::ValueType::UInt16)
    WON_REFLECT_SCALAR(won::int32, "int32", won::ValueType::Int32)
    WON_REFLECT_SCALAR(won::uint32, "uint32", won::ValueType::UInt32)
    WON_REFLECT_SCALAR(won::int64, "int64", won::ValueType::Int64)
    WON_REFLECT_SCALAR(won::uint64, "uint64", won::ValueType::UInt64)
    WON_REFLECT_SCALAR(float, "float", won::ValueType::Float32)
    WON_REFLECT_SCALAR(double, "double", won::ValueType::Float64)
    WON_REFLECT_SCALAR(void*, "Pointer", won::ValueType::Pointer)
    WON_REFLECT_SCALAR(won::String, "String", won::ValueType::String)

    WON_REFLECT_BUILTIN_STRUCT(float2, "float2", won::ValueType::Float32x2)
        WON_REFLECT_FIELD(x, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(float3, "float3", won::ValueType::Float32x3)
        WON_REFLECT_FIELD(x, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(float4, "float4", won::ValueType::Float32x4)
        WON_REFLECT_FIELD(x, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(w, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(int2, "int2", won::ValueType::Int32x2)
        WON_REFLECT_FIELD(x, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(int3, "int3", won::ValueType::Int32x3)
        WON_REFLECT_FIELD(x, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(int4, "int4", won::ValueType::Int32x4)
        WON_REFLECT_FIELD(x, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(w, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(uint2, "uint2", won::ValueType::UInt32x2)
        WON_REFLECT_FIELD(x, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(uint3, "uint3", won::ValueType::UInt32x3)
        WON_REFLECT_FIELD(x, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_BUILTIN_STRUCT(uint4, "uint4", won::ValueType::UInt32x4)
        WON_REFLECT_FIELD(x, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(y, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(z, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(w, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()


    WON_REFLECT_ENUM(won::ecs::LightComponent::LightType, "LightType")
        WON_REFLECT_ENUM_VALUE("Directional", won::ecs::LightComponent::Directional)
        WON_REFLECT_ENUM_VALUE("Point", won::ecs::LightComponent::Point)
        WON_REFLECT_ENUM_VALUE("Spot", won::ecs::LightComponent::Spot)
    WON_REFLECT_ENUM_END()

    WON_REFLECT_ENUM(won::ecs::EnvironmentLightingComponent::GIMode, "GIMode")
        WON_REFLECT_ENUM_VALUE("None", won::ecs::EnvironmentLightingComponent::None)
        WON_REFLECT_ENUM_VALUE("Ambient", won::ecs::EnvironmentLightingComponent::Ambient)
        WON_REFLECT_ENUM_VALUE("DDGI", won::ecs::EnvironmentLightingComponent::DDGI)
    WON_REFLECT_ENUM_END()

    WON_REFLECT_STRUCT(won::ecs::NameComponent, "NameComponent")
        WON_REFLECT_FIELD(value, won::ValueType::String, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::TransformComponent, "TransformComponent")
        WON_REFLECT_FIELD(position, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(rotation, won::ValueType::Float32x4, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(scale, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::HierarchyComponent, "HierarchyComponent")
        WON_REFLECT_FIELD(parent_id, won::ValueType::UInt64, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::CameraComponent, "CameraComponent")
        WON_REFLECT_FIELD(near_plane, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(far_plane, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(aspect_ratio, won::ValueType::Float32, won::FieldFlagNone)
        WON_REFLECT_FIELD(fov_y, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ortho_vertical_size, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(aperture, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(shutter_speed, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sensitivity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::LightComponent, "LightComponent")
        WON_REFLECT_FIELD_TYPED(type, won::ecs::LightComponent::LightType, won::ValueType::Enum, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(range, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(outer_cone_angle, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(inner_cone_angle, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(shadow_map_resolution, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(shadow_cascade_count, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(shadow_cascade_lambda, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(shadow_cascade_blend, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::SkyComponent, "SkyComponent")
        WON_REFLECT_FIELD(sun_direction, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sun_intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sun_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sun_angular_radius, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sun_glow_intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sun_glow_falloff, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sky_horizon_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sky_intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sky_zenith_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(sky_horizon_falloff, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ground_horizon_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ground_intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ground_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ground_falloff, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::FogVolumeComponent, "FogVolumeComponent")
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::EnvironmentLightingComponent, "EnvironmentLightingComponent")
        WON_REFLECT_FIELD_TYPED(gi_mode, won::ecs::EnvironmentLightingComponent::GIMode, won::ValueType::Enum, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ambient_color, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(ambient_intensity, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(indirect_diffuse_scale, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(indirect_specular_scale, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::DDGIVolumeComponent, "DDGIVolumeComponent")
        WON_REFLECT_FIELD(probe_counts, won::ValueType::UInt32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(probe_spacing, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(volume_offset, won::ValueType::Float32x3, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(probes_per_frame, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(priority, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(hysteresis, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(normal_bias, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(view_bias, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(max_distance, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::GeometryComponent, "GeometryComponent")
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::Sprite2DComponent, "Sprite2DComponent")
        WON_REFLECT_FIELD(anchor, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(position, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(size, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pivot, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(uv_rect, won::ValueType::Float32x4, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(layer, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::Sprite3DComponent, "Sprite3DComponent")
        WON_REFLECT_FIELD(size, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pivot, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(uv_rect, won::ValueType::Float32x4, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::Text2DComponent, "Text2DComponent")
        WON_REFLECT_FIELD(text, won::ValueType::String, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(anchor, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(position, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pixel_height, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pivot, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(layer, won::ValueType::Int32, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::Text3DComponent, "Text3DComponent")
        WON_REFLECT_FIELD(text, won::ValueType::String, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pixel_height, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(height, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(pivot, won::ValueType::Float32x2, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::AnimationComponent, "AnimationComponent")
        WON_REFLECT_FIELD(current_clip_index, won::ValueType::UInt32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(time, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(speed, won::ValueType::Float32, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(loop, won::ValueType::Bool, won::FieldFlagEditable | won::FieldFlagSerializable)
        WON_REFLECT_FIELD(playing, won::ValueType::Bool, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::MaterialComponent, "MaterialComponent")
    WON_REFLECT_STRUCT_END()

    WON_REFLECT_STRUCT(won::ecs::ScriptComponent, "ScriptComponent")
        WON_REFLECT_FIELD(enabled, won::ValueType::Bool, won::FieldFlagEditable | won::FieldFlagSerializable)
    WON_REFLECT_STRUCT_END()

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
        RegisterType(TypeMeta<ecs::MaterialComponent>::Get());
        RegisterType(TypeMeta<ecs::ScriptComponent>::Get());
    }
}
