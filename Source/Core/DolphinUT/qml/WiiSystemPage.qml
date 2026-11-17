// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// Wii System (stage 6.3): the Android app's Wii items from its main menu (MainPresenter), on one
// page: Load Wii System Menu, Perform Online System Update, Install WAD, Import Wii Save, Import
// BootMii NAND Backup. A game's "Perform System Update" (the library's long press, for Wii discs)
// opens this page with discUpdatePath set. The actions run in wiiSystem (WiiSystem.cpp).
Page {
    id: page

    // A Wii disc to update the system from, as soon as the page opens.
    property string discUpdatePath: ""
    // A question from the running action is open (the progress panel steps aside).
    property bool asking: false

    header: PageHeader {
        title: "Wii System"
        subtitle: wiiSystem.menuInstalled ? (wiiSystem.menuIsVWii ? "vWii Menu " : "Wii Menu ") + wiiSystem.menuVersion
                                          : "Wii Menu not installed"
        leadingActionBar.actions: Action {
            iconName: "back"
            text: "Back"
            // Not while an action runs: its result is shown here.
            enabled: !wiiSystem.busy
            onTriggered: pageStack.pop()
        }
    }

    Component.onCompleted: {
        wiiSystem.refresh()
        if (discUpdatePath !== "")
            Qt.callLater(() => wiiSystem.discUpdate(discUpdatePath))
    }

    function downloadsFolder() {
        const places = storage.places()
        for (let i = 0; i < places.length; ++i) {
            if (places[i].name === "Downloads")
                return places[i].path
        }
        return ""
    }
    function pickFile(title, extensions, hint, done) {
        const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
            title: title,
            pickFiles: true,
            extensions: extensions,
            startFolder: downloadsFolder(),
            hint: hint
        })
        picker.picked.connect(done)
        return picker
    }

    // MainPresenter.launchWiiSystemMenu / launchOnlineUpdate: without a Wii Menu, offer to install it.
    function loadSystemMenu() {
        if (wiiSystem.menuInstalled)
            emulator.startSystemMenu()
        else
            PopupUtils.open(notInstalledDialog, page)
    }
    function onlineUpdate() {
        if (wiiSystem.menuInstalled)
            wiiSystem.onlineUpdate("")
        else
            PopupUtils.open(notInstalledDialog, page)
    }

    component ActionItem: ListItem {
        property string titleText
        property string subtitleText
        height: layout.height + (divider.visible ? divider.height : 0)
        enabled: !wiiSystem.busy
        ListItemLayout {
            id: layout
            title.text: titleText
            subtitle.text: subtitleText
            subtitle.wrapMode: Text.Wrap
            subtitle.maximumLineCount: 4
            ProgressionSlot {}
        }
    }

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

            ActionItem {
                titleText: wiiSystem.menuInstalled
                            ? (wiiSystem.menuIsVWii ? "Load vWii System Menu (%1)" : "Load Wii System Menu (%1)").arg(wiiSystem.menuVersion)
                            : "Load Wii System Menu"
                subtitleText: "Starts the Wii Menu from the emulated Wii's memory (NAND)."
                onClicked: page.loadSystemMenu()
            }
            ActionItem {
                titleText: "Perform Online System Update"
                subtitleText: "Downloads the Wii's system software from Nintendo and installs it (also the Wii Menu, if it is missing)."
                onClicked: page.onlineUpdate()
            }
            ActionItem {
                titleText: "Install WAD"
                subtitleText: "Installs a title (a channel, for example) from a .wad file to the NAND."
                onClicked: page.pickFile("Install WAD", ["wad"], "Open the folder with the .wad file, then tap it.",
                                         path => wiiSystem.installWad(path))
            }
            ActionItem {
                titleText: "Import Wii Save"
                subtitleText: "Imports a Wii save (data.bin, as the Wii's Data Management exports it)."
                onClicked: page.pickFile("Import Wii Save", ["bin"], "Open the folder with the save (data.bin), then tap it.",
                                         path => wiiSystem.importSave(path))
            }
            ActionItem {
                titleText: "Import BootMii NAND Backup"
                subtitleText: "Merges a real Wii's NAND backup (nand.bin, made with BootMii) into the emulated NAND."
                onClicked: PopupUtils.open(nandWarningDialog, page)
            }
            Label {
                width: parent.width
                padding: units.gu(2)
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: "Files from other apps (Downloads, a microSD card) can be picked directly. Games stay in the library: start a Wii disc there."
            }
        }
    }

    // The running action (SystemUpdateProgressBarDialogFragment, ThreadUtil's progress): on the page,
    // not a popup, so a file picker pushed for a question covers it.
    Rectangle {
        anchors.fill: parent
        visible: wiiSystem.busy && !page.asking
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }  // nothing else while it runs

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
                    width: parent.width
                    textSize: Label.Large
                    text: wiiSystem.cancelling ? "Cancelling…" :
                          wiiSystem.task === "update" ? "Updating" : "Importing…"
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: wiiSystem.cancelling ? (wiiSystem.task === "update" ? "Please wait for the update to cancel." : "Please wait.") :
                          wiiSystem.progressText !== "" ? wiiSystem.progressText :
                          wiiSystem.task === "nand" ? "Do not close the app!" : ""
                    visible: text !== ""
                }
                ProgressBar {
                    width: parent.width
                    indeterminate: wiiSystem.progressTotal <= 0 || wiiSystem.cancelling
                    minimumValue: 0
                    maximumValue: Math.max(1, wiiSystem.progressTotal)
                    value: wiiSystem.progress
                }
                Button {
                    width: parent.width
                    text: "Cancel"
                    visible: wiiSystem.task === "update" || wiiSystem.task === "nand"
                    enabled: !wiiSystem.cancelling
                    onClicked: wiiSystem.cancel()
                }
            }
        }
    }

    Connections {
        target: wiiSystem
        function onFinished(task, ok, title, message) {
            if (message !== "")
                PopupUtils.open(resultDialog, page, { title: title, text: message })
        }
        function onOverwriteAsked() {
            page.asking = true
            PopupUtils.open(overwriteDialog, page)
        }
        function onKeysFileAsked() {
            page.asking = true
            PopupUtils.open(keysDialog, page)
        }
    }

    Component {
        id: resultDialog
        Dialog {
            id: dialog
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // MainPresenter.importWiiSave's question.
    Component {
        id: overwriteDialog
        Dialog {
            id: dialog
            text: "Save data for this title already exists in the NAND. Consider backing up the current data before overwriting.\nOverwrite now?"
            function answer(yes) {
                page.asking = false
                wiiSystem.answerOverwrite(yes)
                PopupUtils.close(dialog)
            }
            Button {
                text: "Yes"
                color: theme.palette.normal.negative
                onClicked: dialog.answer(true)
            }
            Button {
                text: "No"
                onClicked: dialog.answer(false)
            }
        }
    }

    // DolphinQt's keys file question (the Android app only reports that the keys are missing).
    Component {
        id: keysDialog
        Dialog {
            id: dialog
            title: "Keys file"
            text: "This NAND backup has no decryption keys appended. Choose the keys file made by BootMii with it (keys.bin, the OTP/SEEPROM dump)?"
            Button {
                text: "Choose File"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    const picker = page.pickFile("Select Keys File", ["bin"], "Open the folder with keys.bin, then tap it.",
                                                 path => { page.asking = false; wiiSystem.provideKeysFile(path) })
                    picker.cancelled.connect(() => { page.asking = false; wiiSystem.provideKeysFile("") })
                }
            }
            Button {
                text: "Cancel"
                onClicked: {
                    page.asking = false
                    wiiSystem.provideKeysFile("")
                    PopupUtils.close(dialog)
                }
            }
        }
    }

    // MainPresenter.importNANDBin's warning (asked first, as DolphinQt does).
    Component {
        id: nandWarningDialog
        Dialog {
            id: dialog
            title: "Import BootMii NAND Backup"
            text: "Merging a new NAND over your currently selected NAND will overwrite any channels and savegames that already exist. This process is not reversible, so it is recommended that you keep backups of both NANDs. Are you sure you want to continue?"
            Button {
                text: "Yes"
                color: theme.palette.normal.negative
                onClicked: {
                    PopupUtils.close(dialog)
                    page.pickFile("Import BootMii NAND Backup", ["bin"], "Open the folder with the backup (nand.bin), then tap it.",
                                  path => wiiSystem.importNand(path))
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // SystemMenuNotInstalledDialogFragment, then OnlineUpdateRegionSelectDialogFragment.
    Component {
        id: notInstalledDialog
        Dialog {
            id: dialog
            title: "Not installed"
            text: "The Wii Menu isn't installed. Would you like to install it now?\n\nRoughly 110 MiB of data will be downloaded from the internet. A Wi-Fi connection is recommended."
            Button {
                text: "Yes"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    PopupUtils.open(regionDialog, page)
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: regionDialog
        Dialog {
            id: dialog
            title: "Please select a region"
            Repeater {
                model: [
                    { name: "Europe", region: "EUR" },
                    { name: "Japan", region: "JPN" },
                    { name: "Korea", region: "KOR" },
                    { name: "USA", region: "USA" }
                ]
                delegate: Button {
                    text: modelData.name
                    onClicked: {
                        PopupUtils.close(dialog)
                        wiiSystem.onlineUpdate(modelData.region)
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
