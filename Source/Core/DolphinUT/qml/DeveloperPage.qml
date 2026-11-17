// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// App Log (stage 7): the app's live log, for bug reports. Development builds (devTools,
// DOLPHIN_UT_DEV_TOOLS) also have the 3.0 test shell's tools: the spike switches, haptics and
// keyboard checks; a release build has the log only. Not part of the Android app.
Page {
    id: page

    header: PageHeader {
        title: "App Log"
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
                visible: devTools
                height: visible ? hideLayout.height : 0
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
                visible: devTools
                height: visible ? pauseLayout.height : 0
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


            Row {
                visible: devTools
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
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: "What the app did since it started. For a bug report, Dolphin's own log can be written to a file too (Settings → Config → Log), and sent from User Data → Send a File to Another App (Logs/dolphin.log)."
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
