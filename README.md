# QALYVYRON

**An independent, visual-first 3D creation studio built to become a practical Blender alternative.**

QALYVYRON is being developed as a local desktop tool for creating, editing, animating, and rendering 3D work without requiring Blender, a paid subscription, or a cloud account. The goal is not to imitate Blender's interface; it is to provide a workflow that is easier to discover, mouse-friendly by default, and capable of finishing real projects.

**Status: active mainline development build — not yet a finished Blender replacement.** QALYVYRON now has an initial editable mesh kernel and a usable geometry-only OBJ import/export path, but it still lacks full edge/loop modeling, a full PBR material and texture pipeline, animation, soft shadows/denoising and complete production packaging. Those gaps remain explicit work items.

## Implemented in the main build

- First-run welcome screen and a four-step, task-based quick tour; beginner mode keeps common actions visible while hiding numeric transforms, lighting, camera, roughness, and mesh-editing controls until requested. The startup choice is saved and respected on later launches.
- A simple one-cube starting scene, plain-language shape labels, a beginner size slider, visible color swatches with hover labels, and clear viewport mouse guidance.
- Interactive 3D viewport with orbit, zoom, selection, and a reference grid; **Focus Selected** centers one object and **Frame All** calculates a scene-wide view, with the focus point saved in the project.
- Cube, sphere, cylinder, cone, and plane scene objects.
- Object hierarchy, renaming, duplication, deletion, and direct transform editing.
- Editable polygon-mesh tools for built-in primitives and imported OBJ geometry: create faces from ordered viewport vertex selections, merge vertices, change vertex coordinates, extrude and inset convex planar faces, and delete faces.
- Material base-color palette and roughness for the built-in primitive objects.
- Camera field-of-view and key/fill-light controls.
- Versioned `.qvscene` project files with local save/open validation, including the editable mesh topology. Save remembers the active project path; Save As creates a separate copy, common save/open/undo/redo shortcuts are available, and New/Open/window closing protect unsaved edits with Save, Discard, and Cancel choices.
- Undo and redo history for common scene, transform, material, camera, and light edits.
- **Wavefront OBJ (`.obj`) import:** vertex positions and polygon faces are parsed into QALYVYRON's editable mesh data. OBJ meshes are embedded in `.qvscene` projects and can be edited with indexed vertex coordinates and face extrusion.
- **Wavefront OBJ export:** writes editable mesh objects with their scene transforms baked into exported vertex positions. Runtime-loaded `.glb`/`.gltf` assets are skipped with a clear warning.
- **glTF 2.0 (`.glb`, `.gltf`) preview import:** loaded by Qt Quick 3D's runtime loader. These objects remain read-only and reference their original local files.
- CPU offline final PNG rendering from scene triangles with a BVH accelerator, multisample anti-aliasing, directional/point lighting, hard shadows, material roughness, and configurable diffuse indirect bounces.
- Separate viewport-preview PNG export for quick captures. It is not confused with a final offline render.

The first OBJ path is geometry-only: it currently reads `v` and `f` data, not OBJ normals, UV coordinates, MTL materials, or textures. glTF source paths are not yet collected into portable project folders, so those files must remain at their original locations when reopening scenes.

## What must be built before it can replace Blender for daily work

The major remaining systems are edge selection/modeling, bevel and loop tools, non-destructive modifiers, UV and texture workflows, node-based materials, renderer denoising and soft-shadow quality, rigging/animation/timeline tools, render queues, broader asset interoperability, and a complete tested Windows installer. The acceptance criteria live in [ROADMAP.md](ROADMAP.md).

The rule for the project is simple: **a control is not considered implemented just because it appears in the interface.** It must perform the advertised work and be tested. Builds and file-format limits should be documented honestly.

## Product principles

- **Visual-first:** important actions are available through visible controls; shortcuts are optional.
- **Independent:** no Blender installation, Blender runtime, account, subscription, or cloud service is required.
- **Offline-first:** projects and rendering are intended to work locally.
- **Actual 3D:** use real geometry, cameras, materials and lights instead of 2D mockups.
- **Progressive complexity:** the first screen explains tasks in everyday language; deeper controls stay tucked away until requested.
- **Honest output:** preview quality and final-render quality are clearly separated.
- **Monochrome interface:** black, white and grayscale UI; scene content can use any colors.
- **Recoverable editing:** project validation, bounded undo/redo, and tests are part of the product, not optional polish.

## Technology

The desktop application uses C++17, Qt 6, Qt Quick 3D and QML. Qt Quick 3D powers the live viewport; a separate CPU renderer traces camera rays against the saved mesh triangles and writes a real PNG output without capturing the viewport.

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

The CI workflow builds and tests on Linux and builds a Windows x64 portable package. The Windows job now launches the staged desktop app before packaging it and writes Qt/QML startup messages to a diagnostic log if it exits early. Treat the package as unverified unless both build jobs and the packaged-app startup smoke test pass; a successful C++ build alone does not prove the QML application works on every GPU.

## Repository files

- [ROADMAP.md](ROADMAP.md) — milestone plan and the requirements for a practical Blender alternative.
- [CONTRIBUTING.md](CONTRIBUTING.md) — build and development expectations.
- [USER_GUIDE.md](USER_GUIDE.md) — current prototype controls and known limitations.

## License

No license has been granted yet. All rights are reserved by the copyright holder until a license file is added; do not assume the source is open for reuse or redistribution.
