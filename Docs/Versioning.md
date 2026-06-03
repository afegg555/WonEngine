# Versioning

Engine release version and compatibility versions are separate.

## Current v0.1.0 Map

| Name | Current value | Owner | Purpose |
| --- | --- | --- | --- |
| EngineVersion | 0.1.0 | `Source/Runtime/Public/Version.h` | Product and release version. |
| PluginABIVersion | 3 | `Source/Plugins/Include/PluginABI.h` | Plugin DLL and host API compatibility. |
| SceneFormatVersion | 1 | `Source/Runtime/Public/SceneSerializer.h` | Scene save/load structure compatibility. |
| AssetFormatVersion | 1 | `Source/Runtime/Public/ResourceAsset.h` | Asset metadata and cooked asset structure compatibility. |
| ShaderCacheVersion | 1 | `Source/Runtime/Public/ShaderManifest.h` | Shader manifest/cache key compatibility. |

## Increment Rules

`PluginABIVersion`: plugin ABI, host API, or extension descriptor compatibility changes.

`SceneFormatVersion`: scene save/load format changes.

`AssetFormatVersion`: asset metadata, import output, or cooked asset format changes.

`ShaderCacheVersion`: shader manifest, cache key, or shader/PSO cache format changes.
