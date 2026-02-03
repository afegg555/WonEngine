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

set(RUNTIME_PUBLIC
    Source/Runtime/Public/1.cpp
)

set(RUNTIME_PRIVATE
    Source/Runtime/Private/2.cpp
)

set(Editor_PUBLIC
    Source/Editor/3.cpp
)

add_library(Runtime SHARED
    ${RUNTIME_PUBLIC}
    ${RUNTIME_PRIVATE}
)

add_executable(Editor
    ${Editor_PUBLIC}
)

target_include_directories(Runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Public>
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