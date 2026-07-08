#pragma once

#include "AudioMixer.h"
#include "EventHandler.h"
#include "ScriptRuntime.h"

struct lua_State;

namespace won::game
{
    class GameData;
}

namespace won::ecs
{
    struct AnimationComponent;
}

namespace won::script
{
    inline constexpr uint32 lua_script_builtin_function_count = static_cast<uint32>(ScriptCallType::OnClick) + 1u;

    struct LuaScriptModule
    {
        String script_path;
        int module_ref = 0;
        int function_refs[lua_script_builtin_function_count] = {};
        UnorderedMap<String, int> custom_function_refs;
        uint32 ref_count = 0;
    };

    struct LuaScriptInstance
    {
        String script_path;
        int self_ref = 0;
    };

    class LuaScriptRuntime : public ScriptRuntime
    {
    public:
        explicit LuaScriptRuntime(const ScriptRuntimeDesc& desc);

        bool Initialize() override;
        void Shutdown() override;

        bool CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error) override;
        void DestroyInstance(ScriptInstanceHandle handle) override;
        bool ReloadScript(const String& script_path, String& out_error) override;

        bool Call(ScriptInstanceHandle handle, const ScriptCallDesc& desc, String& out_error) override;

        void SetViewResolver(std::function<rendering::View*(float2)> resolver) override { view_resolver = resolver; }

    private:
        std::function<rendering::View*(float2)> view_resolver;

        static int LuaLogInfo(lua_State* state);
        static int LuaLogWarn(lua_State* state);
        static int LuaLogError(lua_State* state);

        static int LuaEntityCreate(lua_State* state);
        static int LuaEntityIsValid(lua_State* state);
        static int LuaEntityDestroy(lua_State* state);
        static int LuaEntityGetName(lua_State* state);
        static int LuaEntitySetName(lua_State* state);

        static int LuaTransformAdd(lua_State* state);
        static int LuaTransformHas(lua_State* state);
        static int LuaTransformGetPosition(lua_State* state);
        static int LuaTransformSetPosition(lua_State* state);
        static int LuaTransformTranslate(lua_State* state);
        static int LuaTransformGetScale(lua_State* state);
        static int LuaTransformSetScale(lua_State* state);
        static int LuaTransformRotateEuler(lua_State* state);
        static int LuaTransformGetForward(lua_State* state);
        static int LuaTransformGetRotation(lua_State* state);
        static int LuaTransformSetRotation(lua_State* state);
        static int LuaTransformSetEuler(lua_State* state);

        static int LuaMaterialAdd(lua_State* state);
        static int LuaMaterialHas(lua_State* state);
        static int LuaMaterialGetBaseColor(lua_State* state);
        static int LuaMaterialSetBaseColor(lua_State* state);
        static int LuaMaterialFork(lua_State* state);

        static int LuaEnvironmentAdd(lua_State* state);
        static int LuaEnvironmentHas(lua_State* state);
        static int LuaEnvironmentSetActive(lua_State* state);
        static int LuaEnvironmentSetSkyColors(lua_State* state);
        static int LuaEnvironmentSetAmbient(lua_State* state);
        static int LuaEnvironmentSetSunIntensity(lua_State* state);

        static int LuaParticleEmitter3DAdd(lua_State* state);
        static int LuaParticleEmitter3DHas(lua_State* state);
        static int LuaParticleEmitter3DSetActive(lua_State* state);
        static int LuaParticleEmitter3DSetSpawnRate(lua_State* state);

        static int LuaAnimationHas(lua_State* state);
        static int LuaAnimationAdd(lua_State* state);
        static int LuaAnimationPlay(lua_State* state);
        static int LuaAnimationPlayByName(lua_State* state);
        static int LuaAnimationCrossfade(lua_State* state);
        static int LuaAnimationSetSpeed(lua_State* state);
        static int LuaAnimationSetLoop(lua_State* state);
        static int LuaAnimationPause(lua_State* state);
        static int LuaAnimationResume(lua_State* state);
        static int LuaAnimationIsPlaying(lua_State* state);
        static int LuaAnimationGetClipCount(lua_State* state);
        static int LuaAnimationGetCurrentClip(lua_State* state);
        static int LuaAnimationGetClipName(lua_State* state);
        static int LuaAnimationGetClipDuration(lua_State* state);
        static int LuaAnimationGetNormalizedTime(lua_State* state);
        static int LuaAnimationIsCurrentFinished(lua_State* state);
        static ecs::AnimationComponent* GetSelfAnimation(LuaScriptRuntime* runtime);

