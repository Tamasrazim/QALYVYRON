# QALYVYRON — Prototype Guide

This guide describes the current early build. It is not a claim that all Blender workflows are supported yet.

## Start a scene

The application opens with a cube and sphere. Use the **Create** panel to add a cube, sphere, cylinder, cone, or plane. Use **Scene Objects** to select an object, rename it in the inspector, duplicate it, or remove it.

## Navigate the viewport

- **Right mouse drag:** orbit the camera around the scene.
- **Mouse wheel:** zoom in or out.
- **Left click:** select an object.
- **Left drag on an object:** move it in the scene.
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

This is still an early mesh editor, not a complete Blender-style Edit Mode. Vertex and face-center markers are clickable; full edge selection, bevel, merge, loop selection, multi-selection, and advanced topology tools are still missing.

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

## Save, reopen, and export

- **Save:** writes a validated `.qvscene` project, including embedded editable OBJ geometry and mesh edits.
- **Open:** reads a supported QALYVYRON scene file.
- **Export OBJ:** writes editable mesh objects to a Wavefront OBJ file and bakes object position, rotation, and scale into the output vertices. Read-only glTF runtime-loaded objects are skipped with a warning.
- **Export Preview:** writes a PNG of the live viewport.

The PNG is only the current viewport image. It is not an offline/path-traced final render, and it should not be used as evidence of a final-render engine. A separate final-render system is a major roadmap item.

## Current limits

There is no full mouse-driven vertex/edge/face selection mode, bevel/inset/merge/loop toolset, modifier stack, editable node-based material system, complete UV workflow, production offline renderer, animation timeline, rigging system, or finished installer yet. Vertex-coordinate editing and single-face extrusion are implemented as early mesh tools, but still need current-build validation. Do not move an important project away from its source assets or rely on the prototype as the only copy of valuable work. Keep backups while testing.

## Report a reproducible issue

Record the app version, Windows version, GPU/driver when known, file format, steps to reproduce, and any status-bar or application error. Use a copy of an asset rather than sharing a private or licensed source file.
