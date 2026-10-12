# QALYVYRON — User Guide

This guide covers the current mainline application. Remaining feature gaps are listed explicitly so you can distinguish supported editing tasks from workflows that are still under development.

## Start a scene

The first launch opens a welcome screen. Choose **Start creating** to enter the workspace, **Take the quick tour** to learn the basics, or **Import a model** to open a local asset. Check **Don't show this screen when I launch QALYVYRON** to keep it closed on later launches; uncheck it to show the welcome screen again.

A new scene starts with one cube so there is only one thing to focus on. Use **Add a Shape** to add a cube, ball, cylinder, cone, or plane. Use **Your Objects** to select a shape, rename it, copy it, or remove it.

**Simple view** is enabled by default. It shows six clickable color swatches and a size slider, while keeping detailed numeric transforms, lighting, camera, roughness, and mesh-editing tools out of the way. Hover over a color to see its name. Click **Show all tools** at the top when you need more control. Click **Simple view** to return to the beginner layout.

## Navigate the viewport

- **Right mouse drag on empty space:** look around the scene.
- **Mouse wheel:** zoom in or out.
- **Left click:** select an object.
- **Left drag on a shape:** move it in the scene.
- **Front / Top / Side:** choose a useful view.
- **Frame Scene:** return to the default framing.

Use the inspector's numeric position, rotation, and scale fields when you need exact values. The transform gizmo is an early convenience control; axis-constrained movement is not yet implemented.

## Edit a built-in mesh

Select a cube, sphere, cylinder, cone, or plane, then click **Edit Selected Mesh**. The early mesh editor supports two actual topology operations:

1. **Vertex coordinates:** choose a vertex index and edit its X, Y, or Z coordinate. The selected vertex is marked in the viewport.
2. **Face extrusion:** click a face-center marker or choose its index, enter a non-zero distance, and click **Extrude Face**. A new cap and side faces are added to the mesh.
3. **Face inset:** select a convex planar face, enter a positive inset distance, and click **Inset Face**. The selected face is shrunk by offsetting its edges and adding a surrounding ring. Excessive insets and non-convex or warped faces are rejected.
4. **Delete face:** choose a face marker and click **Delete Face**. The face is removed and unused vertices are compacted. QALYVYRON prevents deleting the final face of an object; use Undo if you remove the wrong surface.

The vertex and face indices are shown as zero-based values. The editor also displays the selected face's center and normal. Mesh data is stored in the scene file, restored when the project opens, and included when you duplicate a built-in mesh. Undo and redo checkpoints include the mesh topology.

This mesh workspace supports vertex and face-center selection, ordered vertex multi-selection for face creation, merge vertices, vertex-coordinate editing, face extrusion and inset, and face deletion. Full edge selection, bevel, loop operations, modifiers, and advanced topology tools are still missing.

## Import a 3D asset

Choose **Import 3D Asset** and select a local:

- glTF 2.0 binary: `.glb`
- glTF 2.0 JSON: `.gltf`
- Wavefront OBJ: `.obj`

OBJ and glTF use different import paths:

- **Wavefront OBJ:** QALYVYRON parses vertex positions and polygon faces into its own editable mesh data. The imported geometry is embedded in the `.qvscene` file, so the original OBJ is no longer needed to reopen that mesh. It can be edited through **Edit Selected Mesh** and written back out through **Export OBJ**.
- **glTF 2.0 (.glb/.gltf):** loaded by Qt Quick 3D's runtime loader for preview and placement. This path remains read-only and stores the source file's local URL. Keep the source file and its dependent assets in the same location when reopening the scene.

The current OBJ importer is geometry-only. It does not yet preserve texture coordinates, imported normals, MTL material libraries, or textures. Full material/texture preservation and asset collection are still planned.

## Materials, lights, and camera

For built-in primitive objects, the inspector offers a small base-color palette and roughness control. The key/fill lights and camera field of view can be adjusted through sliders. Imported-asset material controls are disabled where the current code cannot apply the edit reliably.

## Undo and redo

Use the **Undo** and **Redo** buttons in the toolbar. The current implementation keeps a bounded history of scene snapshots for common object, transform, material, camera, and light edits. Since this system is new, treat it as prototype functionality until it passes the full Windows runtime tests.

## Final render, save, reopen, and export

- **Render Image:** opens offline-render settings. Choose 720p, 1080p, 1440p, 4K, or 8K, select samples per pixel and diffuse-bounce depth, then choose a local PNG destination. Rendering runs on a worker thread with progress reporting and cancellation. The renderer uses actual scene geometry, directional/point lights, hard shadows, material colors/roughness, and diffuse indirect bounces. Resolution/sample/bounce combinations are capped to keep the render workload bounded.
- **Save:** the first save asks where to store the scene. Later saves update that same project file.
- **Protection against lost work:** New and Open ask before replacing unsaved edits. Choose Save, Discard, or Cancel. Cancel keeps the current scene open.
- **Save As…:** choose a new file or make a separate copy.
- **Ctrl+S:** save the current project; opens the save dialog if the scene has not been saved before.
- **Ctrl+O:** open a scene; **Ctrl+Z** and **Ctrl+Y** undo and redo supported edits.
- Saving writes a validated `.qvscene` project, including embedded editable OBJ geometry and mesh edits.
- **Open:** reads a supported QALYVYRON scene file.
- **Export OBJ:** writes editable mesh objects to a Wavefront OBJ file and bakes object position, rotation, and scale into the output vertices. Read-only glTF runtime-loaded objects are skipped with a warning.
- **Export Preview:** writes a PNG of the live viewport.

The preview PNG is the current viewport image. The **Render Image** command is the separate offline render path. The current renderer has hard shadows and simple diffuse indirect bounces; soft shadows, denoising, advanced PBR nodes, textures, and read-only GLB/glTF source geometry are not yet part of final rendering.

## Current limits

There is no full mouse-driven vertex/edge/face selection mode, bevel/inset/merge/loop toolset, modifier stack, editable node-based material system, complete UV workflow, production offline renderer, animation timeline, rigging system, or finished installer yet. Vertex-coordinate editing and single-face extrusion are implemented as early mesh tools, but still need current-build validation. Do not move an important project away from its source assets or rely on the prototype as the only copy of valuable work. Keep backups while testing.

## Report a reproducible issue

Record the app version, Windows version, GPU/driver when known, file format, steps to reproduce, and any status-bar or application error. Use a copy of an asset rather than sharing a private or licensed source file.
