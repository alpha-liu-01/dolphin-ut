// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// The Android app's game details dialog, as a page: banner, title, description, then the facts.
Page {
    id: page

    property var details: ({})

    header: PageHeader {
        id: header
        title: "Game Details"
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
            spacing: units.gu(1.5)

            Image {
                // GameCube banners are 96x32, Wii banners 192x64: scaled up without smoothing.
                width: Math.min(parent.width, units.gu(36))
                height: width / 3
                visible: page.details.banner !== undefined && page.details.banner !== ""
                source: visible ? page.details.banner : ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                cache: false
            }

            Label {
                width: parent.width
                text: page.details.title || ""
                textSize: Label.Large
                wrapMode: Text.Wrap
            }

            Label {
                width: parent.width
                visible: text !== ""
                text: page.details.description || ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
            }

            Column {
                width: parent.width

                Repeater {
                    model: [
                        { label: "Country", value: page.details.country },
                        { label: "Company", value: page.details.company },
                        { label: "Game ID", value: page.details.gameId },
                        { label: "Revision", value: page.details.revision },
                        { label: "File Size", value: page.details.fileSize },
                        { label: "File Format", value: page.details.fileFormat },
                        { label: "Compression", value: page.details.compression },
                        { label: "Block Size", value: page.details.blockSize },
                        { label: "File", value: page.details.path }
                    ]

                    delegate: ListItem {
                        width: parent.width
                        height: visible ? row.height : 0
                        visible: modelData.value !== undefined && modelData.value !== ""

                        ListItemLayout {
                            id: row

                            padding.leading: 0
                            padding.trailing: 0
                            title.text: modelData.label
                            title.color: theme.palette.normal.backgroundSecondaryText
                            subtitle.text: String(modelData.value)
                            subtitle.textSize: Label.Medium
                            subtitle.color: theme.palette.normal.backgroundText
                            subtitle.wrapMode: Text.WrapAnywhere
                            subtitle.maximumLineCount: 4
                        }
                    }
                }
            }
        }
    }

    Scrollbar {
        flickableItem: flickable
    }
}
