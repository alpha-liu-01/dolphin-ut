// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Content

// Import Games (stage 3.7): content-hub, Ubuntu Touch's way to get files from other apps. Pick an
// app (the File Manager, for example), choose games there, and they move into the games folder
// (storage.gamesFolder). content-hub hard-links a file on the same file system into the app's cache
// first, so even a 4 GB disc takes no time or space. Main.qml shows the result.
Page {
    id: page

    property var activeTransfer: null
    property bool importing: false

    header: PageHeader {
        title: "Import Games"
    }

    function itemUrls(transfer) {
        const urls = []
        for (let i = 0; i < transfer.items.length; ++i)
            urls.push(transfer.items[i].url)
        return urls
    }

    ContentPeerPicker {
        anchors {
            top: page.header.bottom
            topMargin: units.gu(1)
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: page.activeTransfer === null
        showTitle: false
        contentType: ContentType.All
        handler: ContentHandler.Source

        onPeerSelected: {
            peer.selectionType = ContentTransfer.Multiple
            emulator.appendLog("import: from " + peer.appId)
            page.activeTransfer = peer.request()
        }
        onCancelPressed: pageStack.pop()
    }

    ContentTransferHint {
        anchors.fill: parent
        activeTransfer: page.activeTransfer
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - units.gu(4)
        spacing: units.gu(2)
        visible: page.activeTransfer !== null

        ActivityIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: parent.visible
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: page.importing ? "Moving the games into " + storage.gamesFolder + "…" :
                                   "Waiting for the other app…"
        }
    }

    Connections {
        target: page.activeTransfer
        function onStateChanged() {
            const transfer = page.activeTransfer
            emulator.appendLog("import: transfer state " + transfer.state)
            if (transfer.state === ContentTransfer.Charged) {
                page.importing = storage.importFiles(page.itemUrls(transfer))
                if (!page.importing) {
                    transfer.finalize()
                    pageStack.pop()
                }
            } else if (transfer.state === ContentTransfer.Aborted) {
                // Cancelled in the other app: back to the list of apps.
                page.activeTransfer = null
            }
        }
    }

    Connections {
        target: storage
        function onImportFinished(result) {
            if (!page.importing)
                return
            page.importing = false
            page.activeTransfer.finalize()
            pageStack.pop()
        }
    }
}
