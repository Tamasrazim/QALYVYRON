import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Dialogs

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

    ListModel {
        id: objectList
    }

    function setStatus(message) {
        statusLabel.text = message
        statusTimer.restart()
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

    function selectObject(objectId) {
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
        const x = Number(saved.x) || 0
        const y = Number(saved.y) || 0
        const z = Number(saved.z) || 0
        const rx = Number(saved.rx) || 0
        const ry = Number(saved.ry) || 0
        const rz = Number(saved.rz) || 0
        const uniformScale = Number(saved.scale) > 0 ? Number(saved.scale) : 1
        const colorValue = saved.color ? String(saved.color) : "#c8c8c8"
        const roughness = saved.roughness !== undefined ? Math.max(0, Math.min(1, Number(saved.roughness))) : 0.38

        const object = primitiveComponent.createObject(sceneRoot, {
            editorId: objectId,
            editorLabel: label,
            editorShape: shape,
            editorColor: colorValue,
            editorRoughness: roughness,
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
            x: x, y: y, z: z,
            rx: rx, ry: ry, rz: rz,
            scale: uniformScale,
            color: colorValue,
            roughness: roughness
        })
        selectObject(objectId)
        setStatus("Created " + label + ".")
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

        const palette = ["#c8c8c8", "#f2f2f2", "#555555", "#df5b57", "#508bdc", "#d8ae57"]
        const colorValue = palette[Math.max(0, Math.min(palette.length - 1, index))]
        selectedObject.editorColor = colorValue
        objectList.setProperty(selectedIndex, "color", colorValue)
        setStatus("Updated material color for " + selectedObject.editorLabel + ".")
    }

    function setMaterialRoughness(value) {
        if (!selectedObject || selectedIndex < 0)
            return

        const roughness = Math.max(0, Math.min(1, value))
        selectedObject.editorRoughness = roughness
        objectList.setProperty(selectedIndex, "roughness", roughness)
    }

    function removeSelected() {
        if (!selectedObject || selectedIndex < 0)
            return

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
        clearScene()
        addPrimitive("Cube", {id: "object-1", label: "Cube 1", x: -60, y: 0, z: 0, scale: 1, color: "#c8c8c8"})
        addPrimitive("Sphere", {id: "object-2", label: "Sphere 2", x: 65, y: 0, z: 0, scale: 1, color: "#508bdc"})
        orbitYaw = 34
        orbitPitch = 23
        orbitDistance = 470
        updateCamera()
        setStatus("New scene ready.")
    }

    function serializeScene() {
        const serializedObjects = []
        for (let i = 0; i < objectList.count; ++i) {
            const row = objectList.get(i)
            serializedObjects.push({
                id: row.objectId,
                label: row.label,
                shape: row.shape,
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
            objects: serializedObjects
        }
    }

    function applyProject(document) {
        clearScene()
        const savedObjects = document.objects || []
        for (let i = 0; i < savedObjects.length; ++i) {
            const entry = savedObjects[i]
            const shape = ["Cube", "Sphere", "Cylinder", "Cone", "Plane"].indexOf(entry.shape) >= 0
                         ? entry.shape : "Cube"
            addPrimitive(shape, entry)
        }

        if (objectList.count === 0)
            addPrimitive("Cube", {label: "Cube 1"})

        selectObject(objectList.get(0).objectId)
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
        id: primitiveComponent

        Model {
            id: editableModel
            property string editorId: ""
            property string editorLabel: "Object"
            property string editorShape: "Cube"
            property color editorColor: "#c8c8c8"
            property real editorRoughness: 0.38
            property real uniformScale: 1.0

            source: root.meshSource(editorShape)
            pickable: true
            materials: [
                PrincipledMaterial {
                    baseColor: editableModel.editorColor
                    roughness: editableModel.editorRoughness
                    metalness: 0.08
                }
            ]
        }
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
            if (result.ok)
                root.applyProject(result.data)
            else
                root.setStatus("Open failed: " + result.error)
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
                        text: "3D STUDIO  /  EARLY PROTOTYPE"
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

                Item { Layout.fillWidth: true }

                Text {
                    text: "VISUAL-FIRST WORKSPACE"
                    color: "#707070"
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
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
                            onClicked: root.addPrimitive("Cube")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Sphere"
                            onClicked: root.addPrimitive("Sphere")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Cylinder"
                            onClicked: root.addPrimitive("Cylinder")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "+ Cone"
                            onClicked: root.addPrimitive("Cone")
                        }
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
                                eulerRotation: Qt.vector3d(-42, -35, 0)
                                brightness: 1.7
                                castsShadow: true
                            }

                            PointLight {
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

                            onPressed: function(mouse) {
                                lastX = mouse.x
                                lastY = mouse.y
                                orbiting = mouse.button === Qt.RightButton
                                movingSelection = false
                                movingObject = null

                                if (mouse.button !== Qt.LeftButton)
                                    return

                                const hit = view3d.pick(mouse.x, mouse.y)
                                if (hit.objectHit && hit.objectHit.editorId) {
                                    root.selectObject(hit.objectHit.editorId)
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
                                Qt.callLater(root.updateGizmo)
                            }

                            onClicked: function(mouse) {
                                if (mouse.button !== Qt.LeftButton)
                                    return

                                const result = view3d.pick(mouse.x, mouse.y)
                                if (result.objectHit && result.objectHit.editorId)
                                    root.selectObject(result.objectHit.editorId)
                                else
                                    root.selectObject("")
                            }
                        }

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse
                            onWheel: function(event) {
                                root.orbitDistance = Math.max(190, Math.min(1050,
                                    root.orbitDistance - event.angleDelta.y * 0.35))
                                root.updateCamera()
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

                                onPressed: function(mouse) {
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
                        model: ["Silver", "White", "Graphite", "Red", "Blue", "Gold"]
                        onActivated: root.setMaterialColorByIndex(currentIndex)
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
                        from: 0
                        to: 1
                        value: 0.38
                        onMoved: root.setMaterialRoughness(value)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Lower roughness creates a smoother surface. Higher roughness diffuses reflections."
                        color: "#6f6f6f"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
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
                            text: "CURRENT BUILD\nPrimitive objects • transforms • base color • roughness • scene files • viewport preview export"
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

    Component.onCompleted: createNewScene()
}
