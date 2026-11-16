// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// One menu of the settings (stage 3.6): the Android app's menus and items, generated from its
// sources (settings.json), drawn with Lomiri components. With a game open (settingsModel.gameId),
// values go to that game's settings; a press and hold clears a game's value, and those set for the
// game are marked.
Page {
    id: page

    property string menuTag: "SETTINGS"
    // The page that opened a game's settings closes them when it goes.
    property bool closesGame: false
    property string gameTitle: ""
    readonly property var menuData: settingsModel.menu(menuTag)
    readonly property bool gameMode: settingsModel.gameId !== ""

    Component.onDestruction: if (closesGame) settingsModel.closeGame()

    header: PageHeader {
        title: page.menuData.title || "Settings"
        subtitle: page.gameMode ? "Game settings: " + (page.gameTitle || settingsModel.gameId) : ""
    }

    function valueOf(name) {
        settingsModel.revision  // re-evaluate on changes
        return settingsModel.value(name)
    }
    function overriddenOf(name) {
        settingsModel.revision
        return settingsModel.overridden(name)
    }
    function choiceLabel(entry) {
        const value = valueOf(entry.setting)
        for (let i = 0; i < entry.values.length; ++i) {
            if (String(entry.values[i]) === String(value))
                return entry.entries[i]
        }
        return String(value)
    }
    function sliderText(entry) {
        let value = Number(valueOf(entry.setting))
        if (entry.percent)
            value = value * 100
        const shown = Number.isInteger(entry.step) && Number.isInteger(entry.min) ? Math.round(value)
                                                                                 : value.toFixed(2)
        return shown + (entry.units ? " " + entry.units : "")
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

            Repeater {
                model: page.menuData.items || []

                delegate: Loader {
                    width: column.width
                    // Some items are for game settings only, or for the global ones only.
                    active: !(modelData.gameOnly && !page.gameMode) && !(modelData.globalOnly && page.gameMode)
                    visible: active
                    // The loaded component reads it as its parent's "entry".
                    readonly property var entry: modelData
                    sourceComponent: {
                        switch (modelData.type) {
                        case "header": return headerItem
                        case "submenu": return submenuItem
                        case "page": return submenuItem
                        case "switch": return switchItem
                        case "choice": return choiceItem
                        case "slider": return sliderItem
                        case "text": return textItem
                        }
                        return null
                    }
                }
            }
        }
    }

    Component {
        id: headerItem
        ListItem {
            readonly property var entry: parent ? parent.entry : ({})
            height: headerLabel.height + units.gu(3)
            divider.visible: false
            Label {
                id: headerLabel
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: units.gu(2)
                    bottom: parent.bottom
                    bottomMargin: units.gu(0.5)
                }
                text: entry.title
                font.bold: true
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
            }
        }
    }

    Component {
        id: submenuItem
        ListItem {
            readonly property var entry: parent ? parent.entry : ({})
            height: subLayout.height + (divider.visible ? divider.height : 0)
            onClicked: {
                if (entry.type === "page")
                    pageStack.push(Qt.resolvedUrl(entry.page))
                else
                    pageStack.push(Qt.resolvedUrl("SettingsPage.qml"),
                                   { menuTag: entry.menu, gameTitle: page.gameTitle })
            }
            ListItemLayout {
                id: subLayout
                title.text: entry.title
                ProgressionSlot {}
            }
        }
    }

    // A value the game overrides: a mark beside the title, and a press and hold clears it.
    component SettingItem: ListItem {
        readonly property var entry: parent ? parent.entry : ({})
        property bool overridden: page.gameMode && page.overriddenOf(entry.setting)
        onPressAndHold: if (page.gameMode) settingsModel.clear(entry.setting)
    }

    Component {
        id: switchItem
        SettingItem {
            id: switchRow
            height: switchLayout.height + (divider.visible ? divider.height : 0)
            readonly property bool checkedValue: entry.inverted ? !page.valueOf(entry.setting)
                                                               : !!page.valueOf(entry.setting)
            onClicked: settingsModel.setValue(entry.setting, entry.inverted ? checkedValue : !checkedValue)
            ListItemLayout {
                id: switchLayout
                title.text: entry.title + (switchRow.overridden ? "  •" : "")
                subtitle.text: entry.description
                subtitle.maximumLineCount: 4
                subtitle.wrapMode: Text.Wrap
                Switch {
                    SlotsLayout.position: SlotsLayout.Trailing
                    checked: switchRow.checkedValue
                    onClicked: settingsModel.setValue(entry.setting, entry.inverted ? !checked : checked)
                }
            }
        }
    }

    Component {
        id: choiceItem
        SettingItem {
            id: choiceRow
            height: choiceLayout.height + (divider.visible ? divider.height : 0)
            onClicked: PopupUtils.open(choiceDialog, page, { entry: entry })
            ListItemLayout {
                id: choiceLayout
                title.text: entry.title + (choiceRow.overridden ? "  •" : "")
                subtitle.text: page.choiceLabel(entry) + (entry.description ? "\n" + entry.description : "")
                subtitle.maximumLineCount: 5
                subtitle.wrapMode: Text.Wrap
            }
        }
    }

    Component {
        id: sliderItem
        SettingItem {
            id: sliderRow
            height: sliderColumn.height + units.gu(2)
            Column {
                id: sliderColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: units.gu(2)
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: entry.title + ": " + page.sliderText(entry) + (sliderRow.overridden ? "  •" : "")
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    visible: text !== ""
                    text: entry.description
                    textSize: Label.Small
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.backgroundSecondaryText
                }
                Slider {
                    width: parent.width
                    live: false
                    minimumValue: entry.min
                    maximumValue: entry.max
                    value: entry.percent ? Number(page.valueOf(entry.setting)) * 100
                                        : Number(page.valueOf(entry.setting))
                    function formatValue(v) {
                        return Math.round(v / entry.step) * entry.step
                    }
                    // live: false, so this runs when the finger lifts.
                    onValueChanged: {
                        const stepped = Math.round(value / entry.step) * entry.step
                        const stored = entry.percent ? stepped / 100 : stepped
                        if (Math.abs(stored - Number(page.valueOf(entry.setting))) > 1e-6)
                            settingsModel.setValue(entry.setting, stored)
                    }
                }
            }
        }
    }

    Component {
        id: textItem
        SettingItem {
            id: textRow
            height: textColumn.height + units.gu(2)
            Column {
                id: textColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: units.gu(2)
                    verticalCenter: parent.verticalCenter
                }
                spacing: units.gu(0.5)
                Label {
                    text: entry.title + (textRow.overridden ? "  •" : "")
                }
                TextField {
                    width: parent.width
                    text: page.valueOf(entry.setting)
                    onAccepted: settingsModel.setValue(entry.setting, text)
                }
            }
        }
    }

    // A choice: the entries with a check by the current one.
    Component {
        id: choiceDialog
        Dialog {
            id: dialog
            property var entry
            title: entry.title
            Repeater {
                model: dialog.entry.entries
                delegate: Button {
                    readonly property bool current: String(dialog.entry.values[index]) === String(page.valueOf(dialog.entry.setting))
                    text: (current ? "✓  " : "") + modelData
                    color: current ? theme.palette.normal.positive : theme.palette.normal.base
                    onClicked: {
                        settingsModel.setValue(dialog.entry.setting, dialog.entry.values[index])
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
