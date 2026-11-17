// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// Settings → Graphics → GPU Driver (stage 4, GpuDrivers.h), as in the Android app: the phone's own
// Vulkan driver, or an installed driver package such as Mesa Turnip. The choice applies from the
// next start, as the driver is loaded with the app.
Page {
    id: page

    header: PageHeader {
        title: "GPU Driver"
    }

    property string message: ""

    function driverName(id) {
        if (id === "")
            return "System driver"
        const drivers = gpuDrivers.drivers
        for (let i = 0; i < drivers.length; ++i) {
            if (drivers[i].id === id)
                return drivers[i].name
        }
        return id
    }

    readonly property bool restartNeeded: gpuDrivers.selected !== gpuDrivers.active
                                          && !gpuDrivers.active.startsWith("/")

    function install() {
        let start = ""
        const places = storage.places()
        for (let i = 0; i < places.length; ++i) {
            if (places[i].name === "Downloads")
                start = places[i].path
        }
        const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
            title: "Install Driver",
            pickFiles: true,
            extensions: ["zip"],
            startFolder: start,
            hint: "Open the folder with the driver package (.zip), then tap it."
        })
        picker.picked.connect(function (path) {
            const result = gpuDrivers.install(path)
            if (result.ok) {
                gpuDrivers.select(result.id)
                page.message = result.name + " is installed and chosen."
            } else {
                page.message = "Not installed: " + result.error
            }
        })
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

            ListItem {
                height: statusLayout.height + (divider.visible ? divider.height : 0)
                ListItemLayout {
                    id: statusLayout
                    title.text: "In use now: " + page.driverName(gpuDrivers.active)
                    summary.text: gpuDrivers.activeError !== "" ?
                                      "The chosen driver could not be used: " + gpuDrivers.activeError :
                                  page.restartNeeded ?
                                      (lomiri.canRestart ? "Restart Dolphin to use " :
                                                           "Close Dolphin (swipe it away in the app switcher) and open it again to use ") +
                                      page.driverName(gpuDrivers.selected) + "." :
                                      "Used by the Vulkan video backend."
                    summary.wrapMode: Text.Wrap
                    summary.maximumLineCount: 4
                    summary.color: gpuDrivers.activeError !== "" ? theme.palette.normal.negative :
                                                                   theme.palette.normal.backgroundSecondaryText
                }
            }

            // Stage 6.8: restart in place (LomiriIntegration::restart), not during a game.
            Button {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                visible: page.restartNeeded && lomiri.canRestart
                enabled: emulator.state === "idle"
                text: "Restart Dolphin"
                color: theme.palette.normal.positive
                onClicked: lomiri.restart(true)
            }

            ListItem {
                height: systemLayout.height + (divider.visible ? divider.height : 0)
                onClicked: gpuDrivers.select("")
                ListItemLayout {
                    id: systemLayout
                    title.text: "System driver"
                    summary.text: "The phone's own Qualcomm driver."
                    Icon {
                        SlotsLayout.position: SlotsLayout.Trailing
                        width: units.gu(2.5)
                        height: width
                        name: "tick"
                        visible: gpuDrivers.selected === ""
                    }
                }
            }

            Repeater {
                model: gpuDrivers.drivers
                delegate: ListItem {
                    height: driverLayout.height + (divider.visible ? divider.height : 0)
                    onClicked: gpuDrivers.select(modelData.id)
                    leadingActions: ListItemActions {
                        actions: [
                            Action {
                                iconName: "delete"
                                text: "Remove"
                                onTriggered: gpuDrivers.remove(modelData.id)
                            }
                        ]
                    }
                    ListItemLayout {
                        id: driverLayout
                        title.text: modelData.name
                        subtitle.text: [modelData.driverVersion, modelData.vendor, modelData.author,
                                        modelData.version ? "package v" + modelData.version : ""]
                                           .filter(function (part) { return part }).join(" · ")
                        summary.text: modelData.description || ""
                        summary.wrapMode: Text.Wrap
                        summary.maximumLineCount: 3
                        Icon {
                            SlotsLayout.position: SlotsLayout.Trailing
                            width: units.gu(2.5)
                            height: width
                            name: "tick"
                            visible: gpuDrivers.selected === modelData.id
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: units.gu(8)
                Button {
                    anchors.centerIn: parent
                    text: "Install Driver…"
                    onClicked: page.install()
                }
            }

            Label {
                width: parent.width
                padding: units.gu(2)
                topPadding: 0
                visible: page.message !== ""
                text: page.message
                wrapMode: Text.Wrap
            }

            Label {
                width: parent.width
                padding: units.gu(2)
                topPadding: 0
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: "Driver packages are zip files with a meta.json, the format of the Android app " +
                      "(for example Mesa Turnip builds for Adreno). Other drivers are not tested by " +
                      "Dolphin: if games fail with one, choose the system driver. Swipe a driver " +
                      "to the right to remove it."
            }
        }
    }
}