        static int LuaColliderHas(lua_State* state);
        static int LuaColliderAdd(lua_State* state);
        static int LuaColliderIsEnabled(lua_State* state);
        static int LuaColliderSetEnabled(lua_State* state);
        static int LuaColliderIsTrigger(lua_State* state);
        static int LuaColliderSetTrigger(lua_State* state);

        static int LuaRigidbodyHas(lua_State* state);
        static int LuaRigidbodyAdd(lua_State* state);
        static int LuaRigidbodyGetVelocity(lua_State* state);
        static int LuaRigidbodySetVelocity(lua_State* state);
        static int LuaRigidbodyGetAngularVelocity(lua_State* state);
        static int LuaRigidbodySetAngularVelocity(lua_State* state);

        static int LuaPhysicsAddForce(lua_State* state);
        static int LuaPhysicsAddImpulse(lua_State* state);
        static int LuaPhysicsAddTorque(lua_State* state);
        static int LuaPhysicsRaycast(lua_State* state);
        static int LuaPhysicsOverlapSphere(lua_State* state);

        static int LuaInputMousePosition(lua_State* state);
        static int LuaInputMouseDelta(lua_State* state);
        static int LuaInputMouseWheel(lua_State* state);
        static int LuaInputMouseButton(lua_State* state);
        static int LuaCameraScreenToRay(lua_State* state);
        static int LuaCameraPick(lua_State* state);

        static int LuaAudioSourceHas(lua_State* state);
        static int LuaAudioSourceAdd(lua_State* state);
        static int LuaAudioSourcePlay(lua_State* state);
        static int LuaAudioSourceStop(lua_State* state);
        static int LuaAudioSourceIsPlaying(lua_State* state);
        static int LuaAudioSourceSetVolume(lua_State* state);
        static int LuaAudioPlayOneShot(lua_State* state);

        static int LuaAudioListenerHas(lua_State* state);
        static int LuaAudioListenerAdd(lua_State* state);
        static int LuaAudioListenerIsEnabled(lua_State* state);
        static int LuaAudioListenerSetEnabled(lua_State* state);

        static int LuaInputIsKeyDown(lua_State* state);
        static int LuaInputIsKeyPressed(lua_State* state);
        static int LuaInputIsActionDown(lua_State* state);
        static int LuaInputIsActionPressed(lua_State* state);
        static int LuaInputGetActionValue(lua_State* state);
        static int LuaInputGetActionAxis2D(lua_State* state);
        static int LuaInputGetGamepadAxis(lua_State* state);
        static int LuaInputIsGamepadConnected(lua_State* state);

        static int LuaSceneFindByName(lua_State* state);
        static int LuaSceneLoad(lua_State* state);

        static int LuaEventSubscribe(lua_State* state);
        static int LuaEventPost(lua_State* state);
        static int LuaEventFire(lua_State* state);

        static int LuaGameDataGetString(lua_State* state);
        static int LuaGameDataSetString(lua_State* state);
        static int LuaGameDataGetInt(lua_State* state);
        static int LuaGameDataSetInt(lua_State* state);
        static int LuaGameDataGetFloat(lua_State* state);
        static int LuaGameDataSetFloat(lua_State* state);
        static int LuaGameDataGetBool(lua_State* state);
        static int LuaGameDataSetBool(lua_State* state);

        void RegisterAPI();
        bool LoadModule(const String& script_path, LuaScriptModule& out_module, String& out_error);
        bool LoadFunctionRef(int module_ref, const char* function_name, int& out_function_ref, String& out_error);
        void UnloadModule(LuaScriptModule& module);

        lua_State* lua_state = nullptr;
        uint64 next_instance_id = 1;
        ScriptCallContext current_context = {};
        Vector<String> output_strings;
        UnorderedMap<String, LuaScriptModule> modules;
        UnorderedMap<uint64, LuaScriptInstance> instances;
        Vector<eventhandler::Handle> event_handles;
        game::GameData* game_data = nullptr;
        audio::AudioMixer* audio_mixer = nullptr;
        String content_root;
    };
}
