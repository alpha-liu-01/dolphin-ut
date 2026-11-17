// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// Edit Cheats (stage 6.4), as the Android app's CheatsActivity: the game's patches, AR codes, Gecko
// codes and graphics mods, each with a switch; "Add New …" at the end of each kind and "Download
// Gecko Codes", as its CheatsAdapter; a code's details, which can be edited and deleted for codes
// the user added (CheatDetailsFragment). Changes are saved at once (cheats, Cheats.cpp).
Page {
    id: page

    property string gameId
    property int revision: 0
    property string gametdbId
    property string gameTitle

    readonly property var sections: [
        { type: "patch", title: "Patches", add: "Add New Patch" },
        { type: "ar", title: "AR Codes", add: "Add New AR Code" },
        { type: "gecko", title: "Gecko Codes", add: "Add New Gecko Code" },
        { type: "mod", title: "Graphics Mods", add: "" }
    ]
    function itemsOf(type) {
        return cheats.items.filter(item => item.type === type)
    }
    function settingOn(name) {
        settingsModel.revision
        return !!settingsModel.value(name)
    }

    header: PageHeader {
        title: "Cheats: " + page.gameId
        subtitle: page.gameTitle
    }

    Component.onCompleted: cheats.load(gameId, revision, gametdbId)

    // SettingDisabledWarningFragment: the setting that turns these off, with a way to it.
    component Warning: ListItem {
        id: warning
        property string message
        property string menu
        height: visible ? warningLayout.height : 0
        color: theme.palette.normal.foreground
        ListItemLayout {
            id: warningLayout
            title.text: warning.message
            title.wrapMode: Text.Wrap
            title.maximumLineCount: 3
            Button {
                SlotsLayout.position: SlotsLayout.Trailing
                text: "Settings"
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"), { menuTag: warning.menu })
            }
        }
    }

    Flickable {
        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: column.height + units.gu(2)
        clip: true

        Column {
            id: column
            width: parent.width

            Warning {
                width: parent.width
                visible: !page.settingOn("BooleanSetting.MAIN_ENABLE_CHEATS")
                message: "Dolphin's cheat system is currently disabled."
                menu: "CONFIG_GENERAL"
            }
            Warning {
                width: parent.width
                visible: page.itemsOf("mod").length > 0 && !page.settingOn("BooleanSetting.GFX_MODS_ENABLE")
                message: "Graphics mods are currently disabled."
                menu: "ADVANCED_GRAPHICS"
            }

            Repeater {
                model: page.sections
                delegate: Column {
                    id: sectionColumn
                    readonly property var section: modelData
                    readonly property var entries: page.itemsOf(section.type)
                    width: column.width
                    // Graphics mods only when the game has some.
                    visible: section.type !== "mod" || entries.length > 0

                    ListItem {
                        height: units.gu(5)
                        color: theme.palette.normal.base
                        Label {
                            anchors {
                                left: parent.left
                                leftMargin: units.gu(2)
                                verticalCenter: parent.verticalCenter
                            }
                            text: sectionColumn.section.title
                            font.bold: true
                        }
                    }

                    Repeater {
                        model: sectionColumn.entries
                        delegate: ListItem {
                            height: cheatLayout.height + (divider.visible ? divider.height : 0)
                            onClicked: pageStack.push(detailsPage, { entry: modelData })
                            ListItemLayout {
                                id: cheatLayout
                                title.text: modelData.name
                                title.wrapMode: Text.Wrap
                                title.maximumLineCount: 3
                                subtitle.text: modelData.creator
                                Switch {
                                    SlotsLayout.position: SlotsLayout.Trailing
                                    checked: modelData.enabled
                                    onClicked: cheats.setEnabled(modelData.type, modelData.index, checked)
                                }
                            }
                        }
                    }

                    ListItem {
                        visible: sectionColumn.section.add !== ""
                        height: visible ? addLayout.height : 0
                        onClicked: pageStack.push(detailsPage, {
                            entry: { type: sectionColumn.section.type, index: -1, name: "", creator: "",
                                     notes: "", code: "", enabled: false, userDefined: true },
                            editing: true
                        })
                        ListItemLayout {
                            id: addLayout
                            title.text: sectionColumn.section.add
                            title.color: theme.palette.normal.activity
                        }
                    }
                    ListItem {
                        visible: sectionColumn.section.type === "gecko"
                        height: visible ? downloadLayout.height : 0
                        enabled: !cheats.downloading && page.gametdbId !== ""
                        onClicked: cheats.downloadGecko()
                        ListItemLayout {
                            id: downloadLayout
                            title.text: cheats.downloading ? "Downloading…" : "Download Gecko Codes"
                            title.color: theme.palette.normal.activity
                            ActivityIndicator {
                                SlotsLayout.position: SlotsLayout.Trailing
                                running: cheats.downloading
                                visible: running
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: cheats
        function onDownloadFinished(downloaded, added) {
            PopupUtils.open(messageDialog, page, {
                text: downloaded < 0 ? "Failed to download codes." :
                      downloaded === 0 ? "File contained no codes." :
                      "Downloaded %1 codes. (added %2)".arg(downloaded).arg(added)
            })
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

    // CheatDetailsFragment: the code's fields; Edit and Delete for codes the user added.
    Component {
        id: detailsPage
        Page {
            id: details

            property var entry
            property bool editing: false
            readonly property bool adding: entry.index < 0
            readonly property bool gecko: entry.type === "gecko"
            readonly property bool mod: entry.type === "mod"
            property string error: ""

            header: PageHeader {
                title: details.adding ? "New code" : details.entry.name
                trailingActionBar.actions: [
                    Action {
                        iconName: "delete"
                        text: "Delete"
                        visible: details.entry.userDefined && !details.adding && !details.editing
                        onTriggered: PopupUtils.open(deleteDialog, details)
                    },
                    Action {
                        iconName: "edit"
                        text: "Edit"
                        visible: details.entry.userDefined && !details.editing
                        onTriggered: details.editing = true
                    }
                ]
            }

            Flickable {
                anchors {
                    top: details.header.bottom
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                contentHeight: fields.height + units.gu(4)
                clip: true

                Column {
                    id: fields
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: units.gu(2)
                    }
                    spacing: units.gu(1)

                    Label { text: "Name" }
                    TextField {
                        id: nameField
                        width: parent.width
                        text: details.entry.name
                        readOnly: !details.editing
                    }
                    Label {
                        text: "Creator"
                        visible: details.gecko || details.mod
                    }
                    TextField {
                        id: creatorField
                        width: parent.width
                        visible: details.gecko || details.mod
                        text: details.entry.creator
                        readOnly: !details.editing
                    }
                    Label {
                        text: "Notes"
                        visible: details.gecko || details.mod
                    }
                    TextArea {
                        id: notesField
                        width: parent.width
                        visible: details.gecko || details.mod
                        autoSize: true
                        maximumLineCount: 0
                        text: details.entry.notes
                        readOnly: !details.editing
                    }
                    Label {
                        text: "Code"
                        visible: !details.mod
                    }
                    TextArea {
                        id: codeField
                        width: parent.width
                        visible: !details.mod
                        autoSize: true
                        maximumLineCount: 0
                        font.family: "Ubuntu Mono"
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        text: details.entry.code
                        readOnly: !details.editing
                    }
                    Label {
                        width: parent.width
                        visible: details.error !== ""
                        text: details.error
                        color: theme.palette.normal.negative
                        wrapMode: Text.Wrap
                    }
                    Row {
                        visible: details.editing
                        spacing: units.gu(1)
                        Button {
                            text: "Save"
                            color: theme.palette.normal.positive
                            onClicked: {
                                const result = details.adding ?
                                    cheats.add(details.entry.type, nameField.text, creatorField.text, notesField.text, codeField.text) :
                                    cheats.edit(details.entry.type, details.entry.index, nameField.text, creatorField.text,
                                                notesField.text, codeField.text)
                                details.error = result
                                if (result === "")
                                    pageStack.pop()
                            }
                        }
                        Button {
                            text: "Cancel"
                            onClicked: {
                                if (details.adding) {
                                    pageStack.pop()
                                } else {
                                    nameField.text = details.entry.name
                                    creatorField.text = details.entry.creator
                                    notesField.text = details.entry.notes
                                    codeField.text = details.entry.code
                                    details.error = ""
                                    details.editing = false
                                }
                            }
                        }
                    }
                }
            }

            Component {
                id: deleteDialog
                Dialog {
                    id: dialog
                    text: "Are you sure you want to delete \"%1\"?".arg(details.entry.name)
                    Button {
                        text: "Delete"
                        color: theme.palette.normal.negative
                        onClicked: {
                            cheats.remove(details.entry.type, details.entry.index)
                            PopupUtils.close(dialog)
                            pageStack.pop()
                        }
                    }
                    Button {
                        text: "Cancel"
                        onClicked: PopupUtils.close(dialog)
                    }
                }
            }
        }
    }
}
