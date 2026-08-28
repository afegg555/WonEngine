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
    struct AnimationStateMachineComponent;
    struct BehaviorTreeComponent;
    struct NavAgentComponent;
    struct SequenceComponent;
}

namespace won::script
{
    inline constexpr uint32 lua_script_builtin_function_count = static_cast<uint32>(ScriptCallType::Custom);

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
        static int LuaEntitySpawnPrefab(lua_State* state);
        static int LuaEntitySpawnPrefabChild(lua_State* state);
        static int LuaEntityPreloadPrefab(lua_State* state);
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
        static int LuaMaterialGetRoughness(lua_State* state);
        static int LuaMaterialSetRoughness(lua_State* state);
        static int LuaMaterialGetMetallic(lua_State* state);
        static int LuaMaterialSetMetallic(lua_State* state);
        static int LuaMaterialFork(lua_State* state);

        static int LuaText2DSetString(lua_State* state);
        static int LuaText2DGetString(lua_State* state);
        static int LuaText3DSetString(lua_State* state);
        static int LuaText3DGetString(lua_State* state);

        static int LuaEnvironmentAdd(lua_State* state);
        static int LuaEnvironmentHas(lua_State* state);
        static int LuaEnvironmentSetActive(lua_State* state);
        static int LuaEnvironmentSetSkyColors(lua_State* state);
        static int LuaEnvironmentSetAmbient(lua_State* state);
        static int LuaEnvironmentSetSunIntensity(lua_State* state);
        static int LuaEnvironmentSetSunDirection(lua_State* state);
        static int LuaEnvironmentSetSkyType(lua_State* state);
        static int LuaEnvironmentSetDiffuseGIMode(lua_State* state);
        static int LuaEnvironmentSetReflectionMode(lua_State* state);
        static int LuaEnvironmentSetDirectSunActive(lua_State* state);
        static int LuaEnvironmentSetAtmosphere(lua_State* state);
        static int LuaEnvironmentSetScatteringCoefficients(lua_State* state);
        static int LuaEnvironmentSetCloud(lua_State* state);
        static int LuaEnvironmentSetCloudColor(lua_State* state);
        static int LuaEnvironmentSetCloudMotion(lua_State* state);
        static int LuaEnvironmentSetWind(lua_State* state);

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
        static int LuaAnimationAddClipEvent(lua_State* state);
        static int LuaAnimationGetClipCount(lua_State* state);
        static int LuaAnimationGetCurrentClip(lua_State* state);
        static int LuaAnimationGetClipName(lua_State* state);
        static int LuaAnimationGetClipDuration(lua_State* state);
        static int LuaAnimationGetNormalizedTime(lua_State* state);
        static int LuaAnimationIsCurrentFinished(lua_State* state);
        static int LuaAnimationSetBool(lua_State* state);
        static int LuaAnimationSetFloat(lua_State* state);
        static int LuaAnimationSetTrigger(lua_State* state);
        static int LuaAnimationGetState(lua_State* state);
        static int LuaAnimationSMAdd(lua_State* state);
        static int LuaAnimationSMHas(lua_State* state);
        static int LuaAnimationSMAddParameter(lua_State* state);
        static int LuaAnimationSMAddState(lua_State* state);
        static int LuaAnimationSMAddTransition(lua_State* state);
        static int LuaAnimationSMAddCondition(lua_State* state);
        static int LuaAnimationSMSetExitTime(lua_State* state);
        static int LuaAnimationSMSetDefaultState(lua_State* state);
        static ecs::AnimationComponent* GetSelfAnimation(LuaScriptRuntime* runtime);
        static ecs::AnimationStateMachineComponent* GetSelfStateMachine(LuaScriptRuntime* runtime);

        static int LuaSequenceHas(lua_State* state);
        static int LuaSequenceAdd(lua_State* state);
        static int LuaSequenceAddTrack(lua_State* state);
        static int LuaSequencePlay(lua_State* state);
        static int LuaSequenceStop(lua_State* state);
        static int LuaSequenceSetTime(lua_State* state);
        static int LuaSequenceSetLoop(lua_State* state);
        static int LuaSequenceSetDuration(lua_State* state);
        static int LuaSequenceIsPlaying(lua_State* state);
        static ecs::SequenceComponent* GetSequence(LuaScriptRuntime* runtime, lua_State* state, int entity_arg);

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
        static int LuaPhysicsSphereCast(lua_State* state);
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
        static int LuaAudioSetMasterVolume(lua_State* state);
        static int LuaAudioGetMasterVolume(lua_State* state);
        static int LuaAudioSetSubmixVolume(lua_State* state);
        static int LuaAudioGetSubmixVolume(lua_State* state);

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
        static int LuaSceneLoadAdditive(lua_State* state);
        static int LuaSceneUnloadAdditive(lua_State* state);
        static int LuaSceneIsLoading(lua_State* state);

        static int LuaVehicleSetInput(lua_State* state);
        static int LuaVehicleSetGear(lua_State* state);
        static int LuaVehicleGetSpeed(lua_State* state);
        static int LuaVehicleGetRPM(lua_State* state);
        static int LuaVehicleGetGear(lua_State* state);
        static int LuaVehicleIsShiftingGear(lua_State* state);

        static int LuaNavFindPath(lua_State* state);
        static int LuaNavNearestPoint(lua_State* state);
        static int LuaNavIsReady(lua_State* state);

        static int LuaWaterAddRipple(lua_State* state);
        static int LuaWaterSampleHeight(lua_State* state);
        static int LuaWaterSampleSurface(lua_State* state);

        static int LuaAISetTree(lua_State* state);
        static int LuaAISet(lua_State* state);
        static int LuaAIGet(lua_State* state);
        static int LuaAIAddBehaviorTree(lua_State* state);
        static int LuaAIAddNavAgent(lua_State* state);
        static int LuaAIHasBehaviorTree(lua_State* state);
        static int LuaAIHasNavAgent(lua_State* state);
        static int LuaAIMoveTo(lua_State* state);
        static int LuaAIStop(lua_State* state);
        static int LuaAIGetMoveState(lua_State* state);
        static int LuaAISetMoveSpeed(lua_State* state);
        static ecs::NavAgentComponent* GetNavAgent(LuaScriptRuntime* runtime, ecs::Entity entity);
        static ecs::BehaviorTreeComponent* GetSelfBehaviorTree(LuaScriptRuntime* runtime);

        static int LuaSettingsGet(lua_State* state);
        static int LuaSettingsSet(lua_State* state);
        static int LuaSettingsSave(lua_State* state);
        static int LuaLocaleGetText(lua_State* state);
        static int LuaLocaleSetLanguage(lua_State* state);
        static int LuaLocaleGetLanguage(lua_State* state);
        static int LuaLocaleGetAvailableLanguages(lua_State* state);

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
        SceneManager* scene_manager = nullptr;
        String content_root;
        settings::UserSettings* user_settings = nullptr;
        const project::ProjectSettings* project_settings = nullptr;
        std::function<void()> apply_user_settings;
        std::function<bool()> save_user_settings;
    };
}
