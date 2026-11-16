// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// A controller's mapping, as the Android app's (SettingsFragmentPresenter.addControllerMetaSettings
// and addContainerMappingSettings): the device, "Map all devices", Reset to Default, Clear,
// Profiles, then each group's controls and settings. Tap a control, then press the gamepad's
// button or move its stick (MotionAlertDialog); press and hold a control to edit its expression
// (AdvancedMappingDialog).
Page {
    id: page

    property var mapping
    property bool allDevices: false

    header: PageHeader {
        title: page.mapping ? page.mapping.title : ""
        trailingActionBar {
            numberOfSlots: 1
            actions: [
                Action {
                    iconName: "document-open"
                    text: "Profiles"
                    onTriggered: PopupUtils.open(profilesDialog, page)
                },
                Action {
                    iconName: "reload"
                    text: "Reset to Default"
                    onTriggered: PopupUtils.open(confirmDialog, page, {
                        title: "Reset to Default",
                        text: "Replace this controller's mapping with the default one? (The gamepad layout for SDL gamepads.)",
                        action: () => page.mapping.resetToDefault()
                    })
                },
                Action {
                    iconName: "edit-clear"
                    text: "Clear"
                    onTriggered: PopupUtils.open(confirmDialog, page, {
                        title: "Clear",
                        text: "Clear this controller's mapping?",
                        action: () => page.mapping.clear()
                    })
                }
            ]
        }
    }

    Component.onDestruction: if (mapping) mapping.cancelDetect()

    Flickable {
        id: flickable

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: column.height + units.gu(4)
        clip: true

        Column {
            id: column

            width: parent.width

            // Device: the connected devices, and the current one even if it is not connected.
            OptionSelector {
                // Lomiri's OptionSelector cannot size its list from a JavaScript array: the width and
                // the list height are given (S3.5: ports 2–4 showed an empty list).
                containerHeight: itemHeight * (model ? model.length : 0)
                id: deviceSelector

                readonly property var devices: {
                    const list = controllers.devices.slice()
                    if (page.mapping && page.mapping.device !== "" && list.indexOf(page.mapping.device) < 0)
                        list.push(page.mapping.device)
                    return list
                }

                anchors {
                    left: parent.left
                    right: parent.right
                    margins: units.gu(2)
                }
                text: "Device"
                model: devices
                // Assigned, not bound: OptionSelector resets selectedIndex when its model is set
                // (also when the device list changes).
                function select() {
                    selectedIndex = Math.max(0, page.mapping ? devices.indexOf(page.mapping.device) : 0)
                }
                Component.onCompleted: select()
                onModelChanged: Qt.callLater(select)
                onDelegateClicked: index => page.mapping.device = devices[index]
            }

            ListItem {
                height: allLayout.height
                ListItemLayout {
                    id: allLayout
                    title.text: "Map all devices"
                    subtitle.text: "Detect inputs on every device, not only the chosen one"
                    subtitle.maximumLineCount: 2
                    subtitle.wrapMode: Text.Wrap
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: page.allDevices
                        onClicked: page.allDevices = checked
                    }
                }
            }

            Repeater {
                model: page.mapping ? page.mapping.groups : []

                delegate: Column {
                    id: groupColumn

                    readonly property int groupIndex: index
                    readonly property var group: modelData

                    width: column.width

                    ListItem {
                        height: units.gu(5)
                        color: theme.palette.normal.base
                        Label {
                            anchors {
                                left: parent.left
                                leftMargin: units.gu(2)
                                verticalCenter: parent.verticalCenter
                            }
                            text: groupColumn.group.name
                            font.bold: true
                        }
                        Switch {
                            anchors {
                                right: parent.right
                                rightMargin: units.gu(2)
                                verticalCenter: parent.verticalCenter
                            }
                            visible: groupColumn.group.canDisable
                            checked: groupColumn.group.enabled
                            onClicked: page.mapping.setGroupEnabled(groupColumn.groupIndex, checked)
                        }
                    }

                    Repeater {
                        model: groupColumn.group.controls
                        delegate: ListItem {
                            height: controlLayout.height + (divider.visible ? divider.height : 0)
                            onClicked: {
                                if (!modelData.input) {
                                    // Outputs (rumble) cannot be detected: pick one.
                                    PopupUtils.open(outputDialog, page, {
                                        groupIndex: groupColumn.groupIndex,
                                        controlIndex: index,
                                        controlName: modelData.name
                                    })
                                    return
                                }
                                page.mapping.detect(groupColumn.groupIndex, index, page.allDevices)
                                PopupUtils.open(detectDialog, page, { controlName: modelData.name })
                            }
                            onPressAndHold: PopupUtils.open(advancedDialog, page, {
                                groupIndex: groupColumn.groupIndex,
                                controlIndex: index,
                                controlName: modelData.name,
                                expression: modelData.expression
                            })
                            ListItemLayout {
                                id: controlLayout
                                title.text: modelData.name
                                subtitle.text: modelData.expression !== "" ? modelData.expression : "Not mapped"
                                subtitle.elide: Text.ElideRight
                            }
                        }
                    }

                    Repeater {
                        model: groupColumn.group.settings
                        delegate: ListItem {
                            height: settingColumn.height + units.gu(2)
                            Column {
                                id: settingColumn
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    margins: units.gu(2)
                                    verticalCenter: parent.verticalCenter
                                }
                                Item {
                                    width: parent.width
                                    height: Math.max(settingLabel.height, settingSwitch.height)
                                    Label {
                                        id: settingLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name + (modelData.type !== "bool"
                                              ? ": " + (modelData.type === "double" ? Number(modelData.value).toFixed(1) : modelData.value)
                                                + (modelData.suffix ? " " + modelData.suffix : "")
                                              : "")
                                    }
                                    Switch {
                                        id: settingSwitch
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        visible: modelData.type === "bool"
                                        checked: modelData.type === "bool" && modelData.value
                                        onClicked: page.mapping.setSetting(groupColumn.groupIndex, index, checked)
                                    }
                                }
                                Slider {
                                    width: parent.width
                                    visible: modelData.type !== "bool"
                                    live: false
                                    minimumValue: modelData.min !== undefined ? modelData.min : 0
                                    maximumValue: modelData.max !== undefined ? modelData.max : 100
                                    value: modelData.type !== "bool" ? modelData.value : 0
                                    function formatValue(v) { return modelData.type === "int" ? Math.round(v) : v.toFixed(1) }
                                    onValueChanged: {
                                        // live: false, so this runs when the finger lifts.
                                        if (Math.abs(value - modelData.value) > 0.001)
                                            page.mapping.setSetting(groupColumn.groupIndex, index,
                                                                    modelData.type === "int" ? Math.round(value) : value)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // MotionAlertDialog: waits for an input, then maps it.
    Component {
        id: detectDialog

        Dialog {
            id: dialog

            property string controlName
            property string result: ""
            property bool done: false

            title: controlName
            text: done ? (result !== "" ? "Mapped to " + result : "Nothing pressed: unchanged")
                       : "Press a button or move a stick or trigger on the controller…"

            Connections {
                target: page.mapping
                function onDetected(group, control, expression) {
                    dialog.result = expression
                    dialog.done = true
                    closeTimer.start()
                }
            }
            Timer {
                id: closeTimer
                interval: 900
                onTriggered: PopupUtils.close(dialog)
            }
            ActivityIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: !dialog.done
                visible: running
            }
            Button {
                text: dialog.done ? "Close" : "Cancel"
                onClicked: {
                    page.mapping.cancelDetect()
                    PopupUtils.close(dialog)
                }
            }
        }
    }

    // An output (rumble): the chosen device's outputs, and the phone's motor.
    Component {
        id: outputDialog

        Dialog {
            id: dialog

            property int groupIndex
            property int controlIndex
            property string controlName

            title: controlName
            text: "Rumble on:"

            Repeater {
                model: page.mapping.outputs()
                delegate: Button {
                    text: modelData === "`UbuntuTouch/0/Phone:Motor`" ? "The phone's vibrator" : modelData
                    onClicked: {
                        page.mapping.setExpression(dialog.groupIndex, dialog.controlIndex, modelData)
                        PopupUtils.close(dialog)
                    }
                }
            }
            Button {
                text: "None"
                onClicked: {
                    page.mapping.setExpression(dialog.groupIndex, dialog.controlIndex, "")
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // AdvancedMappingDialog: the expression as text.
    Component {
        id: advancedDialog

        Dialog {
            id: dialog

            property int groupIndex
            property int controlIndex
            property string controlName
            property string expression

            title: controlName
            text: "Expression (for example `Button S` | `Trigger R`)"

            TextField {
                id: expressionField
                text: dialog.expression
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            Button {
                text: "Save"
                color: theme.palette.normal.positive
                onClicked: {
                    page.mapping.setExpression(dialog.groupIndex, dialog.controlIndex, expressionField.text)
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Clear"
                onClicked: {
                    page.mapping.setExpression(dialog.groupIndex, dialog.controlIndex, "")
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: confirmDialog

        Dialog {
            id: dialog

            property var action

            Button {
                text: "OK"
                color: theme.palette.normal.negative
                onClicked: {
                    dialog.action()
                    PopupUtils.close(dialog)
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // ProfileDialog: load, save and delete profiles (Config/Profiles/<controller>/).
    Component {
        id: profilesDialog

        Dialog {
            id: dialog

            title: "Profiles"
            text: page.mapping.profiles.length === 0 ? "No profiles yet." : ""

            Repeater {
                model: page.mapping.profiles
                delegate: Row {
                    spacing: units.gu(1)
                    Button {
                        width: units.gu(16)
                        text: modelData
                        onClicked: {
                            page.mapping.loadProfile(modelData)
                            PopupUtils.close(dialog)
                        }
                    }
                    Button {
                        width: units.gu(10)
                        text: "Delete"
                        color: theme.palette.normal.negative
                        onClicked: page.mapping.deleteProfile(modelData)
                    }
                }
            }
            TextField {
                id: nameField
                placeholderText: "New profile name"
            }
            Button {
                text: "Save current as new profile"
                color: theme.palette.normal.positive
                enabled: nameField.text.trim() !== ""
                onClicked: {
                    page.mapping.saveProfile(nameField.text)
                    nameField.text = ""
                }
            }
            Button {
                text: "Close"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
