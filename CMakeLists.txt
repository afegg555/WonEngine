cmake_minimum_required(VERSION 3.20)

project(WonEngine LANGUAGES C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WONENGINE_BUILD_SHARED_RUNTIME "Build Runtime as a shared library" OFF)

if(WIN32)
    set(OS_NAME "Windows")
elseif(APPLE)
    set(OS_NAME "MacOS")
elseif(UNIX)
    set(OS_NAME "Linux")
endif()

set(FINAL_OUT_DIR "${CMAKE_SOURCE_DIR}/Binary/${OS_NAME}")
set(SHADER_BIN_DIR "${FINAL_OUT_DIR}/CompiledShaders")
set(CONTENTS_ROOT_DIR "${CMAKE_SOURCE_DIR}/Contents")
set(PROJECTS_ROOT_DIR "${CMAKE_SOURCE_DIR}/Projects" CACHE PATH "Default projects root directory")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(LUA_SOURCES
    Source/Vendor/lua/src/lapi.c
    Source/Vendor/lua/src/lauxlib.c
    Source/Vendor/lua/src/lbaselib.c
    Source/Vendor/lua/src/lcode.c
    Source/Vendor/lua/src/lcorolib.c
    Source/Vendor/lua/src/lctype.c
    Source/Vendor/lua/src/ldblib.c
    Source/Vendor/lua/src/ldebug.c
    Source/Vendor/lua/src/ldo.c
    Source/Vendor/lua/src/ldump.c
    Source/Vendor/lua/src/lfunc.c
    Source/Vendor/lua/src/lgc.c
    Source/Vendor/lua/src/linit.c
    Source/Vendor/lua/src/liolib.c
    Source/Vendor/lua/src/llex.c
    Source/Vendor/lua/src/lmathlib.c
    Source/Vendor/lua/src/lmem.c
    Source/Vendor/lua/src/loadlib.c
    Source/Vendor/lua/src/lobject.c
    Source/Vendor/lua/src/lopcodes.c
    Source/Vendor/lua/src/loslib.c
    Source/Vendor/lua/src/lparser.c
    Source/Vendor/lua/src/lstate.c
    Source/Vendor/lua/src/lstring.c
    Source/Vendor/lua/src/lstrlib.c
    Source/Vendor/lua/src/ltable.c
    Source/Vendor/lua/src/ltablib.c
    Source/Vendor/lua/src/ltm.c
    Source/Vendor/lua/src/lundump.c
    Source/Vendor/lua/src/lutf8lib.c
    Source/Vendor/lua/src/lvm.c
    Source/Vendor/lua/src/lzio.c
)

set(LUA_HEADERS
    Source/Vendor/lua/src/lapi.h
    Source/Vendor/lua/src/lauxlib.h
    Source/Vendor/lua/src/lcode.h
    Source/Vendor/lua/src/lctype.h
    Source/Vendor/lua/src/ldebug.h
    Source/Vendor/lua/src/ldo.h
    Source/Vendor/lua/src/lfunc.h
    Source/Vendor/lua/src/lgc.h
    Source/Vendor/lua/src/ljumptab.h
    Source/Vendor/lua/src/llex.h
    Source/Vendor/lua/src/llimits.h
    Source/Vendor/lua/src/lmem.h
    Source/Vendor/lua/src/lobject.h
    Source/Vendor/lua/src/lopcodes.h
    Source/Vendor/lua/src/lopnames.h
    Source/Vendor/lua/src/lparser.h
    Source/Vendor/lua/src/lprefix.h
    Source/Vendor/lua/src/lstate.h
    Source/Vendor/lua/src/lstring.h
    Source/Vendor/lua/src/ltable.h
    Source/Vendor/lua/src/ltm.h
    Source/Vendor/lua/src/lua.h
    Source/Vendor/lua/src/lua.hpp
    Source/Vendor/lua/src/luaconf.h
    Source/Vendor/lua/src/lualib.h
    Source/Vendor/lua/src/lundump.h
    Source/Vendor/lua/src/lvm.h
    Source/Vendor/lua/src/lzio.h
)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_DEBUG_POSTFIX "d")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")  # .dll, .exe
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")  # .so, .dylib, Cmake MODULE
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")  # .a, .lib

