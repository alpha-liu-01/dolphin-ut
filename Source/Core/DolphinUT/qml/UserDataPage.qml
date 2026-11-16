// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// Config → User Data (the Android app's UserDataActivity): where the port keeps its files (stage
// 3.7). Importing and exporting the whole user folder as a zip, as on Android, comes later.
Page {
    id: page

    header: PageHeader {
        title: "User Data"
    }

    readonly property var locations: [
        {
            title: "Games",
            path: storage.gamesFolder,
            text: "Shown in the library. Over USB, a computer sees it as Documents/Dolphin/Games. " +
                  "Import Games in the library moves games here from other apps."
        },
        {
            title: "Screenshots",
            path: storage.screenshotsFolder,
            text: "One folder per game. The Gallery shows them."
        },
        {
            title: "User data",
            path: storage.userFolder,
            text: "Settings, saves, memory cards, save states, the Wii NAND, caches and logs. " +
                  "Not visible over USB." + (storage.appId === "" ? "" :
                  " It is the app's own folder: it stays when the app is removed.")
        }
    ]

    Flickable {
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

            Repeater {
                model: page.locations
                delegate: ListItem {
                    height: layout.height + (divider.visible ? divider.height : 0)
                    ListItemLayout {
                        id: layout
                        title.text: modelData.title
                        subtitle.text: modelData.path
                        subtitle.wrapMode: Text.WrapAnywhere
                        subtitle.maximumLineCount: 3
                        summary.text: modelData.text
                        summary.wrapMode: Text.Wrap
                        summary.maximumLineCount: 5
                    }
                }
            }

            Label {
                width: parent.width
                padding: units.gu(2)
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: storage.appId === "" ?
                          "Started without an app ID (from a terminal): the user folder is the one given with -u, or Dolphin's default." :
                          "App ID: " + storage.appId
            }
        }
    }
}
