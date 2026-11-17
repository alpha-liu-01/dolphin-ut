// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtCore
import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups
import DolphinUT.Native

// The game library (stage 3.1), after the Android app's main screen: a grid of covers in the
// platform tabs GameCube, Wii and WiiWare. Tap starts a game; long press opens its properties.
Page {
    id: page

    property bool searching: false

    // The selected tab, kept across runs. Read once the page is complete and written only after
    // that: Sections reports its initial index (0) while starting, which would overwrite the saved one.
    Settings {
        id: settings
        category: "Library"
    }
    property bool tabRestored: false

    Component.onCompleted: {
        sections.selectedIndex = Number(settings.value("platformTab", 0))
        tabRestored = true
    }

    header: searching ? searchHeader : mainHeader

    // The Android app's Open File: start a game file without adding its folder.
    function openFile() {
        const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
            title: "Open File",
            pickFiles: true,
            extensions: ["gcm", "tgc", "bin", "iso", "ciso", "gcz", "wbfs", "wia", "rvz", "nfs",
                         "wad", "dol", "elf", "json"]
        })
        picker.picked.connect(function (path) { emulator.start(path) })
    }

    PageHeader {
        id: mainHeader

        visible: !page.searching
        title: "Dolphin"

        trailingActionBar {
            numberOfSlots: 3
            actions: [
                Action {
                    iconName: "search"
                    text: "Search"
                    onTriggered: {
                        page.searching = true
                        searchField.forceActiveFocus()
                    }
                },
                Action {
                    iconName: "reload"
                    text: "Refresh Library"
                    onTriggered: gameList.refresh()
                },
                Action {
                    iconName: "settings"
                    text: "Settings"
                    onTriggered: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                },
                Action {
                    iconName: "view-grid-symbolic"
                    text: "Grid Options"
                    onTriggered: PopupUtils.open(gridOptionsDialog, page)
                },
                Action {
                    iconName: "import"
                    text: "Import Games"
                    onTriggered: pageStack.push(Qt.resolvedUrl("ImportPage.qml"))
                },
                Action {
                    iconName: "document-open"
                    text: "Open File"
                    onTriggered: page.openFile()
                },
                Action {
                    // Stage 6.3: the Android main menu's Wii items, on one page.
                    iconName: "computer-symbolic"
                    text: "Wii System"
                    onTriggered: pageStack.push(Qt.resolvedUrl("WiiSystemPage.qml"))
                },
                Action {
                    iconName: "input-gaming-symbolic"
                    text: "Controllers"
                    onTriggered: pageStack.push(Qt.resolvedUrl("ControllersPage.qml"))
                },
                Action {
                    iconName: "folder"
                    text: "Game Folders"
                    onTriggered: pageStack.push(Qt.resolvedUrl("GameFoldersPage.qml"))
                },
                Action {
                    iconName: "info"
                    text: "Developer"
                    onTriggered: pageStack.push(Qt.resolvedUrl("DeveloperPage.qml"))
                }
            ]
        }

        extension: Sections {
            id: sections

            anchors {
                left: parent.left
                bottom: parent.bottom
            }
            model: ["GameCube", "Wii", "WiiWare"]
            onSelectedIndexChanged: {
                if (page.tabRestored)
                    settings.setValue("platformTab", selectedIndex)
            }
        }
    }

    PageHeader {
        id: searchHeader

        visible: page.searching
        leadingActionBar.actions: Action {
            iconName: "back"
            text: "Back"
            onTriggered: {
                searchField.text = ""
                page.searching = false
            }
        }
        contents: TextField {
            id: searchField

            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            placeholderText: "Search games"
            inputMethodHints: Qt.ImhNoPredictiveText
        }
    }

    GameListFilter {
        id: filter

        games: gameList
        // While searching, all platforms.
        platformTab: page.searching ? -1 : sections.selectedIndex
        searchText: searchField.text
    }

    ProgressBar {
        id: progress

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
        }
        height: units.dp(3)
        indeterminate: true
        visible: gameList.loading
        z: 1
    }

    GridView {
        id: grid

        // Cover cards of about 16 grid units: 3 columns upright, 7 in landscape on this phone.
        readonly property int columns: Math.max(2, Math.floor(width / units.gu(16)))

        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: units.gu(1)
            rightMargin: units.gu(1)
        }
        clip: true
        model: filter
        cellWidth: width / columns
        cellHeight: cellWidth * 1.4 + (gameList.showTitles ? units.gu(6) : units.gu(1))
        topMargin: units.gu(1)
        bottomMargin: units.gu(1)

        delegate: GameCard {
            width: grid.cellWidth
            height: grid.cellHeight
            showTitle: gameList.showTitles
            onClicked: emulator.start(path)
            onPressAndHold: PopupUtils.open(propertiesDialog, page, {
                gamePath: path, gameTitle: title, gameIdText: gameId
            })
        }

        PullToRefresh {
            refreshing: gameList.loading
            onRefresh: gameList.refresh()
        }
    }

    Scrollbar {
        flickableItem: grid
        align: Qt.AlignTrailing
    }

    Column {
        anchors.centerIn: grid
        width: grid.width - units.gu(4)
        spacing: units.gu(2)
        visible: filter.count === 0 && !gameList.loading

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            textSize: Label.Large
            text: page.searching ? "No games match" :
                  gameList.folders.length === 0 ? "No game folders yet" : "No games here"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: !page.searching
            text: (gameList.folders.length === 0 ?
                       "Add the folder with your GameCube and Wii games." :
                       "Games in other formats or folders can be added in Game Folders.") +
                  " From a computer, copy games over USB into Documents/Dolphin/Games."
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !page.searching
            text: "Import Games"
            onClicked: pageStack.push(Qt.resolvedUrl("ImportPage.qml"))
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !page.searching
            text: "Game Folders"
            color: theme.palette.normal.positive
            onClicked: pageStack.push(Qt.resolvedUrl("GameFoldersPage.qml"))
        }
    }

    // The Android app's grid options.
    Component {
        id: gridOptionsDialog

        Dialog {
            id: dialog

            title: "Grid Options"

            ListItem {
                height: coversLayout.height
                divider.visible: false
                ListItemLayout {
                    id: coversLayout
                    title.text: "Download Game Covers from GameTDB"
                    title.maximumLineCount: 2
                    title.wrapMode: Text.Wrap
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: gameList.useCovers
                        onClicked: gameList.useCovers = checked
                    }
                }
            }
            ListItem {
                height: titlesLayout.height
                divider.visible: false
                ListItemLayout {
                    id: titlesLayout
                    title.text: "Show Titles"
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: gameList.showTitles
                        onClicked: gameList.showTitles = checked
                    }
                }
            }
            Button {
                text: "Close"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    // Long press: the Android app's game properties. Game settings, cheats and the other entries
    // come with their phases (3.6 and later); starting with a given backend is temporary, until
    // per-game settings exist.
    Component {
        id: propertiesDialog

        Dialog {
            id: dialog

            property string gamePath
            property string gameTitle
            property string gameIdText

            title: gameTitle
            text: gameIdText

            Button {
                text: "Details"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    pageStack.push(Qt.resolvedUrl("GameDetailsPage.qml"),
                                        { details: gameList.details(dialog.gamePath) })
                }
            }
            Button {
                text: "Edit Game Settings"
                onClicked: {
                    const details = gameList.details(dialog.gamePath)
                    PopupUtils.close(dialog)
                    if (settingsModel.openGame(details.gameId, details.revision))
                        pageStack.push(Qt.resolvedUrl("SettingsPage.qml"),
                                       { closesGame: true, gameTitle: details.title })
                }
            }
            Button {
                text: "Clear Game Settings"
                onClicked: {
                    settingsModel.clearGameSettings(gameList.details(dialog.gamePath).gameId)
                    PopupUtils.close(dialog)
                }
            }
            Button {
                // Stage 6.3: the Android app's game properties offer it for Wii discs.
                text: "Perform System Update"
                visible: gameList.details(dialog.gamePath).wiiDisc === true
                onClicked: {
                    PopupUtils.close(dialog)
                    pageStack.push(Qt.resolvedUrl("WiiSystemPage.qml"), { discUpdatePath: dialog.gamePath })
                }
            }
            Button {
                text: "Start with Vulkan"
                onClicked: {
                    PopupUtils.close(dialog)
                    emulator.start(dialog.gamePath, "Vulkan")
                }
            }
            Button {
                text: "Start with OpenGL ES"
                onClicked: {
                    PopupUtils.close(dialog)
                    emulator.start(dialog.gamePath, "OGL")
                }
            }
            Button {
                text: "Cancel"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
