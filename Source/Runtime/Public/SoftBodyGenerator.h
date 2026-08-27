#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "Mesh.h"
#include "SoftBodyComponent.h"

#include <memory>

namespace won::ecs
{
    WONENGINE_API std::shared_ptr<resource::Mesh> GenerateSoftBodyMesh(const SoftBodyComponent& soft_body);
}
