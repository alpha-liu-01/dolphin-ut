// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Stage 3.0 shell: a launcher to check Qt Quick on the phone and to start the window spike.
// The real library screen (cover grid, 3.1) replaces it.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    title: "Dolphin"


    // hfd-service over D-Bus (the phone has no Qt 6 haptics plugin for org.kde.tactilefeedback).
    function vibrate() {
        lomiri.vibrate(60)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            Label {
                text: "Dolphin — stage 3.0 shell"
                font.bold: true
                Layout.fillWidth: true
            }
            Label {
                text: emulator.state
                rightPadding: 16
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        TextField {
            Layout.fillWidth: true
            placeholderText: "Keyboard test: tap and type"
            onTextEdited: emulator.appendLog("text field: " + text)
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "Vibrate"
                onClicked: root.vibrate()
            }
            Button {
                text: "Refresh"
                onClicked: emulator.refreshGames()
            }
        }

        Switch {
            text: "Hide this window while playing"
            checked: emulator.hideLauncherWhilePlaying
            onToggled: emulator.hideLauncherWhilePlaying = checked
        }
        Switch {
            text: "Pause when the app is not active"
            checked: emulator.pauseWhenInactive
            onToggled: emulator.pauseWhenInactive = checked
        }

        Label {
            text: "Games"
            font.bold: true
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.35)
            clip: true
            model: emulator.games
            delegate: ItemDelegate {
                width: ListView.view.width
                contentItem: ColumnLayout {
                    Label {
                        text: modelData.split("/").pop()
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Button {
                            text: "Vulkan"
                            onClicked: emulator.start(modelData, "Vulkan")
                        }
                        Button {
                            text: "OpenGL ES"
                            onClicked: emulator.start(modelData, "OGL")
                        }
                    }
                }
            }
        }

        Label {
            text: "Log"
            font.bold: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                readOnly: true
                wrapMode: TextArea.Wrap
                font.pixelSize: 11
                text: emulator.log
                onTextChanged: cursorPosition = length
            }
        }
    }
}
