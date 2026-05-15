#include "PluginSample.h"
#include "Backlog.h"

namespace won::plugin
{
    class PluginSample : public Plugin
    {
    public:
        virtual const char* GetName() const override { return WON_IID_PLUGIN_SAMPLE; }
        virtual const char* GetVersion() const override { return WON_VID_PLUGIN_SAMPLE; }

        virtual void* QueryInterface(const char* iid, const char* version_id) const override
        {
            if (std::strcmp(iid, WON_IID_PLUGIN_SAMPLE) == 0 && std::strcmp(version_id, WON_VID_PLUGIN_SAMPLE) == 0)
                return (void*)&s_api;
            return nullptr;
        }
        virtual bool Initialize() override
        {
            // add something to initialize on LoadPlugin
            return true;
        }
        virtual void Shutdown() override
        {
            // add something to shutdown on UnloadPlugin
        }

        bool PrintSample(const char* input)
        {
            backlog::Post(input);

            return true;
        }
    private:
        static bool PrintSampleThunk(Plugin* self, const char* input)
        {
            return static_cast<PluginSample*>(self)->PrintSample(input);
        }

        inline static PluginSampleAPI s_api{
            &PrintSampleThunk
        };
    };
    IMPLEMENT_PLUGIN(PluginSample, PPluginSample);
}