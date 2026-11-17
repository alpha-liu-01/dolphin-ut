// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// The Skylanders Portal or the Infinity Base during a game (stage 6.8), as the Android app's
// in-game dialogs (EmulationActivity.showSkylanderPortalSettings / showInfinityBaseSettings): the
// slots, each with Load (a figure file), Create (a new figure, saved in the user data) and Clear.
// Opened over the paused game from the in-game menu; Back returns to it (figures, Figures.cpp).
Page {
    id: page

    property string kind: "skylanders"
    property bool returnsToGame: false
    readonly property bool skylanders: kind === "skylanders"
    readonly property var slots: skylanders ? figures.skylanderSlots : figures.infinitySlots

    Component.onDestruction: if (returnsToGame) emulator.returnToGame()

    header: PageHeader {
        title: page.skylanders ? "Skylanders Portal" : "Infinity Base"
        subtitle: page.returnsToGame ? "The game is paused; Back returns to it." : ""
    }

    function showResult(result) {
        if (result !== "")
            PopupUtils.open(messageDialog, page, { text: result })
    }
    function load(slot) {
        const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
            title: page.skylanders ? "Load Skylander" : "Load Figure",
            pickFiles: true,
            extensions: page.skylanders ? ["sky", "bin", "dmp", "dump"] : ["bin"],
            startFolder: storage.exists(page.skylanders ? figures.skylanderFolder : figures.infinityFolder) ?
                             (page.skylanders ? figures.skylanderFolder : figures.infinityFolder) : "",
            hint: "Open the folder with the figure file, then tap it."
        })
        picker.picked.connect(path => Qt.callLater(() => page.showResult(
            page.skylanders ? figures.loadSkylander(slot, path) : figures.loadInfinity(slot, path))))
    }

    ListView {
        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: page.slots
        delegate: ListItem {
            height: slotLayout.height + (divider.visible ? divider.height : 0)
            onClicked: PopupUtils.open(slotDialog, page, { slot: index, slotName: modelData.name, label: modelData.label })
            ListItemLayout {
                id: slotLayout
                title.text: modelData.name
                subtitle.text: modelData.label
            }
        }
    }

    Component {
        id: slotDialog
        Dialog {
            id: dialog
            property int slot
            property string slotName
            property string label
            title: slotName
            text: label
            Button {
                text: "Load…"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    page.load(dialog.slot)
                }
            }
            Button {
                text: "Create…"
                onClicked: {
                    PopupUtils.close(dialog)
                    pageStack.push(createPage, { slot: dialog.slot })
                }
            }
            Button {
                text: "Clear"
                enabled: dialog.label !== "Empty"
                onClicked: {
                    if (page.skylanders)
                        figures.clearSkylander(dialog.slot)
                    else
                        figures.clearInfinity(dialog.slot)
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // Create: the known figures, searchable (the Android app's figure list); the new file goes to
    // the user data (figures.skylanderFolder / infinityFolder) and onto the slot.
    Component {
        id: createPage
        Page {
            id: create
            property int slot
            readonly property var all: page.skylanders ? figures.skylanderList() : figures.infinityList()
            readonly property var shown: {
                const filter = searchField.text.trim().toLowerCase()
                return filter === "" ? all : all.filter(f => f.name.toLowerCase().indexOf(filter) >= 0)
            }
            header: PageHeader {
                title: page.skylanders ? "Create Skylander" : "Create Figure"
                subtitle: create.shown.length + " figures; saved in " +
                          (page.skylanders ? figures.skylanderFolder : figures.infinityFolder)
            }
            TextField {
                id: searchField
                anchors {
                    top: create.header.bottom
                    left: parent.left
                    right: parent.right
                    margins: units.gu(1)
                }
                placeholderText: "Search"
                inputMethodHints: Qt.ImhNoPredictiveText
            }
            ListView {
                anchors {
                    top: searchField.bottom
                    topMargin: units.gu(1)
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                clip: true
                model: create.shown
                delegate: ListItem {
                    height: units.gu(6)
                    onClicked: {
                        const result = page.skylanders ?
                            figures.createSkylander(create.slot, modelData.id, modelData.variant) :
                            figures.createInfinity(create.slot, modelData.number)
                        pageStack.pop()
                        page.showResult(result)
                    }
                    Label {
                        anchors {
                            left: parent.left
                            leftMargin: units.gu(2)
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.name
                    }
                }
            }
        }
    }

    Component {
        id: messageDialog
        Dialog {
            id: dialog
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
