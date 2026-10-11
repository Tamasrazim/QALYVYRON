# QALYVYRON

**An independent, visual-first 3D creation studio built to become a practical Blender alternative.**

QALYVYRON is being developed as a local desktop tool for creating, editing, animating, and rendering 3D work without requiring Blender, a paid subscription, or a cloud account. The goal is not to imitate Blender's interface; it is to provide a workflow that is easier to discover, mouse-friendly by default, and capable of finishing real projects.

**Status: active prototype — not yet a drop-in Blender replacement.** The current build covers the first scene-workspace foundations. It does not yet have full polygon modeling, a final offline renderer, a complete animation system, or production asset export. Those gaps are explicit work items, not features that this repository claims to have finished.

## Current prototype capabilities

- Interactive 3D viewport with orbit, zoom, selection, and a reference grid.
- Cube, sphere, cylinder, cone, and plane scene objects.
- Object hierarchy, renaming, duplication, deletion, and direct transform editing.
- Early editable mesh tools for built-in primitives: inspect indexed vertices/faces, change an individual vertex coordinate, and extrude a selected face by index. The active vertex and face center are marked in the viewport while the mesh editor is open.
- Material base-color palette and roughness for the built-in primitive objects.
- Camera field-of-view and key/fill-light controls.
- Versioned `.qvscene` project files with local save/open validation, including the editable mesh topology.
- Undo and redo history for common scene, transform, material, camera, and light edits.
- Initial import path for local glTF 2.0 (`.glb`, `.gltf`) and Wavefront OBJ (`.obj`) files. Imported asset materials are preserved, but not yet editable in the inspector.
- PNG export of the viewport preview. **This is not a final offline render.**

Imported assets are linked by their local file URL. Keep those source files at the same location when reopening a project on this early build; automatic asset collection and portable project packaging are still planned.

## What must be built before it can replace Blender for daily work

The major remaining systems are real mesh-edit mode (vertex, edge and face operations), reliable import and export, UV and texture workflows, a node-based material system, a production-quality offline renderer, animation and timeline tools, render queues, and a complete Windows installer. The acceptance criteria live in [ROADMAP.md](ROADMAP.md).

The rule for the project is simple: **a control is not considered implemented just because it appears in the interface.** It must perform the advertised work and be tested. Builds and file-format limits should be documented honestly.

## Product principles

- **Visual-first:** important actions are available through visible controls; shortcuts are optional.
- **Independent:** no Blender installation, Blender runtime, account, subscription, or cloud service is required.
- **Offline-first:** projects and rendering are intended to work locally.
- **Actual 3D:** use real geometry, cameras, materials and lights instead of 2D mockups.
- **Progressive complexity:** simple defaults first, deeper controls as the workflow grows.
- **Honest output:** preview quality and final-render quality are clearly separated.
- **Monochrome interface:** black, white and grayscale UI; scene content can use any colors.
- **Recoverable editing:** project validation, bounded undo/redo, and tests are part of the product, not optional polish.

## Technology

The desktop application uses C++17, Qt 6, Qt Quick 3D and QML. Qt Quick 3D provides the current real-time viewport; the long-term architecture must add a dedicated modeling data layer and a final-render backend instead of treating a viewport screenshot as a rendered image.

The application does not use Blender as a library or launch Blender in the background.

## Build from source

A Qt 6 development installation with Quick, Quick Controls, Quick Dialogs, Quick 3D, and Shader Tools is required.

On Ubuntu 24.04, install the current CI toolchain:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends cmake ninja-build g++ \
  qt6-base-dev qt6-declarative-dev qt6-quick3d-dev \
  qt6-quick3d-dev-tools qt6-shadertools-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The CI workflow validates Linux builds and is being extended to produce a portable Windows x64 package. A downloadable package is only considered usable once the Windows job has passed; a successful C++ build alone does not prove the QML application works on every GPU.

## Repository files

- [ROADMAP.md](ROADMAP.md) — milestone plan and the requirements for a practical Blender alternative.
- [CONTRIBUTING.md](CONTRIBUTING.md) — build and development expectations.
- [USER_GUIDE.md](USER_GUIDE.md) — current prototype controls and known limitations.

## License

No license has been granted yet. All rights are reserved by the copyright holder until a license file is added; do not assume the source is open for reuse or redistribution.
