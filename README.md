# QALYVYRON

**A visual-first 3D creation and rendering studio.**

QALYVYRON is an independent desktop application project focused on making 3D creation discoverable: select objects, drag handles, customize materials and lighting in visible panels, and preview the result without needing to memorize keyboard shortcuts.

The long-term goal is a complete 3D workflow with scene editing, materials, lighting, cameras, animation, project files, and high-quality rendering. The repository is an early foundation; capabilities will be marked complete only after they are implemented and tested.

## Product principles

- **Visual-first:** core actions are available through visible controls; shortcuts are optional accelerators.
- **Independent:** no Blender installation, runtime, or background process is required.
- **Real 3D:** scenes use 3D objects, cameras, materials, and lights rather than fake 2D mockups.
- **Progressive complexity:** approachable defaults first, advanced controls when needed.
- **Honest output:** preview quality and export limitations are clearly identified.
- **Native projects:** save and reopen scenes with a versioned, documented format.
- **Monochrome interface:** restrained black, white, and grayscale UI; scene colors remain unrestricted.

## Planned workflow

1. Create a scene and add primitive objects.
2. Select objects directly in the viewport and adjust transforms visually.
3. Customize materials and lighting through the inspector.
4. Frame the scene with a camera and preview it.
5. Save/reopen the project and export an image.
6. Expand the core with mesh editing, asset import, animation, and higher-quality rendering.

## Technology direction

The initial desktop prototype uses **C++ with Qt 6 and Qt Quick 3D** for the interface and interactive 3D viewport. This provides a native application foundation without depending on Blender. The rendering architecture will remain modular so the project can grow beyond the initial viewport renderer.

## Build

A Qt 6 development environment with the Quick, Quick Controls, Quick Dialogs, and Quick 3D modules is required. Ubuntu 24.04 provides a matching Qt 6.4 toolchain for the current CI build.

    cmake -S . -B build
    cmake --build build --config Release

The exact Qt components and supported platforms will be documented as the prototype and CI matrix mature.

## Development status

**Stage 0 — Repository foundation.** The project structure and initial application prototype are being established. This is not yet a production-ready replacement for Blender.

See [ROADMAP.md](ROADMAP.md) for planned milestones and [CONTRIBUTING.md](CONTRIBUTING.md) for development expectations.

## License

No license has been granted yet. Until a license file is added, all rights are reserved by the copyright holder; do not assume the source is open for reuse or redistribution.
