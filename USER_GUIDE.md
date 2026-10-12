# QALYVYRON — User Guide

This guide covers the current mainline application. Remaining feature gaps are listed explicitly so you can distinguish supported editing tasks from workflows that are still under development.

## Start a scene

The first launch opens a welcome screen. Choose **Start creating** to enter the workspace, **Take the quick tour** to learn the basics, or **Import a model** to open a local asset. Check **Don't show this screen when I launch QALYVYRON** to keep it closed on later launches; uncheck it to show the welcome screen again.

A new scene starts with one cube so there is only one thing to focus on. Use **Add a Shape** to add a cube, ball, cylinder, cone, plane, or torus. The torus is an actual ring-shaped mesh, and its vertices, edges, and faces can be edited like the other built-in shapes. Use **Your Objects** to select a shape, rename it, copy it, or remove it. Use the search field above the object list to filter by an object's name, shape type, or ID; clear the field to show the whole scene again. The filter also clears automatically when you create or open another project. Use the small **◉ / ○** button beside an object to hide or show it without deleting it. Hidden objects remain in the hierarchy so you can bring them back later; their visibility is saved with the scene, can be undone/redone, and hidden meshes are skipped by the final PNG renderer.

**Simple view** is enabled by default. It shows six clickable color swatches and a size slider, while keeping detailed numeric transforms, lighting, camera, roughness, and mesh-editing tools out of the way. Hover over a color to see its name. Click **Show all tools** at the top when you need more control. Click **Simple view** to return to the beginner layout.

## Navigate the viewport

- **Right mouse drag on empty space:** look around the scene.
- **Mouse wheel:** zoom in or out.
- **Left click:** select an object.
- **Left drag on a shape:** move it in the scene.
- **Front / Top / Side:** choose an aligned orthographic view for accurate proportions without perspective distortion.
- **Perspective / Orthographic:** switch projection modes directly; the mode is saved with the scene and used by the final PNG render too.
- **Focus Selected:** center the camera on the selected object.
- **Frame All:** calculate a view that includes the current scene; it works for distant objects and supported custom meshes too.
- **Snap: Off / Snap: On:** turn grid snapping on or off for object dragging. Choose a step of 1, 5, 10, 25, or 50 scene units from the dropdown beside it. Snapping is off by default. It affects mouse-drag placement only; the numeric position fields remain exact.

Use the inspector's numeric position, rotation, and independent X/Y/Z scale fields when you need exact values. Scaling one axis does not change the other two. The simple-view size slider still scales all three axes together. In **Show all tools** mode, click **Reset Transform** to move the selected object to the origin, clear its rotation, and restore its scale to 1. The operation can be undone, and the camera is refocused so the reset object stays visible. The viewport gizmo has labeled **X**, **Y**, and **Z** handles that move the selected object along one world axis at a time; dragging the open center area still moves freely. If an axis handle disappears because that axis points almost directly toward the camera, switch to **Front**, **Top**, or **Side** view. Grid snapping can be enabled beside **Frame All** and **Focus Selected**; choose the step from the dropdown next to **Snap: On / Off**.

## Edit a built-in mesh

The viewport exposes local-space vertex markers, edge lines, and face-center handles while the mesh editor is open. Click an edge line to inspect its endpoints. Choose **Select Edge Loop** to multi-select edges; Use **Subdivide Edge** on a selected edge to insert a midpoint vertex into each incident face boundary without adding or deleting faces. The new midpoint is selected for coordinate editing; after later topology changes, reselect edges rather than relying on old edge indices. selected edges should form one connected, closed boundary before using **Create Face from Loop**. The operation accepts only supported planar, convex loops and leaves the mesh unchanged when the boundary is branched, disconnected, or geometrically invalid. For a missing face, select its existing boundary edges rather than selecting internal diagonals.


Select a cube, sphere, cylinder, cone, or plane, then click **Edit Selected Mesh**. The early mesh editor supports two actual topology operations:

1. **Vertex coordinates:** choose a vertex index and edit its X, Y, or Z coordinate. The selected vertex is marked in the viewport.
2. **Face extrusion:** click a face-center marker or choose its index, enter a non-zero distance, and click **Extrude Face**. A new cap and side faces are added to the mesh.
3. **Face inset:** select a convex planar face, enter a positive inset distance, and click **Inset Face**. The selected face is shrunk by offsetting its edges and adding a surrounding ring. Excessive insets and non-convex or warped faces are rejected.
4. **Delete face:** choose a face marker and click **Delete Face**. The face is removed and unused vertices are compacted. QALYVYRON prevents deleting the final face of an object; use Undo if you remove the wrong surface.

The vertex and face indices are shown as zero-based values. The editor also displays the selected face's center and normal. Mesh data is stored in the scene file, restored when the project opens, and included when you duplicate a built-in mesh. Undo and redo checkpoints include the mesh topology.

This mesh workspace supports vertex and face-center selection, ordered vertex multi-selection for face creation, merge vertices, vertex-coordinate editing, face extrusion and inset, face deletion, edge subdivision and a constrained **Bevel Edge** operation. The first bevel version works on closed mesh edges whose endpoints each connect to three edges; try a smaller width if the tool rejects an edge.

## Import a 3D asset

Choose **Import 3D Asset** and select a local:

- glTF 2.0 binary: `.glb`
- glTF 2.0 JSON: `.gltf`
- Wavefront OBJ: `.obj`
- STL triangle mesh: `.stl`

