// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Window
import Lomiri.Components
import Lomiri.Components.Popups
import Lomiri.Content

// The app's window. The game runs in its own window (option A, docs/ut-port/stage3-roadmap.md);
// this one holds the library and the other app screens, in Lomiri's native style.
Window {
    id: window

    width: units.gu(50)
    height: units.gu(80)
    visible: true
    title: "Dolphin"
    color: theme.palette.normal.background

    MainView {
        id: mainView

        anchors.fill: parent
        // The click package (or "dolphin-ut" outside Lomiri): set by Storage::ConfigureApplication.
        applicationName: Qt.application.name

        PageStack {
            id: pageStack

            Component.onCompleted: push(Qt.resolvedUrl("LibraryPage.qml"))
        }
    }

    // Games shared from another app (the File Manager's share, content-hub's destination role; stage
    // 3.7). Import Games (ImportPage) brings them the other way, and both end in storage.importFiles.
    property var sharedTransfer: null

    Connections {
        target: ContentHub
        function onImportRequested(transfer) {
            emulator.appendLog("import: shared from " + transfer.source + ", state " + transfer.state)
            if (transfer.state !== ContentTransfer.Charged || storage.importing)
                return
            const urls = []
            for (let i = 0; i < transfer.items.length; ++i)
                urls.push(transfer.items[i].url)
            if (storage.importFiles(urls))
                window.sharedTransfer = transfer
            else
                transfer.finalize()
        }
    }

    Connections {
        target: storage
        function onImportFinished(result) {
            if (window.sharedTransfer) {
                window.sharedTransfer.finalize()
                window.sharedTransfer = null
            }
            // The games folder may have been removed from the library.
            if (result.imported.length > 0) {
                gameList.addFolder(storage.gamesFolder)
                if (emulator.state === "idle")
                    gameList.refresh()
            }
            PopupUtils.open(importResultDialog, mainView, { result: result })
        }
    }

    Component {
        id: importResultDialog

        Dialog {
            id: dialog

            property var result

            title: result.imported.length === 0 ? "Nothing imported" :
                   result.imported.length === 1 ? "1 game imported" : result.imported.length + " games imported"
            text: {
                let lines = result.imported.slice()
                for (let i = 0; i < result.skipped.length; ++i)
                    lines.push(result.skipped[i].name + ": " + result.skipped[i].reason)
                return lines.join("\n")
            }
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // As the Android app: rescan the library when the user comes back to the app (for example after
    // copying games over USB), unless a game is running.
    Connections {
        property bool wasInactive: false

        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                wasInactive = true
            } else if (wasInactive) {
                wasInactive = false
                if (emulator.state === "idle")
                    gameList.refresh()
            }
        }
    }
}
