#include "Version.h"
#include "VersionGenerated.h"

namespace won
{
	// main engine core
	constexpr int engine_major = 0;
	// minor features, major updates, breaking compatibility changes
	constexpr int engine_minor = 2;
	// minor bug fixes, alterations, refactors, updates
	constexpr int engine_revision = 0;

	const String build_id = WON_BUILD_ID;

	const String version_string = std::to_string(engine_major) + "." + std::to_string(engine_minor) + "." + std::to_string(engine_revision) + (build_id.empty() ? "" : "+" + build_id);

	int GetMajor()
	{
		return engine_major;
	}
	int GetMinor()
	{
		return engine_minor;
	}
	int GetRevision()
	{
		return engine_revision;
	}
	const char* GetBuildId()
	{
		return build_id.c_str();
	}
	const char* GetVersionString()
	{
		return version_string.c_str();
	}
}
