# Release Notes

What changed in each release. For the current (living) feature status see [Features.md](Features.md); for version/format compatibility see [Versioning.md](Versioning.md).

---

## v0.1.0 — Shippable Foundation

Windows / DirectX 12. First shippable release: a packaged, playable sample game on top of a working build pipeline, ECS runtime, physics, audio, Lua scripting, and a renderer baseline.

### Added

**Build & packaging**
- Build tool with build profiles; standalone game target and a sample-game template.
- Package / cook / stage pipeline, with a cook manifest derived from scene dependencies.
- Project settings, and scene save/load hardened for packaged builds.

**Runtime**
- Splash screen and startup flow.
- Runtime save / profile / settings (JSON).
- Startup/context logging — each run's log opens with engine version, project, startup scene, and content root.

**Physics & collision**
- Jolt physics integration; collision primitives; layer components with mask filtering.

**Audio**
- XAudio2 audio source and listener (2D and spatial 3D).

**Input**
- Input action maps with keyboard/mouse and gamepad support.

**Scripting (Lua)**
- Gameplay API baseline: entity lifecycle, common-component bridge, and audio API.

**Animation & scenes**
- Skeletal animation import and playback (with cross-fade blending).
- Scene transitions.

**Rendering**
- Forward renderer with depth prepass; instance-data batching, frustum culling, and FXAA anti-aliasing.
- Lighting: directional / point / spot lights, ambient and procedural sky, environment lighting, and cascaded shadow maps.
- Global illumination via DDGI *(experimental)*.
- Material flags: transparent, double-sided, wireframe.
- Font, sprite, and text (2D/3D) rendering.
- Editor viewport picking.

**Effects**
- CPU sprite particle emitter.
- Projected decals *(prototype)*.
- Grid / heightmap terrain *(prototype)*.
- Weather parameter hook — sky / fog / rain *(prototype)*.

**Sample game**
- `ShowCase_v0_1_0` — a third-person physics character in Sponza: push a crate into the light to ignite a brazier, with animation blending, particles, spatial audio, decals, a HUD, and a clear-screen overlay, all driven by sample-local Lua.

**Tooling**
- Packaged-build smoke test (build → required-file checks → launch → log scan) usable as a release gate.

### Known issues

- **Projected decals crash with 3+ active decals.** 1–2 render fine; the 3rd triggers a hard crash. Keep ≤2 active decals per scene.
- **`won.audio_source.play_oneshot(path)` does not resolve content-relative paths** — it loads relative to the working directory, so `"Audio/foo.wav"` fails. Use a dedicated audio-source entity with `won.audio_source.play(entity)` instead.

Detailed per-system status: [Features.md](Features.md).
