#include "Version.h"

namespace won
{
	// main engine core
	constexpr int major = 0;
	// minor features, major updates, breaking compatibility changes
	constexpr int minor = 0;
	// minor bug fixes, alterations, refactors, updates
	constexpr int revision = 0;

	const String version_string = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision);

	int GetMajor()
	{
		return major;
	}
	int GetMinor()
	{
		return minor;
	}
	int GetRevision()
	{
		return revision;
	}
	const char* GetVersionString()
	{
		return version_string.c_str();
	}
}

