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
    // Opened from the in-game menu (stage 6.1): leaving this page returns to the game.
    property bool returnsToGame: false
    property string gameTitle: ""
    readonly property var menuData: settingsModel.menu(menuTag)
    readonly property bool gameMode: settingsModel.gameId !== ""
    // Stage 6.1: with a game running (Settings from the in-game menu), the settings that cannot
    // change then are greyed out, as in the Android app.
    readonly property bool gameRunning: emulator.state !== "idle"
    function lockedOf(name) {
        return gameRunning && name !== undefined && settingsModel.lockedWhileRunning(name)
    }
    readonly property bool hasLocked: {
        const items = menuData.items || []
        for (let i = 0; i < items.length; ++i) {
            if (lockedOf(items[i].setting))
                return true
        }
        return false
    }

    Component.onDestruction: {
        if (closesGame)
            settingsModel.closeGame()
        if (returnsToGame)
            emulator.returnToGame()
    }

    header: PageHeader {
        title: page.menuData.title || "Settings"
        subtitle: page.gameMode ? "Game settings: " + (page.gameTitle || settingsModel.gameId) :
                  page.returnsToGame ? "The game is paused; Back returns to it." : ""
    }

    // Only the page that started an action shows its result (the menus above it are pages too).
    property bool waitingForAction: false
    function runAction(action) {
        if (settingsModel.runAction(action))
            waitingForAction = true
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

            Label {
                width: parent.width
                padding: units.gu(2)
                visible: page.hasLocked
                text: "Greyed-out settings can't be changed while a game is running."
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
            }

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
                        case "file": return pathItem
                        case "folder": return pathItem
                        case "action": return actionItem
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
    // While a game runs, a setting that cannot change then is greyed out and does not respond.
    component SettingItem: ListItem {
        readonly property var entry: parent ? parent.entry : ({})
        property bool overridden: page.gameMode && page.overriddenOf(entry.setting)
        readonly property bool locked: page.lockedOf(entry.setting)
        enabled: !locked
        opacity: locked ? 0.4 : 1
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

    // A path (stage 3.7): the port's file browser. Press and hold goes back to Dolphin's default.
    Component {
        id: pathItem
        SettingItem {
            id: pathRow
            readonly property string path: String(page.valueOf(entry.setting))
            height: pathLayout.height + (divider.visible ? divider.height : 0)
            onClicked: {
                const slash = pathRow.path.lastIndexOf("/")
                const start = entry.type === "folder" ? pathRow.path : (slash > 0 ? pathRow.path.substring(0, slash) : "")
                const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
                    title: entry.title,
                    pickFiles: entry.type === "file",
                    extensions: entry.extensions || [],
                    startFolder: storage.exists(start) ? start : "",
                    hint: entry.type === "file" ? "Open the folder with the file, then tap it." :
                                                  "Open the folder, then tap ✓ to use it."
                })
                picker.picked.connect(function (chosen) { settingsModel.setValue(entry.setting, chosen) })
            }
            onPressAndHold: PopupUtils.open(resetPathDialog, page, { entry: entry })
            ListItemLayout {
                id: pathLayout
                title.text: entry.title
                subtitle.text: (pathRow.path !== "" ? pathRow.path : "Not set") +
                               (entry.description ? "\n" + entry.description : "")
                subtitle.maximumLineCount: 4
                subtitle.wrapMode: Text.WrapAnywhere
                ProgressionSlot {}
            }
        }
    }

    Component {
        id: resetPathDialog
        Dialog {
            id: dialog
            property var entry
            title: entry.title
            text: "Go back to the default?"
            Button {
                text: "Reset"
                color: theme.palette.normal.negative
                onClicked: {
                    settingsModel.setValue(dialog.entry.setting, "")
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // An action (the Android app's RunRunnable items): a confirmation, then a result.
    Component {
        id: actionItem
        ListItem {
            readonly property var entry: parent ? parent.entry : ({})
            height: actionLayout.height + (divider.visible ? divider.height : 0)
            enabled: !settingsModel.busy
            onClicked: {
                if (entry.confirmation)
                    PopupUtils.open(confirmActionDialog, page, { entry: entry })
                else
                    page.runAction(entry.action)
            }
            ListItemLayout {
                id: actionLayout
                title.text: entry.title
                subtitle.text: entry.description
                subtitle.wrapMode: Text.Wrap
                ActivityIndicator {
                    SlotsLayout.position: SlotsLayout.Trailing
                    running: settingsModel.busy
                    visible: running
                }
            }
        }
    }

    Component {
        id: confirmActionDialog
        Dialog {
            id: dialog
            property var entry
            title: entry.title
            text: entry.confirmation
            Button {
                text: "Yes"
                color: theme.palette.normal.positive
                onClicked: {
                    page.runAction(dialog.entry.action)
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: actionResultDialog
        Dialog {
            id: dialog
            property bool ok
            title: ok ? "Done" : "Failed"
            text: ok ? "" : "See the log on the Developer page."
            Button {
                text: "OK"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Connections {
        target: settingsModel
        function onActionFinished(action, ok) {
            if (!page.waitingForAction)
                return
            page.waitingForAction = false
            PopupUtils.open(actionResultDialog, page, { ok: ok })
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
