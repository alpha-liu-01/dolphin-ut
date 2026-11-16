// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Window
import Lomiri.Components

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
        applicationName: "dolphin-ut"

        PageStack {
            id: pageStack

            Component.onCompleted: push(Qt.resolvedUrl("LibraryPage.qml"))
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
