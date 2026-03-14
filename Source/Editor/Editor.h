#include "Application.h"

namespace won::editor
{
	class EditorApplication : public Application
	{
	public:
		void Initialize(const ApplicationDesc& desc) override;
		void Shutdown() override;
		void Update(float dt) override;

	private:
		void RenderUI() override;
		void ImGui_Impl_CreateDeviceObjects();
	};
}
