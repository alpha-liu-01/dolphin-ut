// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// One GameCube port: its controller type (the Android app's gcpadTypeEntries) and its mapping.
Page {
    id: page

    property int port: 0
    property var types: controllers.gcPortTypes()
    property int type: controllers.gcPortType(port)
    // Emulated controllers the mapping editor handles (the Android app's 6, 8, 9, 10, 11).
    readonly property bool mappable: [6, 8, 9, 10, 11].indexOf(type) >= 0

    header: PageHeader {
        title: "GameCube Controller " + (page.port + 1)
    }

    Column {
        anchors {
            top: page.header.bottom
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
            objectName: "typeSelector"
            text: "Controller type"
            model: page.types.map(t => t.name)
            // Assigned once complete (OptionSelector resets selectedIndex when its model is set).
            Component.onCompleted: selectedIndex = page.types.findIndex(t => t.value === page.type)
            onDelegateClicked: index => {
                controllers.setGcPortType(page.port, page.types[index].value)
                page.type = controllers.gcPortType(page.port)
            }
        }

        Button {
            width: parent.width
            visible: page.mappable
            text: "Configure"
            color: theme.palette.normal.positive
            onClicked: pageStack.push(Qt.resolvedUrl("MappingPage.qml"),
                                      { mapping: controllers.mapping("gcpad", page.port) })
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: theme.palette.normal.backgroundSecondaryText
            visible: !page.mappable && page.type !== 0
            text: page.type === 7 ? "The keyboard controller is mapped in DolphinQt for now." :
                  page.type === 12 ? "A GameCube Controller Adapter for Wii U, over USB." : ""
        }
    }
}
