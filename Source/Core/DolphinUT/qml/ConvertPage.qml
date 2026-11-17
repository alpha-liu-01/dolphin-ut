// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Components.Popups

// Convert File (stage 6.4), as the Android app's ConvertFragment: format, block size, compression,
// compression level and Remove Junk Data, with its lists, defaults and warnings; then where to save
// (Android asks with the system's save dialog; here a folder and a file name, the game's folder by
// default, with DolphinQt's question before replacing a file). Converts with discConverter
// (DiscConverter.cpp), with progress and Cancel.
Page {
    id: page

    property string path
    readonly property var info: discConverter.info(path)

    // arrays.xml: convertFormat*, convertBlockSize*, convertCompression*, convertCompressionLevel*.
    readonly property var formats: [
        { name: "ISO", value: 0, ext: ".iso" },
        { name: "GCZ", value: 3, ext: ".gcz" },
        { name: "WIA", value: 7, ext: ".wia" },
        { name: "RVZ", value: 8, ext: ".rvz" }
    ]
    readonly property var blockSizes: ({
        3: { names: ["32 KiB"], values: [32768], initial: 0 },
        7: { names: ["2 MiB"], values: [2097152], initial: 0 },
        8: { names: ["32 KiB", "64 KiB", "128 KiB", "256 KiB", "512 KiB", "1 MiB", "2 MiB"],
             values: [32768, 65536, 131072, 262144, 524288, 1048576, 2097152], initial: 2 }
    })
    readonly property var compressions: ({
        3: { names: ["Deflate"], values: [0], initial: 0 },
        7: { names: ["No Compression", "Purge", "bzip2 (slow)", "LZMA (slow)", "LZMA2 (slow)"],
             values: [0, 1, 2, 3, 4], initial: 0 },
        8: { names: ["No Compression", "bzip2 (slow)", "LZMA (slow)", "LZMA2 (slow)", "Zstandard (recommended)"],
             values: [0, 2, 3, 4, 5], initial: 4 }
    })
    function levelsFor(compression) {
        const count = compression === 5 ? 22 : (compression >= 2 && compression <= 4 ? 9 : 0)
        const levels = []
        for (let i = 1; i <= count; ++i)
            levels.push(i)
        return levels
    }

    // ConvertFragment.populateFormats: ISO, or RVZ for a game that is an ISO already.
    property int formatIndex: info.blobType === 0 ? 3 : 0
    readonly property int format: formats[formatIndex].value
    property int blockIndex: 0
    property int compressionIndex: 0
    property int levelIndex: 0
    property bool scrub: false
    readonly property var blockList: blockSizes[format] || { names: [], values: [] }
    readonly property var compressionList: compressions[format] || { names: [], values: [] }
    readonly property int compression: compressionList.values.length > 0 ? compressionList.values[compressionIndex] : 0
    readonly property var levels: levelsFor(compression)
    readonly property bool scrubAllowed: format !== 8 && !info.datel

    property string folder: info.folder
    property string fileName: info.baseName + formats[formatIndex].ext
    readonly property string outPath: folder + "/" + fileName

    // A new format brings its own defaults (populateBlockSize / populateCompression).
    onFormatChanged: {
        blockIndex = blockSizes[format] ? blockSizes[format].initial : 0
        compressionIndex = compressions[format] ? compressions[format].initial : 0
        if (!scrubAllowed)
            scrub = false
        fileName = info.baseName + formats[formatIndex].ext
    }
    onCompressionChanged: levelIndex = Math.min(4, Math.max(0, levels.length - 1))
    Component.onCompleted: {
        blockIndex = blockSizes[format] ? blockSizes[format].initial : 0
        compressionIndex = compressions[format] ? compressions[format].initial : 0
        levelIndex = Math.min(4, Math.max(0, levels.length - 1))
    }

    header: PageHeader {
        title: "Convert File"
        subtitle: page.info.title
        leadingActionBar.actions: Action {
            iconName: "back"
            text: "Back"
            enabled: !discConverter.busy
            onTriggered: pageStack.pop()
        }
    }

    component ChoiceRow: ListItem {
        id: row
        property string title
        property var names: []
        property int current: 0
        signal chosen(int index)
        height: rowLayout.height + (divider.visible ? divider.height : 0)
        enabled: names.length > 1
        onClicked: PopupUtils.open(choiceDialog, page, { title: row.title, names: row.names, current: row.current, row: row })
        ListItemLayout {
            id: rowLayout
            title.text: row.title
            subtitle.text: row.names.length > 0 ? row.names[row.current] : "—"
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

            ChoiceRow {
                title: "Format"
                names: page.formats.map(f => f.name)
                current: page.formatIndex
                onChosen: index => page.formatIndex = index
            }
            ChoiceRow {
                title: "Block Size"
                names: page.blockList.names
                current: page.blockIndex
                onChosen: index => page.blockIndex = index
            }
            ChoiceRow {
                title: "Compression"
                names: page.compressionList.names
                current: page.compressionIndex
                onChosen: index => page.compressionIndex = index
            }
            ChoiceRow {
                title: "Compression Level"
                names: page.levels.map(l => String(l))
                current: page.levelIndex
                onChosen: index => page.levelIndex = index
            }
            ListItem {
                height: junkLayout.height
                enabled: page.scrubAllowed
                ListItemLayout {
                    id: junkLayout
                    title.text: "Remove Junk Data (Irreversible)"
                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: page.scrub
                        onClicked: page.scrub = checked
                    }
                }
            }
            ListItem {
                height: folderLayout.height + (divider.visible ? divider.height : 0)
                onClicked: {
                    const picker = pageStack.push(Qt.resolvedUrl("FolderPickerPage.qml"), {
                        title: "Save to",
                        startFolder: page.folder,
                        hint: "Open the folder to save the converted file in, then tap ✓."
                    })
                    picker.picked.connect(chosen => page.folder = chosen)
                }
                ListItemLayout {
                    id: folderLayout
                    title.text: "Save to"
                    subtitle.text: page.folder
                    subtitle.wrapMode: Text.WrapAnywhere
                    subtitle.maximumLineCount: 3
                    ProgressionSlot {}
                }
            }
            Column {
                width: parent.width - units.gu(4)
                x: units.gu(2)
                spacing: units.gu(0.5)
                topPadding: units.gu(1)
                Label { text: "File name" }
                TextField {
                    width: parent.width
                    text: page.fileName
                    onTextChanged: page.fileName = text
                    inputMethodHints: Qt.ImhNoPredictiveText
                }
            }
            Button {
                x: units.gu(2)
                width: parent.width - units.gu(4)
                anchors.margins: units.gu(2)
                text: "Convert"
                color: theme.palette.normal.positive
                enabled: page.info.allowed && page.fileName.trim() !== "" && !discConverter.busy
                onClicked: page.startConvert()
            }
            Label {
                width: parent.width
                padding: units.gu(2)
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: "ISO: A simple and robust format which is supported by many programs. It takes up more space than any other format.\n\n" +
                      "GCZ: A basic compressed format which is compatible with most versions of Dolphin and some other programs. It can't efficiently compress junk data (unless removed) or encrypted Wii data.\n\n" +
                      "WIA: An advanced compressed format which is compatible with Dolphin 5.0-12188 and later, and a few other programs. It can efficiently compress encrypted Wii data, but not junk data (unless removed).\n\n" +
                      "RVZ: An advanced compressed format which is compatible with Dolphin 5.0-12188 and later. It can efficiently compress both junk data and encrypted Wii data."
            }
        }
    }

    // ConvertFragment.onClick: its warnings, one after another, then the save location.
    function startConvert() {
        const steps = []
        if (scrub && format === 0)
            steps.push("Removing junk data does not save any space when converting to ISO (unless you package the ISO file in a compressed file format such as ZIP afterwards). Do you want to continue anyway?")
        if (!scrub && format === 3 && !info.datel && info.wii)
            steps.push("Converting Wii disc images to GCZ without removing junk data does not save any noticeable amount of space compared to converting to ISO. Do you want to continue anyway?")
        if (info.nkit)
            steps.push("Dolphin can't convert NKit files to non-NKit files. Converting an NKit file in Dolphin will result in another NKit file.\n\nIf you want to convert an NKit file to a non-NKit file, you can use the same program as you originally used when converting the file to the NKit format.\n\nDo you want to continue anyway?")
        if (storage.exists(outPath))
            steps.push("The file %1 already exists.\nDo you wish to replace it?".arg(fileName))
        askThenConvert(steps)
    }
    function askThenConvert(steps) {
        if (steps.length === 0) {
            discConverter.convert(path, outPath, format,
                                  blockList.values.length > 0 ? blockList.values[blockIndex] : 0,
                                  compression, levels.length > 0 ? levels[levelIndex] : 0, scrub)
            return
        }
        PopupUtils.open(questionDialog, page, { text: steps[0], rest: steps.slice(1) })
    }

    Component {
        id: questionDialog
        Dialog {
            id: dialog
            property var rest: []
            Button {
                text: "Yes"
                color: theme.palette.normal.positive
                onClicked: {
                    PopupUtils.close(dialog)
                    page.askThenConvert(dialog.rest)
                }
            }
            Button {
                text: "No"
                onClicked: PopupUtils.close(dialog)
            }
        }
    }

    Component {
        id: choiceDialog
        Dialog {
            id: dialog
            property var names: []
            property int current
            property var row
            Repeater {
                model: dialog.names
                delegate: Button {
                    text: (index === dialog.current ? "✓  " : "") + modelData
                    color: index === dialog.current ? theme.palette.normal.positive : theme.palette.normal.base
                    onClicked: {
                        dialog.row.chosen(index)
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

    // The conversion's progress (Android: a progress dialog with Cancel).
    Rectangle {
        anchors.fill: parent
        visible: discConverter.busy
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
                    text: "Converting"
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: discConverter.progressText
                    visible: text !== ""
                }
                ProgressBar {
                    width: parent.width
                    minimumValue: 0
                    maximumValue: 1000
                    value: Math.round(discConverter.progress * 1000)
                }
                Button {
                    width: parent.width
                    text: "Cancel"
                    onClicked: discConverter.cancel()
                }
            }
        }
    }

    Connections {
        target: discConverter
        function onFinished(ok, cancelled, outPath) {
            if (cancelled)
                return
            if (ok)
                gameList.refresh()
            PopupUtils.open(resultDialog, page, {
                text: ok ? "The disc image was successfully converted." : "Dolphin failed to complete the requested action.",
                ok: ok
            })
        }
    }

    Component {
        id: resultDialog
        Dialog {
            id: dialog
            property bool ok
            Button {
                text: "OK"
                onClicked: {
                    PopupUtils.close(dialog)
                    if (dialog.ok)
                        pageStack.pop()
                }
            }
        }
    }
}
