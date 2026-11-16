// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// The folders the library scans (Dolphin's ISO paths). Swipe a folder to remove it.
Page {
    id: page

    header: PageHeader {
        title: "Game Folders"
        trailingActionBar.actions: Action {
            iconName: "add"
            text: "Add Folder"
            onTriggered: pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"))
        }
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
        model: gameList.folders

        header: ListItem {
            height: subfoldersLayout.height + (divider.visible ? divider.height : 0)
            ListItemLayout {
                id: subfoldersLayout
                title.text: "Search Subfolders"
                Switch {
                    SlotsLayout.position: SlotsLayout.Trailing
                    checked: gameList.searchSubfolders
                    onClicked: gameList.searchSubfolders = checked
                }
            }
        }

        delegate: ListItem {
            height: layout.height + (divider.visible ? divider.height : 0)

            leadingActions: ListItemActions {
                actions: Action {
                    iconName: "delete"
                    text: "Remove"
                    onTriggered: gameList.removeFolder(modelData)
                }
            }

            ListItemLayout {
                id: layout
                title.text: modelData
                title.wrapMode: Text.WrapAnywhere
                title.maximumLineCount: 3
                Icon {
                    SlotsLayout.position: SlotsLayout.Leading
                    width: units.gu(3)
                    height: width
                    name: "folder"
                }
            }
        }

        footer: Label {
            width: list.width
            padding: units.gu(2)
            wrapMode: Text.Wrap
            color: theme.palette.normal.backgroundSecondaryText
            text: list.count === 0 ?
                      "No folders yet. Add the folder with your games (ISO, GCM, RVZ, WBFS, WIA, GCZ, CISO, WAD, DOL, ELF)." :
                      "Swipe a folder to the right to remove it. The games stay on the phone."
        }
    }
}
