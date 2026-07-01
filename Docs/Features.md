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
- [x] Scene serialization (save / load)
- [x] Transform component
- [x] Hierarchy component
- [x] Name component
- [x] Layer component
- [x] Geometry component
- [x] Material component
- [x] Camera component
- [x] Light component
- [x] Sky component
- [~] Fog volume component
- [x] Environment lighting component
- [x] DDGI volume component
- [x] Rigidbody component
- [x] Collider component
- [x] Audio source component
- [x] Audio listener component
- [x] Animation component
- [x] Particle emitter component
- [~] Decal component
- [x] Sprite 2D / 3D component
- [x] Text 2D / 3D component
- [x] Script component
- [~] Terrain component
- [x] Core update systems (transform, camera, geometry, material, light, environment, renderable, sprite, text)
- [ ] Scene TLAS for ray tracing
- [ ] Environment probes

---

# Physics

- [x] Jolt Physics integration
- [x] Rigidbody bodies (static / kinematic / dynamic)
- [x] Collider shapes (box, sphere)
- [x] Collision and trigger events
- [x] Collision layer masks
- [x] Locked-rotation / upright bodies (allowed DOFs)
- [x] Physics update system
- [ ] Joints / constraints
- [ ] Physics shape queries / raycast for gameplay

---

# Audio

- [x] XAudio2 driver
- [x] Audio source (2D and spatial 3D)
- [x] Looping and one-shot playback
- [x] Audio listener with distance attenuation
- [x] WAV loading (PCM / IEEE float)
- [x] Audio update system
- [ ] Compressed audio (mp3 / ogg)

---

# Animation

- [x] Skeletal animation import
- [x] Clip playback
- [x] Cross-fade blending between clips
- [x] Per-bone pose sampling and skinning matrices
- [x] Animation update system
- [ ] Blend trees
- [ ] Animation events / notifies

---

# Effects

- [x] CPU sprite particle emitter
- [x] Particle update system
- [~] Projected decals
- [x] Decal update system
- [~] Terrain (grid / heightmap)
- [~] Weather hook (sky / fog / rain)

---

# Scripting (Lua)

- [x] Lua script runtime with per-entity script instances
- [x] Lifecycle callbacks (OnCreate / OnUpdate / OnDestroy)
- [x] Physics trigger callbacks (OnTriggerEnter / Stay / Exit 3D)
- [x] Entity API (create / destroy / find by name / get-set name)
- [x] Transform API (position / rotation / scale / forward)
- [x] Rigidbody API (linear / angular velocity)
- [x] Audio source API (play / play_oneshot / stop)
- [x] Animation API (play by name / crossfade / speed / pause / resume)
- [x] Material API (base color / fork)
- [x] Particle emitter API (active / spawn rate)
- [x] Input API (actions, axes, gamepad)
- [x] Persistent game data (schema-typed float / int / bool / string)
- [x] Event publish / subscribe
- [ ] Script hot reload

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
- [x] Transparent material flag support (`SHADER_MATERIAL_FLAG_TRANSPARENT`)
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

- [x] Instance-data batching
- [ ] Meshlet pipeline
- [ ] Virtualized geometry research
- [ ] Voxelizer

## Image Quality

- [x] GPU texture mip generation
- [x] Anti-aliasing (FXAA)
- [ ] Tone mapping
- [ ] Bloom
- [ ] Ambient occlusion
- [ ] FSR
- [ ] DLSS
- [ ] Color grading

## Material Flags (v0.1 Limitations)

- Transparent draw ordering is back-to-front sorted per object (not per triangle); overlapping transparent meshes may show artifacts depending on camera angle.
- Double-sided (`SHADER_MATERIAL_FLAG_DOUBLE_SIDED`) disables backface culling at the rasterizer level; no normal flipping on back faces.
- Wireframe is a global editor/runtime debug toggle, not a per-material flag.

## Scene Rendering Features

- [ ] Impostors
- [ ] Trail rendering
- [ ] Lens flare
- [ ] Water
- [ ] Wetmap
- [ ] Water ripple
- [ ] Clouds
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
- [x] Animation import
- [x] Skeletal mesh / skinning
- [x] DDS texture support
- [x] Block compression support (BC1 / BC3)
- [~] Material asset files (`.wonmat`)
- [ ] Editor asset browser workflow

---

# Shader System

- [x] Runtime shader compilation with DXC
- [x] Runtime shader loading
- [x] Runtime shader reload
- [x] Offline shader compiler
- [x] Cooked shader binaries + manifest in packaged builds (runtime loads precompiled shaders)
- [x] Shared C++ / HLSL shader definitions
- [x] Object shaders
- [x] Sky shader
- [x] Primitive shaders
- [~] DDGI compute shader
- [~] GPU BVH compute shaders
- [x] Texture mip generation compute shader
- [x] Shader metadata dump command
- [ ] Shader dump (embed compiled shaders into the executable)
- [ ] Shader permutation system

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
- [x] Asset importer / cook tool (mesh + texture)
- [x] Texture block-compression (BC) on import
- [x] Package tool and packaged-build smoke test

---

# Platform Support

- [x] Windows
- [ ] Linux
- [ ] macOS
- [ ] Console-oriented platform support

---

# Planned Engine Systems

- [ ] Video
- [ ] Networking
- [ ] UI system (Canvas)
- [ ] Water system
- [ ] Vehicle system

---