# for MSVC
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${FINAL_OUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${FINAL_OUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${FINAL_OUT_DIR}")
endforeach()

set(RUNTIME_CORE
    Source/Runtime/Public/Types.h
    Source/Runtime/Public/RuntimeExport.h
    Source/Runtime/Public/Platform.h
)

set(PLUGIN_PUBLIC
    Source/Plugins/Include/StableHash.h
    Source/Plugins/Include/ReflectionTypes.h
    Source/Plugins/Include/PluginABI.h
    Source/Plugins/Include/FunctionTypes.h
    Source/Plugins/Include/CustomFunctionExtension.h
    Source/Plugins/Include/CustomComponentExtension.h
    Source/Plugins/Include/CustomSystemExtension.h
)

set(RUNTIME_UTILS
    Source/Runtime/Public/Timer.h
    Source/Runtime/Public/SpinLock.h
    Source/Runtime/Public/Backlog.h
    Source/Runtime/Public/Profiler.h
    Source/Runtime/Public/RectPacker.h
    Source/Runtime/Private/Backlog.cpp
    Source/Runtime/Private/Profiler.cpp
    Source/Runtime/Public/StringUtils.h
    Source/Runtime/Private/StringUtils.cpp
)

set(RUNTIME_IO
    Source/Runtime/Public/FileSystem.h
    Source/Runtime/Public/Input.h
    Source/Runtime/Public/InputActionMap.h
    Source/Runtime/Private/FileSystem.cpp
    Source/Runtime/Private/Input.cpp
)

set(RUNTIME_RHI
    Source/Runtime/Public/RHIDevice.h
    Source/Runtime/Public/RHIContext.h
    Source/Runtime/Public/RHISwapchain.h
    Source/Runtime/Public/RHICommandList.h
    Source/Runtime/Public/RHICommandAllocator.h
    Source/Runtime/Public/RHIQueryHeap.h
    Source/Runtime/Public/RHIResource.h
    Source/Runtime/Public/RHIPipeline.h
    Source/Runtime/Public/RHISampler.h
    Source/Runtime/Public/RHIShader.h
    Source/Runtime/Public/RHIObject.h
    Source/Runtime/Private/RHIDevice.cpp
)

set(RUNTIME_RHI_DX12
    Source/Runtime/Private/RHIFormatDX12.h
    Source/Runtime/Private/RHIDeviceDX12.h
    Source/Runtime/Private/RHIDeviceDX12.cpp
    Source/Runtime/Private/RHIContextDX12.h
    Source/Runtime/Private/RHIContextDX12.cpp
    Source/Runtime/Private/RHIFenceDX12.h
    Source/Runtime/Private/RHIFenceDX12.cpp
    Source/Runtime/Private/RHICommandAllocatorDX12.h
    Source/Runtime/Private/RHICommandAllocatorDX12.cpp
    Source/Runtime/Private/RHICommandListDX12.h
    Source/Runtime/Private/RHICommandListDX12.cpp
    Source/Runtime/Private/RHIQueryHeapDX12.h
    Source/Runtime/Private/RHIQueryHeapDX12.cpp
    Source/Runtime/Private/RHIResourceDX12.h
    Source/Runtime/Private/RHIResourceDX12.cpp
    Source/Runtime/Private/DescriptorAllocatorDX12.h
    Source/Runtime/Private/DescriptorAllocatorDX12.cpp
    Source/Runtime/Private/RHISamplerDX12.h
    Source/Runtime/Private/RHISamplerDX12.cpp
    Source/Runtime/Private/RHIPipelineDX12.h
    Source/Runtime/Private/RHIPipelineDX12.cpp
    Source/Runtime/Private/RHISwapchainDX12.h
    Source/Runtime/Private/RHISwapchainDX12.cpp
)

set(RUNTIME_RESOURCE
    Source/Runtime/Public/Animation.h
    Source/Runtime/Public/Resource.h
    Source/Runtime/Public/ResourceExtension.h
    Source/Runtime/Public/ResourceAsset.h
    Source/Runtime/Public/Font.h
    Source/Runtime/Public/Image.h
    Source/Runtime/Public/Sound.h
    Source/Runtime/Public/Mesh.h
    Source/Runtime/Public/ShaderCompiler.h
    Source/Runtime/Public/ShaderManifest.h
    Source/Runtime/Public/ShaderLoader.h
    Source/Runtime/Private/ShaderLibrary.h
    Source/Runtime/Private/Font.cpp
    Source/Runtime/Private/Image.cpp
    Source/Runtime/Private/Sound.cpp
    Source/Runtime/Private/ResourceAsset.cpp
    Source/Runtime/Private/ShaderCompiler.cpp
    Source/Runtime/Private/ShaderManifest.cpp
    Source/Runtime/Private/ShaderLoader.cpp
    Source/Runtime/Private/ShaderLibrary.cpp
    Source/Runtime/Private/DXCShaderCompiler.h
    Source/Runtime/Private/DXCShaderCompiler.cpp
)

set(RUNTIME_RENDERING
    Source/Runtime/Public/Renderer.h
    Source/Runtime/Public/RenderingUtils.h
    Source/Runtime/Private/Renderer.cpp
    Source/Runtime/Private/RenderingUtils.cpp
    Source/Runtime/Public/View.h
    Source/Runtime/Public/ViewOptionEnums.h
    Source/Runtime/Private/View.cpp
)

set(RUNTIME_RENDERING_INTERNAL
    Source/Runtime/Private/RendererInternal.h
    Source/Runtime/Private/RendererInternal.cpp
)

set(RUNTIME_CONFIGURATION
    Source/Runtime/Public/KeyValueStore.h
    Source/Runtime/Public/Configuration.h
    Source/Runtime/Public/GameData.h
    Source/Runtime/Private/Configuration.cpp
    Source/Runtime/Private/GameData.cpp
    Source/Runtime/Public/ProjectSettings.h
)

set(RUNTIME_PLUGIN
    Source/Runtime/Public/Plugin.h
    Source/Runtime/Private/Plugin.cpp
)

set(RUNTIME_SCRIPT
    Source/Runtime/Public/ScriptRuntime.h
    Source/Runtime/Private/ScriptRuntime.cpp
)

set(RUNTIME_SCRIPT_LUA
    Source/Runtime/Private/LuaScriptRuntime.h
    Source/Runtime/Private/LuaScriptRuntime.cpp
)

set(RUNTIME_JOBSYSTEM
    Source/Runtime/Public/JobSystem.h
    Source/Runtime/Public/EventHandler.h
    Source/Runtime/Private/JobSystem.cpp
    Source/Runtime/Private/EventHandler.cpp
)

set(RUNTIME_SERIALIZATION
    Source/Runtime/Public/BinaryArchive.h
    Source/Runtime/Public/ArchiveMode.h
    Source/Runtime/Public/JsonArchive.h
    Source/Runtime/Public/SceneSerializer.h
    Source/Runtime/Private/JsonArchive.cpp
    Source/Runtime/Private/SceneSerializer.cpp
)

set(RUNTIME_REFLECTION
    Source/Runtime/Public/TypeMeta.h
    Source/Runtime/Public/Reflection.h
    Source/Runtime/Private/Reflection.cpp
    Source/Runtime/Public/BuiltinTypeMeta.h
    Source/Runtime/Public/BuiltinTypeReflection.h
    Source/Runtime/Private/BuiltinTypeReflection.cpp
)

set(RUNTIME_MATH
    Source/Runtime/Public/MathTypes.h
    Source/Runtime/Public/MathUtils.h
    Source/Runtime/Public/BVH.h
    Source/Runtime/Public/Primitives.h
)

set(RUNTIME_ECS
    Source/Runtime/Public/Entity.h
    Source/Runtime/Public/ComponentManager.h
    Source/Runtime/Public/Scene.h
    Source/Runtime/Public/System.h
    Source/Runtime/Public/PhysicsWorld.h
    Source/Runtime/Private/PhysicsWorld.cpp
)


set(RUNTIME_ECS_COMPONENTS
    Source/Runtime/Public/SceneComponents.h
    Source/Runtime/Public/NameComponent.h
    Source/Runtime/Public/GeometryComponent.h
    Source/Runtime/Public/MaterialComponent.h
    Source/Runtime/Public/Sprite2DComponent.h
    Source/Runtime/Public/Sprite3DComponent.h
    Source/Runtime/Public/Text2DComponent.h
    Source/Runtime/Public/Text3DComponent.h
    Source/Runtime/Public/TransformComponent.h
    Source/Runtime/Public/HierarchyComponent.h
    Source/Runtime/Public/CameraComponent.h
    Source/Runtime/Public/LightComponent.h
    Source/Runtime/Public/SkyComponent.h
    Source/Runtime/Public/FogVolumeComponent.h
    Source/Runtime/Public/EnvironmentLightingComponent.h
    Source/Runtime/Public/DDGIVolumeComponent.h
    Source/Runtime/Public/AnimationComponent.h
    Source/Runtime/Public/ScriptComponent.h
    Source/Runtime/Public/Collider3DComponent.h
    Source/Runtime/Public/Rigidbody3DComponent.h
    Source/Runtime/Public/AudioSourceComponent.h
    Source/Runtime/Public/AudioListenerComponent.h
    Source/Runtime/Public/LayerComponent.h
    Source/Runtime/Public/TerrainComponent.h
)

set(RUNTIME_AUDIO
    Source/Runtime/Public/AudioDriver.h
    Source/Runtime/Public/AudioMixer.h
    Source/Runtime/Private/AudioMixer.cpp
    Source/Runtime/Private/AudioDriver.cpp
)

set(RUNTIME_AUDIO_WINDOWS
    Source/Runtime/Private/AudioDriverXAudio2.h
    Source/Runtime/Private/AudioDriverXAudio2.cpp
)

set(RUNTIME_ECS_SYSTEMS
    Source/Runtime/Public/Systems.h
    Source/Runtime/Public/EnvironmentUpdateSystem.h
    Source/Runtime/Public/CameraUpdateSystem.h
    Source/Runtime/Public/GeometryUpdateSystem.h
    Source/Runtime/Public/LightUpdateSystem.h
    Source/Runtime/Public/MaterialUpdateSystem.h
    Source/Runtime/Public/AnimationUpdateSystem.h
    Source/Runtime/Public/TransformUpdateSystem.h
    Source/Runtime/Public/RenderableUpdateSystem.h
    Source/Runtime/Public/SpriteUpdateSystem.h
    Source/Runtime/Public/TextUpdateSystem.h
    Source/Runtime/Public/ScriptUpdateSystem.h
    Source/Runtime/Public/ScriptEventDispatchSystem.h
    Source/Runtime/Public/PhysicsUpdateSystem.h
    Source/Runtime/Public/AudioUpdateSystem.h
    Source/Runtime/Private/EnvironmentUpdateSystem.cpp
    Source/Runtime/Private/CameraUpdateSystem.cpp
    Source/Runtime/Private/GeometryUpdateSystem.cpp
    Source/Runtime/Private/LightUpdateSystem.cpp
    Source/Runtime/Private/MaterialUpdateSystem.cpp
    Source/Runtime/Private/AnimationUpdateSystem.cpp
    Source/Runtime/Private/TransformUpdateSystem.cpp
    Source/Runtime/Private/RenderableUpdateSystem.cpp
    Source/Runtime/Private/SpriteUpdateSystem.cpp
    Source/Runtime/Private/TextUpdateSystem.cpp
    Source/Runtime/Private/ScriptUpdateSystem.cpp
    Source/Runtime/Private/ScriptEventDispatchSystem.cpp
    Source/Runtime/Private/PhysicsUpdateSystem.cpp
    Source/Runtime/Private/AudioUpdateSystem.cpp
)


set(RUNTIME_WINDOW
    Source/Runtime/Public/Window.h
    Source/Runtime/Public/SplashWindow.h
    Source/Runtime/Private/Window.cpp
    Source/Runtime/Private/SplashWindow.cpp
)

set(RUNTIME_PLATFORM_WINDOWS
    Source/Runtime/Private/WindowWin32.h
    Source/Runtime/Private/WindowWin32.cpp
    Source/Runtime/Private/SplashWindowWin32.h
    Source/Runtime/Private/SplashWindowWin32.cpp
)

set(RUNTIME_APPLICATION
    Source/Runtime/Public/Application.h
    Source/Runtime/Private/Application.cpp
)

set(RUNTIME_ALLOCATOR
    Source/Runtime/Public/LinearAllocator.h
    Source/Runtime/Public/PoolAllocator.h
    Source/Runtime/Public/RingBuffer.h
)

set(RUNTIME_VERSION
    Source/Runtime/Public/Version.h
    Source/Runtime/Private/Version.cpp
)

set(SHADERS_INTEROP
    Source/Shaders/ShaderInterop.h
    Source/Shaders/ShaderInterop_BVH.h
    Source/Shaders/ShaderInterop_Renderer.h
    Source/Shaders/ShaderInterop_Sprite.h
    Source/Shaders/ShaderInterop_Utility.h
    Source/Shaders/ShaderInterop_PostProcess.h
)

set(SHADERS_HLSL
    Source/Shaders/Common.hlsli
    Source/Shaders/FullTriangleVS.hlsl
    Source/Shaders/SkyPS.hlsl
    Source/Shaders/FXAACS.hlsl
    Source/Shaders/TonemapCS.hlsl
    Source/Shaders/CompositePS.hlsl
    Source/Shaders/ShadingCommon.hlsli
    Source/Shaders/BRDFCommon.hlsli
    Source/Shaders/FogCommon.hlsli
    Source/Shaders/RayTraceCommon.hlsli
    Source/Shaders/DDGICommon.hlsli
    Source/Shaders/ObjectCommon.hlsli
    Source/Shaders/ObjectVS.hlsli
    Source/Shaders/ObjectVS_simple.hlsl
    Source/Shaders/ObjectVS_common.hlsl
    Source/Shaders/ObjectVS_prepass.hlsl
    Source/Shaders/SpriteCommon.hlsli
    Source/Shaders/Sprite2DVS.hlsl
    Source/Shaders/Sprite3DVS.hlsl
    Source/Shaders/ObjectPS.hlsli
    Source/Shaders/ObjectPS_unlit.hlsl
    Source/Shaders/ObjectPS_common.hlsl
    Source/Shaders/ObjectPS_prepass.hlsl
    Source/Shaders/SpritePS.hlsl
    Source/Shaders/TextPS.hlsl
    Source/Shaders/DDGIProbeUpdateCS.hlsl
    Source/Shaders/GPUBVHBuildCommon.hlsli
    Source/Shaders/GPUBVHGeneratePrimitivesCS.hlsl
    Source/Shaders/GPUBVHSortPrimitivesCS.hlsl
    Source/Shaders/GPUBVHBuildNodesCS.hlsl
    Source/Shaders/GPUBVHReduceBoundsCS.hlsl
    Source/Shaders/TextureMipGenCS.hlsl
    Source/Shaders/TextureBCCompressCommon.hlsli
    Source/Shaders/TextureBC1CompressCS.hlsl
    Source/Shaders/TextureBC3CompressCS.hlsl
    Source/Shaders/TextureBC4CompressCS.hlsl
    Source/Shaders/TextureBC5CompressCS.hlsl
    Source/Shaders/Compressonator/common_def.h
    Source/Shaders/Compressonator/bcn_common_api.h
    Source/Shaders/Compressonator/bcn_common_kernel.h
)

# Jolt Physics target integration (from source)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE) # Disable warnings-as-errors for Jolt
add_subdirectory(Source/Vendor/JoltPhysics/Build JoltPhysics)
set(BUILD_DX12 OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(Source/Vendor/DirectXTex DirectXTex)
set_target_properties(Jolt PROPERTIES FOLDER "Vendors")

set(WONENGINE_RUNTIME_LIBRARY_TYPE STATIC)
if(WONENGINE_BUILD_SHARED_RUNTIME)
    set(WONENGINE_RUNTIME_LIBRARY_TYPE SHARED)
endif()

add_library(Runtime ${WONENGINE_RUNTIME_LIBRARY_TYPE}
    ${PLUGIN_PUBLIC}
    ${RUNTIME_CORE}
    ${RUNTIME_UTILS}
    ${RUNTIME_IO}
    ${RUNTIME_RHI}
    ${RUNTIME_RHI_DX12}
    ${RUNTIME_RESOURCE}
    ${RUNTIME_RENDERING}
    ${RUNTIME_RENDERING_INTERNAL}
    ${RUNTIME_CONFIGURATION}
    ${RUNTIME_PLUGIN}
    ${RUNTIME_SCRIPT}
    ${RUNTIME_SCRIPT_LUA}
    ${LUA_SOURCES}
    ${LUA_HEADERS}
    ${RUNTIME_JOBSYSTEM}
    ${RUNTIME_SERIALIZATION}
    ${RUNTIME_REFLECTION}
    ${RUNTIME_MATH}
    ${RUNTIME_ECS}
    ${RUNTIME_ECS_COMPONENTS}
    ${RUNTIME_ECS_SYSTEMS}
    ${RUNTIME_AUDIO}
    ${RUNTIME_AUDIO_WINDOWS}
    ${RUNTIME_WINDOW}
    ${RUNTIME_PLATFORM_WINDOWS}
    ${RUNTIME_APPLICATION}
    ${RUNTIME_ALLOCATOR}
    ${RUNTIME_VERSION}
    ${SHADERS_INTEROP}
    ${SHADERS_HLSL}
)

source_group("Core" FILES ${RUNTIME_CORE})
source_group("Utils" FILES ${RUNTIME_UTILS})
source_group("IO" FILES ${RUNTIME_IO})
source_group("RHI" FILES ${RUNTIME_RHI})
source_group("RHI/DirectX12" FILES ${RUNTIME_RHI_DX12})
source_group("Resource" FILES ${RUNTIME_RESOURCE})
source_group("Rendering" FILES ${RUNTIME_RENDERING})
source_group("Application" FILES ${RUNTIME_APPLICATION})
source_group("Rendering/Internal" FILES ${RUNTIME_RENDERING_INTERNAL})
source_group("Configuration" FILES ${RUNTIME_CONFIGURATION})
source_group("Plugin" FILES ${RUNTIME_PLUGIN})
source_group("Plugin/Include" FILES ${PLUGIN_PUBLIC})
source_group("Script" FILES ${RUNTIME_SCRIPT})
source_group("Script/Lua" FILES ${RUNTIME_SCRIPT_LUA})
source_group("Script/Lua/src" FILES ${LUA_SOURCES} ${LUA_HEADERS})
source_group("JobSystem" FILES ${RUNTIME_JOBSYSTEM})
source_group("Serialization" FILES ${RUNTIME_SERIALIZATION})
source_group("Reflection" FILES ${RUNTIME_REFLECTION})
source_group("Math" FILES ${RUNTIME_MATH})
source_group("ECS" FILES ${RUNTIME_ECS})
source_group("ECS/Components" FILES ${RUNTIME_ECS_COMPONENTS})
source_group("ECS/Systems" FILES ${RUNTIME_ECS_SYSTEMS})
source_group("Audio" FILES ${RUNTIME_AUDIO})
source_group("Audio/Windows" FILES ${RUNTIME_AUDIO_WINDOWS})
source_group("Window" FILES ${RUNTIME_WINDOW})
source_group("Window/Windows" FILES ${RUNTIME_PLATFORM_WINDOWS})
source_group("Allocator" FILES ${RUNTIME_ALLOCATOR})
source_group("Version" FILES ${RUNTIME_VERSION})
source_group("ShaderInterop" FILES ${SHADERS_INTEROP})
source_group("Shaders" FILES ${SHADERS_HLSL})

set_source_files_properties(${SHADERS_HLSL} PROPERTIES HEADER_FILE_ONLY TRUE)

set(Editor_PUBLIC
    Source/Editor/main.cpp
    Source/Editor/Editor.h
    Source/Editor/EditorSettings.h
    Source/Editor/Editor.cpp
    Source/Editor/Themes.h
    Source/Editor/Themes.cpp
)

set(HELLO_TRIANGLE_SOURCES
    Source/Samples/HelloTriangle/main.cpp
)

set(PLAYER_SOURCES
    Source/Player/main.cpp
)

set(Editor_IMGUI
    Source/Vendor/imgui-docking/imgui.h
    Source/Vendor/imgui-docking/imgui.cpp
    Source/Vendor/imgui-docking/imgui_internal.h
    Source/Vendor/imgui-docking/imgui_demo.cpp
    Source/Vendor/imgui-docking/imgui_draw.cpp
    Source/Vendor/imgui-docking/imgui_tables.cpp
    Source/Vendor/imgui-docking/imgui_widgets.cpp
    Source/Vendor/imgui-docking/imgui_impl_win32.h
    Source/Vendor/imgui-docking/imgui_impl_win32.cpp
    Source/Vendor/imgui-docking/imgui_impl_dx12.h
    Source/Vendor/imgui-docking/imgui_impl_dx12.cpp
    Source/Vendor/imgui-docking/imstb_truetype.h
    Source/Vendor/imgui-docking/imstb_textedit.h
    Source/Vendor/imgui-docking/imstb_rectpack.h
)

set(SHADER_OFFLINE_COMPILER_SOURCES
    Source/Tools/ShaderOfflineCompiler/main.cpp
)

set(PACKAGE_TOOL_SOURCES
    Source/Tools/PackageTool/main.cpp
)

set(ASSET_IMPORTER_TOOL_SOURCES
    Source/Tools/AssetImporterTool/main.cpp
)

set(PLUGIN_PRINT_LOG_SAMPLE
    Source/Plugins/PrintLogSample/PrintLogSample.cpp
    Source/Plugins/PrintLogSample/plugin.json
)

set(PLUGIN_CUSTOM_COMPONENT_SAMPLE
    Source/Plugins/CustomComponentSample/CustomComponentSample.cpp
    Source/Plugins/CustomComponentSample/plugin.json
)

add_executable(Editor
    ${Editor_PUBLIC}
    ${Editor_IMGUI}
)

source_group("ImGui" FILES ${Editor_IMGUI})
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Editor)

add_executable(HelloTriangle
    ${HELLO_TRIANGLE_SOURCES}
)

add_executable(Player
    ${PLAYER_SOURCES}
)

add_executable(ShaderOfflineCompiler
    ${SHADER_OFFLINE_COMPILER_SOURCES}
)

add_executable(PackageTool
    ${PACKAGE_TOOL_SOURCES}
)

add_executable(AssetImporterTool
    ${ASSET_IMPORTER_TOOL_SOURCES}
)

add_library(PrintLogSample SHARED
    ${PLUGIN_PRINT_LOG_SAMPLE}
)

add_library(CustomComponentSample SHARED
    ${PLUGIN_CUSTOM_COMPONENT_SAMPLE}
)

foreach(PLUGIN_TARGET PrintLogSample CustomComponentSample)
    set(PLUGIN_OUTPUT_DIR "${FINAL_OUT_DIR}/Plugins/${PLUGIN_TARGET}")
    set_target_properties(${PLUGIN_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    )
    foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG} CONFIG_UPPER)
        set_target_properties(${PLUGIN_TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${PLUGIN_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${PLUGIN_OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${PLUGIN_OUTPUT_DIR}"
        )
    endforeach()
endforeach()

target_include_directories(Runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Public>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Vendor>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Vendor/lua/src>
)

target_link_libraries(Runtime PUBLIC Jolt)

target_link_libraries(Editor PRIVATE Runtime)
target_link_libraries(HelloTriangle PRIVATE Runtime)
target_link_libraries(Player PRIVATE Runtime)
target_link_libraries(ShaderOfflineCompiler PRIVATE Runtime)
target_link_libraries(PackageTool PRIVATE Runtime)

add_dependencies(Editor PackageTool ShaderOfflineCompiler AssetImporterTool)
target_include_directories(PrintLogSample PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins/Include>
)
target_include_directories(CustomComponentSample PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins/Include>
)
target_include_directories(AssetImporterTool PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Tools/AssetImporterTool/ThirdParty/assimp/include>
)
target_link_libraries(AssetImporterTool PRIVATE
    Runtime
    DirectXTex
    debug    assimp-vc143-mtd
    optimized assimp-vc143-mt
)
target_link_directories(AssetImporterTool PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Tools/AssetImporterTool/ThirdParty/assimp/lib>
)
target_compile_definitions(AssetImporterTool PRIVATE ASSIMP_DLL)
add_custom_command(TARGET AssetImporterTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${CMAKE_CURRENT_SOURCE_DIR}/Source/Tools/AssetImporterTool/ThirdParty/assimp/bin/assimp-vc143-mtd.dll,${CMAKE_CURRENT_SOURCE_DIR}/Source/Tools/AssetImporterTool/ThirdParty/assimp/bin/assimp-vc143-mt.dll>"
        "$<TARGET_FILE_DIR:AssetImporterTool>"
)

add_custom_command(TARGET PrintLogSample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins/PrintLogSample/plugin.json"
        "$<TARGET_FILE_DIR:PrintLogSample>/plugin.json"
)

add_custom_command(TARGET CustomComponentSample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugins/CustomComponentSample/plugin.json"
        "$<TARGET_FILE_DIR:CustomComponentSample>/plugin.json"
)

set_target_properties(ShaderOfflineCompiler PROPERTIES
    FOLDER "Tools"
)

set_target_properties(PackageTool PROPERTIES
    FOLDER "Tools"
)

set_target_properties(AssetImporterTool PROPERTIES
    FOLDER "Tools"
)

set_target_properties(DirectXTex PROPERTIES
    FOLDER "Vendors"
)

set_target_properties(HelloTriangle PROPERTIES
    FOLDER "Samples (Native)"
)

set_target_properties(Editor PROPERTIES
    WIN32_EXECUTABLE TRUE
)

set_target_properties(Player PROPERTIES
    FOLDER "Tools"
    WIN32_EXECUTABLE TRUE
)

set_target_properties(PrintLogSample PROPERTIES
    FOLDER "Plugins"
)

set_target_properties(CustomComponentSample PROPERTIES
    FOLDER "Plugins"
)

target_compile_definitions(Runtime
    PRIVATE
        WONENGINE_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders"
        WONENGINE_SHADER_BIN_DIR="${SHADER_BIN_DIR}"
)

if(WONENGINE_BUILD_SHARED_RUNTIME)
    target_compile_definitions(Runtime
        PUBLIC
            WONENGINE_SHARED_RUNTIME
        PRIVATE
            WONENGINE_EXPORTS
    )
endif()

target_compile_definitions(Editor
    PRIVATE
        CONTENTS_ROOT_DIR="${CONTENTS_ROOT_DIR}"
        PROJECTS_ROOT_DIR="${PROJECTS_ROOT_DIR}"
)

target_compile_definitions(HelloTriangle
    PRIVATE
        CONTENTS_ROOT_DIR="${CONTENTS_ROOT_DIR}"
)

target_compile_definitions(PackageTool
    PRIVATE
        ENGINE_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(Runtime PRIVATE /W4 /permissive-)
    target_link_options(Editor PRIVATE /ENTRY:mainCRTStartup)
    target_link_options(Player PRIVATE /ENTRY:mainCRTStartup)
    set_source_files_properties(${LUA_SOURCES} PROPERTIES COMPILE_OPTIONS "/W3")
    set_target_properties(Runtime PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

if(WIN32)
    target_link_libraries(Runtime PRIVATE d3d12 dxgi dxguid d3dcompiler dxcompiler dwmapi gdiplus ole32 shell32 uuid xaudio2)
    target_link_libraries(Editor PRIVATE d3d12 dxgi dxguid d3dcompiler dxcompiler)
    target_link_libraries(HelloTriangle PRIVATE d3d12 dxgi dxguid d3dcompiler dxcompiler)
    target_link_libraries(Player PRIVATE d3d12 dxgi dxguid d3dcompiler dxcompiler)
endif()
