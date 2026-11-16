// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// The 3.0 test shell's tools, kept for development: the live log, the spike switches, haptics and
// keyboard checks. Not part of the Android app.
Page {
    id: page

    header: PageHeader {
        title: "Developer"
    }

    Flickable {
        id: flickable

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: column.height + units.gu(4)
        clip: true

        Column {
            id: column

            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                margins: units.gu(2)
            }
            spacing: units.gu(1)

            ListItem {
                height: hideLayout.height
                ListItemLayout {
                    id: hideLayout
                    padding.leading: 0
                    title.text: "Hide this window while playing"
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: emulator.hideLauncherWhilePlaying
                        onClicked: emulator.hideLauncherWhilePlaying = checked
                    }
                }
            }
            ListItem {
                height: pauseLayout.height
                ListItemLayout {
                    id: pauseLayout
                    padding.leading: 0
                    title.text: "Pause when the app is not active"
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: emulator.pauseWhenInactive
                        onClicked: emulator.pauseWhenInactive = checked
                    }
                }
            }

            ListItem {
                height: statsLayout.height
                ListItemLayout {
                    id: statsLayout
                    padding.leading: 0
                    title.text: "Show FPS and speed in games"
                    subtitle.text: "Dolphin's on-screen statistics (Graphics settings in 3.6)"
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: emulator.showPerformanceStats
                        onClicked: emulator.showPerformanceStats = checked
                    }
                }
            }

            ListItem {
                height: saveStatesLayout.height
                ListItemLayout {
                    id: saveStatesLayout
                    padding.leading: 0
                    title.text: "Enable Save States"
                    subtitle.text: "In the in-game menu; off by default, as in the Android app (Settings in 3.6)"
                    subtitle.maximumLineCount: 2
                    subtitle.wrapMode: Text.Wrap
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: emulator.enableSaveStates
                        onClicked: emulator.enableSaveStates = checked
                    }
                }
            }

            Row {
                spacing: units.gu(1)
                Button {
                    text: "Vibrate"
                    onClicked: lomiri.vibrate(60)
                }
                TextField {
                    width: units.gu(24)
                    placeholderText: "Keyboard test"
                }
            }

            Label {
                text: "Log"
                textSize: Label.Large
            }
            TextArea {
                width: parent.width
                height: units.gu(50)
                readOnly: true
                text: emulator.log
                font.family: "Ubuntu Mono"
                textFormat: TextEdit.PlainText
            }
        }
    }
}
