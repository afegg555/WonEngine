cmake_minimum_required(VERSION 3.20)

project(WonEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(OS_NAME "Windows")
elseif(APPLE)
    set(OS_NAME "MacOS")
elseif(UNIX)
    set(OS_NAME "Linux")
endif()

set(FINAL_OUT_DIR "${CMAKE_SOURCE_DIR}/Binary/${OS_NAME}")

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_DEBUG_POSTFIX "d")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FINAL_OUT_DIR}")

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
)

set(RUNTIME_UTILS
    Source/Runtime/Public/Timer.h
    Source/Runtime/Public/SpinLock.h
    Source/Runtime/Public/Backlog.h
    Source/Runtime/Public/Profiler.h
    Source/Runtime/Private/Backlog.cpp
    Source/Runtime/Private/Profiler.cpp
    Source/Runtime/Public/StringUtils.h
    Source/Runtime/Private/StringUtils.cpp
)

set(RUNTIME_IO
    Source/Runtime/Public/FileSystem.h
    Source/Runtime/Public/Input.h
    Source/Runtime/Public/PathUtils.h
    Source/Runtime/Private/FileSystem.cpp
    Source/Runtime/Private/Input.cpp
    Source/Runtime/Private/PathUtils.cpp
)

set(RUNTIME_JOBSYSTEM
    Source/Runtime/Public/JobSystem.h
    Source/Runtime/Public/EventHandler.h
    Source/Runtime/Private/JobSystem.cpp
    Source/Runtime/Private/EventHandler.cpp
)

set(RUNTIME_MATH
    Source/Runtime/Public/MathTypes.h
    Source/Runtime/Public/MathUtils.h
)

set(RUNTIME_ECS
    Source/Runtime/Public/Entity.h
    Source/Runtime/Private/Entity.cpp
)

set(RUNTIME_PLATFORM
    Source/Runtime/Public/Platform.h
    Source/Runtime/Private/2.cpp
)

set(RUNTIME_ALLOCATOR
    Source/Runtime/Public/Allocator.h
    Source/Runtime/Public/LinearAllocator.h
    Source/Runtime/Public/RingBuffer.h
)

set(RUNTIME_VERSION
    Source/Runtime/Public/Version.h
    Source/Runtime/Private/Version.cpp
)

set(SHADERS_INTEROP
    Source/Shaders/ShaderInterop.h
)

add_library(Runtime SHARED
    ${RUNTIME_CORE}
    ${RUNTIME_UTILS}
    ${RUNTIME_IO}
    ${RUNTIME_JOBSYSTEM}
    ${RUNTIME_MATH}
    ${RUNTIME_ECS}
    ${RUNTIME_PLATFORM}
    ${RUNTIME_ALLOCATOR}
    ${RUNTIME_VERSION}
    ${SHADERS_INTEROP}
)

source_group("Core" FILES ${RUNTIME_CORE})
source_group("Utils" FILES ${RUNTIME_UTILS})
source_group("IO" FILES ${RUNTIME_IO})
source_group("JobSystem" FILES ${RUNTIME_JOBSYSTEM})
source_group("Math" FILES ${RUNTIME_MATH})
source_group("ECS" FILES ${RUNTIME_ECS})
source_group("Platform" FILES ${RUNTIME_PLATFORM})
source_group("Allocator" FILES ${RUNTIME_ALLOCATOR})
source_group("Version" FILES ${RUNTIME_VERSION})
source_group("ShaderInterop" FILES ${SHADERS_INTEROP})

set(Editor_PUBLIC
    Source/Editor/3.cpp
)

add_executable(Editor
    ${Editor_PUBLIC}
)

target_include_directories(Runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Public>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Vendor>
)

target_link_libraries(Editor PRIVATE Runtime)

target_compile_definitions(Runtime
    PRIVATE
        WONENGINE_EXPORTS
)

if(MSVC)
    target_compile_options(Runtime PRIVATE /W4 /permissive-)
    set_target_properties(Runtime PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()