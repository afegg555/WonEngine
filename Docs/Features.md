# Features

This document tracks user-facing and developer-facing features available in WonEngine.

> WonEngine is still in an early prototype stage.  
> Feature status describes the current source tree, not production readiness.

## Status Legend

- `[x]` Implemented
- `[~]` In progress / experimental
- `[ ]` Planned

---

# Editor

- [x] Dockable editor interface
- [x] Default editor layout
- [x] Viewport panel
- [x] Viewport object picking
- [x] Editor camera movement
- [x] Entity list
- [x] Entity creation
- [x] Entity deletion
- [x] Inspector panel
- [x] Component add / remove workflow
- [x] Transform editing
- [x] Camera editing
- [x] Light editing
- [x] Sky and environment lighting editing
- [x] DDGI volume editing
- [x] Geometry component inspection
- [x] Material slot editing
- [x] Log panel
- [x] Profiler panel
- [x] VSync toggle
- [~] Contents Browser panel
- [~] BVH debug visualization
- [~] DDGI debug overlay
- [~] CameraController editor integration
- [ ] Full asset browser workflow
- [ ] Editor-driven asset import
- [ ] Material editor workflow
- [ ] Scene hierarchy tree
- [ ] Scene save / load
- [ ] Project save / load
- [ ] Undo / redo
- [x] Editor grid
- [ ] Splash screen

---

# Runtime Application

- [x] C++ application framework
- [x] Window creation
- [x] Main loop
- [x] Window resize handling
- [x] Input handling
- [x] Runtime logging
- [x] Runtime profiling
- [x] File system utilities
- [x] Event dispatch
- [x] Job system
- [x] Configuration storage
- [x] Linear allocator
- [x] Binary archive
- [x] Basic serialization helpers
- [ ] Project save / load

---

# Scene And ECS

- [x] Entity creation and destruction
- [x] Component-based scene model
- [x] Custom scene systems
- [x] Dependency-aware system scheduling
- [x] Transform component
- [x] Hierarchy component
- [x] Name component
- [x] Geometry component
- [x] Material component
- [x] Camera component
- [x] Light component
- [x] Sky component
- [~] Fog volume component
- [x] Environment lighting component
- [x] DDGI volume component
- [x] Transform update system
- [x] Camera update system
- [x] Geometry update system
- [x] Material update system
- [x] Light update system
- [x] Environment update system
- [x] Renderable update system
- [ ] Scene serialization
- [ ] Scene TLAS for ray tracing
- [ ] Collision system
- [ ] Physics integration
- [ ] Force / interaction system
- [ ] Environment probes

---

# Rendering

## Renderer

- [x] DirectX 12 rendering backend
- [x] Forward renderer
- [x] Depth-only prepass
- [x] Sky rendering
- [x] Mesh rendering
- [x] Submesh rendering
- [x] Material slot rendering
- [~] Transparent material flag support
- [x] Line primitive rendering
- [x] Point primitive rendering
- [x] Runtime shader reload
- [ ] Vulkan rendering backend
- [ ] Deferred renderer
- [ ] Visibility buffer
- [ ] Post-processing chain

## Lighting And Shadows

- [x] Directional lights
- [x] Point lights
- [x] Spot lights
- [x] Ambient lighting
- [x] Procedural sky lighting controls
- [x] Environment lighting component
- [x] Directional shadow maps
- [x] Cascaded shadow map settings
- [ ] Area lights
- [ ] Shadow frustum fitting for CSM
- [ ] Static sky map
- [ ] Dynamic sky map
- [ ] Physical sky model
- [ ] Environment map lighting
- [ ] Environment map to irradiance map

## Global Illumination

- [~] DDGI volume component
- [~] DDGI probe update
- [~] DDGI irradiance and visibility textures
- [~] DDGI history resources
- [~] DDGI scene tracing integration
- [~] DDGI debug overlay
- [ ] SSGI
- [ ] VXGI
- [ ] Voxel GI

## Reflections

- [ ] Screen-space reflections
- [ ] Environment reflections
- [ ] Reflection probes

## Ray Queries And Ray Tracing

- [x] CPU BVH
- [x] Mesh local BVH
- [x] Scene BVH
- [x] Scene ray casting
- [x] Viewport ray casting
- [~] GPU BVH generation
- [ ] Hardware ray tracing
- [ ] Path tracer

## Geometry And Visibility

- [ ] Instancing
- [ ] Meshlet pipeline
- [ ] Virtualized geometry research
- [ ] Voxelizer

## Image Quality

- [x] GPU texture mip generation
- [ ] Tone mapping
- [ ] Bloom
- [ ] Anti-aliasing
- [ ] Ambient occlusion
- [ ] FSR
- [ ] DLSS
- [ ] Color grading

## Scene Rendering Features

- [ ] Decals
- [ ] Impostors
- [ ] Trail rendering
- [ ] Lens flare
- [ ] Particle rendering
- [ ] Terrain
- [ ] Water
- [ ] Wetmap
- [ ] Water ripple
- [ ] Clouds
- [ ] Weather
- [ ] Vehicle rendering support

---

# Resources And Assets

- [x] Mesh resource
- [x] Image resource
- [x] Image loading
- [x] Image cache
- [x] Procedural mesh creation from C++
- [x] GPU upload for mesh render data
- [x] Assimp-based asset import plugin
- [x] OBJ import
- [x] glTF import
- [x] Imported material parameters
- [x] Imported material textures
- [ ] Editor asset browser workflow
- [ ] DDS texture support
- [ ] Animation import
- [ ] Skeletal mesh / skinning
- [ ] Material asset files
- [ ] Block compression support

---

# Shader System

- [x] Runtime shader compilation with DXC
- [x] Runtime shader loading
- [x] Runtime shader reload
- [x] Offline shader compiler
- [x] Shared C++ / HLSL shader definitions
- [x] Object shaders
- [x] Sky shader
- [x] Primitive shaders
- [~] DDGI compute shader
- [~] GPU BVH compute shaders
- [x] Texture mip generation compute shader
- [ ] Shader permutation system
- [ ] Shader dump tool

---

# Plugins

- [x] Runtime plugin interface
- [x] Plugin loading
- [x] Plugin unloading
- [x] AssetImporter plugin
- [~] CameraController plugin
- [x] PluginSample
- [ ] Plugin hot reload
- [ ] Plugin dependency metadata

---

# Tools And Build

- [x] ShaderOfflineCompiler
- [ ] Asset processing tools
- [ ] Texture compression tools

---

# Platform Support

- [x] Windows
- [ ] Linux
- [ ] macOS
- [ ] Console-oriented platform support

---

# Planned Engine Systems

- [ ] Animation
- [ ] Physics
- [ ] Audio
- [ ] Video
- [ ] Networking
- [ ] Scripting
- [ ] UI system
- [ ] Particle system
- [ ] Terrain system
- [ ] Water system
- [ ] Vehicle system

---
