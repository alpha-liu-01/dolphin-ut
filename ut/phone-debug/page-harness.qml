// Opens one of dolphin-ut's pages for a screenshot (stage 3.1), over the library page:
//   sh run-dolphin.sh ut 9 --qml /home/phablet/dolphin-ut-probe/page-harness.qml --test-page NAME \
//     [--test-arg GAME_PATH] --grab /home/phablet/dolphin-ut-probe/page.png --grab-after 6000
// NAME: details (needs GAME_PATH), folders, picker, developer, controllers, gcport, wiimote,
// mapping-gc, mapping-wii, settings:<MENU TAG>, game-settings (--test-arg GAMEID),
// settings-roundtrip; anything else shows the library.
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
            else if (page.startsWith("settings:")) stack.push(base + "SettingsPage.qml", { menuTag: page.substring(9) })
            else if (page === "game-settings") {
                // --test-arg GAMEID: that game's settings
                if (settingsModel.openGame(arg, 0))
                    stack.push(base + "SettingsPage.qml", { closesGame: true, gameTitle: arg })
            }
            else if (page === "game-roundtrip") {
                // A game setting (--test-arg GAMEID): the backend for that game only.
                const name = "StringSetting.MAIN_GFX_BACKEND"
                console.log("GAME global before " + settingsModel.value(name))
                console.log("GAME open " + settingsModel.openGame(arg, 0))
                settingsModel.setValue(name, "OGL")
                console.log("GAME in game " + settingsModel.value(name) + " overridden " + settingsModel.overridden(name))
                settingsModel.closeGame()
                console.log("GAME global after " + settingsModel.value(name))
            }
            else if (page === "settings-roundtrip") {
                // Write a setting, read it back, and print both (for the test copy of the config).
                const name = "BooleanSetting.MAIN_CPU_THREAD"
                console.log("ROUNDTRIP before " + settingsModel.value(name))
                settingsModel.setValue(name, !settingsModel.value(name))
                console.log("ROUNDTRIP after " + settingsModel.value(name))
                console.log("ROUNDTRIP boost " + settingsModel.value("Virtual.CPU_BOOST") + " dsp " + settingsModel.value("Virtual.DSP_ENGINE") + " mem1 " + settingsModel.value("Virtual.MEM1_MB") + " backend " + settingsModel.value("StringSetting.MAIN_GFX_BACKEND") + " speed " + settingsModel.value("FloatSetting.MAIN_EMULATION_SPEED"))
            }
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
