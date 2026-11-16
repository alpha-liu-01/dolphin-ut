// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// Controllers (stage 3.5): the four GameCube ports and the four Wii Remotes, as the Android app's
// "GameCube Input" and "Wii Input" settings.
Page {
    id: page

    header: PageHeader {
        title: "Controllers"
    }

    function gcTypeName(port) {
        const value = controllers.gcPortType(port)
        const types = controllers.gcPortTypes()
        for (let i = 0; i < types.length; ++i) {
            if (types[i].value === value)
                return types[i].name
        }
        return "?"
    }

    function wiimoteSummary(index) {
        const source = controllers.wiimoteSource(index)
        if (source === 0)
            return "None"
        if (source === 2)
            return "Real Wii Remote"
        const extension = controllers.wiimoteExtension(index)
        return "Emulated" + (extension > 0 ? " + " + controllers.wiimoteExtensions()[extension] : "")
    }

    Flickable {
        id: flickable

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: column.height
        clip: true

        Column {
            id: column

            width: parent.width

            ListItem {
                height: units.gu(5)
                Label {
                    anchors {
                        left: parent.left
                        leftMargin: units.gu(2)
                        verticalCenter: parent.verticalCenter
                    }
                    text: "GameCube Controllers"
                    font.bold: true
                }
            }
            Repeater {
                model: 4
                delegate: ListItem {
                    id: gcItem
                    property string summary: page.gcTypeName(index)
                    height: gcLayout.height + (divider.visible ? divider.height : 0)
                    onClicked: pageStack.push(Qt.resolvedUrl("GameCubePortPage.qml"), { port: index })
                    Connections {
                        target: controllers
                        function onPortsChanged() { gcItem.summary = page.gcTypeName(index) }
                    }
                    ListItemLayout {
                        id: gcLayout
                        title.text: "GameCube Controller " + (index + 1)
                        subtitle.text: gcItem.summary
                        ProgressionSlot {}
                    }
                }
            }

            ListItem {
                height: units.gu(5)
                Label {
                    anchors {
                        left: parent.left
                        leftMargin: units.gu(2)
                        verticalCenter: parent.verticalCenter
                    }
                    text: "Wii Remotes"
                    font.bold: true
                }
            }
            Repeater {
                model: 4
                delegate: ListItem {
                    id: wiiItem
                    property string summary: page.wiimoteSummary(index)
                    height: wiiLayout.height + (divider.visible ? divider.height : 0)
                    onClicked: pageStack.push(Qt.resolvedUrl("WiimotePage.qml"), { wiimote: index })
                    Connections {
                        target: controllers
                        function onPortsChanged() { wiiItem.summary = page.wiimoteSummary(index) }
                    }
                    ListItemLayout {
                        id: wiiLayout
                        title.text: "Wii Remote " + (index + 1)
                        subtitle.text: wiiItem.summary
                        ProgressionSlot {}
                    }
                }
            }

            ListItem {
                height: devicesLabel.height + units.gu(3)
                divider.visible: false
                Label {
                    id: devicesLabel
                    anchors {
                        left: parent.left
                        right: parent.right
                        margins: units.gu(2)
                        verticalCenter: parent.verticalCenter
                    }
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.backgroundSecondaryText
                    text: "Input devices: " + (controllers.devices.length ? controllers.devices.join(", ") : "none") +
                          "\nA gamepad that connects is mapped to GameCube Controller 1 and Wii Remote 1 when they have no usable device."
                }
            }
        }
    }
}
