#pragma once

#include "RuntimeExport.h"
#include <Types.h>

namespace won
{
	// major features
	WONENGINE_API int GetMajor();
	// minor features, major bug fixes
	WONENGINE_API int GetMinor();
	// minor bug fixes, alterations
	WONENGINE_API int GetRevision();

	WONENGINE_API const char* GetVersionString();
}
