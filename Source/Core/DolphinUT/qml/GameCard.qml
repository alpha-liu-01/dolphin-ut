// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import Lomiri.Components

// One game in the library grid, after the Android app's card_game.xml: the cover (or the title on
// a plain card when there is none), then the title and the company.
AbstractButton {
    id: card

    property bool showTitle: true

    LomiriShape {
        id: cover

        anchors {
            top: parent.top
            topMargin: units.gu(0.5)
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width - units.gu(1.5)
        height: width * 1.4
        aspect: LomiriShape.DropShadow
        backgroundColor: theme.palette.normal.base
        sourceFillMode: LomiriShape.PreserveAspectCrop
        source: model.cover !== "" ? coverImage : null

        Image {
            id: coverImage

            visible: false
            source: model.cover
            asynchronous: true
            // The cache key changes when the game's metadata does, but an address can be reused.
            cache: false
            sourceSize.width: cover.width
        }

        Label {
            anchors {
                fill: parent
                margins: units.gu(1)
            }
            visible: model.cover === "" || coverImage.status === Image.Error
            text: model.title
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: theme.palette.normal.baseText
        }
    }

    Column {
        anchors {
            top: cover.bottom
            topMargin: units.gu(0.5)
            left: cover.left
            right: cover.right
        }
        visible: card.showTitle

        Label {
            width: parent.width
            text: model.title
            textSize: Label.Small
            maximumLineCount: 2
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }
        Label {
            width: parent.width
            text: model.company
            textSize: Label.XSmall
            color: theme.palette.normal.backgroundSecondaryText
            elide: Text.ElideRight
        }
    }
}
