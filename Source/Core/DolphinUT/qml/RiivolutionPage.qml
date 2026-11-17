// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// Start with Riivolution Patches (stage 6.4), as the Android app's RiivolutionBootActivity: the SD
// root, each patch file's options with their choices ("Disabled" or one of them), and Start. The
// choices are saved when the page goes and before starting, as there (riivolution, Riivolution.cpp).
Page {
    id: page

    property string path
    property string gameId
    property int revision: 0
    property int discNumber: 0
    property string gameTitle

    header: PageHeader {
        title: "Riivolution"
        subtitle: page.gameTitle
    }

    Component.onCompleted: riivolution.load(gameId, revision, discNumber)
    Component.onDestruction: riivolution.save()

    Flickable {
        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: startBar.top
        }
        contentHeight: column.height + units.gu(2)
        clip: true

        Column {
            id: column
            width: parent.width

            Label {
                width: parent.width
                padding: units.gu(2)
                wrapMode: Text.WrapAnywhere
                text: "SD Root: " + riivolution.sdRoot
            }
            Label {
                width: parent.width
                leftPadding: units.gu(2)
                rightPadding: units.gu(2)
                bottomPadding: units.gu(2)
                visible: riivolution.discs.length === 0
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: "No Riivolution patches for this game. Put a patch's files in the SD root, with its XML file in " +
                      riivolution.xmlFolder + "."
            }

            Repeater {
                model: riivolution.discs
                delegate: Column {
                    id: discColumn
                    readonly property int discIndex: index
                    width: column.width

                    ListItem {
                        height: units.gu(5)
                        color: theme.palette.normal.base
                        Label {
                            anchors {
                                left: parent.left
                                leftMargin: units.gu(2)
                                verticalCenter: parent.verticalCenter
                            }
                            text: modelData.name
                            font.bold: true
                        }
                    }

                    Repeater {
                        model: modelData.sections
                        delegate: Column {
                            id: sectionColumn
                            readonly property int sectionIndex: index
                            width: column.width

                            Label {
                                leftPadding: units.gu(2)
                                topPadding: units.gu(1)
                                text: modelData.name
                                color: theme.palette.normal.backgroundSecondaryText
                            }

                            Repeater {
                                model: modelData.options
                                delegate: ListItem {
                                    readonly property var option: modelData
                                    readonly property int optionIndex: index
                                    height: optionLayout.height + (divider.visible ? divider.height : 0)
                                    onClicked: PopupUtils.open(choiceDialog, page, {
                                        disc: discColumn.discIndex, section: sectionColumn.sectionIndex,
                                        option: optionIndex, optionData: option
                                    })
                                    ListItemLayout {
                                        id: optionLayout
                                        title.text: option.name
                                        subtitle.text: option.selected === 0 ? "Disabled" : option.choices[option.selected - 1]
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: startBar
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: units.gu(8)
        color: theme.palette.normal.background
        Button {
            anchors.centerIn: parent
            width: parent.width - units.gu(4)
            text: "Start"
            color: theme.palette.normal.positive
            onClicked: {
                riivolution.save()
                const path = page.path
                pageStack.pop()
                emulator.startWithRiivolution(path)
            }
        }
    }

    Component {
        id: choiceDialog
        Dialog {
            id: dialog
            property int disc
            property int section
            property int option
            property var optionData
            title: optionData.name
            Repeater {
                model: ["Disabled"].concat(dialog.optionData.choices)
                delegate: Button {
                    readonly property bool current: index === dialog.optionData.selected
                    text: (current ? "✓  " : "") + modelData
                    color: current ? theme.palette.normal.positive : theme.palette.normal.base
                    onClicked: {
                        riivolution.setChoice(dialog.disc, dialog.section, dialog.option, index)
                        PopupUtils.close(dialog)
                    }
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
