// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// One Wii Remote: source, extension, motion controls from the phone (stage 6.6), and the mapping
// pages the Android app splits it into (addWiimoteSubSettings: general, motion simulation, motion
// input, extension, options).
Page {
    id: page

    property int wiimote: 0
    property int source: controllers.wiimoteSource(wiimote)
    property int extension: controllers.wiimoteExtension(wiimote)

    // ControllerEmu::GroupType numbers.
    readonly property var groupsButtons: [3]           // Buttons (Buttons, D-Pad)
    readonly property var groupsMotion: [4, 6, 7, 10]  // Force (Swing), Tilt, Cursor (IR), Shake
    readonly property var groupsMotionInput: [11, 12, 13]  // accelerometer, gyroscope, IMU pointer
    readonly property var groupsOther: [0]             // Options, Rumble, Hotkeys and the like

    function open(title, extension, types) {
        pageStack.push(Qt.resolvedUrl("MappingPage.qml"),
                       { mapping: controllers.mapping("wiimote", page.wiimote, extension, types) })
    }

    header: PageHeader {
        title: "Wii Remote " + (page.wiimote + 1)
    }

    Flickable {
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
            spacing: units.gu(2)

            OptionSelector {
                // Lomiri's OptionSelector cannot size its list from a JavaScript array: the width and
                // the list height are given (S3.5: ports 2–4 showed an empty list).
                width: parent.width
                containerHeight: itemHeight * (model ? model.length : 0)
                text: "Wii Remote"
                model: ["None", "Emulated", "Real Wii Remote"]
                // Lomiri's OptionSelector resets selectedIndex when its model is set, so it is
                // assigned once the selector is complete, not bound.
                Component.onCompleted: selectedIndex = page.source
                onDelegateClicked: index => {
                    controllers.setWiimoteSource(page.wiimote, index)
                    page.source = controllers.wiimoteSource(page.wiimote)
                }
            }

            OptionSelector {
                // Lomiri's OptionSelector cannot size its list from a JavaScript array: the width and
                // the list height are given (S3.5: ports 2–4 showed an empty list).
                width: parent.width
                containerHeight: itemHeight * (model ? model.length : 0)
                visible: page.source === 1
                text: "Extension"
                model: controllers.wiimoteExtensions()
                Component.onCompleted: selectedIndex = page.extension
                onDelegateClicked: index => {
                    controllers.setWiimoteExtension(page.wiimote, index)
                    page.extension = controllers.wiimoteExtension(page.wiimote)
                }
            }

            // Stage 6.6: the Android app's motion controls choice (motionControlsEntries). Its pages
            // below show the resulting mapping (Motion Input).
            OptionSelector {
                width: parent.width
                containerHeight: itemHeight * (model ? model.length : 0)
                visible: page.source === 1
                text: "Motion Controls"
                model: ["Use Device Sensors (With Pointer Emulation)",
                        "Use Device Sensors (Without Pointer Emulation)",
                        "Don't Use Device Sensors"]
                Component.onCompleted: selectedIndex = controllers.wiimoteMotion(page.wiimote)
                onDelegateClicked: index => controllers.setWiimoteMotion(page.wiimote, index)
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                visible: page.source === 1
                color: theme.palette.normal.backgroundSecondaryText
                text: "Device sensors: tilting, shaking and turning the phone move the Wii Remote. With pointer emulation, the phone's turning also aims the pointer, instead of your finger."
            }

            Column {
                width: parent.width
                visible: page.source === 1
                spacing: units.gu(1)

                Button {
                    width: parent.width
                    text: "Buttons and D-Pad"
                    color: theme.palette.normal.positive
                    onClicked: page.open("", -1, page.groupsButtons)
                }
                Button {
                    width: parent.width
                    visible: page.extension > 0
                    text: controllers.wiimoteExtensions()[page.extension]
                    color: theme.palette.normal.positive
                    onClicked: page.open("", page.extension, [])
                }
                Button {
                    width: parent.width
                    text: "Motion Simulation (pointer, swing, tilt, shake)"
                    onClicked: page.open("", -1, page.groupsMotion)
                }
                Button {
                    width: parent.width
                    text: "Motion Input"
                    onClicked: page.open("", -1, page.groupsMotionInput)
                }
                Button {
                    width: parent.width
                    text: "Options, Rumble"
                    onClicked: page.open("", -1, page.groupsOther)
                }
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                visible: page.source === 2
                color: theme.palette.normal.backgroundSecondaryText
                text: "A real Wii Remote is used over Bluetooth (\"Refresh Wii Remotes\" in the in-game menu)."
            }
        }
    }
}
