#pragma once
#include "EventHandler.h"
#include "GameData.h"
#include "Types.h"
#include "Renderer.h"
#include "SceneManager.h"
#include "Timer.h"
#include "Window.h"
#include "View.h"
#include "RHIDevice.h"
#include "ScriptRuntime.h"
#include "ProjectSettings.h"
#include "UserSettings.h"
#include "AudioDriver.h"
#include "AudioMixer.h"
#include "ConsoleOverlay.h"
#include "PerformanceOverlay.h"

#include <memory>

#pragma warning(push)
#pragma warning(disable: 4251)

namespace won::ecs { class Scene; }

namespace won
{
    struct ApplicationDesc
    {
        project::ProjectSettings project_settings = {};
        rendering::RHIDevicePreference device_preference = rendering::RHIDevicePreference::Default;
        uint32 jobsystem_thread_count = ~0u;
        bool defer_window_show = false;
        won::audio::AudioDriverDesc audio;
        Vector<String> command_line_args;
    };

    class WONENGINE_API Application
    {
    public:
        bool IsRunning() const;
        void Run();

        virtual void Initialize(const ApplicationDesc& desc);
        virtual void Shutdown();
        virtual void Update(float dt);
        virtual void Render();

        rendering::RHIDevice* GetDevice();
        script::ScriptRuntime* GetScriptRuntime();
        won::audio::AudioMixer* GetAudioMixer();
        game::GameData* GetGameData();
        SceneManager* GetSceneManager();
        void ShowMainWindow();
        void WaitIdle();
        void ClearViews();
        uint32 AddView(const rendering::View& view = {});
        void ApplyUserSettings();
        bool SaveUserSettings();

    protected:
        virtual void RenderScene();
        virtual void RenderUI();
        virtual void OnWindowResized(int width, int height);

        rendering::View& GetView(uint32 view_index = 0);
        const rendering::View& GetView(uint32 view_index = 0) const;
        void ProcessWindowResize();

        void ProcessSceneLifecycle();
        void RebindViewCameras(ecs::Scene& scene);
        void ApplyProjectSettings(const project::ProjectSettings& settings);

        bool is_running = false;
        std::shared_ptr<rendering::RHIDevice> device;
        std::shared_ptr<platform::Window> window;
        std::shared_ptr<rendering::Renderer> renderer;
        std::shared_ptr<script::ScriptRuntime> script_runtime;
        // audio_mixer must be declared before audio_driver so the driver (which calls into the
        // mixer from the audio thread) is destroyed first.
        std::unique_ptr<won::audio::AudioMixer> audio_mixer;
        std::unique_ptr<won::audio::IAudioDriver> audio_driver;
        Vector<std::unique_ptr<rendering::View>> views;
        project::ProjectSettings project_settings;
        settings::UserSettings user_settings;
        game::GameData game_data;
        utils::Timer frame_timer;
        uint64 update_index = 0;
        bool is_first_frame = true;
        bool simulation_paused = false;
        std::unique_ptr<SceneManager> scene_manager;
        console::ConsoleOverlay console_overlay;
        stats::PerformanceOverlay performance_overlay;
        bool developer_console_enabled = false;
    };
}

#pragma warning(pop)
