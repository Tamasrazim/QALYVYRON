import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick3D.AssetUtils
import QtQuick.Dialogs
import Qalyvyron 1.0

ApplicationWindow {
    id: root

    width: 1480
    height: 920
    minimumWidth: 1080
    minimumHeight: 700
    visible: true
    title: "QALYVYRON — 3D Studio"
    color: "#0b0b0b"

    property color panelColor: "#111111"
    property color panelRaised: "#191919"
    property color borderColor: "#2d2d2d"
    property color mainText: "#eeeeee"
    property color secondaryText: "#999999"
    property int selectedIndex: -1
    property var selectedObject: null
    property var sceneObjects: []
    property int nextObjectNumber: 1
    property real orbitYaw: 34
    property real orbitPitch: 23
    property real orbitDistance: 470
    property real gizmoX: 0
    property real gizmoY: 0
    property real gizmoDepth: 1
    property bool gizmoVisible: false
    property var undoStack: []
    property var redoStack: []
    property int historyLimit: 80
    property bool historyLocked: false
    property bool editorReady: false
    property var meshVertexSelection: []
    property bool vertexSelectionMode: false

    ListModel {
        id: objectList
    }

    function setStatus(message) {
        statusLabel.text = message
        statusTimer.restart()
    }

    // Bounded snapshots make common scene edits reversible without retaining
    // destroyed Qt objects or allowing the history to grow without limit.
    function historySnapshot() {
        return JSON.parse(JSON.stringify(serializeScene()))
    }

    function pushUndoSnapshot() {
        if (historyLocked)
            return

        const stack = undoStack.slice()
        stack.push(historySnapshot())
        if (stack.length > historyLimit)
            stack.shift()
        undoStack = stack
        redoStack = []
    }

    function clearHistory() {
        undoStack = []
        redoStack = []
    }

    function undo() {
        if (undoStack.length === 0)
            return

        const target = undoStack[undoStack.length - 1]
        const current = historySnapshot()
        undoStack = undoStack.slice(0, -1)
        redoStack = redoStack.concat([current])
        historyLocked = true
        applyProject(target)
        historyLocked = false
        setStatus("Undid the previous edit.")
    }

    function redo() {
        if (redoStack.length === 0)
            return

        const target = redoStack[redoStack.length - 1]
        const current = historySnapshot()
        redoStack = redoStack.slice(0, -1)
        undoStack = undoStack.concat([current])
        if (undoStack.length > historyLimit)
            undoStack = undoStack.slice(undoStack.length - historyLimit)
        historyLocked = true
        applyProject(target)
        historyLocked = false
        setStatus("Restored the edit.")
    }

    function createPrimitive(shape) {
        pushUndoSnapshot()
        addPrimitive(shape)
    }

    function meshSource(shape) {
        if (shape === "Sphere") return "#Sphere"
        if (shape === "Cylinder") return "#Cylinder"
        if (shape === "Cone") return "#Cone"
        if (shape === "Plane") return "#Rectangle"
        return "#Cube"
    }

    function updateCamera() {
        const yaw = orbitYaw * Math.PI / 180
        const pitch = orbitPitch * Math.PI / 180
        const horizontal = orbitDistance * Math.cos(pitch)
        camera.position = Qt.vector3d(
            horizontal * Math.sin(yaw),
            orbitDistance * Math.sin(pitch),
            horizontal * Math.cos(yaw)
        )
        camera.lookAt(focusTarget)
        Qt.callLater(root.updateGizmo)
    }

    function updateGizmo() {
        if (!view3d || !selectedObject) {
            gizmoVisible = false
            return
        }

        const projected = view3d.mapFrom3DScene(selectedObject.position)
        gizmoX = projected.x
        gizmoY = projected.y
        gizmoDepth = projected.z
        gizmoVisible = projected.z >= 0
                       && projected.x >= 38 && projected.x <= view3d.width - 38
                       && projected.y >= 38 && projected.y <= view3d.height - 38
    }

    function editorIdFromObject(hitObject) {
        let candidate = hitObject
        while (candidate) {
            if (candidate.editorId !== undefined && String(candidate.editorId).length > 0)
                return String(candidate.editorId)
            candidate = candidate.parent
        }
        return ""
    }

    function selectObject(objectId) {
        clearMeshVertexSelection()
        vertexSelectionMode = false
        selectedIndex = -1
        selectedObject = null

        for (let i = 0; i < objectList.count; ++i) {
            if (objectList.get(i).objectId === objectId) {
                selectedIndex = i
                break
            }
        }

        for (let j = 0; j < sceneObjects.length; ++j) {
            if (sceneObjects[j].editorId === objectId) {
                selectedObject = sceneObjects[j]
                break
            }
        }

        refreshInspector()
        Qt.callLater(root.updateGizmo)
    }

    function refreshInspector() {
        if (!selectedObject || selectedIndex < 0) {
            objectNameField.text = ""
            xField.text = "0"
            yField.text = "0"
            zField.text = "0"
            rxField.text = "0"
            ryField.text = "0"
            rzField.text = "0"
            scaleField.text = "1"
            roughnessSlider.value = 0.38
            colorCombo.currentIndex = 0
            return
        }

        objectNameField.text = selectedObject.editorLabel
        const p = selectedObject.position
        const r = selectedObject.eulerRotation
        xField.text = Number(p.x).toFixed(2)
        yField.text = Number(p.y).toFixed(2)
        zField.text = Number(p.z).toFixed(2)
        rxField.text = Number(r.x).toFixed(1)
        ryField.text = Number(r.y).toFixed(1)
        rzField.text = Number(r.z).toFixed(1)
        scaleField.text = Number(selectedObject.uniformScale).toFixed(2)
        roughnessSlider.value = selectedObject.editorRoughness

        const palette = ["#c8c8c8", "#f2f2f2", "#555555", "#df5b57", "#508bdc", "#d8ae57"]
        let nearest = 0
        for (let i = 0; i < palette.length; ++i) {
            if (String(selectedObject.editorColor).toLowerCase() === palette[i]) {
                nearest = i
                break
            }
        }
        colorCombo.currentIndex = nearest
    }

    function addPrimitive(shape, savedData) {
        const saved = savedData || ({})
        const objectId = saved.id ? String(saved.id) : "object-" + nextObjectNumber
        let numericSuffix = Number(objectId.replace("object-", ""))
        if (Number.isFinite(numericSuffix) && numericSuffix >= nextObjectNumber)
            nextObjectNumber = numericSuffix + 1
        else if (!saved.id)
            nextObjectNumber += 1

        const label = saved.label ? String(saved.label) : shape + " " + objectId.replace("object-", "")
        const x = saved.x !== undefined ? Number(saved.x) : objectList.count * 120
        const y = saved.y !== undefined ? Number(saved.y) : 0
        const z = saved.z !== undefined ? Number(saved.z) : 0
        const rx = Number(saved.rx) || 0
        const ry = Number(saved.ry) || 0
        const rz = Number(saved.rz) || 0
        const uniformScale = Number(saved.scale) > 0 ? Number(saved.scale) : 1
        const defaultColors = ["#c8c8c8", "#508bdc", "#d8ae57", "#df5b57", "#f2f2f2"]
        const defaultColor = defaultColors[objectList.count % defaultColors.length]
        const colorValue = saved.color ? String(saved.color) : defaultColor
        const roughness = saved.roughness !== undefined ? Math.max(0, Math.min(1, Number(saved.roughness))) : 0.38

        const object = primitiveComponent.createObject(sceneRoot, {
            editorId: objectId,
            editorLabel: label,
            editorShape: shape,
            editorColor: colorValue,
            editorRoughness: roughness,
            editorMeshData: saved.mesh || ({}),
            uniformScale: uniformScale,
            position: Qt.vector3d(x, y, z),
            eulerRotation: Qt.vector3d(rx, ry, rz),
            scale: Qt.vector3d(uniformScale, uniformScale, uniformScale),
            objectName: objectId
        })

        if (!object) {
            setStatus("Could not create the " + shape.toLowerCase() + ".")
            return
        }

        sceneObjects.push(object)
        objectList.append({
            objectId: objectId,
            label: label,
            shape: shape,
            kind: "primitive",
            source: "",
            x: x, y: y, z: z,
            rx: rx, ry: ry, rz: rz,
            scale: uniformScale,
            color: colorValue,
            roughness: roughness
        })
        selectObject(objectId)
        setStatus("Created " + label + ".")
    }

    function createImportedAsset(sourceUrl) {
        const sourceText = String(sourceUrl || "")
        if (/\.obj$/i.test(sourceText.split(/[?#]/)[0])) {
            const result = projectIO.importObj(sourceUrl)
            if (!result.ok) {
                setStatus("OBJ import failed: " + result.error)
                return
            }

            pushUndoSnapshot()
            addPrimitive("Custom", {
                label: result.label || "Imported OBJ Mesh",
                x: objectList.count * 120,
                y: 0,
                z: 0,
                scale: 1,
                color: "#c8c8c8",
                roughness: 0.48,
                mesh: result.mesh
            })
            setStatus("Imported editable OBJ mesh: " + (result.label || "Mesh") + ".")
            return
        }

        pushUndoSnapshot()
        addImportedAsset(sourceUrl)
    }

    function addImportedAsset(sourceUrl, savedData) {
        const saved = savedData || ({})
        const sourceText = saved.source ? String(saved.source) : String(sourceUrl || "")
        if (!/^file:/i.test(sourceText)) {
            setStatus("Choose a local .glb, .gltf, or .obj file.")
            return
        }
        if (!/\.(glb|gltf|obj)$/i.test(sourceText.split(/[?#]/)[0])) {
            setStatus("Supported imports are glTF 2.0 (.glb/.gltf) and Wavefront OBJ (.obj).")
            return
        }

        const objectId = saved.id ? String(saved.id) : "object-" + nextObjectNumber++
        const numericSuffix = Number(objectId.replace("object-", ""))
        if (Number.isFinite(numericSuffix) && numericSuffix >= nextObjectNumber)
            nextObjectNumber = numericSuffix + 1

        const label = saved.label ? String(saved.label) : "Imported Asset " + objectId.replace("object-", "")
        const x = saved.x !== undefined ? Number(saved.x) : objectList.count * 120
        const y = saved.y !== undefined ? Number(saved.y) : 0
        const z = saved.z !== undefined ? Number(saved.z) : 0
        const rx = Number(saved.rx) || 0
        const ry = Number(saved.ry) || 0
        const rz = Number(saved.rz) || 0
        const uniformScale = Number(saved.scale) > 0 ? Number(saved.scale) : 1

        const object = importedAssetComponent.createObject(sceneRoot, {
            editorId: objectId,
            editorLabel: label,
            editorShape: "Imported Asset",
            editorImported: true,
            editorColor: "#c8c8c8",
            editorRoughness: 0.38,
            uniformScale: uniformScale,
            assetUrl: sourceText,
            position: Qt.vector3d(x, y, z),
            eulerRotation: Qt.vector3d(rx, ry, rz),
            scale: Qt.vector3d(uniformScale, uniformScale, uniformScale),
            objectName: objectId
        })

        if (!object) {
            setStatus("Could not create an imported-asset entry.")
            return
        }

        sceneObjects.push(object)
        objectList.append({
            objectId: objectId,
            label: label,
            shape: "Imported Asset",
            kind: "asset",
            source: sourceText,
            x: x, y: y, z: z,
            rx: rx, ry: ry, rz: rz,
            scale: uniformScale,
            color: "#c8c8c8",
            roughness: 0.38
        })
        selectObject(objectId)
        setStatus("Importing " + label + "…")
    }

    function selectedEditableGeometry() {
        if (!selectedObject || selectedObject.editorImported)
            return null
        return selectedObject.geometry
    }

    function clearMeshVertexSelection() {
        meshVertexSelection = []
    }

    function toggleMeshVertexSelection(index) {
        if (!selectedEditableGeometry())
            return

        const selection = meshVertexSelection.slice()
        const existingIndex = selection.indexOf(index)
        if (existingIndex >= 0)
            selection.splice(existingIndex, 1)
        else
            selection.push(index)

        meshVertexSelection = selection
        vertexIndexSpinner.value = index
        refreshMeshEditFields()
        setStatus(meshVertexSelection.length + " mesh vertex/vertices selected in boundary order.")
    }

    function selectMeshVertex(index) {
        if (!selectedEditableGeometry())
            return
        if (vertexSelectionMode) {
            toggleMeshVertexSelection(index)
            return
        }
        vertexIndexSpinner.value = Math.max(0, Math.min(selectedObject.geometry.vertexCount - 1, index))
        refreshMeshEditFields()
        setStatus("Selected vertex " + vertexIndexSpinner.value + ".")
    }

    function createFaceFromSelectedVertices() {
        const geometry = selectedEditableGeometry()
        if (!geometry || meshVertexSelection.length < 3) {
            setStatus("Select at least three vertices in boundary order.")
            return
        }

        pushUndoSnapshot()
        if (!geometry.createFace(meshVertexSelection)) {
            setStatus("Face creation failed. Choose unique, coplanar vertices around a convex boundary.")
            return
        }

        faceIndexSpinner.value = geometry.faceCount - 1
        clearMeshVertexSelection()
        vertexSelectionMode = false
        refreshMeshEditFields()
        setStatus("Created a polygon face from the selected vertices.")
    }

    function selectMeshFace(index) {
        if (!selectedEditableGeometry())
            return
        faceIndexSpinner.value = Math.max(0, Math.min(selectedObject.geometry.faceCount - 1, index))
        refreshMeshEditFields()
        setStatus("Selected face " + faceIndexSpinner.value + ".")
    }

    function refreshMeshEditFields() {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        const vertex = geometry.vertexPosition(vertexIndexSpinner.value)
        vertexXField.text = Number(vertex.x).toFixed(3)
        vertexYField.text = Number(vertex.y).toFixed(3)
        vertexZField.text = Number(vertex.z).toFixed(3)
        meshVertexCountLabel.text = String(geometry.vertexCount) + " vertices"
        meshFaceCountLabel.text = String(geometry.faceCount) + " faces"

        if (faceIndexSpinner.value < geometry.faceCount) {
            const center = geometry.faceCenter(faceIndexSpinner.value)
            const normal = geometry.faceNormal(faceIndexSpinner.value)
            faceInfoLabel.text = "Center  " + center.x.toFixed(2) + ", "
                + center.y.toFixed(2) + ", " + center.z.toFixed(2)
                + "\\nNormal  " + normal.x.toFixed(2) + ", "
                + normal.y.toFixed(2) + ", " + normal.z.toFixed(2)
        } else {
            faceInfoLabel.text = "No face selected."
        }
    }

    function openMeshEditor() {
        const geometry = selectedEditableGeometry()
        if (!geometry) {
            setStatus("Select a built-in mesh object first.")
            return
        }
        vertexIndexSpinner.value = Math.min(vertexIndexSpinner.value, geometry.vertexCount - 1)
        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        refreshMeshEditFields()
        meshEditDialog.open()
    }

    function updateMeshVertex(axis, inputText) {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        const value = Number(inputText)
        if (!Number.isFinite(value) || Math.abs(value) > 1000000) {
            refreshMeshEditFields()
            setStatus("Enter a finite vertex coordinate between -1,000,000 and 1,000,000.")
            return
        }

        const current = geometry.vertexPosition(vertexIndexSpinner.value)
        let x = current.x
        let y = current.y
        let z = current.z
        if (axis === "x") x = value
        else if (axis === "y") y = value
        else if (axis === "z") z = value

        pushUndoSnapshot()
        if (!geometry.setVertexPosition(vertexIndexSpinner.value, Qt.vector3d(x, y, z))) {
            setStatus("The vertex update was rejected.")
            return
        }
        refreshMeshEditFields()
        setStatus("Updated vertex " + vertexIndexSpinner.value + ".")
    }

    function extrudeSelectedFace() {
        const geometry = selectedEditableGeometry()
        const distance = Number(extrudeDistanceField.text)
        if (!geometry || !Number.isFinite(distance)) {
            setStatus("Select a built-in mesh and enter a valid extrusion distance.")
            return
        }

        pushUndoSnapshot()
        if (!geometry.extrudeFace(faceIndexSpinner.value, distance)) {
            setStatus("Face extrusion failed. Choose a valid face and a non-zero distance.")
            return
        }

        refreshMeshEditFields()
        setStatus("Extruded face and updated mesh topology.")
    }

    function insetSelectedFace() {
        const geometry = selectedEditableGeometry()
        const distance = Number(extrudeDistanceField.text)
        if (!geometry || !Number.isFinite(distance) || distance <= 0) {
            setStatus("Choose a built-in mesh and a positive inset distance.")
            return
        }

        pushUndoSnapshot()
        if (!geometry.insetFace(faceIndexSpinner.value, distance)) {
            setStatus("Inset failed. Use a smaller distance on a convex, planar face.")
            return
        }

        refreshMeshEditFields()
        setStatus("Inset face and updated mesh topology.")
    }

    function deleteSelectedFace() {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        if (geometry.faceCount <= 1) {
            setStatus("A mesh must keep at least one face.")
            return
        }

        pushUndoSnapshot()
        if (!geometry.deleteFace(faceIndexSpinner.value)) {
            setStatus("Face deletion was rejected; the mesh has not changed.")
            return
        }

        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        vertexIndexSpinner.value = Math.min(vertexIndexSpinner.value, geometry.vertexCount - 1)
        mergeTargetSpinner.value = Math.min(mergeTargetSpinner.value, geometry.vertexCount - 1)
        clearMeshVertexSelection()
        vertexSelectionMode = false
        refreshMeshEditFields()
        setStatus("Deleted face and compacted unused mesh vertices.")
    }

    function mergeSelectedVertices() {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        const sourceIndex = vertexIndexSpinner.value
        const targetIndex = mergeTargetSpinner.value
        if (sourceIndex === targetIndex) {
            setStatus("Choose two different vertex indices to merge.")
            return
        }

        pushUndoSnapshot()
        if (!geometry.mergeVertices(sourceIndex, targetIndex)) {
            setStatus("Vertex merge failed; the mesh was left unchanged.")
            return
        }

        vertexIndexSpinner.value = Math.min(targetIndex, geometry.vertexCount - 1)
        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        mergeTargetSpinner.value = Math.min(vertexIndexSpinner.value, geometry.vertexCount - 1)
        clearMeshVertexSelection()
        vertexSelectionMode = false
        refreshMeshEditFields()
        setStatus("Merged vertex " + sourceIndex + " into vertex " + targetIndex + ".")
    }

    function updateTransform(axis, inputText) {
        if (!selectedObject || selectedIndex < 0)
            return

        const value = Number(inputText)
        if (!Number.isFinite(value)) {
            refreshInspector()
            setStatus("Enter a valid number.")
            return
        }

        pushUndoSnapshot()
        const p = selectedObject.position
        const r = selectedObject.eulerRotation
        let x = p.x, y = p.y, z = p.z
        let rx = r.x, ry = r.y, rz = r.z
        if (axis === "x") x = value
        else if (axis === "y") y = value
        else if (axis === "z") z = value
        else if (axis === "rx") rx = value
        else if (axis === "ry") ry = value
        else if (axis === "rz") rz = value

        if (axis === "scale") {
            const scale = Math.max(0.01, Math.min(1000, value))
            selectedObject.uniformScale = scale
            selectedObject.scale = Qt.vector3d(scale, scale, scale)
            objectList.setProperty(selectedIndex, "scale", scale)
        } else {
            selectedObject.position = Qt.vector3d(x, y, z)
            selectedObject.eulerRotation = Qt.vector3d(rx, ry, rz)
            objectList.setProperty(selectedIndex, "x", x)
            objectList.setProperty(selectedIndex, "y", y)
            objectList.setProperty(selectedIndex, "z", z)
            objectList.setProperty(selectedIndex, "rx", rx)
            objectList.setProperty(selectedIndex, "ry", ry)
            objectList.setProperty(selectedIndex, "rz", rz)
        }

        refreshInspector()
        Qt.callLater(root.updateGizmo)
    }

    function setMaterialColorByIndex(index) {
        if (!selectedObject || selectedIndex < 0)
            return

        pushUndoSnapshot()
        if (selectedObject.editorImported)
            return

        const palette = ["#c8c8c8", "#f2f2f2", "#555555", "#df5b57", "#508bdc", "#d8ae57"]
        const colorValue = palette[Math.max(0, Math.min(palette.length - 1, index))]
        selectedObject.editorColor = colorValue
        objectList.setProperty(selectedIndex, "color", colorValue)
        setStatus("Updated material color for " + selectedObject.editorLabel + ".")
    }

    function setMaterialRoughness(value) {
        if (!selectedObject || selectedIndex < 0)
            return

        if (selectedObject.editorImported)
            return

        const roughness = Math.max(0, Math.min(1, value))
        selectedObject.editorRoughness = roughness
        objectList.setProperty(selectedIndex, "roughness", roughness)
    }

    function duplicateSelected() {
        if (!selectedObject || selectedIndex < 0)
            return

        pushUndoSnapshot()
        const row = objectList.get(selectedIndex)
        const copy = {
            label: row.label + " Copy",
            x: row.x + 125,
            y: row.y,
            z: row.z,
            rx: row.rx,
            ry: row.ry,
            rz: row.rz,
            scale: row.scale,
            color: row.color,
            roughness: row.roughness
        }
        if (row.kind === "asset") {
            addImportedAsset(row.source, copy)
        } else {
            copy.mesh = selectedObject.geometry.serializeMesh()
            addPrimitive(row.shape, copy)
        }
    }

    function removeSelected() {
        if (!selectedObject || selectedIndex < 0)
            return

        pushUndoSnapshot()
        const removedName = selectedObject.editorLabel
        const removedId = selectedObject.editorId
        selectedObject.destroy()

        const remaining = []
        for (let i = 0; i < sceneObjects.length; ++i) {
            if (sceneObjects[i].editorId !== removedId)
                remaining.push(sceneObjects[i])
        }
        sceneObjects = remaining
        objectList.remove(selectedIndex)
        selectedObject = null
        selectedIndex = -1
        if (objectList.count > 0)
            selectObject(objectList.get(Math.min(objectList.count - 1, 0)).objectId)
        else
            refreshInspector()

        setStatus("Removed " + removedName + ".")
    }

    function clearScene() {
        for (let i = 0; i < sceneObjects.length; ++i) {
            if (sceneObjects[i])
                sceneObjects[i].destroy()
        }
        sceneObjects = []
        objectList.clear()
        selectedObject = null
        selectedIndex = -1
        nextObjectNumber = 1
        refreshInspector()
    }

    function createNewScene() {
        if (editorReady)
            pushUndoSnapshot()

        clearScene()
        addPrimitive("Cube", {id: "object-1", label: "Cube 1", x: -60, y: 0, z: 0, scale: 1, color: "#c8c8c8"})
        addPrimitive("Sphere", {id: "object-2", label: "Sphere 2", x: 65, y: 0, z: 0, scale: 1, color: "#508bdc"})
        orbitYaw = 34
        orbitPitch = 23
        orbitDistance = 470
        camera.fieldOfView = 45
        keyLight.brightness = 1.7
        fillLight.brightness = 65
        fillLight.color = "#ffffff"
        fillTintCombo.currentIndex = 0
        updateCamera()
        setStatus("New scene ready.")
    }

    function meshDataForObject(objectId) {
        for (let i = 0; i < sceneObjects.length; ++i) {
            const object = sceneObjects[i]
            if (object && object.editorId === objectId && !object.editorImported
                    && object.geometry && typeof object.geometry.serializeMesh === "function")
                return object.geometry.serializeMesh()
        }
        return null
    }

    function serializeScene() {
        const serializedObjects = []
        for (let i = 0; i < objectList.count; ++i) {
            const row = objectList.get(i)
            serializedObjects.push({
                id: row.objectId,
                label: row.label,
                shape: row.shape,
                kind: row.kind || "primitive",
                source: row.source || "",
                mesh: row.kind === "asset" ? null : meshDataForObject(row.objectId),
                x: row.x, y: row.y, z: row.z,
                rx: row.rx, ry: row.ry, rz: row.rz,
                scale: row.scale,
                color: row.color,
                roughness: row.roughness
            })
        }

        return {
            format: "qalyvyron.scene",
            version: 1,
            title: "QALYVYRON Scene",
            settings: {
                cameraFov: camera.fieldOfView,
                keyLightBrightness: keyLight.brightness,
                fillLightBrightness: fillLight.brightness,
                fillTintIndex: fillTintCombo.currentIndex,
                orbitYaw: root.orbitYaw,
                orbitPitch: root.orbitPitch,
                orbitDistance: root.orbitDistance
            },
            objects: serializedObjects
        }
    }

    function applyProject(document) {
        const previousHistoryLock = historyLocked
        historyLocked = true
        clearScene()

        // Start with known defaults so older scene files remain predictable.
        camera.fieldOfView = 45
        keyLight.brightness = 1.7
        fillLight.brightness = 65
        fillLight.color = "#ffffff"
        fillTintCombo.currentIndex = 0
        orbitYaw = 34
        orbitPitch = 23
        orbitDistance = 470

        const settings = document.settings || ({})
        if (settings.cameraFov !== undefined && Number.isFinite(Number(settings.cameraFov)))
            camera.fieldOfView = Math.max(25, Math.min(80, Number(settings.cameraFov)))
        if (settings.keyLightBrightness !== undefined && Number.isFinite(Number(settings.keyLightBrightness)))
            keyLight.brightness = Math.max(0, Math.min(4, Number(settings.keyLightBrightness)))
        if (settings.fillLightBrightness !== undefined && Number.isFinite(Number(settings.fillLightBrightness)))
            fillLight.brightness = Math.max(0, Math.min(150, Number(settings.fillLightBrightness)))
        if (settings.fillTintIndex !== undefined) {
            const tintIndex = Math.max(0, Math.min(2, Number(settings.fillTintIndex) || 0))
            fillTintCombo.currentIndex = tintIndex
            if (tintIndex === 1)
                fillLight.color = "#ffd6ad"
            else if (tintIndex === 2)
                fillLight.color = "#b8d6ff"
            else
                fillLight.color = "#ffffff"
        }
        if (settings.orbitYaw !== undefined && Number.isFinite(Number(settings.orbitYaw)))
            orbitYaw = Number(settings.orbitYaw)
        if (settings.orbitPitch !== undefined && Number.isFinite(Number(settings.orbitPitch)))
            orbitPitch = Math.max(-80, Math.min(80, Number(settings.orbitPitch)))
        if (settings.orbitDistance !== undefined && Number.isFinite(Number(settings.orbitDistance)))
            orbitDistance = Math.max(190, Math.min(1050, Number(settings.orbitDistance)))

        const savedObjects = document.objects || []
        for (let i = 0; i < savedObjects.length; ++i) {
            const entry = savedObjects[i]
            if (entry.kind === "asset" && entry.source) {
                addImportedAsset(entry.source, entry)
                continue
            }
            const isCustomMesh = entry.shape === "Custom" && entry.mesh
                                 && entry.mesh.vertices && entry.mesh.faces
            const shape = ["Cube", "Sphere", "Cylinder", "Cone", "Plane"].indexOf(entry.shape) >= 0
                         ? entry.shape : (isCustomMesh ? "Custom" : "Cube")
            addPrimitive(shape, entry)
        }

        if (objectList.count === 0)
            addPrimitive("Cube", {label: "Cube 1"})

        updateCamera()
        selectObject(objectList.get(0).objectId)
        historyLocked = previousHistoryLock
        setStatus("Opened scene: " + savedObjects.length + " object(s).")
    }

    function exportViewportPreview(fileUrl) {
        const localPath = fileUrl.toLocalFile()
        if (!localPath) {
            setStatus("Choose a local PNG file path.")
            return
        }

        setStatus("Capturing viewport preview…")
        view3d.grabToImage(function(result) {
            if (result.saveToFile(localPath))
                root.setStatus("Viewport preview exported to " + localPath)
            else
                root.setStatus("Could not write the preview PNG.")
        })
    }

    Component {
        id: importedAssetComponent

        Node {
            id: importedRoot
            property string editorId: ""
            property string editorLabel: "Imported Asset"
            property string editorShape: "Imported Asset"
            property bool editorImported: true
            property color editorColor: "#c8c8c8"
            property real editorRoughness: 0.38
            property real uniformScale: 1.0
            property url assetUrl: ""

            RuntimeLoader {
                id: runtimeAssetLoader
                source: importedRoot.assetUrl
                onStatusChanged: {
                    if (status === RuntimeLoader.Error)
                        root.setStatus("Asset import failed: " + errorString)
                    else if (status === RuntimeLoader.Success)
                        root.setStatus("Imported " + importedRoot.editorLabel + ".")
                }
            }
        }
    }

    Component {
        id: primitiveComponent

        Model {
            id: editableModel
            property string editorId: ""
            property string editorLabel: "Object"
            property string editorShape: "Cube"
            property color editorColor: "#c8c8c8"
            property real editorRoughness: 0.38
            property real uniformScale: 1.0
            property var editorMeshData: ({})

            geometry: EditableGeometry {
                id: editableGeometry
                Component.onCompleted: {
                    // Set a correct base shape first, then restore custom topology if present.
                    setShape(editableModel.editorShape)
                    const savedMesh = editableModel.editorMeshData
                    if (savedMesh && Object.keys(savedMesh).length > 0
                            && !loadMesh(savedMesh)) {
                        setShape(editableModel.editorShape)
                        root.setStatus("Saved mesh data was invalid; the base primitive was loaded.")
                    }
                }
            }
            pickable: true
            materials: [
                PrincipledMaterial {
                    baseColor: editableModel.editorColor
                    roughness: editableModel.editorRoughness
                    metalness: 0.08
                }
            ]

            // Selection markers live in the model's local space, so they follow
            // its transform instead of drifting when the object is moved/rotated.
            Model {
                source: "#Sphere"
                visible: meshEditDialog.visible
                         && root.selectedObject === editableModel
                         && editableModel.geometry
                         && editableModel.geometry.vertexCount > 0
                position: editableModel.geometry
                          ? editableModel.geometry.vertexPosition(vertexIndexSpinner.value)
                          : Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.10, 0.10, 0.10)
                pickable: false
                materials: [
                    PrincipledMaterial {
                        baseColor: "#ffffff"
                        roughness: 0.25
                    }
                ]
            }

            Model {
                source: "#Sphere"
                visible: meshEditDialog.visible
                         && root.selectedObject === editableModel
                         && editableModel.geometry
                         && editableModel.geometry.faceCount > 0
                position: editableModel.geometry
                          ? editableModel.geometry.faceCenter(faceIndexSpinner.value)
                          : Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.13, 0.13, 0.13)
                pickable: false
                materials: [
                    PrincipledMaterial {
                        baseColor: "#777777"
                        roughness: 0.45
                    }
                ]
            }
        }
    }

    Dialog {
        id: meshEditDialog
        title: "Edit Mesh"
        // Keep the editor palette non-modal so vertex/face handles behind
        // it remain clickable in the 3D viewport.
        modal: false
        dim: false
        focus: true
        width: 560
        height: 540
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.Close

        contentItem: ScrollView {
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 13

                Text {
                    Layout.fillWidth: true
                    text: root.selectedObject ? root.selectedObject.editorLabel : "No object selected"
                    color: root.mainText
                    font.pixelSize: 16
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        id: meshVertexCountLabel
                        text: "0 vertices"
                        color: root.secondaryText
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        id: meshFaceCountLabel
                        text: "0 faces"
                        color: root.secondaryText
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.borderColor
                }

                Text {
                    text: "VERTEX POSITION"
                    color: "#bcbcbc"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Vertex"; color: root.secondaryText; font.pixelSize: 11 }
                    SpinBox {
                        id: vertexIndexSpinner
                        from: 0
                        to: Math.max(0, root.selectedObject && !root.selectedObject.editorImported
                                      ? root.selectedObject.geometry.vertexCount - 1 : 0)
                        editable: true
                        onValueChanged: if (meshEditDialog.visible) root.refreshMeshEditFields()
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        Layout.fillWidth: true
                        text: root.vertexSelectionMode ? "Vertex Selection: ON" : "Select Face Vertices"
                        highlighted: root.vertexSelectionMode
                        onClicked: {
                            root.vertexSelectionMode = !root.vertexSelectionMode
                            if (!root.vertexSelectionMode)
                                root.clearMeshVertexSelection()
                            root.setStatus(root.vertexSelectionMode
                                           ? "Click vertex markers in boundary order, then create a face."
                                           : "Vertex multi-selection mode closed.")
                        }
                    }
                    Button {
                        text: "Clear"
                        enabled: root.meshVertexSelection.length > 0
                        onClicked: root.clearMeshVertexSelection()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.meshVertexSelection.length + " vertices selected"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }
                    Button {
                        text: "Create Face"
                        enabled: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                                 && root.meshVertexSelection.length >= 3
                        onClicked: root.createFaceFromSelectedVertices()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 7

                    Text { text: "X"; color: root.secondaryText; font.pixelSize: 11 }
                    TextField {
                        id: vertexXField
                        Layout.fillWidth: true
                        text: "0"
                        selectByMouse: true
                        validator: DoubleValidator { }
                        onEditingFinished: root.updateMeshVertex("x", text)
                    }
                    Text { text: "Y"; color: root.secondaryText; font.pixelSize: 11 }
                    TextField {
                        id: vertexYField
                        Layout.fillWidth: true
                        text: "0"
                        selectByMouse: true
                        validator: DoubleValidator { }
                        onEditingFinished: root.updateMeshVertex("y", text)
                    }
                    Text { text: "Z"; color: root.secondaryText; font.pixelSize: 11 }
                    TextField {
                        id: vertexZField
                        Layout.fillWidth: true
                        text: "0"
                        selectByMouse: true
                        validator: DoubleValidator { }
                        onEditingFinished: root.updateMeshVertex("z", text)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.borderColor
                }

                Text {
                    text: "MERGE VERTICES"
                    color: "#bcbcbc"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Merge current vertex into"
                        color: root.secondaryText
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    SpinBox {
                        id: mergeTargetSpinner
                        from: 0
                        to: Math.max(0, root.selectedObject && !root.selectedObject.editorImported
                                      ? root.selectedObject.geometry.vertexCount - 1 : 0)
                        value: 1
                        editable: true
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Merge Vertices"
                    enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                    onClicked: root.mergeSelectedVertices()
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.borderColor
                }

                Text {
                    text: "FACE TOOLS"
                    color: "#bcbcbc"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Face index"; color: root.secondaryText; font.pixelSize: 11 }
                    SpinBox {
                        id: faceIndexSpinner
                        from: 0
                        to: Math.max(0, root.selectedObject && !root.selectedObject.editorImported
                                      ? root.selectedObject.geometry.faceCount - 1 : 0)
                        editable: true
                        onValueChanged: if (meshEditDialog.visible) root.refreshMeshEditFields()
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    id: faceInfoLabel
                    Layout.fillWidth: true
                    text: "Select a face to inspect its center and normal."
                    color: root.secondaryText
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Distance"; color: root.secondaryText; font.pixelSize: 11 }
                    TextField {
                        id: extrudeDistanceField
                        Layout.preferredWidth: 130
                        text: "25"
                        selectByMouse: true
                        validator: DoubleValidator { bottom: -1000000; top: 1000000 }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Extrude Face"
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        onClicked: root.extrudeSelectedFace()
                    }
                    Button {
                        text: "Inset Face"
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        onClicked: root.insetSelectedFace()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "TOPOLOGY"
                        color: root.secondaryText
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.0
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Delete Face"
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        onClicked: root.deleteSelectedFace()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Enable Select Face Vertices, then click vertex markers in boundary order and choose Create Face. Other tools edit coordinates, merge vertices, extrude/inset faces, and delete faces. Bevel, edge selection, loops and advanced topology still need implementation."
                    color: "#777777"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        onOpened: Qt.callLater(root.refreshMeshEditFields)
    }

    FileDialog {
        id: importAssetDialog
        title: "Import 3D Asset"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Supported 3D assets (*.glb *.gltf *.obj)",
            "glTF binary (*.glb)",
            "glTF scenes (*.gltf)",
            "Wavefront OBJ (*.obj)"
        ]

        onAccepted: root.createImportedAsset(selectedFile)
    }

    FileDialog {
        id: saveSceneDialog
        title: "Save QALYVYRON Scene"
        fileMode: FileDialog.SaveFile
        nameFilters: ["QALYVYRON Scene (*.qvscene)", "JSON files (*.json)"]
        defaultSuffix: "qvscene"

        onAccepted: {
            const result = projectIO.saveProject(selectedFile, root.serializeScene())
            if (result.ok)
                root.setStatus("Scene saved: " + result.path)
            else
                root.setStatus("Save failed: " + result.error)
        }
    }

    FileDialog {
        id: openSceneDialog
        title: "Open QALYVYRON Scene"
        fileMode: FileDialog.OpenFile
        nameFilters: ["QALYVYRON Scene (*.qvscene *.json)", "All files (*)"]

        onAccepted: {
            const result = projectIO.loadProject(selectedFile)
            if (result.ok) {
                root.clearHistory()
                root.applyProject(result.data)
            } else {
                root.setStatus("Open failed: " + result.error)
            }
        }
    }

    FileDialog {
        id: exportObjDialog
        title: "Export Editable Meshes as Wavefront OBJ"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Wavefront OBJ (*.obj)"]
        defaultSuffix: "obj"

        onAccepted: {
            const result = projectIO.exportObj(selectedFile, root.serializeScene())
            if (!result.ok) {
                root.setStatus("OBJ export failed: " + result.error)
            } else if (result.warning) {
                root.setStatus("OBJ exported to " + result.path + ". " + result.warning)
            } else {
                root.setStatus("OBJ exported to " + result.path)
            }
        }
    }

    FileDialog {
        id: exportImageDialog
        title: "Export Viewport Preview"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG image (*.png)"]
        defaultSuffix: "png"

        onAccepted: root.exportViewportPreview(selectedFile)
    }

    Timer {
        id: statusTimer
        interval: 7000
        repeat: false
        onTriggered: statusLabel.text = "Ready"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: "#101010"
            border.color: root.borderColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 12

                Column {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 1
                    Text {
                        text: "QALYVYRON"
                        color: root.mainText
                        font.pixelSize: 18
                        font.bold: true
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "3D CREATION WORKSPACE"
                        color: root.secondaryText
                        font.pixelSize: 9
                        font.letterSpacing: 1.1
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 30
                    color: root.borderColor
                }

                Button {
                    text: "New"
                    onClicked: root.createNewScene()
                }
                Button {
                    text: "Open"
                    onClicked: openSceneDialog.open()
                }
                Button {
                    text: "Save"
                    onClicked: saveSceneDialog.open()
                }
                Button {
                    text: "Undo"
                    enabled: root.undoStack.length > 0
                    onClicked: root.undo()
                }
                Button {
                    text: "Redo"
                    enabled: root.redoStack.length > 0
                    onClicked: root.redo()
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "VISUAL-FIRST WORKSPACE"
                    color: "#707070"
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Button {
                    text: "Export OBJ"
                    onClicked: exportObjDialog.open()
                }
                Button {
                    text: "Export Preview"
                    highlighted: true
                    onClicked: exportImageDialog.open()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 218
                Layout.fillHeight: true
                color: root.panelColor
                border.color: root.borderColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    Text {
                        text: "CREATE"
                        color: root.secondaryText
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.8
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 7
                        columnSpacing: 7

                        Button {
                            Layout.fillWidth: true
                            text: "+ Cube"
                            onClicked: root.createPrimitive("Cube")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Sphere"
                            onClicked: root.createPrimitive("Sphere")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Cylinder"
                            onClicked: root.createPrimitive("Cylinder")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Cone"
                            onClicked: root.createPrimitive("Cone")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Plane"
                            onClicked: root.createPrimitive("Plane")
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Import 3D Asset"
                        onClicked: importAssetDialog.open()
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Imports local GLB, glTF 2.0 and OBJ files."
                        color: "#707070"
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "SCENE OBJECTS"
                            color: root.secondaryText
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1.4
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: String(objectList.count)
                            color: root.secondaryText
                            font.pixelSize: 10
                        }
                    }

                    ListView {
                        id: hierarchyView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: objectList

                        delegate: Rectangle {
                            required property int index
                            required property string objectId
                            required property string label
                            required property string shape

                            width: hierarchyView.width
                            height: 39
                            radius: 3
                            color: root.selectedIndex === index ? "#292929" : "transparent"
                            border.color: root.selectedIndex === index ? "#666666" : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 7
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 5
                                    Layout.preferredHeight: 20
                                    radius: 2
                                    color: root.selectedIndex === index ? "#efefef" : "#555555"
                                }

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: label
                                        color: root.selectedIndex === index ? root.mainText : "#b6b6b6"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: shape
                                        color: "#777777"
                                        font.pixelSize: 9
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectObject(objectId)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: objectList.count === 0
                            text: "No objects yet"
                            color: "#666666"
                            font.pixelSize: 11
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Edit Selected Mesh"
                        enabled: root.selectedIndex >= 0 && root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                        onClicked: root.openMeshEditor()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Duplicate Selected"
                        enabled: root.selectedIndex >= 0
                        onClicked: root.duplicateSelected()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Remove Selected"
                        enabled: root.selectedIndex >= 0
                        onClicked: root.removeSelected()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#151515"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        color: "#121212"
                        border.color: root.borderColor

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 13
                            spacing: 10

                            Text {
                                text: "VIEWPORT"
                                color: "#bdbdbd"
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1.5
                            }
                            Rectangle {
                                width: 5
                                height: 5
                                radius: 3
                                color: "#77b88a"
                            }
                            Text {
                                text: "LIVE 3D"
                                color: "#8c8c8c"
                                font.pixelSize: 9
                                font.letterSpacing: 1
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Right-drag: orbit    •    Wheel: zoom    •    Click: select"
                                color: "#7a7a7a"
                                font.pixelSize: 10
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        View3D {
                            id: view3d
                            anchors.fill: parent
                            environment: SceneEnvironment {
                                backgroundMode: SceneEnvironment.Color
                                clearColor: "#171717"
                                antialiasingMode: SceneEnvironment.MSAA
                                antialiasingQuality: SceneEnvironment.High
                            }

                            Node {
                                id: sceneRoot

                                Node {
                                    id: focusTarget
                                    position: Qt.vector3d(0, 0, 0)
                                }

                                AxisHelper {
                                    position: Qt.vector3d(0, -65, 0)
                                    enableAxisLines: true
                                    enableXZGrid: true
                                    enableXYGrid: false
                                    enableYZGrid: false
                                    gridColor: "#777777"
                                    gridOpacity: 0.16
                                }
                            }

                            PerspectiveCamera {
                                id: camera
                                clipNear: 1
                                clipFar: 4000
                                fieldOfView: 45
                            }

                            DirectionalLight {
                                id: keyLight
                                eulerRotation: Qt.vector3d(-42, -35, 0)
                                brightness: 1.7
                                castsShadow: true
                            }

                            PointLight {
                                id: fillLight
                                position: Qt.vector3d(-160, 190, 140)
                                brightness: 65
                                color: "#ffffff"
                                quadraticFade: 0.00008
                            }

                            Component.onCompleted: root.updateCamera()
                        }

                        MouseArea {
                            id: viewportMouse
                            anchors.fill: view3d
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            hoverEnabled: true
                            property real lastX: 0
                            property real lastY: 0
                            property bool orbiting: false
                            property bool movingSelection: false
                            property real selectionDepth: 1
                            property var movingObject: null
                            property var dragStartPosition: Qt.vector3d(0, 0, 0)
                            property var dragStartPoint: Qt.vector3d(0, 0, 0)
                            cursorShape: orbiting ? Qt.ClosedHandCursor : movingSelection ? Qt.SizeAllCursor : Qt.ArrowCursor

                            property bool dragHistorySaved: false

                            onPressed: function(mouse) {
                                lastX = mouse.x
                                lastY = mouse.y
                                orbiting = mouse.button === Qt.RightButton
                                movingSelection = false
                                movingObject = null
                                dragHistorySaved = false
                                if (orbiting)
                                    root.pushUndoSnapshot()

                                if (mouse.button !== Qt.LeftButton)
                                    return

                                const hit = view3d.pick(mouse.x, mouse.y)
                                const hitId = hit.objectHit ? root.editorIdFromObject(hit.objectHit) : ""
                                if (hitId.length > 0) {
                                    root.selectObject(hitId)
                                    movingObject = root.selectedObject
                                    if (movingObject) {
                                        const projected = view3d.mapFrom3DScene(movingObject.position)
                                        selectionDepth = Math.max(1, projected.z)
                                        dragStartPosition = movingObject.position
                                        dragStartPoint = view3d.mapTo3DScene(
                                            Qt.vector3d(mouse.x, mouse.y, selectionDepth))
                                        movingSelection = true
                                    }
                                }
                            }

                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return

                                if (orbiting) {
                                    const dx = mouse.x - lastX
                                    const dy = mouse.y - lastY
                                    lastX = mouse.x
                                    lastY = mouse.y
                                    root.orbitYaw -= dx * 0.45
                                    root.orbitPitch = Math.max(-80, Math.min(80, root.orbitPitch + dy * 0.35))
                                    root.updateCamera()
                                    return
                                }

                                if (!movingSelection || !movingObject)
                                    return

                                if (!dragHistorySaved) {
                                    root.pushUndoSnapshot()
                                    dragHistorySaved = true
                                }

                                const point = view3d.mapTo3DScene(
                                    Qt.vector3d(mouse.x, mouse.y, selectionDepth))
                                const start = dragStartPosition
                                const anchor = dragStartPoint
                                movingObject.position = Qt.vector3d(
                                    start.x + point.x - anchor.x,
                                    start.y + point.y - anchor.y,
                                    start.z + point.z - anchor.z
                                )

                                if (root.selectedIndex >= 0) {
                                    objectList.setProperty(root.selectedIndex, "x", movingObject.position.x)
                                    objectList.setProperty(root.selectedIndex, "y", movingObject.position.y)
                                    objectList.setProperty(root.selectedIndex, "z", movingObject.position.z)
                                    root.refreshInspector()
                                }
                            }

                            onReleased: {
                                orbiting = false
                                movingSelection = false
                                movingObject = null
                                dragHistorySaved = false
                                Qt.callLater(root.updateGizmo)
                            }

                            onClicked: function(mouse) {
                                if (mouse.button !== Qt.LeftButton)
                                    return

                                const result = view3d.pick(mouse.x, mouse.y)
                                const hitId = result.objectHit ? root.editorIdFromObject(result.objectHit) : ""
                                if (hitId.length > 0)
                                    root.selectObject(hitId)
                                else
                                    root.selectObject("")
                            }
                        }

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse
                            onWheel: function(event) {
                                root.pushUndoSnapshot()
                                root.orbitDistance = Math.max(190, Math.min(1050,
                                    root.orbitDistance - event.angleDelta.y * 0.35))
                                root.updateCamera()
                            }
                        }

                        // Clickable screen-space handles make the first mesh tools
                        // usable directly in the viewport rather than only by index.
                        Repeater {
                            id: meshVertexHandles
                            model: meshEditDialog.visible && root.selectedEditableGeometry()
                                   ? root.selectedEditableGeometry().vertexCount : 0

                            delegate: Rectangle {
                                required property int index
                                readonly property var projectedPosition: {
                                    if (!root.selectedObject || !root.selectedEditableGeometry())
                                        return Qt.vector3d(-1000, -1000, -1)
                                    // Reading a NOTIFY-backed property makes this projected
                                    // handle refresh after coordinates change, even if the count
                                    // of vertices remains the same.
                                    const meshChangeTrigger = root.selectedObject.geometry.vertexCount
                                    const local = root.selectedObject.geometry.vertexPosition(index)
                                    const scene = root.selectedObject.mapPositionToScene(local)
                                    return view3d.mapFrom3DScene(scene)
                                }
                                readonly property bool isVertexSelected: root.meshVertexSelection.indexOf(index) >= 0
                                width: isVertexSelected ? 14 : (index === vertexIndexSpinner.value ? 13 : 9)
                                height: width
                                radius: width / 2
                                x: projectedPosition.x - width / 2
                                y: projectedPosition.y - height / 2
                                z: 30
                                visible: meshEditDialog.visible
                                         && root.selectedObject !== null
                                         && !root.selectedObject.editorImported
                                         && projectedPosition.z >= 0
                                         && projectedPosition.x >= 0
                                         && projectedPosition.x <= view3d.width
                                         && projectedPosition.y >= 0
                                         && projectedPosition.y <= view3d.height
                                color: isVertexSelected ? "#ffffff"
                                       : (index === vertexIndexSpinner.value ? "#e5e5e5" : "#a0a0a0")
                                border.color: "#111111"
                                border.width: 1

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectMeshVertex(index)
                                }
                            }
                        }

                        Repeater {
                            id: meshFaceHandles
                            model: meshEditDialog.visible && root.selectedEditableGeometry()
                                   ? root.selectedEditableGeometry().faceCount : 0

                            delegate: Rectangle {
                                required property int index
                                readonly property var projectedPosition: {
                                    if (!root.selectedObject || !root.selectedEditableGeometry())
                                        return Qt.vector3d(-1000, -1000, -1)
                                    const meshChangeTrigger = root.selectedObject.geometry.faceCount
                                    const local = root.selectedObject.geometry.faceCenter(index)
                                    const scene = root.selectedObject.mapPositionToScene(local)
                                    return view3d.mapFrom3DScene(scene)
                                }
                                width: index === faceIndexSpinner.value ? 15 : 10
                                height: width
                                radius: 2
                                x: projectedPosition.x - width / 2
                                y: projectedPosition.y - height / 2
                                z: 29
                                visible: meshEditDialog.visible
                                         && root.selectedObject !== null
                                         && !root.selectedObject.editorImported
                                         && projectedPosition.z >= 0
                                         && projectedPosition.x >= 0
                                         && projectedPosition.x <= view3d.width
                                         && projectedPosition.y >= 0
                                         && projectedPosition.y <= view3d.height
                                color: index === faceIndexSpinner.value ? "#818181" : "#4c4c4c"
                                border.color: "#eeeeee"
                                border.width: 1

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectMeshFace(index)
                                }
                            }
                        }

                        Item {
                            id: transformGizmo
                            visible: root.selectedObject !== null && root.gizmoVisible
                            x: root.gizmoX - width / 2
                            y: root.gizmoY - height / 2
                            z: 20
                            width: 76
                            height: 76

                            Rectangle {
                                x: 12
                                y: parent.height / 2 - 1
                                width: parent.width - 24
                                height: 2
                                color: "#d6d6d6"
                                opacity: 0.9
                            }
                            Rectangle {
                                x: parent.width / 2 - 1
                                y: 12
                                width: 2
                                height: parent.height - 24
                                color: "#d6d6d6"
                                opacity: 0.9
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 15
                                height: 15
                                radius: 8
                                color: "#f4f4f4"
                                border.color: "#111111"
                                border.width: 2
                            }
                            Rectangle {
                                x: parent.width - 18
                                y: parent.height / 2 - 5
                                width: 10
                                height: 10
                                radius: 5
                                color: "#aaaaaa"
                                border.color: "#111111"
                                border.width: 1
                            }
                            Rectangle {
                                x: parent.width / 2 - 5
                                y: 8
                                width: 10
                                height: 10
                                radius: 5
                                color: "#aaaaaa"
                                border.color: "#111111"
                                border.width: 1
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                property var dragObject: null
                                property var initialObjectPosition: Qt.vector3d(0, 0, 0)
                                property var initialPointerPosition: Qt.vector3d(0, 0, 0)
                                property bool historySaved: false

                                onPressed: function(mouse) {
                                    historySaved = false
                                    dragObject = root.selectedObject
                                    if (!dragObject)
                                        return
                                    initialObjectPosition = dragObject.position
                                    initialPointerPosition = view3d.mapTo3DScene(Qt.vector3d(
                                        transformGizmo.x + mouse.x,
                                        transformGizmo.y + mouse.y,
                                        root.gizmoDepth
                                    ))
                                }

                                onPositionChanged: function(mouse) {
                                    if (!pressed || !dragObject)
                                        return
                                    if (!historySaved) {
                                        root.pushUndoSnapshot()
                                        historySaved = true
                                    }
                                    const pointer = view3d.mapTo3DScene(Qt.vector3d(
                                        transformGizmo.x + mouse.x,
                                        transformGizmo.y + mouse.y,
                                        root.gizmoDepth
                                    ))
                                    const start = initialObjectPosition
                                    const pointerStart = initialPointerPosition
                                    dragObject.position = Qt.vector3d(
                                        start.x + pointer.x - pointerStart.x,
                                        start.y + pointer.y - pointerStart.y,
                                        start.z + pointer.z - pointerStart.z
                                    )
                                    if (root.selectedIndex >= 0) {
                                        objectList.setProperty(root.selectedIndex, "x", dragObject.position.x)
                                        objectList.setProperty(root.selectedIndex, "y", dragObject.position.y)
                                        objectList.setProperty(root.selectedIndex, "z", dragObject.position.z)
                                    }
                                    root.refreshInspector()
                                }

                                onReleased: {
                                    dragObject = null
                                    historySaved = false
                                    root.updateGizmo()
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 14
                            width: 190
                            height: 54
                            radius: 4
                            color: "#bb101010"
                            border.color: "#343434"

                            Column {
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 4
                                Text {
                                    text: root.selectedObject ? root.selectedObject.editorLabel : "No selection"
                                    color: "#e8e8e8"
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                    width: 172
                                }
                                Text {
                                    text: root.selectedObject ? root.selectedObject.editorShape + "  /  editable" : "Select an object in the scene"
                                    color: "#858585"
                                    font.pixelSize: 10
                                }
                            }
                        }

                        Column {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 5

                            Repeater {
                                model: [
                                    {label: "FRONT", yaw: 0, pitch: 0},
                                    {label: "TOP", yaw: 0, pitch: 78},
                                    {label: "SIDE", yaw: 90, pitch: 0}
                                ]

                                delegate: Button {
                                    required property var modelData
                                    width: 66
                                    height: 27
                                    text: modelData.label
                                    onClicked: {
                                        root.orbitYaw = modelData.yaw
                                        root.orbitPitch = modelData.pitch
                                        root.updateCamera()
                                    }
                                }
                            }
                        }

                        Button {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 14
                            text: "Frame Scene"
                            onClicked: {
                                root.orbitYaw = 34
                                root.orbitPitch = 23
                                root.orbitDistance = 470
                                root.updateCamera()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: "#111111"
                        border.color: root.borderColor

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 13
                            Text {
                                text: "SCENE"
                                color: "#c4c4c4"
                                font.pixelSize: 9
                                font.bold: true
                                font.letterSpacing: 1.2
                            }
                            Text {
                                text: "Primitive editing"
                                color: "#7e7e7e"
                                font.pixelSize: 10
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Viewport export is a preview, not a final offline render"
                                color: "#8f8f8f"
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 278
                Layout.fillHeight: true
                color: root.panelColor
                border.color: root.borderColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 12

                    Text {
                        text: "INSPECTOR"
                        color: root.mainText
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 1.5
                    }
                    Text {
                        text: root.selectedObject ? root.selectedObject.editorLabel : "Nothing selected"
                        color: root.secondaryText
                        font.pixelSize: 11
                    }

                    TextField {
                        id: objectNameField
                        Layout.fillWidth: true
                        text: ""
                        placeholderText: "Object name"
                        enabled: root.selectedObject !== null
                        selectByMouse: true
                        onEditingFinished: {
                            if (!root.selectedObject || root.selectedIndex < 0)
                                return

                            const newName = text.trim()
                            if (newName.length === 0) {
                                text = root.selectedObject.editorLabel
                                return
                            }

                            root.pushUndoSnapshot()
                            root.selectedObject.editorLabel = newName
                            objectList.setProperty(root.selectedIndex, "label", newName)
                            root.setStatus("Renamed object to " + newName + ".")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: "TRANSFORM"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    Text {
                        text: "Position"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Text { text: "X"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: xField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("x", text)
                        }
                        Text { text: "Y"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: yField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("y", text)
                        }
                        Text { text: "Z"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: zField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("z", text)
                        }
                    }

                    Text {
                        text: "Rotation (degrees)"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Text { text: "X"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: rxField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("rx", text)
                        }
                        Text { text: "Y"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: ryField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("ry", text)
                        }
                        Text { text: "Z"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: rzField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { }
                            onEditingFinished: root.updateTransform("rz", text)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Uniform scale"
                            color: root.secondaryText
                            font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                        TextField {
                            id: scaleField
                            Layout.preferredWidth: 92
                            text: "1"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: 0.01; top: 1000 }
                            onEditingFinished: root.updateTransform("scale", text)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: "MATERIAL"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    Text {
                        text: "Base color"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    ComboBox {
                        id: colorCombo
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        model: ["Silver", "White", "Graphite", "Red", "Blue", "Gold"]
                        onActivated: root.setMaterialColorByIndex(currentIndex)
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.selectedObject !== null && root.selectedObject.editorImported
                        text: "Imported asset materials are preserved from the source file. Direct material editing is not available yet."
                        color: "#777777"
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Roughness"
                            color: root.secondaryText
                            font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.selectedObject ? Number(root.selectedObject.editorRoughness).toFixed(2) : "0.38"
                            color: "#cecece"
                            font.pixelSize: 10
                        }
                    }

                    Slider {
                        id: roughnessSlider
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        from: 0
                        to: 1
                        value: 0.38
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: root.setMaterialRoughness(value)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Lower roughness creates a smoother surface. Higher roughness diffuses reflections."
                        color: "#6f6f6f"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: "LIGHTING"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Key light"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(keyLight.brightness).toFixed(1); color: "#cecece"; font.pixelSize: 10 }
                    }

                    Slider {
                        id: keyBrightnessSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 4
                        value: keyLight.brightness
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: keyLight.brightness = value
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Fill light"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(fillLight.brightness).toFixed(0); color: "#cecece"; font.pixelSize: 10 }
                    }

                    Slider {
                        id: fillBrightnessSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 150
                        value: fillLight.brightness
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: fillLight.brightness = value
                    }

                    ComboBox {
                        id: fillTintCombo
                        Layout.fillWidth: true
                        model: ["Neutral fill", "Warm fill", "Cool fill"]
                        onActivated: {
                            root.pushUndoSnapshot()
                            if (currentIndex === 0)
                                fillLight.color = "#ffffff"
                            else if (currentIndex === 1)
                                fillLight.color = "#ffd6ad"
                            else
                                fillLight.color = "#b8d6ff"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: "CAMERA"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Field of view"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(camera.fieldOfView).toFixed(0) + "°"; color: "#cecece"; font.pixelSize: 10 }
                    }

                    Slider {
                        id: cameraFovSlider
                        Layout.fillWidth: true
                        from: 25
                        to: 80
                        value: camera.fieldOfView
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: camera.fieldOfView = value
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 4
                        color: "#171717"
                        border.color: root.borderColor
                        implicitHeight: capabilityText.implicitHeight + 22

                        Text {
                            id: capabilityText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: "CURRENT BUILD\nEditable polygon meshes • indexed vertex editing • face extrusion • OBJ import/export • materials • transforms • scene files • viewport preview"
                            color: "#999999"
                            font.pixelSize: 10
                            lineHeight: 1.25
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#0c0c0c"
            border.color: root.borderColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: "#83bd94"
                }
                Text {
                    id: statusLabel
                    text: "Ready"
                    color: "#bcbcbc"
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    text: "QALYVYRON 0.1.0"
                    color: "#666666"
                    font.pixelSize: 9
                    font.letterSpacing: 1
                }
            }
        }
    }

    Component.onCompleted: {
        createNewScene()
        editorReady = true
        clearHistory()
    }
}
