// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// Config → User Data (the Android app's UserDataActivity): where the port keeps its files (stage
// 3.7), and (stage 6.5) Android's Export User Data, Import User Data and Reset Dolphin Settings
// (userData, UserData.cpp), plus single files out of and into the user data, which is not visible
// over USB: sent to another app through content-hub (SharePage.qml), or copied in from elsewhere.
Page {
    id: page

    readonly property bool gameRunning: emulator.state !== "idle"
    // The zip being (or last) exported.
    property string lastExport: ""

    component ActionItem: ListItem {
        property string titleText
        property string subtitleText
        width: parent ? parent.width : 0
        height: actionLayout.height + (divider.visible ? divider.height : 0)
        enabled: !page.gameRunning && !userData.busy
        ListItemLayout {
            id: actionLayout
            title.text: titleText
            subtitle.text: subtitleText
            subtitle.wrapMode: Text.Wrap
            subtitle.maximumLineCount: 5
        }
    }
    component Header: Label {
        width: parent ? parent.width : 0
        leftPadding: units.gu(2)
        topPadding: units.gu(3)
        bottomPadding: units.gu(1)
        font.bold: true
        color: theme.palette.normal.backgroundSecondaryText
    }

    function startFolder(preferred) {
        if (storage.exists(preferred))
            return preferred
        const places = storage.places()
        for (let i = 0; i < places.length; ++i) {
            if (places[i].name === "Downloads")
                return places[i].path
        }
        return ""
    }

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

            Header { text: "Backup" }
            Label {
                width: parent.width
                leftPadding: units.gu(2)
                rightPadding: units.gu(2)
                visible: page.gameRunning
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: "Not while a game is running."
            }
            ActionItem {
                titleText: "Export User Data"
                subtitleText: "Saves all of the user data as a zip in " + userData.backupFolder +
                              " (visible over USB). It can then be sent to another app."
                onClicked: {
                    page.lastExport = userData.newBackupPath()
                    userData.exportTo(page.lastExport)
                }
            }
            ActionItem {
                titleText: "Import User Data"
                subtitleText: "Replaces all of the user data with a backup (from this app or Dolphin on another device). Dolphin closes afterwards."
                onClicked: {
                    const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
                        title: "Import User Data",
                        pickFiles: true,
                        extensions: ["zip"],
                        startFolder: page.startFolder(userData.backupFolder),
                        hint: "Open the folder with the backup (.zip), then tap it."
                    })
                    picker.picked.connect(function (path) {
                        const problem = userData.checkBackup(path)
                        if (problem !== "")
                            PopupUtils.open(messageDialog, page, { text: problem })
                        else
                            PopupUtils.open(importWarningDialog, page, { zipPath: path })
                    })
                }
            }
            ActionItem {
                titleText: "Reset Dolphin Settings"
                subtitleText: "Restores all settings to their defaults. Saves, games and controller mappings stay."
                onClicked: PopupUtils.open(resetDialog, page)
            }

            Header { text: "Single files" }
            ActionItem {
                titleText: "Send a File to Another App"
                subtitleText: "A save state, memory card or other file from the user data, to the File Manager (to save it where you like) or another app."
                onClicked: {
                    const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
                        title: "Send a File",
                        pickFiles: true,
                        startFolder: storage.userFolder,
                        hint: "Open the folder with the file (StateSaves, GC, Wii, …), then tap it."
                    })
                    picker.picked.connect(path => Qt.callLater(() => pageStack.push(Qt.resolvedUrl("SharePage.qml"), { path: path })))
                }
            }
            ActionItem {
                titleText: "Copy a File In"
                subtitleText: "A save state or memory card from elsewhere (Downloads, a microSD card) into a folder of the user data."
                onClicked: {
                    const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
                        title: "Copy a File In",
                        pickFiles: true,
                        startFolder: page.startFolder(""),
                        hint: "Open the folder with the file, then tap it."
                    })
                    picker.picked.connect(file => Qt.callLater(() => page.chooseTarget(file)))
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

    function chooseTarget(file) {
        const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
            title: "Copy Into",
            startFolder: storage.userFolder,
            hint: "Open the folder of the user data to copy it into (StateSaves, GC/EUR/Card A, …), then tap ✓."
        })
        picker.picked.connect(folder => Qt.callLater(() => page.copyIn(file, folder, false)))
    }
    function copyIn(file, folder, overwrite) {
        const result = userData.copyIn(file, folder, overwrite)
        if (result === "exists")
            PopupUtils.open(replaceDialog, page, { file: file, folder: folder })
        else
            PopupUtils.open(messageDialog, page, { text: result === "" ? "Copied to " + folder + "." : result })
    }

    // The running export or import (Android's TaskDialog): on the page, with Cancel for the export.
    Rectangle {
        anchors.fill: parent
        visible: userData.busy
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - units.gu(4), units.gu(44))
            height: progressColumn.height + units.gu(4)
            radius: units.gu(1)
            color: theme.palette.normal.overlay
            Column {
                id: progressColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    margins: units.gu(2)
                }
                spacing: units.gu(2)
                Label {
                    textSize: Label.Large
                    text: userData.task === "import" ? "Importing…" : "Exporting…"
                }
                Label {
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: userData.progressText
                    visible: text !== ""
                }
                ProgressBar {
                    width: parent.width
                    indeterminate: userData.progressTotal <= 0
                    minimumValue: 0
                    maximumValue: Math.max(1, userData.progressTotal)
                    value: userData.progress
                }
                Button {
                    width: parent.width
                    text: "Cancel"
                    visible: userData.task === "export"
                    onClicked: userData.cancel()
                }
            }
        }
    }

    Connections {
        target: userData
        function onFinished(task, ok, cancelled, message) {
            if (cancelled)
                return
            if (task === "import" && ok)
                PopupUtils.open(importedDialog, page)
            else
                PopupUtils.open(task === "export" && ok ? exportedDialog : messageDialog, page,
                                { text: task === "export" && ok ? message + "\n" + page.lastExport : message })
        }
    }

    Component {
        id: messageDialog
        Dialog {
            id: dialog
            property string zipPath
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: exportedDialog
        Dialog {
            id: dialog
            property string zipPath
            title: "Export User Data"
            Button {
                text: "Send to Another App"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    pageStack.push(Qt.resolvedUrl("SharePage.qml"), { path: page.lastExport })
                }
            }
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // UserDataImportWarningDialog.
    Component {
        id: importWarningDialog
        Dialog {
            id: dialog
            property string zipPath
            title: "Import User Data"
            text: "Are you sure you want to replace your user data with the data in this file? All existing user data will be deleted!"
            Button {
                text: "Yes"
                color: theme.palette.normal.negative
                onClicked: {
                    PopupUtils.close(dialog)
                    userData.importFrom(dialog.zipPath)
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // After an import the app closes without saving (Android: exitProcess), so the settings in
    // memory do not overwrite the imported ones.
    Component {
        id: importedDialog
        Dialog {
            id: dialog
            title: "Import User Data"
            text: lomiri.canRestart ? "The user data has been imported. Dolphin restarts now to use it." :
                                      "The user data has been imported. Dolphin closes now; start it again to use it."
            Button {
                // Stage 6.8: restarted when Lomiri started it; without saving either way.
                text: lomiri.canRestart ? "Restart Dolphin" : "Close Dolphin"
                color: theme.palette.normal.positive
                onClicked: {
                    if (!lomiri.canRestart || !lomiri.restart(false))
                        userData.quitNow()
                }
            }
        }
    }

    Component {
        id: resetDialog
        Dialog {
            id: dialog
            title: "Reset Dolphin Settings"
            text: "Are you sure you want to restore all Dolphin settings to their default values? This action cannot be undone!\nAll customizations or changes you have made will be lost.\n\nDo you want to proceed?"
            Button {
                text: "Yes"
                color: theme.palette.normal.negative
                onClicked: {
                    PopupUtils.close(dialog)
                    const ok = userData.resetSettings()
                    PopupUtils.open(messageDialog, page, { text: ok ? "The settings are back to their defaults." : "Not while a game is running." })
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: replaceDialog
        Dialog {
            id: dialog
            property string file
            property string folder
            text: "The file %1 already exists there. Replace it?".arg(file.substring(file.lastIndexOf("/") + 1))
            Button {
                text: "Replace"
                color: theme.palette.normal.negative
                onClicked: {
                    PopupUtils.close(dialog)
                    page.copyIn(dialog.file, dialog.folder, true)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
