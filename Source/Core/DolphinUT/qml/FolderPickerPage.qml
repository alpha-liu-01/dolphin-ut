// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Qt.labs.folderlistmodel
import Lomiri.Components

// A file and folder browser (Ubuntu Touch has no system picker for paths). Folders: open one and tap
// ✓; files: tap one. The places at the top are the games folder, Documents, Downloads, home and any
// microSD card (storage.places()). A confined click sees only what its policy allows (3.7):
// Documents and the cards' Documents folders. Files from other apps come through Import Games.
Page {
    id: page

    property string title: pickFiles ? "Choose File" : "Add Folder"
    property bool pickFiles: false
    // File extensions to show, without the dot (empty: all files).
    property var extensions: []
    property string startFolder: ""
    property string hint: pickFiles ? "Open the folder with the file, then tap it." :
                                      "Open the folder that holds your games, then tap ✓ to add it."
    readonly property string currentPath: decodeURIComponent(folders.folder.toString().replace("file://", ""))

    signal picked(string path)
    // Left without a choice (Back). For callers that wait for an answer (stage 6.3: the keys file).
    signal cancelled()
    property bool chosen: false

    function choose(path) {
        page.chosen = true
        page.picked(path)
        pageStack.pop()
    }

    Component.onDestruction: if (!chosen) page.cancelled()

    header: PageHeader {
        title: page.title
        subtitle: page.currentPath
        trailingActionBar.actions: [
            Action {
                iconName: "ok"
                text: "Use This Folder"
                visible: !page.pickFiles
                onTriggered: page.choose(page.currentPath)
            },
            Action {
                iconName: "go-up"
                text: "Up"
                enabled: page.currentPath !== "/"
                onTriggered: folders.folder = folders.parentFolder
            }
        ]
    }

    FolderListModel {
        id: folders

        folder: "file://" + (page.startFolder !== "" ? page.startFolder : storage.gamesFolder)
        showFiles: page.pickFiles
        showDirsFirst: true
        showDotAndDotDot: false
        showHidden: false
        sortCaseSensitive: false
        caseSensitive: false
        nameFilters: page.extensions.map(function (ext) { return "*." + ext })
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

        header: Column {
            width: list.width

            Repeater {
                model: storage.places()
                delegate: ListItem {
                    height: placeLayout.height + (divider.visible ? divider.height : 0)
                    color: page.currentPath === modelData.path ? theme.palette.highlighted.background :
                                                                 "transparent"
                    onClicked: folders.folder = "file://" + modelData.path
                    ListItemLayout {
                        id: placeLayout
                        title.text: modelData.name
                        subtitle.text: modelData.path
                        Icon {
                            SlotsLayout.position: SlotsLayout.Leading
                            width: units.gu(3)
                            height: width
                            name: modelData.icon
                        }
                    }
                }
            }
            ListItem {
                height: units.gu(4)
                divider.visible: false
                Label {
                    anchors {
                        left: parent.left
                        leftMargin: units.gu(2)
                        bottom: parent.bottom
                        bottomMargin: units.gu(0.5)
                    }
                    text: page.currentPath
                    font.bold: true
                    elide: Text.ElideMiddle
                    width: parent.width - units.gu(4)
                    color: theme.palette.normal.backgroundSecondaryText
                }
            }
        }

        delegate: ListItem {
            height: layout.height + (divider.visible ? divider.height : 0)
            onClicked: {
                if (fileIsDir)
                    folders.folder = "file://" + filePath
                else
                    page.choose(filePath)
            }

            ListItemLayout {
                id: layout
                title.text: fileName
                subtitle.text: fileIsDir ? "" : (fileSize / 1048576).toFixed(fileSize < 10485760 ? 1 : 0) + " MB"
                Icon {
                    SlotsLayout.position: SlotsLayout.Leading
                    width: units.gu(3)
                    height: width
                    name: fileIsDir ? "folder" : "empty-symbolic"
                }
                ProgressionSlot {
                    visible: fileIsDir
                }
            }
        }

        footer: Label {
            width: list.width
            padding: units.gu(2)
            wrapMode: Text.Wrap
            color: theme.palette.normal.backgroundSecondaryText
            text: (list.count === 0 ? (page.pickFiles ? "Nothing to choose here. " : "No subfolders here. ") : "") +
                  page.hint
        }
    }
}
