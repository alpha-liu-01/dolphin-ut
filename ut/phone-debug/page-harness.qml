// Opens one of dolphin-ut's pages for a screenshot (stage 3.1), over the library page:
//   sh run-dolphin.sh ut 9 --qml /home/phablet/dolphin-ut-probe/page-harness.qml --test-page NAME \
//     [--test-arg GAME_PATH] --grab /home/phablet/dolphin-ut-probe/page.png --grab-after 6000
// NAME: details (needs GAME_PATH), folders, picker, developer, controllers, gcport, wiimote,
// mapping-gc, mapping-wii; anything else shows the library.
import QtQuick
import QtQuick.Window
import Lomiri.Components

Window {
    visible: true
    width: units.gu(50); height: units.gu(80)
    color: theme.palette.normal.background
    MainView {
        anchors.fill: parent
        applicationName: "dolphin-ut"
        PageStack {
            id: stack
            Component.onCompleted: {
                const a = Qt.application.arguments
                const page = a[a.indexOf("--test-page") + 1]
                const arg = a.indexOf("--test-arg") >= 0 ? a[a.indexOf("--test-arg") + 1] : ""
                const base = "qrc:/qt/qml/DolphinUT/qml/"
                stack.push(base + "LibraryPage.qml")
                timer.page = page; timer.arg = arg; timer.base = base
                timer.start()
            }
        }
    }
    property var testMapping
    Timer {
        id: timer
        property string page; property string arg; property string base
        interval: 2500
        onTriggered: {
            if (page === "details") stack.push(base + "GameDetailsPage.qml", { details: gameList.details(arg) })
            else if (page === "folders") stack.push(base + "GameFoldersPage.qml")
            else if (page === "picker") { stack.push(base + "GameFoldersPage.qml"); stack.push(base + "FolderPickerPage.qml") }
            else if (page === "developer") stack.push(base + "DeveloperPage.qml")
            else if (page === "controllers") stack.push(base + "ControllersPage.qml")
            else if (page === "gcport") stack.push(base + "GameCubePortPage.qml", { port: arg ? Number(arg) : 0 })
            else if (page === "gcport-open") {
                // The type selector opened, as after a tap.
                stack.push(base + "GameCubePortPage.qml", { port: arg ? Number(arg) : 1 })
                const find = (item) => {
                    if (item.objectName === "typeSelector") return item
                    for (let i = 0; i < item.children.length; ++i) {
                        const found = find(item.children[i])
                        if (found) return found
                    }
                    return null
                }
                const selector = find(stack.currentPage)
                console.log("selector " + selector + " model " + (selector ? selector.model : ""))
                if (selector) selector.expanded = true
            }
            else if (page === "wiimote") stack.push(base + "WiimotePage.qml", { wiimote: 0 })
            else if (page === "mapping-gc") stack.push(base + "MappingPage.qml", { mapping: controllers.mapping("gcpad", 0) })
            else if (page === "detect") {
                // Input detection on every device: write "PRESS A" into a Pipes/ FIFO meanwhile.
                testMapping = controllers.mapping("gcpad", 0)
                testMapping.detected.connect((g, c, e) => console.log("DETECTED group " + g + " control " + c + ": " + e))
                testMapping.detect(0, 0, true)
                console.log("DETECTING")
            }
            else if (page === "mapping-wii") stack.push(base + "MappingPage.qml", { mapping: controllers.mapping("wiimote", 0, -1, [3]) })
        }
    }
}
