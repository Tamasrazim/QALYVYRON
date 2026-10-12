# QALYVYRON Roadmap

The target is a **usable, independent desktop alternative to Blender for everyday 3D work**, not a visual mockup or a viewport-only demo. Milestones describe required working behavior. A feature is not complete merely because a button, panel, or data structure exists.

## Current state

QALYVYRON is an active mainline desktop application under development. It now has a Qt Quick 3D viewport, primitive scene objects, object transforms, a hierarchy, primitive material controls, camera/light settings, versioned scene files and viewport-preview export. The editable mesh kernel supports indexed vertex changes, viewport-driven face creation from ordered vertex selections, vertex merging, convex planar face extrusion/inset, face deletion, local geometry-only OBJ import, and OBJ export with transforms baked. Runtime-loaded GLB/glTF remains read-only. The current commit has a green Linux build/test run and a Windows x64 build/test plus packaged-app startup smoke test; broader interactive workflow testing on supported hardware is still required.

## Stage 1 — Dependable scene workspace

- [x] Establish the C++/Qt 6 application and versioned local scene-file I/O.
- [x] Add a real-time 3D viewport and common primitives.
- [x] Add hierarchy selection, rename, duplicate, delete, numeric transforms, and per-object visibility controls.
- [x] Add basic primitive material, camera and light controls.
- [x] Export a PNG of the viewport, labelled as a preview rather than a final render.
- [x] Add bounded undo/redo checkpoints for common scene edits in the current implementation.
- [x] Add initial local OBJ and glTF 2.0 loading using Qt Quick 3D AssetUtils.
- [x] Pass current Linux and Windows CI jobs (Linux Qt 6 build/tests; Windows x64 build/tests, portable package staging, and packaged-app startup smoke test).
- [ ] Run an application-level smoke test on a clean Windows machine and supported graphics hardware.
- [ ] Verify undo/redo for each supported command, including mouse drags and save/reopen behavior.
- [ ] Ensure failed file loads leave the prior scene intact and give a clear message.

**Acceptance:** create a scene, add and transform several objects, undo and redo edits, save, fully close the app, reopen the project, and export a preview without Blender or network access. Imported source assets must still be available locally.

## Stage 2 — Actual polygon modeling

This stage is what separates a scene viewer from a practical 3D creation tool.

- [ ] Implement a persistent mesh data model with vertices, edges, faces, normals, and topology validation.
- [ ] Add object mode and mesh-edit mode with visible vertex, edge, and face selection.
- [x] Implement an initial vertex-coordinate editor, face extrusion, and convex planar face inset for supported mesh topology.
- [x] Add a derived unique-edge index, clickable viewport edge handles, and closed edge-loop selection to create supported planar convex faces.
- [x] Add single-edge subdivision that inserts a midpoint into adjacent face boundaries, with topology-budget checks and regression tests.
- [ ] Implement interactive vertex/edge/face selection, edge operations, bevel, merge, delete, face creation, and non-destructive mesh operations.
- [x] Add selected-object focus and approximate scene-wide framing, with the view target saved and restored in project files.
- [x] Add optional world-grid snapping for viewport object dragging, with selectable step sizes.
- [x] Provide numeric position, rotation, and uniform-scale fields for the selected object.
- [x] Add draggable X/Y/Z world-axis handles to constrain object movement in the viewport.
- [ ] Add pivot modes and fully reliable modeling selection.
- [ ] Add subdivision and non-destructive modifier stacks with editable parameters.
- [ ] Make mesh operations undoable and test them against invalid or degenerate geometry.
- [ ] Save mesh topology in the native project format and migrate older projects safely.

**Acceptance:** build a non-trivial mesh from primitives, edit its topology, save it, reopen it, and continue editing without losing topology or material assignments.

## Stage 3 — Asset interoperability

- [ ] Complete robust OBJ and glTF 2.0 import with materials, textures, hierarchy, transforms, and useful error reporting.
- [ ] Export edited meshes to tested standard formats, initially glTF 2.0 and OBJ.
- [ ] Add asset collection/relocation so a project can be moved to another folder or machine without broken paths.
- [ ] Validate file size, formats, texture paths, and malformed inputs before allocating large resources.
- [ ] Test round-trip import/export with reference assets and image comparisons.

**Acceptance:** import a real asset, edit it, save a self-contained project, export it, and verify that the exported asset opens in an independent viewer.

## Stage 4 — Production materials, lighting, and final rendering

- [x] Add a separate CPU offline still renderer using scene triangles, BVH acceleration, camera sampling, base color/roughness, directional and point lights, hard shadows, PNG output, cancellation, and progress.
- [x] Add configurable diffuse indirect bounces and a bounded resolution/sample/bounce workload.
- [ ] Add a principled PBR material editor with metallic, alpha, normal, and texture inputs.
- [ ] Add UV coordinates and a practical UV editing workflow.
- [ ] Add environment and area lights, soft shadows, and a denoiser.
- [ ] Add renderer support for imported asset geometry and portable asset dependencies.
- [ ] Add repeatable render tests for geometry, materials, lighting, bounce behavior, color handling, cancellation, and image output.

**Acceptance:** render a saved scene at a user-selected resolution to a real output image, with reproducible settings and error messages for unsupported hardware or failed renders.

## Stage 5 — Animation

- [ ] Add a timeline, keyframes, interpolation curves, playback, and visible frame/range controls.
- [ ] Animate object transforms and camera properties.
- [ ] Save/reopen animation data and support undo/redo for keyframes.
- [ ] Render animation to numbered image sequences.
- [ ] Add a local video-export workflow with progress, cancellation, and recovery.

**Acceptance:** animate a scene, reopen the project, render a frame range, and export a complete sequence/video without manual frame-by-frame workarounds.

## Stage 6 — Broader creative tools

- [ ] Improve sculpting and high-resolution mesh workflows.
- [ ] Add rigging and skeletal animation where feasible.
- [ ] Add particles, simple simulations, and additional modifier types.
- [ ] Add a compositor or equivalent post-processing graph.
- [ ] Add asset libraries, reusable scene elements, and practical project templates.

These capabilities are planned only after the core modeling, asset, rendering, and animation paths are stable.

## Stage 7 — Release quality

- [ ] Produce a repeatable Windows x64 build and a tested portable package/installer.
- [x] Add automatic local recovery snapshots for unsaved scenes, with a startup restore/discard prompt and recovery tests on Linux and Windows CI.
- [ ] Add crash logs, project backup history, and safe format migrations.
- [ ] Test GPU and fallback paths, display scaling, large scenes, and memory pressure.
- [ ] Document supported hardware, formats, limitations, license terms, and offline behavior.
- [ ] Publish a release only after clean-machine installation and a complete real-project test.

## Product constraints

- No Blender installation, Blender runtime, subscription, account, or cloud service should be required.
- Most common operations must be discoverable in the UI; shortcuts may accelerate them but must not be mandatory.
- Project editing and supported rendering should work offline.
- Do not label an unimplemented control as working.
- Scene files and source assets must be handled safely; failures should not silently erase work.
- Keep the interface monochrome while allowing unrestricted colors in the 3D scene.
- Every release milestone needs evidence from a real build and an exercised workflow, not only a code review.
