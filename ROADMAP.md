# QALYVYRON Roadmap

The milestones describe intended work, not claims that those features have shipped. Each stage should have a runnable build and verifiable acceptance checks.

## Stage 0 — Foundation

- [x] Create the public repository and project overview.
- [x] Establish a Qt 6 desktop application and native scene-file I/O layer.
- [ ] Build on a clean machine in CI.
- [ ] Verify supported Qt versions, graphics backends, and operating systems.

## Stage 1 — First usable 3D workspace

- [ ] Real-time 3D viewport with camera orbit and zoom.
- [ ] Create and delete primitive meshes through visible controls.
- [ ] Select scene objects from the viewport or hierarchy.
- [ ] Edit position, rotation, scale, and material color.
- [ ] Save and reopen versioned scene files.
- [ ] Export a viewport preview to PNG.
- [ ] Undo and redo common scene operations.
- [ ] Automated smoke tests and a reproducible developer build.

**Acceptance:** a user can create a small scene, customize it, save it, reopen it, and export a preview without launching Blender.

## Stage 2 — Modeling and scene editing

- Mesh selection and editing tools.
- Extrude, bevel, subdivision, and non-destructive modifiers.
- Collections, hierarchy, naming, visibility, and reusable assets.
- Object snapping, numeric transforms, and discoverable transform handles.
- Import/export for glTF 2.0, with format limitations displayed clearly.

## Stage 3 — Materials and lighting

- Principled material inspector and texture input.
- Point, spot, directional, and area lights.
- Environment lighting, shadow controls, and camera framing.
- Clear separation of real-time preview quality and final-render quality.

## Stage 4 — Animation

- Timeline, keyframes, playback, and interpolation.
- Object and camera animation.
- Rendered image sequences and video output.
- Render queue progress, cancellation, and recoverable failures.

## Stage 5 — Higher-quality rendering

- Modular rendering backend with documented capability and hardware requirements.
- Improved global illumination, reflections, denoising, and sampling controls.
- Performance profiling and repeatable image-based regression tests.
- Memory limits, crash recovery, and useful render diagnostics.

## Stage 6 — Packaging and production hardening

- Signed installer and application packaging for supported platforms.
- Import/export compatibility testing.
- Accessibility, localization, and input-device testing.
- Project format migration tests and complete user documentation.

## Non-negotiable constraints

- The application must not require Blender to be installed or running.
- UI controls must do real work; unfinished functions must be marked as unavailable.
- Saving must not silently overwrite a project without following the chosen save workflow.
- Opening a scene file must validate format, version, and size before loading.
- No milestone is complete until its acceptance criteria have been exercised.
