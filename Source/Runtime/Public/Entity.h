#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::ecs
{
	using Entity = uint64;
	inline static constexpr Entity INVALID_ENTITY = 0;
	
	WONENGINE_API Entity CreateEntity();
}
