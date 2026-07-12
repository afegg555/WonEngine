#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::ecs
{
    class Scene;
    struct SceneDesc;
}

namespace won
{
    class WONENGINE_API SceneManager
    {
    public:
        ~SceneManager();

        ecs::Scene& CreateScene(const ecs::SceneDesc& desc);
        void DestroyScene(ecs::Scene* scene);
        const Vector<std::unique_ptr<ecs::Scene>>& GetScenes() const;

    private:
        Vector<std::unique_ptr<ecs::Scene>> scenes;
    };
}
