// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtCore
import QtQuick
import Qt.labs.folderlistmodel
import Lomiri.Components

// A folder browser for adding a game folder. Ubuntu Touch has no system folder picker; a confined
// click will see only its own folders and what content-hub hands it (3.7).
Page {
    id: page

    header: PageHeader {
        title: "Add Folder"
        subtitle: decodeURIComponent(folders.folder.toString().replace("file://", ""))
        trailingActionBar.actions: [
            Action {
                iconName: "ok"
                text: "Use This Folder"
                onTriggered: {
                    gameList.addFolder(decodeURIComponent(folders.folder.toString().replace("file://", "")))
                    pageStack.pop()
                }
            },
            Action {
                iconName: "go-up"
                text: "Up"
                enabled: folders.folder.toString() !== "file:///"
                onTriggered: folders.folder = folders.parentFolder
            },
            Action {
                iconName: "home"
                text: "Home"
                onTriggered: folders.folder = "file://" + StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", "")
            }
        ]
    }

    FolderListModel {
        id: folders

        folder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        showFiles: false
        showDotAndDotDot: false
        showHidden: false
        sortCaseSensitive: false
    }

    ListView {
        id: list

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: folders

        delegate: ListItem {
            height: layout.height + (divider.visible ? divider.height : 0)
            onClicked: folders.folder = "file://" + filePath

            ListItemLayout {
                id: layout
                title.text: fileName
                Icon {
                    SlotsLayout.position: SlotsLayout.Leading
                    width: units.gu(3)
                    height: width
                    name: "folder"
                }
                ProgressionSlot {}
            }
        }

        footer: Label {
            width: list.width
            padding: units.gu(2)
            wrapMode: Text.Wrap
            color: theme.palette.normal.backgroundSecondaryText
            text: (list.count === 0 ? "No subfolders here. " : "") +
                  "Open the folder that holds your games, then tap ✓ to add it."
        }
    }
}
