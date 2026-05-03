# WonEngine
<p align="center">
  <img src="Docs/logo.png" width="360" alt="WonEngine Logo">
</p>

WonEngine is a work-in-progress C++ rendering engine for experimenting with modern real-time graphics, editor tooling, and game-engine systems.

> Currently developed on Windows with DirectX 12.  
> The long-term direction is multi-backend (Vulkan) and multi-platform support (Linux and console-oriented platforms).

## Showcase

<p align="center">
  <img src="Docs/.png" width="900" alt="WonEngine Editor Showcase">
</p>

> Screenshots and videos will be added as the renderer and editor become more stable.

## Getting Started

### Windows

WonEngine is currently developed and tested on Windows.

#### Requirements

- Visual Studio 2022
- CMake 3.20 or later

#### Setup

Generate the Visual Studio project files by running the provided batch file:

```bat
GenerateProjectFiles.bat
```

Then open the generated Visual Studio solution and build the `Editor` target.

To run the editor:

```text
Binary/Windows/Editor.exe
```

### Other Platforms

Linux and console-oriented platforms are planned but not currently supported.

## Documentation

- [Documentation](Docs/Documentation.md)
- [Features](Docs/Features.md)

## Engine Architecture

WonEngine is structured around runtime code, editor code, shaders, plugins, and offline tools.

```text
Source/
  Runtime/        Core engine runtime source
  Editor/         Editor application source
  Shaders/        HLSL shader source and shader interop headers
  Plugins/        Runtime plugin source
  Tools/          Offline tool source, including ShaderOfflineCompiler

Contents/         Editor and runtime assets
CompiledShaders/  Generated shader binaries
Binary/           Build output
```