OBJ and glTF use different import paths:

- **Wavefront OBJ:** QALYVYRON parses vertex positions and polygon faces into its own editable mesh data. The imported geometry is embedded in the `.qvscene` file, so the original OBJ is no longer needed to reopen that mesh. It can be edited through **Edit Selected Mesh** and written back out through **Export OBJ**.
- **STL:** both ASCII STL and binary STL triangles import as editable mesh topology. Adjacent triangles with matching corner coordinates are joined at shared vertices. **Export STL** writes a binary STL, triangulates visible editable faces, and includes object position, rotation, and scale. The current STL path is geometry-only: normals are recomputed and STL color/material extensions are not preserved.
- **glTF 2.0 (.glb/.gltf):** loaded by Qt Quick 3D's runtime loader for preview and placement. This path remains read-only and stores the source file's local URL. Keep the source file and its dependent assets in the same location when reopening the scene.

The current OBJ importer is geometry-only. It does not yet preserve texture coordinates, imported normals, MTL material libraries, or textures. Full material/texture preservation and asset collection are still planned.

## Materials, lights, and camera

For built-in editable mesh objects, the inspector offers six quick color swatches plus **Choose custom color…**, as well as roughness and metallic controls. In **Show all tools** mode, **Plastic**, **Metal**, **Rubber**, and **Gold** presets update base color, roughness, and metalness together. Each preset appears in both the live viewport and offline render, and the change can be undone. In **Show all tools** mode, enable **Smooth shading** to average normals across connected mesh faces for a rounder-looking sphere or torus; disable it to use flat face normals. This setting is saved in the project, can be undone, and also affects final PNG rendering. The metallic value affects both the live PrincipledMaterial preview and the offline renderer. This is a basic metallic response; full PBR nodes, alpha, normal maps, and texture inputs are not implemented yet. The key/fill lights and camera field of view can be adjusted through sliders. Imported-asset material controls are disabled where the current code cannot apply the edit reliably.

## Undo and redo

Use the **Undo** and **Redo** buttons in the toolbar. The current implementation keeps a bounded history of scene snapshots for common object, transform, material, camera, and light edits. Since this system is new, treat it as prototype functionality until it passes the full Windows runtime tests.

## Final render, save, reopen, and export

- **Render Image:** opens offline-render settings. Choose 720p, 1080p, 1440p, 4K, or 8K, select samples per pixel and diffuse-bounce depth, then choose a local PNG destination. Rendering runs on a worker thread with progress reporting and cancellation. The renderer uses actual scene geometry, directional/point lights, hard shadows, material colors/roughness, and diffuse indirect bounces. Resolution/sample/bounce combinations are capped to keep the render workload bounded. The final render uses the saved viewport focus point, so a scene framed around objects far from world origin is still aimed at those objects.
- **Save:** the first save asks where to store the scene. Later saves update that same project file.
- **Protection against lost work:** New, Open, and closing the app ask before replacing or closing a scene with unsaved edits. Choose Save, Discard, or Cancel. Cancel keeps the current scene open.
- **Automatic recovery:** while the editor has unsaved changes, QALYVYRON writes a recovery snapshot to app-local storage about every 30 seconds. On next launch, you can restore the snapshot or discard it. Saving or explicitly discarding clears the recovery copy; it stays separate from the original `.qvscene` file. This is one recovery snapshot, not a versioned backup history.
- **Save As…:** choose a new file or make a separate copy. **Ctrl+Shift+S** opens Save As. Select an object and press **F2** to focus and select its name for renaming, **Ctrl+D** to duplicate it, or **Ctrl+Shift+F** to frame all visible objects.
- **Ctrl+S:** save the current project; opens the save dialog if the scene has not been saved before.
- **Ctrl+O:** open a scene; **Ctrl+Z** and **Ctrl+Y** undo and redo supported edits.
- Saving writes a validated `.qvscene` project, including embedded editable OBJ geometry and mesh edits.
- **Open:** reads a supported QALYVYRON scene file.
- **Export OBJ:** writes editable mesh objects to a Wavefront OBJ file and bakes object position, rotation, and scale into the output vertices. Read-only glTF runtime-loaded objects are skipped with a warning.
- **Export STL:** writes visible editable geometry to a binary STL file. Polygon faces are triangulated, transforms are baked into the exported vertices, and hidden or read-only imported objects are skipped with a warning.
- **Export Preview:** writes a PNG of the live viewport.

The preview PNG is the current viewport image. The **Render Image** command is the separate offline render path. The current renderer has hard shadows and simple diffuse indirect bounces; soft shadows, denoising, advanced PBR nodes, textures, and read-only GLB/glTF source geometry are not yet part of final rendering.

## Current limits

There is no full mouse-driven vertex/edge/face selection mode, bevel/inset/merge/loop toolset, modifier stack, editable node-based material system, complete UV workflow, production offline renderer, animation timeline, rigging system, or finished installer yet. Vertex-coordinate editing and single-face extrusion are implemented as early mesh tools, but still need current-build validation. Do not move an important project away from its source assets or rely on the prototype as the only copy of valuable work. Keep backups while testing.

## Report a reproducible issue

Record the app version, Windows version, GPU/driver when known, file format, steps to reproduce, and any status-bar or application error. Use a copy of an asset rather than sharing a private or licensed source file.
