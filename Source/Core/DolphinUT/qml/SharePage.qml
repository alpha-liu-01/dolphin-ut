// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components
import Lomiri.Content

// Send a file to another app (stage 6.5): content-hub, the other way round from Import Games. Pick
// an app that takes any file (the File Manager, which then saves it where you choose); Dolphin hands
// it the file. Used for a user data backup and for single files from the user data (save states,
// memory cards), which are not visible over USB.
Page {
    id: page

    property string path
    property var activeTransfer: null

    header: PageHeader {
        title: "Send to Another App"
        subtitle: page.path.substring(page.path.lastIndexOf("/") + 1)
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
        handler: ContentHandler.Destination

        onPeerSelected: {
            emulator.appendLog("share: " + page.path + " to " + peer.appId)
            const transfer = peer.request()
            transfer.items = [itemComponent.createObject(page, { url: "file://" + page.path })]
            transfer.state = ContentTransfer.Charged
            page.activeTransfer = transfer
        }
        onCancelPressed: pageStack.pop()
    }

    ContentTransferHint {
        anchors.fill: parent
        activeTransfer: page.activeTransfer
    }

    Component {
        id: itemComponent
        ContentItem {}
    }

    Connections {
        target: page.activeTransfer
        function onStateChanged() {
            emulator.appendLog("share: transfer state " + page.activeTransfer.state)
            // Collected by the other app, or given up there: done here either way.
            if (page.activeTransfer.state === ContentTransfer.Collected ||
                page.activeTransfer.state === ContentTransfer.Aborted ||
                page.activeTransfer.state === ContentTransfer.Finalized)
                pageStack.pop()
        }
    }
}
