// What this does: Builds the app window and the controls a person uses to work with scenes, edit geometry, and start a render.

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

    onClosing: function(closeEvent) {
        if (allowWindowClose || !projectDirty)
            return

        closeEvent.accepted = false
        pendingAction = "exit"
        pendingOpenUrl = ""
        pendingOpenData = null
        if (!unsavedChangesDialog.visible)
            unsavedChangesDialog.open()
    }

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
    // Parallel projection is useful for proportion checks and axis-aligned modeling views.
    property bool orthographicMode: false
    // One solid background colour is used by the viewport and the offline still renderer.
    property color backgroundColor: "#171717"
    // Transparent mode writes an alpha channel instead of filling empty pixels with the background colour.
    property bool transparentBackground: false
    // The fill light uses one saved position for the viewport and final renderer.
    property vector3d fillLightPosition: Qt.vector3d(-160, 190, 140)
    // Keep the fill-light colour in one place so viewport and final-render colours agree.
    property color fillLightColor: "#ffffff"
    // Euler angles define the key-light direction in both the viewport and final renderer.
    property vector3d keyLightRotation: Qt.vector3d(-42, -35, 0)
    property real gizmoX: 0
    property real gizmoY: 0
    property real gizmoDepth: 1
    property bool gizmoVisible: false
    property var undoStack: []
    property var redoStack: []
    property int historyLimit: 80
    property bool historyLocked: false
    property bool editorReady: false
    property bool beginnerMode: true
    // Keep the hierarchy searchable when a scene grows beyond a few objects.
    property string hierarchyFilter: ""
    property int tutorialStep: 0
    property var meshVertexSelection: []
    property bool vertexSelectionMode: false
    property var meshEdgeSelection: []
    property bool edgeSelectionMode: false
    // Grid snapping helps place objects neatly without typing coordinates.
    // It only affects mouse dragging; exact numeric transform fields stay exact.
    property bool snapMovementToGrid: false
    property real movementSnapStep: 10
    // State for axis-constrained movement. The gizmo stays still while its handle is dragged.
    property bool axisMoveActive: false
    property string axisMoveName: ""
    property var axisMoveStartPosition: Qt.vector3d(0, 0, 0)
    property point axisMoveStartPointer: Qt.point(0, 0)
    property point axisMoveProjectedVector: Qt.point(0, 0)
    property bool axisMoveHistorySaved: false
    property int requestedRenderWidth: 1920
    property int requestedRenderHeight: 1080
    property int requestedRenderSamples: 8
    property int requestedRenderBounces: 1
    property real renderProgressValue: 0
    property string renderStatusText: "Ready"
    property url currentProjectUrl: ""
    property bool projectDirty: false
    property string pendingAction: ""
    property url pendingOpenUrl: ""
    property var pendingOpenData: null
    property bool continuePendingAfterSave: false
    property bool allowWindowClose: false

    ListModel {
        id: objectList
    }

    Shortcut {
        sequence: StandardKey.Save
        onActivated: root.saveCurrentProject()
    }

    Shortcut {
        sequence: StandardKey.SaveAs
        enabled: !unsavedChangesDialog.visible
                 && !renderProgressDialog.visible
                 && !meshEditDialog.visible
                 && !introDialog.visible
        onActivated: saveSceneDialog.open()
    }

    Shortcut {
        // F2 is the common rename key; it starts editing the selected object's name.
        sequence: "F2"
        enabled: root.selectedObject !== null && root.selectedIndex >= 0
                 && !unsavedChangesDialog.visible
                 && !renderProgressDialog.visible
                 && !meshEditDialog.visible
        onActivated: {
            objectNameField.forceActiveFocus()
            objectNameField.selectAll()
        }
    }

    Shortcut {
        // Ctrl+D duplicates the selected scene object.
        sequence: "Ctrl+D"
        enabled: root.selectedObject !== null && root.selectedIndex >= 0
                 && !unsavedChangesDialog.visible
                 && !renderProgressDialog.visible
                 && !meshEditDialog.visible
                 && !introDialog.visible
        onActivated: root.duplicateSelected()
    }

    Shortcut {
        // Ctrl+Shift+F frames the visible scene in the viewport.
        sequence: "Ctrl+Shift+F"
        enabled: !unsavedChangesDialog.visible
                 && !renderProgressDialog.visible
                 && !meshEditDialog.visible
                 && !introDialog.visible
        onActivated: root.frameAllObjects()
    }

    Shortcut {
        sequence: StandardKey.Open
        onActivated: openSceneDialog.open()
    }

    Shortcut {
        sequence: StandardKey.Undo
        enabled: root.undoStack.length > 0
        onActivated: root.undo()
    }

    Shortcut {
        sequence: StandardKey.Redo
        enabled: root.redoStack.length > 0
        onActivated: root.redo()
    }

    Connections {
        target: finalRenderer

        function onProgressChanged() {
            root.renderProgressValue = finalRenderer.progress
        }

        function onStatusChanged() {
            root.renderStatusText = finalRenderer.status
        }

        function onRenderFinished(success, message) {
            root.renderStatusText = message
            root.renderProgressValue = finalRenderer.progress
            root.setStatus(message)
        }
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

    // A rejected mesh operation must not add a fake Undo step or erase the Redo path.
    function captureHistoryCheckpoint() {
        return {
            undoStack: undoStack.slice(),
            redoStack: redoStack.slice(),
            projectDirty: projectDirty
        }
    }

    function restoreHistoryCheckpoint(checkpoint) {
        if (!checkpoint)
            return
        undoStack = checkpoint.undoStack
        redoStack = checkpoint.redoStack
        projectDirty = checkpoint.projectDirty
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
        projectDirty = true
    }

    function clearHistory() {
        undoStack = []
        redoStack = []
    }

    function saveSceneTo(url) {
        const result = projectIO.saveProject(url, root.serializeScene())
        if (result.ok) {
            currentProjectUrl = url
            projectDirty = false
            root.setStatus("Scene saved: " + result.path)
            if (continuePendingAfterSave) {
                continuePendingAfterSave = false
                unsavedChangesDialog.close()
                Qt.callLater(root.performPendingAction)
            }
            return true
        }

        if (continuePendingAfterSave) {
            continuePendingAfterSave = false
            pendingAction = ""
            pendingOpenUrl = ""
            pendingOpenData = null
            unsavedChangesDialog.close()
        }
        root.setStatus("Save failed: " + result.error)
        return false
    }

    function saveCurrentProject() {
        if (String(currentProjectUrl).length > 0)
            return saveSceneTo(currentProjectUrl)
        saveSceneDialog.open()
        return false
    }

    function requestNewScene() {
        pendingAction = "new"
        pendingOpenUrl = ""
        pendingOpenData = null
        if (projectDirty)
            unsavedChangesDialog.open()
        else
            performPendingAction()
    }

    function requestOpenScene(url) {
        const result = projectIO.loadProject(url)
        if (!result.ok) {
            root.setStatus("Open failed: " + result.error)
            return
        }

        pendingAction = "open"
        pendingOpenUrl = url
        pendingOpenData = result.data
        if (projectDirty)
            unsavedChangesDialog.open()
        else
            performPendingAction()
    }

    function performPendingAction() {
        const action = pendingAction
        const openUrl = pendingOpenUrl
        const openData = pendingOpenData
        pendingAction = ""
        pendingOpenUrl = ""
        pendingOpenData = null

        if (action === "new") {
            createNewScene()
        } else if (action === "open" && openData) {
            currentProjectUrl = openUrl
            clearHistory()
            applyProject(openData)
            projectDirty = false
            root.setStatus("Scene opened successfully.")
        } else if (action === "exit") {
            allowWindowClose = true
            Qt.quit()
        }
    }

    function cancelPendingAction(message) {
        continuePendingAfterSave = false
        pendingAction = ""
        pendingOpenUrl = ""
        pendingOpenData = null
        unsavedChangesDialog.close()
        if (message)
            root.setStatus(message)
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

    function refreshFillTintSelection() {
        const currentColor = String(fillLightColor).toLowerCase()
        if (currentColor === "#ffffff")
            fillTintCombo.currentIndex = 0
        else if (currentColor === "#ffd6ad")
            fillTintCombo.currentIndex = 1
        else if (currentColor === "#b8d6ff")
            fillTintCombo.currentIndex = 2
        else
            fillTintCombo.currentIndex = 3
    }

    function openCustomFillLightColorPicker() {
        customFillLightColorDialog.selectedColor = fillLightColor
        customFillLightColorDialog.open()
    }

    function setCustomFillLightColor(pickedColor) {
        // Store a predictable six-digit value, so colour choices survive JSON save/reopen.
        function toHexByte(component) {
            const byteValue = Math.max(0, Math.min(255, Math.round(component * 255)))
            return byteValue.toString(16).padStart(2, "0")
        }
        const hexColor = "#" + toHexByte(pickedColor.r)
                               + toHexByte(pickedColor.g)
                               + toHexByte(pickedColor.b)
        if (String(fillLightColor).toLowerCase() === hexColor
                && fillTintCombo.currentIndex === 3) {
            refreshFillTintSelection()
            return
        }

        pushUndoSnapshot()
        fillLightColor = hexColor
        fillTintCombo.currentIndex = 3
        setStatus("Updated custom fill-light colour " + hexColor + ".")
    }

    function applyFillLightTint(index) {
        if (index === 3) {
            openCustomFillLightColorPicker()
            return
        }

        const presetColors = ["#ffffff", "#ffd6ad", "#b8d6ff"]
        if (index < 0 || index >= presetColors.length)
            return
        const presetColor = presetColors[index]
        if (String(fillLightColor).toLowerCase() === presetColor) {
            refreshFillTintSelection()
            return
        }

        pushUndoSnapshot()
        fillLightColor = presetColor
        fillTintCombo.currentIndex = index
        setStatus("Updated fill-light colour.")
    }

    // Refresh the numeric light fields after opening, resetting, undoing, or redoing a scene.
    function refreshFillLightPositionFields() {
        fillLightXField.text = Number(fillLightPosition.x).toFixed(1)
        fillLightYField.text = Number(fillLightPosition.y).toFixed(1)
        fillLightZField.text = Number(fillLightPosition.z).toFixed(1)
    }

    // Change only one light axis. Invalid edits leave the live scene untouched.
    function updateFillLightPosition(axis, inputText) {
        const parsed = Number(inputText)
        if (!Number.isFinite(parsed)) {
            refreshFillLightPositionFields()
            setStatus("Enter a valid fill-light coordinate.")
            return
        }

        const current = fillLightPosition
        let x = current.x, y = current.y, z = current.z
        const value = Math.max(-1000, Math.min(1000, parsed))
        if (axis === "x") x = value
        else if (axis === "y") y = value
        else if (axis === "z") z = value
        else {
            refreshFillLightPositionFields()
            return
        }

        if (Math.abs(x - current.x) < 0.0001
                && Math.abs(y - current.y) < 0.0001
                && Math.abs(z - current.z) < 0.0001) {
            refreshFillLightPositionFields()
            return
        }

        // Snapshot first so Undo restores the exact previous light position.
        pushUndoSnapshot()
        fillLightPosition = Qt.vector3d(x, y, z)
        refreshFillLightPositionFields()
        setStatus("Updated fill-light position.")
    }

    // Keep key-light angle fields in sync when a scene is restored or history is replayed.
    function refreshKeyLightRotationFields() {
        keyLightRotationXField.text = Number(keyLightRotation.x).toFixed(1)
        keyLightRotationYField.text = Number(keyLightRotation.y).toFixed(1)
        keyLightRotationZField.text = Number(keyLightRotation.z).toFixed(1)
    }

    // Rotate the directional key light on one axis, without changing its brightness.
    function updateKeyLightRotation(axis, inputText) {
        const parsed = Number(inputText)
        if (!Number.isFinite(parsed)) {
            refreshKeyLightRotationFields()
            setStatus("Enter a valid key-light angle.")
            return
        }

        const current = keyLightRotation
        let x = current.x, y = current.y, z = current.z
        const value = Math.max(-180, Math.min(180, parsed))
        if (axis === "x") x = value
        else if (axis === "y") y = value
        else if (axis === "z") z = value
        else {
            refreshKeyLightRotationFields()
            return
        }

        if (Math.abs(x - current.x) < 0.0001
                && Math.abs(y - current.y) < 0.0001
                && Math.abs(z - current.z) < 0.0001) {
            refreshKeyLightRotationFields()
            return
        }

        // Undo restores the old direction because the angles are part of the scene settings.
        pushUndoSnapshot()
        keyLightRotation = Qt.vector3d(x, y, z)
        refreshKeyLightRotationFields()
        setStatus("Updated directional key-light rotation.")
    }

    // Use the same dimension and ray-work limits that the C++ renderer enforces.
    function selectedRenderWidth() {
        if (renderResolutionCombo.currentIndex === 5)
            return customRenderWidthSpinBox.value
        return Number(String(renderResolutionCombo.currentText).split("x")[0])
    }

    function selectedRenderHeight() {
        if (renderResolutionCombo.currentIndex === 5)
            return customRenderHeightSpinBox.value
        return Number(String(renderResolutionCombo.currentText).split("x")[1])
    }

    function renderSettingsWithinBudget() {
        const width = selectedRenderWidth()
        const height = selectedRenderHeight()
        const samples = renderSamplesSpinBox.value
        const bounces = renderBouncesSpinBox.value
        if (!Number.isFinite(width) || !Number.isFinite(height)
                || width < 16 || height < 16 || width > 16384 || height > 16384)
            return false
        const pixels = width * height
        return pixels <= 33554432
                && pixels * samples * (bounces + 1) <= 268435456
    }

    function renderBudgetMessage() {
        const width = selectedRenderWidth()
        const height = selectedRenderHeight()
        const samples = renderSamplesSpinBox.value
        const bounces = renderBouncesSpinBox.value
        if (!Number.isFinite(width) || !Number.isFinite(height)
                || width < 16 || height < 16 || width > 16384 || height > 16384)
            return "Choose width and height from 16 to 16,384 pixels."
        const pixels = width * height
        if (pixels > 33554432)
            return "This output exceeds the 33.5-million-pixel limit. Choose smaller dimensions."
        const workload = pixels * samples * (bounces + 1)
        if (workload > 268435456)
            return "This setting exceeds the render workload limit. Lower the sample count, bounce depth, or output resolution."
        return "Within safety limits: " + (pixels / 1000000).toFixed(2)
                + " megapixels · " + samples + " samples/pixel · "
                + bounces + " diffuse bounce(s)."
    }

    function openBackgroundColorPicker() {
        backgroundColorDialog.selectedColor = backgroundColor
        backgroundColorDialog.open()
    }

    function setBackgroundColor(pickedColor) {
        // Store a simple RGB hex value so the choice survives a project save and reopen.
        function toHexByte(component) {
            const byteValue = Math.max(0, Math.min(255, Math.round(component * 255)))
            return byteValue.toString(16).padStart(2, "0")
        }
        const hexColor = "#" + toHexByte(pickedColor.r)
                               + toHexByte(pickedColor.g)
                               + toHexByte(pickedColor.b)
        if (String(backgroundColor).toLowerCase() === hexColor)
            return

        pushUndoSnapshot()
        backgroundColor = hexColor
        setStatus("Updated scene background colour to " + hexColor + ".")
    }

    function updateCamera() {
        const yaw = orbitYaw * Math.PI / 180
        const pitch = orbitPitch * Math.PI / 180
        const horizontal = orbitDistance * Math.cos(pitch)
        const target = focusTarget.position
        camera.position = Qt.vector3d(
            target.x + horizontal * Math.sin(yaw),
            target.y + orbitDistance * Math.sin(pitch),
            target.z + horizontal * Math.cos(yaw)
        )
        camera.lookAt(focusTarget)
        Qt.callLater(root.updateGizmo)
    }

    function toggleCameraProjection() {
        pushUndoSnapshot()
        orthographicMode = !orthographicMode
        setStatus(orthographicMode
                  ? "Orthographic projection: parallel edges stay parallel."
                  : "Perspective projection: depth changes apparent size.")
    }

    function snapMovementValue(value) {
        // If snapping is off, leave the movement exactly where the mouse put it.
        if (!snapMovementToGrid || !Number.isFinite(movementSnapStep)
                || movementSnapStep <= 0)
            return value

        // Round each world-space coordinate to the nearest chosen grid step.
        return Math.round(value / movementSnapStep) * movementSnapStep
    }

    function worldAxisScreenVector(axis, position) {
        // Project a 100-unit move along a world axis into screen pixels.
        const axisDistance = 100
        let axisEnd = Qt.vector3d(position.x, position.y, position.z)
        if (axis === "x")
            axisEnd = Qt.vector3d(position.x + axisDistance, position.y, position.z)
        else if (axis === "y")
            axisEnd = Qt.vector3d(position.x, position.y + axisDistance, position.z)
        else
            axisEnd = Qt.vector3d(position.x, position.y, position.z + axisDistance)

        const startOnScreen = view3d.mapFrom3DScene(position)
        const endOnScreen = view3d.mapFrom3DScene(axisEnd)
        return Qt.point(endOnScreen.x - startOnScreen.x,
                        endOnScreen.y - startOnScreen.y)
    }

    function gizmoAxisProjectionPosition() {
        // Keep handles steady while dragging; the gizmo catches up when released.
        if (axisMoveActive)
            return axisMoveStartPosition
        return selectedObject ? selectedObject.position : Qt.vector3d(0, 0, 0)
    }

    function gizmoAxisHandlePoint(axis) {
        if (!selectedObject)
            return Qt.point(-1000, -1000)

        const projected = worldAxisScreenVector(axis, gizmoAxisProjectionPosition())
        const length = Math.sqrt(projected.x * projected.x + projected.y * projected.y)
        // Hide an axis handle when it points almost straight into the camera.
        if (!Number.isFinite(length) || length < 1.0)
            return Qt.point(-1000, -1000)

        const handleDistance = 29
        return Qt.point(38 + projected.x / length * handleDistance,
                        38 + projected.y / length * handleDistance)
    }

    function gizmoAxisRotation(axis) {
        const projected = worldAxisScreenVector(axis, gizmoAxisProjectionPosition())
        return Math.atan2(projected.y, projected.x) * 180 / Math.PI
    }

    function beginAxisMove(axis, pointerX, pointerY) {
        if (!selectedObject || selectedIndex < 0)
            return false

        const projected = worldAxisScreenVector(axis, selectedObject.position)
        const lengthSquared = projected.x * projected.x + projected.y * projected.y
        if (!Number.isFinite(lengthSquared) || lengthSquared < 1.0) {
            setStatus("That axis points almost straight at the camera. Try Front, Top, or Side view.")
            return false
        }

        axisMoveName = axis
        axisMoveStartPosition = selectedObject.position
        axisMoveStartPointer = Qt.point(pointerX, pointerY)
        axisMoveProjectedVector = projected
        axisMoveHistorySaved = false
        axisMoveActive = true
        setStatus("Drag the " + axis.toUpperCase() + " handle to move along that world axis.")
        return true
    }

    function updateAxisMove(axis, pointerX, pointerY) {
        if (!axisMoveActive || axis !== axisMoveName || !selectedObject || selectedIndex < 0)
            return

        const projected = axisMoveProjectedVector
        const lengthSquared = projected.x * projected.x + projected.y * projected.y
        if (lengthSquared < 1.0)
            return

        // Project the mouse movement onto the screen direction of the chosen 3D axis.
        const mouseX = pointerX - axisMoveStartPointer.x
        const mouseY = pointerY - axisMoveStartPointer.y
        const worldDistance = (mouseX * projected.x + mouseY * projected.y)
                              / lengthSquared * 100
        if (!Number.isFinite(worldDistance))
            return

        // Change only the chosen coordinate; leave the other two exactly where they were.
        const start = axisMoveStartPosition
        let x = start.x
        let y = start.y
        let z = start.z
        if (axisMoveName === "x")
            x = snapMovementValue(start.x + worldDistance)
        else if (axisMoveName === "y")
            y = snapMovementValue(start.y + worldDistance)
        else
            z = snapMovementValue(start.z + worldDistance)

        const current = selectedObject.position
        if (Math.abs(current.x - x) < 0.0001
                && Math.abs(current.y - y) < 0.0001
                && Math.abs(current.z - z) < 0.0001)
            return

        if (!axisMoveHistorySaved) {
            // Save one undo step before the first real change, not on a simple click.
            pushUndoSnapshot()
            axisMoveHistorySaved = true
        }

        selectedObject.position = Qt.vector3d(x, y, z)
        objectList.setProperty(selectedIndex, "x", x)
        objectList.setProperty(selectedIndex, "y", y)
        objectList.setProperty(selectedIndex, "z", z)
        refreshInspector()
    }

    function endAxisMove() {
        if (!axisMoveActive)
            return
        axisMoveActive = false
        axisMoveName = ""
        axisMoveHistorySaved = false
        Qt.callLater(root.updateGizmo)
    }

    function objectFrameRadius(object) {
        if (!object)
            return 80

        const objectScale = object.scale
        const scale = Math.max(0.01, Math.abs(objectScale.x),
                               Math.abs(objectScale.y), Math.abs(objectScale.z))
        let radius = 80 * scale
        const geometry = !object.editorImported ? object.geometry : null
        if (geometry && geometry.vertexCount > 0) {
            for (let i = 0; i < geometry.vertexCount; ++i) {
                const vertex = geometry.vertexPosition(i)
                const vertexRadius = Math.sqrt(vertex.x * vertex.x
                                               + vertex.y * vertex.y
                                               + vertex.z * vertex.z) * scale
                radius = Math.max(radius, vertexRadius)
            }
        }
        return Math.max(20, radius)
    }

    function focusSelectedObject() {
        if (!selectedObject || selectedObject.editorVisible === false) {
            setStatus("Show an object before focusing the view on it.")
            return
        }

        pushUndoSnapshot()
        focusTarget.position = selectedObject.position
        orbitDistance = Math.max(470, Math.min(2800, objectFrameRadius(selectedObject) * 4.8))
        updateCamera()
        setStatus("Focused the view on " + selectedObject.editorLabel + ".")
    }

    function frameAllObjects() {
        if (sceneObjects.length === 0) {
            focusTarget.position = Qt.vector3d(0, 0, 0)
            orbitDistance = 470
            updateCamera()
            setStatus("The scene is empty.")
            return
        }

        pushUndoSnapshot()
        let minX = Infinity, minY = Infinity, minZ = Infinity
        let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity
        let framedCount = 0

        for (let i = 0; i < sceneObjects.length; ++i) {
            const object = sceneObjects[i]
            // Hidden objects remain in the hierarchy but should not expand the framed view.
            if (!object || object.editorVisible === false)
                continue

            const position = object.position
            const radius = objectFrameRadius(object)
            minX = Math.min(minX, position.x - radius)
            minY = Math.min(minY, position.y - radius)
            minZ = Math.min(minZ, position.z - radius)
            maxX = Math.max(maxX, position.x + radius)
            maxY = Math.max(maxY, position.y + radius)
            maxZ = Math.max(maxZ, position.z + radius)
            ++framedCount
        }

        if (framedCount === 0)
            return

        focusTarget.position = Qt.vector3d((minX + maxX) * 0.5,
                                           (minY + maxY) * 0.5,
                                           (minZ + maxZ) * 0.5)
        const span = Math.max(maxX - minX, maxY - minY, maxZ - minZ)
        orbitDistance = Math.max(470, Math.min(2800, span * 1.4 + 130))
        updateCamera()
        setStatus("Framed " + framedCount + " scene object(s).")
    }

    function updateGizmo() {
        if (!view3d || !selectedObject || selectedObject.editorVisible === false) {
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

    function objectMatchesFilter(label, shape, objectId) {
        const query = hierarchyFilter.trim().toLowerCase()
        if (query.length === 0)
            return true

        return String(label).toLowerCase().includes(query)
            || String(shape).toLowerCase().includes(query)
            || String(objectId).toLowerCase().includes(query)
    }

    function visibleHierarchyMatchCount() {
        let matchCount = 0
        for (let i = 0; i < objectList.count; ++i) {
            const row = objectList.get(i)
            if (objectMatchesFilter(row.label, row.shape, row.objectId))
                ++matchCount
        }
        return matchCount
    }

    function selectObject(objectId) {
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
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

    function toggleObjectVisibility(index) {
        if (index < 0 || index >= objectList.count)
            return

        const row = objectList.get(index)
        // A missing value means visible, keeping older project files compatible.
        const nextVisible = row.isVisible === false
        pushUndoSnapshot()
        objectList.setProperty(index, "isVisible", nextVisible)

        for (let i = 0; i < sceneObjects.length; ++i) {
            const object = sceneObjects[i]
            if (object && object.editorId === row.objectId) {
                object.editorVisible = nextVisible
                break
            }
        }

        if (selectedObject && selectedObject.editorId === row.objectId)
            Qt.callLater(root.updateGizmo)

        setStatus((nextVisible ? "Shown " : "Hidden ") + row.label + ".")
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
            scaleXField.text = "1"
            scaleYField.text = "1"
            scaleZField.text = "1"
            roughnessSlider.value = 0.38
            metalnessSlider.value = 0.08
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
        scaleXField.text = Number(selectedObject.scale.x).toFixed(2)
        scaleYField.text = Number(selectedObject.scale.y).toFixed(2)
        scaleZField.text = Number(selectedObject.scale.z).toFixed(2)
        roughnessSlider.value = selectedObject.editorRoughness
        metalnessSlider.value = selectedObject.editorMetalness

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
        const legacyScale = Number(saved.scale) > 0 ? Number(saved.scale) : 1
        const scaleX = Number(saved.scaleX) > 0 ? Number(saved.scaleX) : legacyScale
        const scaleY = Number(saved.scaleY) > 0 ? Number(saved.scaleY) : legacyScale
        const scaleZ = Number(saved.scaleZ) > 0 ? Number(saved.scaleZ) : legacyScale
        const uniformScale = (scaleX + scaleY + scaleZ) / 3
        const defaultColors = ["#c8c8c8", "#508bdc", "#d8ae57", "#df5b57", "#f2f2f2"]
        const defaultColor = defaultColors[objectList.count % defaultColors.length]
        const colorValue = saved.color ? String(saved.color) : defaultColor
        const roughness = saved.roughness !== undefined ? Math.max(0, Math.min(1, Number(saved.roughness))) : 0.38
        const storedMetalness = Number(saved.metalness)
        const metalness = saved.metalness !== undefined && Number.isFinite(storedMetalness)
            ? Math.max(0, Math.min(1, storedMetalness)) : 0.08
        // Older scene files have no visibility field, so their objects stay visible.
        const objectIsVisible = saved.visible === undefined ? true : Boolean(saved.visible)

        const object = primitiveComponent.createObject(sceneRoot, {
            editorId: objectId,
            editorLabel: label,
            editorShape: shape,
            editorColor: colorValue,
            editorRoughness: roughness,
            editorMetalness: metalness,
            editorVisible: objectIsVisible,
            editorMeshData: saved.mesh || ({}),
            uniformScale: uniformScale,
            position: Qt.vector3d(x, y, z),
            eulerRotation: Qt.vector3d(rx, ry, rz),
            scale: Qt.vector3d(scaleX, scaleY, scaleZ),
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
            scaleX: scaleX, scaleY: scaleY, scaleZ: scaleZ,
            color: colorValue,
            roughness: roughness,
            metalness: metalness,
            isVisible: objectIsVisible
        })
        selectObject(objectId)
        setStatus("Created " + label + ".")
    }

    function createImportedAsset(sourceUrl) {
        const sourceText = String(sourceUrl || "")
        const extensionPath = sourceText.split(/[?#]/)[0]
        const isStl = /\.stl$/i.test(extensionPath)
        const isObj = /\.obj$/i.test(extensionPath)
        if (isObj || isStl) {
            // STL and OBJ become editable native meshes rather than read-only runtime assets.
            const result = isStl ? projectIO.importStl(sourceUrl) : projectIO.importObj(sourceUrl)
            const formatLabel = isStl ? "STL" : "OBJ"
            if (!result.ok) {
                setStatus(formatLabel + " import failed: " + result.error)
                return
            }

            pushUndoSnapshot()
            const importedLabel = result.label || ("Imported " + formatLabel + " Mesh")
            addPrimitive("Custom", {
                label: importedLabel,
                x: objectList.count * 120,
                y: 0,
                z: 0,
                scale: 1,
                color: "#c8c8c8",
                roughness: 0.48,
                mesh: result.mesh
            })
            setStatus("Imported editable " + formatLabel + " mesh: " + importedLabel + ".")
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
        const legacyScale = Number(saved.scale) > 0 ? Number(saved.scale) : 1
        const scaleX = Number(saved.scaleX) > 0 ? Number(saved.scaleX) : legacyScale
        const scaleY = Number(saved.scaleY) > 0 ? Number(saved.scaleY) : legacyScale
        const scaleZ = Number(saved.scaleZ) > 0 ? Number(saved.scaleZ) : legacyScale
        const uniformScale = (scaleX + scaleY + scaleZ) / 3
        const objectIsVisible = saved.visible === undefined ? true : Boolean(saved.visible)

        const object = importedAssetComponent.createObject(sceneRoot, {
            editorId: objectId,
            editorLabel: label,
            editorShape: "Imported Asset",
            editorImported: true,
            editorColor: "#c8c8c8",
            editorRoughness: 0.38,
            editorMetalness: 0.0,
            editorVisible: objectIsVisible,
            uniformScale: uniformScale,
            assetUrl: sourceText,
            position: Qt.vector3d(x, y, z),
            eulerRotation: Qt.vector3d(rx, ry, rz),
            scale: Qt.vector3d(scaleX, scaleY, scaleZ),
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
            scaleX: scaleX, scaleY: scaleY, scaleZ: scaleZ,
            color: "#c8c8c8",
            roughness: 0.38,
            metalness: 0.0,
            isVisible: objectIsVisible
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
        if (edgeSelectionMode) {
            setStatus("Close edge-selection mode before selecting vertices.")
            return
        }
        if (vertexSelectionMode) {
            toggleMeshVertexSelection(index)
            return
        }
        vertexIndexSpinner.value = Math.max(0, Math.min(selectedObject.geometry.vertexCount - 1, index))
        refreshMeshEditFields()
        setStatus("Selected vertex " + vertexIndexSpinner.value + ".")
    }

    function clearMeshEdgeSelection() {
        meshEdgeSelection = []
    }

    function toggleMeshEdgeSelection(index) {
        const geometry = selectedEditableGeometry()
        if (!geometry || index < 0 || index >= geometry.edgeCount)
            return

        const selection = meshEdgeSelection.slice()
        const existingIndex = selection.indexOf(index)
        if (existingIndex >= 0)
            selection.splice(existingIndex, 1)
        else
            selection.push(index)

        meshEdgeSelection = selection
        edgeIndexSpinner.value = index
        refreshMeshEditFields()
        setStatus(selection.length + " edge(s) selected. Select a closed loop to create a face.")
    }

    function selectMeshEdge(index) {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return
        if (vertexSelectionMode) {
            setStatus("Close vertex-selection mode before selecting edges.")
            return
        }
        if (edgeSelectionMode) {
            toggleMeshEdgeSelection(index)
            return
        }

        edgeIndexSpinner.value = Math.max(0, Math.min(geometry.edgeCount - 1, index))
        refreshMeshEditFields()
        setStatus("Selected edge " + edgeIndexSpinner.value + ".")
    }

    function createFaceFromSelectedEdges() {
        const geometry = selectedEditableGeometry()
        if (!geometry || meshEdgeSelection.length < 3) {
            setStatus("Select at least three edges that form one closed loop.")
            return
        }

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.createFaceFromEdges(meshEdgeSelection)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Face creation was rejected. Select one connected, closed loop that is planar and convex.")
            return
        }

        faceIndexSpinner.value = geometry.faceCount - 1
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
        refreshMeshEditFields()
        setStatus("Created a face from the selected edge loop.")
    }
    function bevelSelectedEdge() {
        const geometry = selectedEditableGeometry()
        const distance = Number(bevelDistanceField.text)
        if (!geometry || edgeIndexSpinner.value < 0 || edgeIndexSpinner.value >= geometry.edgeCount) {
            setStatus("Select a valid mesh edge first.")
            return
        }
        if (!Number.isFinite(distance) || distance <= 0) {
            setStatus("Enter a bevel width greater than zero.")
            return
        }

        const selectedEdge = edgeIndexSpinner.value
        // Save a candidate snapshot first, but add it to Undo only if the bevel succeeds.
        // Trying an unsupported edge should not create a useless history entry.
        const previousScene = historySnapshot()
        if (!geometry.bevelEdge(selectedEdge, distance)) {
            setStatus("This edge cannot be safely beveled at that width. Try a smaller width on a closed mesh corner.")
            return
        }

        if (!historyLocked) {
            const stack = undoStack.slice()
            stack.push(previousScene)
            if (stack.length > historyLimit)
                stack.shift()
            undoStack = stack
            redoStack = []
            projectDirty = true
        }

        clearMeshEdgeSelection()
        edgeSelectionMode = false
        clearMeshVertexSelection()
        vertexSelectionMode = false
        edgeIndexSpinner.value = Math.max(0, geometry.edgeCount - 1)
        refreshMeshEditFields()
        setStatus("Beveled edge " + selectedEdge + " by " + distance + " scene units.")
    }

    function subdivideSelectedEdge() {
        const geometry = selectedEditableGeometry()
        if (!geometry || edgeIndexSpinner.value < 0 || edgeIndexSpinner.value >= geometry.edgeCount) {
            setStatus("Select a valid mesh edge first.")
            return
        }

        const selectedEdge = edgeIndexSpinner.value
        const midpointIndex = geometry.vertexCount
        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.subdivideEdge(selectedEdge)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Edge subdivision was rejected; the mesh remains unchanged.")
            return
        }

        clearMeshEdgeSelection()
        edgeSelectionMode = false
        clearMeshVertexSelection()
        vertexSelectionMode = false
        vertexIndexSpinner.value = midpointIndex
        edgeIndexSpinner.value = Math.min(selectedEdge, geometry.edgeCount - 1)
        refreshMeshEditFields()
        setStatus("Subdivided edge " + selectedEdge + "; the new midpoint vertex is selected.")
    }


    function createFaceFromSelectedVertices() {
        const geometry = selectedEditableGeometry()
        if (!geometry || meshVertexSelection.length < 3) {
            setStatus("Select at least three vertices in boundary order.")
            return
        }

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.createFace(meshVertexSelection)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Face creation failed. Choose unique, coplanar vertices around a convex boundary.")
            return
        }

        faceIndexSpinner.value = geometry.faceCount - 1
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
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

        meshEdgeCountLabel.text = String(geometry.edgeCount) + " edges"
        const edge = geometry.edgeVertexIndices(edgeIndexSpinner.value)
        if (edge && edge.length === 2) {
            const center = geometry.edgeCenter(edgeIndexSpinner.value)
            edgeInfoLabel.text = "Vertices " + edge[0] + " — " + edge[1]
                + " | Center " + center.x.toFixed(2) + ", "
                + center.y.toFixed(2) + ", " + center.z.toFixed(2)
        } else {
            edgeInfoLabel.text = "No edge selected."
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
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
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

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.setVertexPosition(vertexIndexSpinner.value, Qt.vector3d(x, y, z))) {
            restoreHistoryCheckpoint(historyBefore)
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

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.extrudeFace(faceIndexSpinner.value, distance)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Face extrusion failed. Choose a valid face and a non-zero distance.")
            return
        }

        clearMeshEdgeSelection()
        edgeSelectionMode = false
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
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

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.insetFace(faceIndexSpinner.value, distance)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Inset failed. Use a smaller distance on a convex, planar face.")
            return
        }

        clearMeshEdgeSelection()
        edgeSelectionMode = false
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
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

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.deleteFace(faceIndexSpinner.value)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Face deletion was rejected; the mesh has not changed.")
            return
        }

        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        vertexIndexSpinner.value = Math.min(vertexIndexSpinner.value, geometry.vertexCount - 1)
        mergeTargetSpinner.value = Math.min(mergeTargetSpinner.value, geometry.vertexCount - 1)
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
        refreshMeshEditFields()
        setStatus("Deleted face and compacted unused mesh vertices.")
    }

    function deleteSelectedVertex() {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        const removedIndex = vertexIndexSpinner.value
        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.deleteVertex(removedIndex)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Vertex deletion was rejected. The last face cannot be removed.")
            return
        }

        vertexIndexSpinner.value = Math.min(removedIndex, geometry.vertexCount - 1)
        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        mergeTargetSpinner.value = Math.min(mergeTargetSpinner.value, geometry.vertexCount - 1)
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
        refreshMeshEditFields()
        setStatus("Deleted vertex " + removedIndex + " and the faces connected to it.")
    }

    function flipSelectedMeshNormals() {
        const geometry = selectedEditableGeometry()
        if (!geometry)
            return

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.flipNormals()) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Normals could not be flipped because the mesh contains an invalid face.")
            return
        }

        refreshMeshEditFields()
        setStatus("Flipped all face normals. Undo restores the previous direction.")
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

        const historyBefore = captureHistoryCheckpoint()
        pushUndoSnapshot()
        if (!geometry.mergeVertices(sourceIndex, targetIndex)) {
            restoreHistoryCheckpoint(historyBefore)
            setStatus("Vertex merge failed; the mesh was left unchanged.")
            return
        }

        vertexIndexSpinner.value = Math.min(targetIndex, geometry.vertexCount - 1)
        faceIndexSpinner.value = Math.min(faceIndexSpinner.value, geometry.faceCount - 1)
        mergeTargetSpinner.value = Math.min(vertexIndexSpinner.value, geometry.vertexCount - 1)
        clearMeshVertexSelection()
        clearMeshEdgeSelection()
        vertexSelectionMode = false
        edgeSelectionMode = false
        edgeIndexSpinner.value = Math.min(edgeIndexSpinner.value, geometry.edgeCount - 1)
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

        const scale = selectedObject.scale
        let sx = scale.x, sy = scale.y, sz = scale.z
        if (axis === "scale") {
            sx = sy = sz = Math.max(0.01, Math.min(1000, value))
        } else if (axis === "scaleX") {
            sx = Math.max(0.01, Math.min(1000, value))
        } else if (axis === "scaleY") {
            sy = Math.max(0.01, Math.min(1000, value))
        } else if (axis === "scaleZ") {
            sz = Math.max(0.01, Math.min(1000, value))
        }

        if (axis === "scale" || axis === "scaleX" || axis === "scaleY" || axis === "scaleZ") {
            selectedObject.scale = Qt.vector3d(sx, sy, sz)
            selectedObject.uniformScale = (sx + sy + sz) / 3
            objectList.setProperty(selectedIndex, "scale", selectedObject.uniformScale)
            objectList.setProperty(selectedIndex, "scaleX", sx)
            objectList.setProperty(selectedIndex, "scaleY", sy)
            objectList.setProperty(selectedIndex, "scaleZ", sz)
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

    function resetSelectedTransform() {
        if (!selectedObject || selectedIndex < 0)
            return

        const position = selectedObject.position
        const rotation = selectedObject.eulerRotation
        const alreadyReset = Math.abs(position.x) < 0.0001
            && Math.abs(position.y) < 0.0001
            && Math.abs(position.z) < 0.0001
            && Math.abs(rotation.x) < 0.0001
            && Math.abs(rotation.y) < 0.0001
            && Math.abs(rotation.z) < 0.0001
            && Math.abs(selectedObject.scale.x - 1.0) < 0.0001
            && Math.abs(selectedObject.scale.y - 1.0) < 0.0001
            && Math.abs(selectedObject.scale.z - 1.0) < 0.0001
        if (alreadyReset) {
            setStatus("This object already has its default transform.")
            return
        }

        // Save the previous scene once so Undo restores the old object transform.
        pushUndoSnapshot()
        selectedObject.position = Qt.vector3d(0, 0, 0)
        selectedObject.eulerRotation = Qt.vector3d(0, 0, 0)
        selectedObject.uniformScale = 1.0
        selectedObject.scale = Qt.vector3d(1, 1, 1)

        objectList.setProperty(selectedIndex, "x", 0)
        objectList.setProperty(selectedIndex, "y", 0)
        objectList.setProperty(selectedIndex, "z", 0)
        objectList.setProperty(selectedIndex, "rx", 0)
        objectList.setProperty(selectedIndex, "ry", 0)
        objectList.setProperty(selectedIndex, "rz", 0)
        objectList.setProperty(selectedIndex, "scale", 1)
        objectList.setProperty(selectedIndex, "scaleX", 1)
        objectList.setProperty(selectedIndex, "scaleY", 1)
        objectList.setProperty(selectedIndex, "scaleZ", 1)

        // Refocus the view so the object doesn't appear to vanish after a reset.
        focusTarget.position = Qt.vector3d(0, 0, 0)
        updateCamera()
        refreshInspector()
        Qt.callLater(root.updateGizmo)
        setStatus("Reset position, rotation, and scale for " + selectedObject.editorLabel + ".")
    }

    function openCustomColorPicker() {
        // Use the selected object's current color as the starting point.
        if (!selectedObject || selectedIndex < 0 || selectedObject.editorImported)
            return
        customMaterialColorDialog.selectedColor = selectedObject.editorColor
        customMaterialColorDialog.open()
    }

    function setMaterialCustomColor(pickedColor) {
        if (!selectedObject || selectedIndex < 0 || selectedObject.editorImported)
            return

        // Save a six-digit color value so the exact choice survives scene save/reopen.
        function toHexByte(component) {
            const byteValue = Math.max(0, Math.min(255, Math.round(component * 255)))
            return byteValue.toString(16).padStart(2, "0")
        }
        const hexColor = "#" + toHexByte(pickedColor.r)
                                + toHexByte(pickedColor.g)
                                + toHexByte(pickedColor.b)

        pushUndoSnapshot()
        selectedObject.editorColor = hexColor
        objectList.setProperty(selectedIndex, "color", hexColor)
        setStatus("Applied custom material color " + hexColor + " to "
                  + selectedObject.editorLabel + ".")
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

    function setMaterialMetalness(value) {
        if (!selectedObject || selectedIndex < 0 || selectedObject.editorImported)
            return

        // Metallic response changes how the final renderer mixes diffuse color and reflections.
        const metalness = Math.max(0, Math.min(1, value))
        selectedObject.editorMetalness = metalness
        objectList.setProperty(selectedIndex, "metalness", metalness)
    }

    function applyMaterialPreset(presetName) {
        if (!selectedObject || selectedIndex < 0 || selectedObject.editorImported)
            return

        const presets = {
            plastic: {label: "Plastic", color: "#508bdc", roughness: 0.28, metalness: 0.0},
            metal: {label: "Metal", color: "#c8c8c8", roughness: 0.22, metalness: 1.0},
            rubber: {label: "Rubber", color: "#555555", roughness: 0.90, metalness: 0.0},
            gold: {label: "Gold", color: "#d8ae57", roughness: 0.24, metalness: 0.92}
        }
        const preset = presets[String(presetName).toLowerCase()]
        if (!preset)
            return

        // Change all material values together so the result is immediately useful.
        pushUndoSnapshot()
        selectedObject.editorColor = preset.color
        selectedObject.editorRoughness = preset.roughness
        selectedObject.editorMetalness = preset.metalness
        objectList.setProperty(selectedIndex, "color", preset.color)
        objectList.setProperty(selectedIndex, "roughness", preset.roughness)
        objectList.setProperty(selectedIndex, "metalness", preset.metalness)
        refreshInspector()
        setStatus("Applied " + preset.label + " material to " + selectedObject.editorLabel + ".")
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
            scaleX: row.scaleX, scaleY: row.scaleY, scaleZ: row.scaleZ,
            color: row.color,
            roughness: row.roughness,
            metalness: row.metalness
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
        // A filter from the previous project must not hide every object in the new scene.
        hierarchyFilter = ""
        selectedObject = null
        selectedIndex = -1
        nextObjectNumber = 1
        refreshInspector()
    }

    function createNewScene() {
        // A new document starts a fresh history. Undo must never jump back into
        // the previous document after the user has confirmed replacing it.
        currentProjectUrl = ""
        clearHistory()
        clearScene()
        addPrimitive("Cube", {id: "object-1", label: "Cube", x: 0, y: 0, z: 0, scale: 1, color: "#c8c8c8"})
        orbitYaw = 34
        orbitPitch = 23
        orbitDistance = 470
        focusTarget.position = Qt.vector3d(0, 0, 0)
        camera.fieldOfView = 45
        orthographicMode = false
        backgroundColor = "#171717"
        transparentBackground = false
        keyLightRotation = Qt.vector3d(-42, -35, 0)
        keyLight.brightness = 1.7
        fillLight.brightness = 65
        fillLightPosition = Qt.vector3d(-160, 190, 140)
        fillLightColor = "#ffffff"
        fillTintCombo.currentIndex = 0
        refreshFillLightPositionFields()
        refreshKeyLightRotationFields()
        refreshFillTintSelection()
        updateCamera()
        projectDirty = editorReady
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
                scaleX: row.scaleX === undefined ? row.scale : row.scaleX,
                scaleY: row.scaleY === undefined ? row.scale : row.scaleY,
                scaleZ: row.scaleZ === undefined ? row.scale : row.scaleZ,
                color: row.color,
                roughness: row.roughness,
                metalness: row.metalness,
                visible: row.isVisible !== false
            })
        }

        return {
            format: "qalyvyron.scene",
            version: 1,
            title: "QALYVYRON Scene",
            settings: {
                cameraFov: camera.fieldOfView,
                backgroundColor: String(root.backgroundColor),
                transparentBackground: root.transparentBackground,
                keyLightBrightness: keyLight.brightness,
                keyLightRotationX: root.keyLightRotation.x,
                keyLightRotationY: root.keyLightRotation.y,
                keyLightRotationZ: root.keyLightRotation.z,
                fillLightBrightness: fillLight.brightness,
                fillLightX: root.fillLightPosition.x,
                fillLightY: root.fillLightPosition.y,
                fillLightZ: root.fillLightPosition.z,
                fillTintIndex: fillTintCombo.currentIndex,
                fillLightColor: String(root.fillLightColor),
                orbitYaw: root.orbitYaw,
                orbitPitch: root.orbitPitch,
                orbitDistance: root.orbitDistance,
                orbitTargetX: focusTarget.position.x,
                orbitTargetY: focusTarget.position.y,
                orbitTargetZ: focusTarget.position.z,
                orthographicMode: root.orthographicMode
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
        backgroundColor = "#171717"
        transparentBackground = false
        keyLightRotation = Qt.vector3d(-42, -35, 0)
        keyLight.brightness = 1.7
        fillLight.brightness = 65
        fillLightPosition = Qt.vector3d(-160, 190, 140)
        fillLightColor = "#ffffff"
        fillTintCombo.currentIndex = 0
        refreshFillLightPositionFields()
        orbitYaw = 34
        orbitPitch = 23
        orbitDistance = 470
        focusTarget.position = Qt.vector3d(0, 0, 0)

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
                fillLightColor = "#ffd6ad"
            else if (tintIndex === 2)
                fillLightColor = "#b8d6ff"
            else
                fillLightColor = "#ffffff"
        }

        // Only accept a stored RGB hex value; old scenes continue to use their named preset.
        const savedFillColor = settings.fillLightColor === undefined
            ? "" : String(settings.fillLightColor)
        if (/^#[0-9a-f]{6}$/i.test(savedFillColor))
            fillLightColor = savedFillColor
        refreshFillTintSelection()

        // Old scenes have no background setting, so keep the original dark workspace colour.
        const savedBackgroundColor = settings.backgroundColor === undefined
            ? "" : String(settings.backgroundColor)
        if (/^#[0-9a-f]{6}$/i.test(savedBackgroundColor))
            backgroundColor = savedBackgroundColor
        // Older projects omit this flag, so preserve the established opaque default.
        transparentBackground = settings.transparentBackground === true

        // Older scenes don't store the light position, so keep the old default for them.
        const fillX = settings.fillLightX !== undefined && Number.isFinite(Number(settings.fillLightX))
            ? Math.max(-1000, Math.min(1000, Number(settings.fillLightX))) : -160
        const fillY = settings.fillLightY !== undefined && Number.isFinite(Number(settings.fillLightY))
            ? Math.max(-1000, Math.min(1000, Number(settings.fillLightY))) : 190
        const fillZ = settings.fillLightZ !== undefined && Number.isFinite(Number(settings.fillLightZ))
            ? Math.max(-1000, Math.min(1000, Number(settings.fillLightZ))) : 140
        fillLightPosition = Qt.vector3d(fillX, fillY, fillZ)
        refreshFillLightPositionFields()

        // Missing angles in older project files fall back to the original key-light direction.
        const keyRotationX = settings.keyLightRotationX !== undefined
                && Number.isFinite(Number(settings.keyLightRotationX))
            ? Math.max(-180, Math.min(180, Number(settings.keyLightRotationX))) : -42
        const keyRotationY = settings.keyLightRotationY !== undefined
                && Number.isFinite(Number(settings.keyLightRotationY))
            ? Math.max(-180, Math.min(180, Number(settings.keyLightRotationY))) : -35
        const keyRotationZ = settings.keyLightRotationZ !== undefined
                && Number.isFinite(Number(settings.keyLightRotationZ))
            ? Math.max(-180, Math.min(180, Number(settings.keyLightRotationZ))) : 0
        keyLightRotation = Qt.vector3d(keyRotationX, keyRotationY, keyRotationZ)
        refreshKeyLightRotationFields()

        if (settings.orbitYaw !== undefined && Number.isFinite(Number(settings.orbitYaw)))
            orbitYaw = Number(settings.orbitYaw)
        if (settings.orbitPitch !== undefined && Number.isFinite(Number(settings.orbitPitch)))
            orbitPitch = Math.max(-80, Math.min(80, Number(settings.orbitPitch)))
        if (settings.orbitDistance !== undefined && Number.isFinite(Number(settings.orbitDistance)))
            orbitDistance = Math.max(120, Math.min(2800, Number(settings.orbitDistance)))
        orthographicMode = settings.orthographicMode === true

        const targetX = settings.orbitTargetX !== undefined && Number.isFinite(Number(settings.orbitTargetX))
                        ? Math.max(-1000000, Math.min(1000000, Number(settings.orbitTargetX))) : 0
        const targetY = settings.orbitTargetY !== undefined && Number.isFinite(Number(settings.orbitTargetY))
                        ? Math.max(-1000000, Math.min(1000000, Number(settings.orbitTargetY))) : 0
        const targetZ = settings.orbitTargetZ !== undefined && Number.isFinite(Number(settings.orbitTargetZ))
                        ? Math.max(-1000000, Math.min(1000000, Number(settings.orbitTargetZ))) : 0
        focusTarget.position = Qt.vector3d(targetX, targetY, targetZ)

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
            property bool editorVisible: true
            visible: editorVisible
            property color editorColor: "#c8c8c8"
            property real editorRoughness: 0.38
            property real editorMetalness: 0.0
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
            property real editorMetalness: 0.0
            property real uniformScale: 1.0
            property bool editorVisible: true
            visible: editorVisible
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
                    metalness: editableModel.editorMetalness
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
                        id: meshEdgeCountLabel
                        text: "0 edges"
                        color: root.secondaryText
                        font.pixelSize: 11
                    }
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

                Button {
                    Layout.fillWidth: true
                    text: "Delete Vertex + Connected Faces"
                    enabled: root.selectedEditableGeometry() !== null
                             && root.selectedEditableGeometry().vertexCount > 3
                             && root.selectedEditableGeometry().faceCount > 1
                    onClicked: root.deleteSelectedVertex()
                    ToolTip.visible: hovered
                    ToolTip.text: "Removes the selected vertex and every face attached to it. Undo can restore the removed geometry."
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        Layout.fillWidth: true
                        text: root.vertexSelectionMode ? "Vertex Selection: ON" : "Select Face Vertices"
                        highlighted: root.vertexSelectionMode
                        onClicked: {
                            const enabling = !root.vertexSelectionMode
                            root.vertexSelectionMode = enabling
                            if (enabling) {
                                root.edgeSelectionMode = false
                                root.clearMeshEdgeSelection()
                            } else {
                                root.clearMeshVertexSelection()
                            }
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

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: root.borderColor
                }

                Text {
                    text: "EDGE SELECTION"
                    color: "#bcbcbc"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        Layout.fillWidth: true
                        text: root.edgeSelectionMode ? "Edge Selection: ON" : "Select Edge Loop"
                        highlighted: root.edgeSelectionMode
                        onClicked: {
                            const enabling = !root.edgeSelectionMode
                            root.edgeSelectionMode = enabling
                            if (enabling) {
                                root.vertexSelectionMode = false
                                root.clearMeshVertexSelection()
                            } else {
                                root.clearMeshEdgeSelection()
                            }
                            root.setStatus(enabling
                                ? "Click edge lines to select one closed loop. Then create a face."
                                : "Edge multi-selection mode closed.")
                        }
                    }
                    Button {
                        text: "Clear"
                        enabled: root.meshEdgeSelection.length > 0
                        onClicked: root.clearMeshEdgeSelection()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.meshEdgeSelection.length + " edges selected"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }
                    Button {
                        text: "Create Face from Loop"
                        enabled: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                                 && root.meshEdgeSelection.length >= 3
                        onClicked: root.createFaceFromSelectedEdges()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Edge index"; color: root.secondaryText; font.pixelSize: 11 }
                    SpinBox {
                        id: edgeIndexSpinner
                        from: 0
                        to: Math.max(0, root.selectedObject && !root.selectedObject.editorImported
                                      ? root.selectedObject.geometry.edgeCount - 1 : 0)
                        editable: true
                        onValueChanged: if (meshEditDialog.visible) root.refreshMeshEditFields()
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    id: edgeInfoLabel
                    Layout.fillWidth: true
                    text: "Select an edge to inspect its endpoints and midpoint."
                    color: root.secondaryText
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                Button {
                    Layout.fillWidth: true
                    text: "Subdivide Edge"
                    enabled: root.selectedObject !== null
                             && !root.selectedObject.editorImported
                             && root.selectedEditableGeometry() !== null
                             && edgeIndexSpinner.value >= 0
                             && edgeIndexSpinner.value < root.selectedEditableGeometry().edgeCount
                    onClicked: root.subdivideSelectedEdge()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Width"; color: root.secondaryText; font.pixelSize: 11 }
                    TextField {
                        id: bevelDistanceField
                        Layout.preferredWidth: 90
                        text: "5"
                        selectByMouse: true
                        validator: DoubleValidator { bottom: 0.01; top: 1000 }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Bevel Edge"
                        enabled: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                                 && root.selectedEditableGeometry() !== null
                                 && edgeIndexSpinner.value >= 0
                                 && edgeIndexSpinner.value < root.selectedEditableGeometry().edgeCount
                        onClicked: root.bevelSelectedEdge()
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

                Button {
                    Layout.fillWidth: true
                    text: "Flip All Face Normals"
                    enabled: root.selectedEditableGeometry() !== null
                    onClicked: root.flipSelectedMeshNormals()
                    ToolTip.visible: hovered
                    ToolTip.text: "Reverse the winding of every face. Use this if the surface is lit from the wrong side; Undo restores it."
                }

                Text {
                    Layout.fillWidth: true
                    text: "Enable Select Face Vertices, then click vertex markers in boundary order and choose Create Face. Subdivision, constrained edge beveling, vertex merging, face creation, extrusion, inset and deletion edit the mesh directly. The first bevel version supports closed manifold edges with three connected edges at both endpoints."
                    color: "#777777"
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        onOpened: Qt.callLater(root.refreshMeshEditFields)
    }

    Dialog {
        id: renderSettingsDialog
        title: "Offline Image Render"
        modal: true
        focus: true
        width: 460
        height: 380
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "Render the current scene geometry to a full-resolution PNG. This runs separately from the live viewport and keeps the workspace responsive."
                color: root.secondaryText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                text: "OUTPUT RESOLUTION"
                color: "#bcbcbc"
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.2
            }

            ComboBox {
                id: renderResolutionCombo
                Layout.fillWidth: true
                model: ["1280x720", "1920x1080", "2560x1440", "3840x2160", "7680x4320", "Custom…"]
                currentIndex: 1
            }

            RowLayout {
                visible: renderResolutionCombo.currentIndex === 5
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "Width"
                    color: root.secondaryText
                    font.pixelSize: 11
                }
                SpinBox {
                    id: customRenderWidthSpinBox
                    Layout.fillWidth: true
                    from: 16
                    to: 16384
                    value: 1920
                    editable: true
                    validator: IntValidator { bottom: 16; top: 16384 }
                }
                Text {
                    text: "Height"
                    color: root.secondaryText
                    font.pixelSize: 11
                }
                SpinBox {
                    id: customRenderHeightSpinBox
                    Layout.fillWidth: true
                    from: 16
                    to: 16384
                    value: 1080
                    editable: true
                    validator: IntValidator { bottom: 16; top: 16384 }
                }
            }

            Text {
                visible: renderResolutionCombo.currentIndex === 5
                Layout.fillWidth: true
                text: {
                    const pixels = customRenderWidthSpinBox.value * customRenderHeightSpinBox.value
                    return "Custom output: " + customRenderWidthSpinBox.value + " × "
                        + customRenderHeightSpinBox.value + " pixels ("
                        + (pixels / 1000000).toFixed(2) + " megapixels). Render safety limits still apply."
                }
                color: "#777777"
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: root.renderBudgetMessage()
                color: root.renderSettingsWithinBudget() ? "#777777" : "#bcbcbc"
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Samples per pixel"
                    color: root.secondaryText
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                SpinBox {
                    id: renderSamplesSpinBox
                    from: 1
                    to: 64
                    value: 8
                    editable: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Diffuse light bounces"
                    color: root.secondaryText
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                SpinBox {
                    id: renderBouncesSpinBox
                    from: 0
                    to: 4
                    value: 1
                    editable: true
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Higher sample counts reduce edge noise; diffuse bounces add indirect light. Both increase render time. 8K output is supported within the pixel and ray-work budgets. Shadows are currently hard-edged and there is no denoiser yet."
                color: "#777777"
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "Cancel"
                    onClicked: renderSettingsDialog.close()
                }
                Button {
                    text: "Choose Output…"
                    highlighted: true
                    enabled: root.renderSettingsWithinBudget()
                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: root.renderBudgetMessage()
                    onClicked: {
                        if (renderResolutionCombo.currentIndex === 5) {
                            root.requestedRenderWidth = customRenderWidthSpinBox.value
                            root.requestedRenderHeight = customRenderHeightSpinBox.value
                        } else {
                            const parts = String(renderResolutionCombo.currentText).split("x")
                            root.requestedRenderWidth = Number(parts[0])
                            root.requestedRenderHeight = Number(parts[1])
                        }
                        root.requestedRenderSamples = renderSamplesSpinBox.value
                        root.requestedRenderBounces = renderBouncesSpinBox.value
                        renderSettingsDialog.close()
                        Qt.callLater(renderOutputDialog.open)
                    }
                }
            }
        }
    }

    FileDialog {
        id: renderOutputDialog
        title: "Save Final Render"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG image (*.png)"]
        defaultSuffix: "png"

        onAccepted: {
            const started = finalRenderer.startRender(
                selectedFile,
                root.serializeScene(),
                root.requestedRenderWidth,
                root.requestedRenderHeight,
                root.requestedRenderSamples,
                root.requestedRenderBounces)
            if (started) {
                root.renderProgressValue = 0
                root.renderProgressDialog.open()
            } else {
                root.setStatus(finalRenderer.status)
            }
        }
    }

    Dialog {
        id: renderProgressDialog
        title: "Rendering Image"
        modal: false
        dim: false
        focus: true
        width: 470
        height: 245
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: 13

            Text {
                Layout.fillWidth: true
                text: root.renderStatusText
                color: root.mainText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: root.renderProgressValue
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: Math.round(root.renderProgressValue) + "%"
                    color: root.secondaryText
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: finalRenderer.busy ? "Cancel Render" : "Close"
                    highlighted: !finalRenderer.busy
                    onClicked: {
                        if (finalRenderer.busy)
                            finalRenderer.cancelRender()
                        else
                            renderProgressDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: importAssetDialog
        title: "Import 3D Asset"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Supported 3D assets (*.glb *.gltf *.obj *.stl)",
            "glTF binary (*.glb)",
            "glTF scenes (*.gltf)",
            "Wavefront OBJ (*.obj)",
            "STL triangle mesh (*.stl)"
        ]

        onAccepted: root.createImportedAsset(selectedFile)
    }

    FileDialog {
        id: saveSceneDialog
        title: "Save QALYVYRON Scene"
        fileMode: FileDialog.SaveFile
        nameFilters: ["QALYVYRON Scene (*.qvscene)", "JSON files (*.json)"]
        defaultSuffix: "qvscene"

        onAccepted: root.saveSceneTo(selectedFile)
        onRejected: {
            if (root.continuePendingAfterSave)
                root.cancelPendingAction("Save canceled. Your current scene is still open.")
        }
    }

    Dialog {
        id: unsavedChangesDialog
        title: "Unsaved changes"
        modal: true
        focus: true
        width: 465
        height: 230
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "This scene has changes that aren't saved. Save it before continuing?"
                color: root.mainText
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "Your current work will stay open if you choose Cancel."
                color: root.secondaryText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "Save"
                    highlighted: true
                    onClicked: {
                        root.continuePendingAfterSave = true
                        unsavedChangesDialog.close()
                        root.saveCurrentProject()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Discard"
                    onClicked: {
                        unsavedChangesDialog.close()
                        Qt.callLater(root.performPendingAction)
                    }
                }

                Button {
                    text: "Cancel"
                    onClicked: root.cancelPendingAction("Canceled. Your scene was kept.")
                }
            }
        }
    }

    FileDialog {
        id: openSceneDialog
        title: "Open QALYVYRON Scene"
        fileMode: FileDialog.OpenFile
        nameFilters: ["QALYVYRON Scene (*.qvscene *.json)", "All files (*)"]

        onAccepted: root.requestOpenScene(selectedFile)
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
        id: exportStlDialog
        title: "Export Visible Editable Meshes as Binary STL"
        fileMode: FileDialog.SaveFile
        nameFilters: ["STL triangle mesh (*.stl)"]
        defaultSuffix: "stl"

        onAccepted: {
            const result = projectIO.exportStl(selectedFile, root.serializeScene())
            if (!result.ok) {
                root.setStatus("STL export failed: " + result.error)
            } else if (result.warning) {
                root.setStatus("STL exported to " + result.path + ". " + result.warning)
            } else {
                root.setStatus("STL exported to " + result.path)
            }
        }
    }

    ColorDialog {
        id: customMaterialColorDialog
        title: "Choose a material color"
        onAccepted: root.setMaterialCustomColor(selectedColor)
    }

    ColorDialog {
        id: customFillLightColorDialog
        title: "Choose a fill-light colour"
        onAccepted: root.setCustomFillLightColor(selectedColor)
        onRejected: root.refreshFillTintSelection()
    }

    ColorDialog {
        id: backgroundColorDialog
        title: "Choose a scene background colour"
        onAccepted: root.setBackgroundColor(selectedColor)
    }

    FileDialog {
        id: exportImageDialog
        title: "Export Viewport Preview"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG image (*.png)"]
        defaultSuffix: "png"

        onAccepted: root.exportViewportPreview(selectedFile)
    }

    Dialog {
        id: introDialog
        title: "Welcome to QALYVYRON"
        modal: true
        focus: true
        width: 650
        height: 450
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.NoButton
        onClosed: projectIO.setShowWelcomeOnStartup(!rememberWelcomeCheck.checked)

        background: Rectangle {
            color: "#111111"
            border.color: "#454545"
            radius: 7
        }

        contentItem: ColumnLayout {
            spacing: 13
            Text {
                Layout.fillWidth: true
                text: "Make something in 3D."
                color: root.mainText
                font.pixelSize: 26
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "No experience needed. Start with one simple shape, then move it, change its color and save your work."
                color: root.secondaryText
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 108
                    radius: 5
                    color: "#191919"
                    border.color: root.borderColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Text { text: "01  ADD"; color: root.mainText; font.bold: true; font.pixelSize: 12 }
                        Text {
                            Layout.fillWidth: true
                            text: "Choose Cube, Ball, or another shape on the left."
                            color: root.secondaryText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 108
                    radius: 5
                    color: "#191919"
                    border.color: root.borderColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Text { text: "02  MOVE"; color: root.mainText; font.bold: true; font.pixelSize: 12 }
                        Text {
                            Layout.fillWidth: true
                            text: "Drag a shape to move it. Right-drag the background to look around."
                            color: root.secondaryText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 108
                    radius: 5
                    color: "#191919"
                    border.color: root.borderColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Text { text: "03  FINISH"; color: root.mainText; font.bold: true; font.pixelSize: 12 }
                        Text {
                            Layout.fillWidth: true
                            text: "Save a scene to reopen it later, or render a PNG picture."
                            color: root.secondaryText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: "Start creating"
                    highlighted: true
                    onClicked: {
                        root.beginnerMode = true
                        introDialog.close()
                        root.selectObject("object-1")
                        root.setStatus("Start by choosing a shape on the left.")
                    }
                }
                Button {
                    text: "Take the quick tour"
                    onClicked: {
                        root.tutorialStep = 0
                        introDialog.close()
                        Qt.callLater(tutorialDialog.open)
                    }
                }
                Button {
                    text: "Import a model"
                    onClicked: {
                        introDialog.close()
                        Qt.callLater(importAssetDialog.open)
                    }
                }
            }

            Item { Layout.fillHeight: true }

            CheckBox {
                id: rememberWelcomeCheck
                checked: false
                text: "Don't show this screen when I launch QALYVYRON"
            }
        }
    }

    Dialog {
        id: tutorialDialog
        title: "The one-minute tour"
        modal: true
        focus: true
        width: 560
        height: 350
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: ["1 of 4  ·  Add a shape",
                       "2 of 4  ·  Move and look around",
                       "3 of 4  ·  Change the look",
                       "4 of 4  ·  Save or render"][root.tutorialStep]
                color: root.mainText
                font.pixelSize: 21
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: root.borderColor
            }
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: [
                    "Click a shape on the left to add it. A cube is already waiting for you. Nothing you do here changes your computer or original files until you save or export.",
                    "Left-drag a shape to move it. Right-drag an empty area to rotate your view. Turn the mouse wheel to zoom in or out. Click any shape to select it.",
                    "Select a shape, then choose a color in the right-hand Look panel. Use the Size slider to make it bigger or smaller. Choose Show all tools when you are ready for detailed controls.",
                    "Click Save to keep an editable QALYVYRON scene. Click Render Image to create a PNG image from the scene. Export Preview is only a picture of the current viewport."
                ][root.tutorialStep]
                color: root.secondaryText
                font.pixelSize: 14
                lineHeight: 1.25
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Back"
                    enabled: root.tutorialStep > 0
                    onClicked: root.tutorialStep = Math.max(0, root.tutorialStep - 1)
                }
                Button {
                    visible: root.tutorialStep === 0
                    text: "Add a cube now"
                    onClicked: {
                        root.createPrimitive("Cube")
                        root.tutorialStep = 1
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: root.tutorialStep === 3 ? "Finish" : "Next"
                    highlighted: true
                    onClicked: {
                        if (root.tutorialStep < 3)
                            root.tutorialStep += 1
                        else
                            tutorialDialog.close()
                    }
                }
            }
        }
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
                    onClicked: root.requestNewScene()
                }
                Button {
                    text: "Open"
                    onClicked: openSceneDialog.open()
                }
                Button {
                    text: "Save"
                    onClicked: root.saveCurrentProject()
                }
                Button {
                    text: "Save As…"
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
                Button {
                    text: "First steps"
                    onClicked: introDialog.open()
                }
                Button {
                    text: root.beginnerMode ? "Show all tools" : "Simple view"
                    onClicked: {
                        root.beginnerMode = !root.beginnerMode
                        root.setStatus(root.beginnerMode
                                       ? "Simple view: common controls only."
                                       : "All tools are visible.")
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "VISUAL-FIRST WORKSPACE"
                    color: "#707070"
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                }

                Button {
                    text: "Render Image"
                    highlighted: true
                    onClicked: renderSettingsDialog.open()
                }
                Button {
                    text: "Export OBJ"
                    onClicked: exportObjDialog.open()
                }
                Button {
                    text: "Export STL"
                    onClicked: exportStlDialog.open()
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
                        text: root.beginnerMode ? "ADD A SHAPE" : "CREATE"
                        color: root.secondaryText
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.8
                    }

                    Text {
                        visible: root.beginnerMode
                        Layout.fillWidth: true
                        text: "Pick one. You can move it or change its color later."
                        color: "#858585"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
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
                            text: "+ Ball"
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
                        Button {
                            Layout.fillWidth: true
                            text: "+ Torus"
                            onClicked: root.createPrimitive("Torus")
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Import a 3D model"
                        onClicked: importAssetDialog.open()
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Open a model file (.glb, .gltf, .obj or .stl)."
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
                            text: root.beginnerMode ? "YOUR OBJECTS" : "SCENE OBJECTS"
                            color: root.secondaryText
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1.4
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.hierarchyFilter.trim().length > 0
                                  ? root.visibleHierarchyMatchCount() + " / " + objectList.count
                                  : String(objectList.count)
                            color: root.secondaryText
                            font.pixelSize: 10
                        }
                    }

                    TextField {
                        id: hierarchySearchField
                        Layout.fillWidth: true
                        placeholderText: "Find by name, shape or ID…"
                        selectByMouse: true
                        text: root.hierarchyFilter
                        onTextChanged: root.hierarchyFilter = text
                    }

                    ListView {
                        id: hierarchyView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        // Hidden delegates collapse to zero height so the filter
                        // does not leave empty rows between matching objects.
                        spacing: 0
                        model: objectList

                        delegate: Rectangle {
                            required property int index
                            required property string objectId
                            required property string label
                            required property string shape
                            required property bool isVisible

                            width: hierarchyView.width
                            height: root.objectMatchesFilter(label, shape, objectId) ? 39 : 0
                            radius: 3
                            visible: height > 0
                            opacity: isVisible ? 1.0 : 0.48
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

                                Button {
                                    Layout.preferredWidth: 31
                                    Layout.preferredHeight: 27
                                    text: isVisible ? "◉" : "○"
                                    ToolTip.visible: hovered
                                    ToolTip.text: isVisible ? "Hide this object" : "Show this object"
                                    onClicked: root.toggleObjectVisibility(index)
                                }
                            }

                            MouseArea {
                                // Leave the right edge free so the visibility button receives clicks.
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: Math.max(0, parent.width - 42)
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

                        Text {
                            anchors.centerIn: parent
                            visible: objectList.count > 0
                                     && root.hierarchyFilter.trim().length > 0
                                     && root.visibleHierarchyMatchCount() === 0
                            text: "No objects match that search"
                            color: "#666666"
                            font.pixelSize: 11
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        visible: !root.beginnerMode
                        text: "Edit Selected Mesh"
                        enabled: root.selectedIndex >= 0 && root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                        onClicked: root.openMeshEditor()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Copy selected"
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
                                text: root.beginnerMode
                                      ? "Drag a shape to move it  •  Right-drag to look around  •  Wheel to zoom"
                                      : "Right-drag: orbit    •    Wheel: zoom    •    Click: select"
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
                            camera: root.orthographicMode ? orthographicCamera : camera
                            environment: SceneEnvironment {
                                backgroundMode: root.transparentBackground
                                                ? SceneEnvironment.Transparent : SceneEnvironment.Color
                                clearColor: root.backgroundColor
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

                            OrthographicCamera {
                                id: orthographicCamera
                                position: camera.position
                                eulerRotation: camera.eulerRotation
                                clipNear: 1
                                clipFar: 4000
                                // Match perspective zoom at the current orbit distance.
                                // These values are the half-width and half-height of the view box.
                                verticalMagnification: Math.max(
                                    80, root.orbitDistance
                                       * Math.tan(camera.fieldOfView * Math.PI / 360))
                                horizontalMagnification: verticalMagnification
                                    * Math.max(0.1, view3d.width / Math.max(1, view3d.height))
                            }

                            DirectionalLight {
                                id: keyLight
                                eulerRotation: root.keyLightRotation
                                brightness: 1.7
                                castsShadow: true
                            }

                            PointLight {
                                id: fillLight
                                position: root.fillLightPosition
                                brightness: 65
                                color: root.fillLightColor
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
                                // Snap the object's world position only when the user enables it.
                                // The starting drag position is kept unchanged, so movement stays stable.
                                const movedX = start.x + point.x - anchor.x
                                const movedY = start.y + point.y - anchor.y
                                const movedZ = start.z + point.z - anchor.z
                                movingObject.position = Qt.vector3d(
                                    root.snapMovementValue(movedX),
                                    root.snapMovementValue(movedY),
                                    root.snapMovementValue(movedZ)
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
                                root.orbitDistance = Math.max(120, Math.min(2800,
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
                            id: meshEdgeHandles
                            model: meshEditDialog.visible && root.selectedEditableGeometry()
                                   ? root.selectedEditableGeometry().edgeCount : 0

                            delegate: Item {
                                required property int index
                                readonly property var edgePair: {
                                    const geometry = root.selectedEditableGeometry()
                                    if (!geometry)
                                        return []
                                    const revision = geometry.edgeCount
                                    return geometry.edgeVertexIndices(index)
                                }
                                readonly property var projectedA: {
                                    const geometry = root.selectedEditableGeometry()
                                    if (!geometry || !root.selectedObject || edgePair.length !== 2)
                                        return Qt.vector3d(-1000, -1000, -1)
                                    const revision = geometry.edgeCount
                                    const local = geometry.vertexPosition(Number(edgePair[0]))
                                    return view3d.mapFrom3DScene(root.selectedObject.mapPositionToScene(local))
                                }
                                readonly property var projectedB: {
                                    const geometry = root.selectedEditableGeometry()
                                    if (!geometry || !root.selectedObject || edgePair.length !== 2)
                                        return Qt.vector3d(-1000, -1000, -1)
                                    const revision = geometry.edgeCount
                                    const local = geometry.vertexPosition(Number(edgePair[1]))
                                    return view3d.mapFrom3DScene(root.selectedObject.mapPositionToScene(local))
                                }
                                readonly property real dx: projectedB.x - projectedA.x
                                readonly property real dy: projectedB.y - projectedA.y
                                readonly property real edgeLength: Math.sqrt(dx * dx + dy * dy)
                                readonly property bool isEdgeSelected: root.meshEdgeSelection.indexOf(index) >= 0
                                                                              || index === edgeIndexSpinner.value
                                x: (projectedA.x + projectedB.x) / 2 - width / 2
                                y: (projectedA.y + projectedB.y) / 2 - height / 2
                                width: Math.max(12, edgeLength)
                                height: 14
                                rotation: Math.atan2(dy, dx) * 180 / Math.PI
                                transformOrigin: Item.Center
                                z: 28
                                visible: meshEditDialog.visible
                                         && root.selectedObject !== null
                                         && !root.selectedObject.editorImported
                                         && edgePair.length === 2
                                         && projectedA.z >= 0 && projectedB.z >= 0
                                         && projectedA.x >= 0 && projectedA.x <= view3d.width
                                         && projectedA.y >= 0 && projectedA.y <= view3d.height
                                         && projectedB.x >= 0 && projectedB.x <= view3d.width
                                         && projectedB.y >= 0 && projectedB.y <= view3d.height

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.isEdgeSelected ? 4 : 2
                                    radius: 1
                                    color: parent.isEdgeSelected ? "#f5f5f5" : "#666666"
                                    opacity: parent.isEdgeSelected ? 0.95 : 0.72
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectMeshEdge(index)
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

                            Repeater {
                                model: ["x", "y", "z"]
                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property point axisPoint: root.gizmoAxisHandlePoint(modelData)
                                    x: transformGizmo.width / 2
                                    y: transformGizmo.height / 2 - height / 2
                                    width: 29
                                    height: 2
                                    visible: axisPoint.x > -100
                                    transformOrigin: Item.Left
                                    rotation: root.gizmoAxisRotation(modelData)
                                    color: modelData === "x" ? "#eeeeee"
                                         : modelData === "y" ? "#bdbdbd" : "#858585"
                                    opacity: 0.95
                                }
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
                                    // The center gizmo is another way to move an object,
                                    // so it uses the same snapping rule as a direct viewport drag.
                                    const movedX = start.x + pointer.x - pointerStart.x
                                    const movedY = start.y + pointer.y - pointerStart.y
                                    const movedZ = start.z + pointer.z - pointerStart.z
                                    dragObject.position = Qt.vector3d(
                                        root.snapMovementValue(movedX),
                                        root.snapMovementValue(movedY),
                                        root.snapMovementValue(movedZ)
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
                            // The labeled end handles move on one world axis at a time.
                            Repeater {
                                model: ["x", "y", "z"]
                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property point axisPoint: root.gizmoAxisHandlePoint(modelData)
                                    width: 17
                                    height: 17
                                    radius: 5
                                    x: axisPoint.x - width / 2
                                    y: axisPoint.y - height / 2
                                    visible: axisPoint.x > -100
                                    color: modelData === "x" ? "#eeeeee"
                                         : modelData === "y" ? "#bdbdbd" : "#858585"
                                    border.color: "#111111"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.toUpperCase()
                                        color: "#111111"
                                        font.pixelSize: 9
                                        font.bold: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor

                                        onPressed: function(mouse) {
                                            const pointer = mapToItem(root, mouse.x, mouse.y)
                                            root.beginAxisMove(modelData, pointer.x, pointer.y)
                                        }

                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            const pointer = mapToItem(root, mouse.x, mouse.y)
                                            root.updateAxisMove(modelData, pointer.x, pointer.y)
                                        }

                                        onReleased: root.endAxisMove()
                                        onCanceled: root.endAxisMove()
                                    }
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
                                        root.pushUndoSnapshot()
                                        root.orbitYaw = modelData.yaw
                                        root.orbitPitch = modelData.pitch
                                        root.orthographicMode = true
                                        root.updateCamera()
                                    }
                                }
                            }
                            Button {
                                width: 118
                                height: 27
                                text: root.orthographicMode ? "ORTHOGRAPHIC" : "PERSPECTIVE"
                                onClicked: root.toggleCameraProjection()
                                ToolTip.visible: hovered
                                ToolTip.text: "Switch between parallel projection and perspective depth."
                            }
                        }

                        RowLayout {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 14
                            spacing: 6

                            Button {
                                text: "Frame All"
                                onClicked: root.frameAllObjects()
                            }
                            Button {
                                text: "Focus Selected"
                                enabled: root.selectedObject !== null
                                onClicked: root.focusSelectedObject()
                            }
                            Button {
                                id: gridSnapButton
                                text: root.snapMovementToGrid ? "Snap: On" : "Snap: Off"
                                checkable: true
                                checked: root.snapMovementToGrid
                                onToggled: {
                                    root.snapMovementToGrid = checked
                                    root.setStatus(checked
                                        ? "Grid snapping is on. Dragged objects land on the chosen grid step."
                                        : "Grid snapping is off. Objects can move freely.")
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: "Toggle snapping while dragging an object."
                            }
                            ComboBox {
                                id: movementSnapStepCombo
                                Layout.preferredWidth: 92
                                model: ["1", "5", "10", "25", "50"]
                                currentIndex: 2
                                onActivated: function(index) {
                                    root.movementSnapStep = Number(textAt(index))
                                    root.setStatus("Grid snap step set to "
                                                   + root.movementSnapStep + " units.")
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: "Choose how far apart the snapping grid steps are."
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
                        visible: !root.beginnerMode
                        text: "TRANSFORM"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    Text {
                        visible: !root.beginnerMode
                        text: "Position"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        visible: !root.beginnerMode
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
                        visible: !root.beginnerMode
                        text: "Rotation (degrees)"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        visible: !root.beginnerMode
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

                    Text {
                        visible: !root.beginnerMode
                        text: "Scale (independent axes)"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Text { text: "X"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: scaleXField
                            Layout.fillWidth: true
                            text: "1"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: 0.01; top: 1000 }
                            onEditingFinished: root.updateTransform("scaleX", text)
                        }
                        Text { text: "Y"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: scaleYField
                            Layout.fillWidth: true
                            text: "1"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: 0.01; top: 1000 }
                            onEditingFinished: root.updateTransform("scaleY", text)
                        }
                        Text { text: "Z"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: scaleZField
                            Layout.fillWidth: true
                            text: "1"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: 0.01; top: 1000 }
                            onEditingFinished: root.updateTransform("scaleZ", text)
                        }
                    }

                    Button {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        text: "Reset Transform"
                        enabled: root.selectedObject !== null
                        onClicked: root.resetSelectedTransform()
                        ToolTip.visible: hovered
                        ToolTip.text: "Move the object to the origin, clear rotation, and restore scale to 1."
                    }

                    Text {
                        visible: root.beginnerMode
                        text: "Size"
                        color: root.secondaryText
                        font.pixelSize: 11
                    }

                    Slider {
                        id: beginnerScaleSlider
                        visible: root.beginnerMode
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null
                        from: 0.25
                        to: 3
                        stepSize: 0.05
                        value: root.selectedObject
                               ? Math.max(0.25, Math.min(3, root.selectedObject.uniformScale)) : 1
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: {
                            if (!root.selectedObject)
                                return
                            const scale = Math.max(0.25, Math.min(3, value))
                            root.selectedObject.uniformScale = scale
                            root.selectedObject.scale = Qt.vector3d(scale, scale, scale)
                            if (root.selectedIndex >= 0) {
                                objectList.setProperty(root.selectedIndex, "scale", scale)
                                objectList.setProperty(root.selectedIndex, "scaleX", scale)
                                objectList.setProperty(root.selectedIndex, "scaleY", scale)
                                objectList.setProperty(root.selectedIndex, "scaleZ", scale)
                            }
                            root.refreshInspector()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: root.beginnerMode ? "LOOK" : "MATERIAL"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    Text {
                        text: root.beginnerMode ? "Color" : "Base color"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    RowLayout {
                        visible: root.beginnerMode
                        Layout.fillWidth: true
                        spacing: 5

                        Repeater {
                            model: [
                                {name: "Silver", value: "#c8c8c8"},
                                {name: "White", value: "#f2f2f2"},
                                {name: "Graphite", value: "#555555"},
                                {name: "Red", value: "#df5b57"},
                                {name: "Blue", value: "#508bdc"},
                                {name: "Gold", value: "#d8ae57"}
                            ]

                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 31
                                radius: 4
                                color: modelData.value
                                border.width: root.selectedObject
                                             && String(root.selectedObject.editorColor).toLowerCase()
                                                === modelData.value ? 3 : 1
                                border.color: root.selectedObject
                                               && String(root.selectedObject.editorColor).toLowerCase()
                                                  === modelData.value ? "#ffffff" : "#454545"
                                ToolTip.visible: swatchMouse.containsMouse
                                ToolTip.text: modelData.name

                                MouseArea {
                                    id: swatchMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.setMaterialColorByIndex(index)
                                }
                            }
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Choose custom color…"
                        enabled: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                        onClicked: root.openCustomColorPicker()
                    }

                    ComboBox {
                        id: colorCombo
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        model: ["Silver", "White", "Graphite", "Red", "Blue", "Gold"]
                        onActivated: root.setMaterialColorByIndex(currentIndex)
                    }

                    Text {
                        visible: !root.beginnerMode
                        text: "QUICK MATERIAL PRESETS"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.1
                    }

                    GridLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6

                        Button {
                            Layout.fillWidth: true
                            text: "Plastic"
                            enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                            onClicked: root.applyMaterialPreset("plastic")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Metal"
                            enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                            onClicked: root.applyMaterialPreset("metal")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Rubber"
                            enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                            onClicked: root.applyMaterialPreset("rubber")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "Gold"
                            enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                            onClicked: root.applyMaterialPreset("gold")
                        }
                    }

                    CheckBox {
                        visible: !root.beginnerMode
                        enabled: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                                 && root.selectedObject.geometry !== null
                        text: "Smooth shading"
                        checked: root.selectedObject !== null
                                 && !root.selectedObject.editorImported
                                 && root.selectedObject.geometry !== null
                                 ? root.selectedObject.geometry.smoothShading : false
                        ToolTip.visible: hovered
                        ToolTip.text: "Average shared-vertex normals for a rounded look on curved meshes."
                        onToggled: {
                            if (!root.selectedObject || root.selectedObject.editorImported
                                    || !root.selectedObject.geometry
                                    || root.selectedObject.geometry.smoothShading === checked) {
                                return
                            }
                            root.pushUndoSnapshot()
                            root.selectedObject.geometry.smoothShading = checked
                            root.setStatus(checked
                                           ? "Smooth shading enabled for " + root.selectedObject.editorLabel + "."
                                           : "Flat shading enabled for " + root.selectedObject.editorLabel + ".")
                        }
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
                        visible: !root.beginnerMode
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
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        from: 0
                        to: 1
                        value: 0.38
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: root.setMaterialRoughness(value)
                    }

                    Text {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        text: "Lower roughness creates a smoother surface. Higher roughness diffuses reflections."
                        color: "#6f6f6f"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        Text {
                            text: "Metalness"
                            color: root.secondaryText
                            font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.selectedObject
                                  ? Number(root.selectedObject.editorMetalness).toFixed(2) : "0.08"
                            color: "#cecece"
                            font.pixelSize: 10
                        }
                    }

                    Slider {
                        id: metalnessSlider
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        enabled: root.selectedObject !== null && !root.selectedObject.editorImported
                        from: 0
                        to: 1
                        value: 0.08
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: root.setMaterialMetalness(value)
                    }

                    Text {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        text: "Low values behave like non-metals; high values tint the surface reflections with the material color."
                        color: "#6f6f6f"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        visible: !root.beginnerMode
                        text: "LIGHTING"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    RowLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        Text { text: "Key light"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(keyLight.brightness).toFixed(1); color: "#cecece"; font.pixelSize: 10 }
                    }

                    Slider {
                        id: keyBrightnessSlider
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        from: 0
                        to: 4
                        value: keyLight.brightness
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: keyLight.brightness = value
                    }

                    Text {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        text: "Key light rotation (degrees)"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5

                        Text { text: "X"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: keyLightRotationXField
                            Layout.fillWidth: true
                            text: "-42"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -180; top: 180 }
                            onEditingFinished: root.updateKeyLightRotation("x", text)
                        }
                        Text { text: "Y"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: keyLightRotationYField
                            Layout.fillWidth: true
                            text: "-35"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -180; top: 180 }
                            onEditingFinished: root.updateKeyLightRotation("y", text)
                        }
                        Text { text: "Z"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: keyLightRotationZField
                            Layout.fillWidth: true
                            text: "0"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -180; top: 180 }
                            onEditingFinished: root.updateKeyLightRotation("z", text)
                        }
                    }

                    RowLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        Text { text: "Fill light"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(fillLight.brightness).toFixed(0); color: "#cecece"; font.pixelSize: 10 }
                    }

                    Text {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        text: "Fill light position (scene units)"
                        color: root.secondaryText
                        font.pixelSize: 10
                    }

                    GridLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5

                        Text { text: "X"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: fillLightXField
                            Layout.fillWidth: true
                            text: "-160"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -1000; top: 1000 }
                            onEditingFinished: root.updateFillLightPosition("x", text)
                        }
                        Text { text: "Y"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: fillLightYField
                            Layout.fillWidth: true
                            text: "190"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -1000; top: 1000 }
                            onEditingFinished: root.updateFillLightPosition("y", text)
                        }
                        Text { text: "Z"; color: "#8f8f8f"; font.pixelSize: 11 }
                        TextField {
                            id: fillLightZField
                            Layout.fillWidth: true
                            text: "140"
                            selectByMouse: true
                            validator: DoubleValidator { bottom: -1000; top: 1000 }
                            onEditingFinished: root.updateFillLightPosition("z", text)
                        }
                    }

                    ComboBox {
                        id: fillTintCombo
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        model: ["Neutral fill", "Warm fill", "Cool fill", "Custom…"]
                        onActivated: root.applyFillLightTint(currentIndex)
                    }

                    Rectangle {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        visible: !root.beginnerMode
                        text: "CAMERA"
                        color: "#bcbcbc"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.3
                    }

                    RowLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        Text { text: "Field of view"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: Number(camera.fieldOfView).toFixed(0) + "°"; color: "#cecece"; font.pixelSize: 10 }
                    }

                    Slider {
                        id: cameraFovSlider
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        from: 25
                        to: 80
                        value: camera.fieldOfView
                        onPressedChanged: if (pressed) root.pushUndoSnapshot()
                        onMoved: camera.fieldOfView = value
                    }

                    RowLayout {
                        visible: !root.beginnerMode
                        Layout.fillWidth: true
                        Text { text: "Background"; color: root.secondaryText; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 24
                            height: 20
                            radius: 2
                            color: root.backgroundColor
                            border.color: root.borderColor
                        }
                        Text {
                            text: String(root.backgroundColor)
                            color: "#cecece"
                            font.pixelSize: 10
                        }
                        Button {
                            text: "Choose…"
                            onClicked: root.openBackgroundColorPicker()
                        }
                    }

                    CheckBox {
                        visible: !root.beginnerMode
                        text: "Transparent background in final PNG"
                        checked: root.transparentBackground
                        onToggled: {
                            if (root.transparentBackground === checked)
                                return
                            root.pushUndoSnapshot()
                            root.transparentBackground = checked
                            root.setStatus(checked
                                ? "Final PNG will use transparent pixels outside visible geometry."
                                : "Final PNG will use the selected solid background colour.")
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Writes alpha transparency outside the visible geometry in the offline PNG render."
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
                            text: root.beginnerMode
                                  ? "QUICK TIP\nDrag a shape to move it. Right-drag the empty space to look around. Use the mouse wheel to zoom."
                                  : "ADVANCED TOOLS\nEditable polygon meshes • vertex editing • extrusion and inset • OBJ import/export • materials • lighting • camera"
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
                    color: root.projectDirty ? "#d8ae57" : "#83bd94"
                }
                Text {
                    text: !root.currentProjectUrl || String(root.currentProjectUrl).length === 0
                          ? (root.projectDirty ? "New scene — unsaved changes" : "New scene — not saved yet")
                          : (root.projectDirty ? "Unsaved changes" : "All changes saved")
                    color: root.projectDirty ? "#d8ae57" : "#888888"
                    font.pixelSize: 10
                    Layout.rightMargin: 10
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
        rememberWelcomeCheck.checked = !projectIO.showWelcomeOnStartup()
        if (projectIO.showWelcomeOnStartup())
            Qt.callLater(introDialog.open)
    }
}
