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

	// git short sha of the built commit, empty when git is unavailable
	WONENGINE_API const char* GetBuildId();

	WONENGINE_API const char* GetVersionString();
}
