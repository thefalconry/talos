// SPDX-License-Identifier: GPL-2.0-only
import QtQuick
import QtQuick.Layouts
import Talos.Native

// Per-chat model, reasoning effort and backend.
Sheet {
    id: sheet

    title: qsTr("Model")
    maxWidth: 460

    property string chatId
    property bool loading: false
    property var options: ({})
    property string query
    property string backendError
    property string switching

    readonly property var shown: (options.models || []).filter(
        m => query === "" || m.displayName.toLowerCase().includes(query.toLowerCase())
             || m.id.toLowerCase().includes(query.toLowerCase()))

    function load() {
        loading = true
        App.session.fetchChatOptions(chatId, function (result) {
            sheet.options = result
            sheet.loading = false
        })
    }

    onAboutToShow: {
        chatId = App.session.selectedChatId
        query = ""
        backendError = ""
        options = ({})
        load()
    }

    Spinner {
        visible: sheet.loading
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 20
        Layout.bottomMargin: 20
    }

    TField {
        visible: !sheet.loading && (sheet.options.models || []).length > 7
        Layout.fillWidth: true
        placeholderText: qsTr("Find a model")
        onTextChanged: sheet.query = text
    }

    Text {
        visible: !sheet.loading && (sheet.options.models || []).length === 0
        text: qsTr("This backend lists no selectable models.")
        color: Theme.textSecondary
        font.pixelSize: Theme.body
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    ColumnLayout {
        visible: !sheet.loading
        Layout.fillWidth: true
        spacing: 2

        Repeater {
            model: sheet.shown

            Rectangle {
                id: modelRow
                required property var modelData
                readonly property bool on: modelData.id === sheet.options.model

                Layout.fillWidth: true
                implicitHeight: 44
                radius: 9
                color: on ? Theme.selected : rowHover.hovered ? Theme.hover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: modelRow.modelData.displayName
                            color: Theme.text
                            font.pixelSize: Theme.body
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: modelRow.modelData.provider
                                  + (modelRow.modelData.reasoning ? " · " + qsTr("reasoning") : "")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.small
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Icon {
                        visible: modelRow.on
                        name: "check"
                        size: 16
                        color: Theme.accent
                    }
                }
                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        App.session.setModel(sheet.chatId, modelRow.modelData.id)
                        const next = Object.assign({}, sheet.options)
                        next.model = modelRow.modelData.id
                        sheet.options = next
                    }
                }
            }
        }
    }

    // Reasoning effort, when the backend has such a knob.
    ColumnLayout {
        visible: !sheet.loading && (sheet.options.efforts || []).length > 0
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 8

        Text {
            text: qsTr("Reasoning effort")
            color: Theme.text
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: sheet.options.efforts || []

                Rectangle {
                    id: effort
                    required property string modelData
                    readonly property bool on: modelData === sheet.options.effort
                    width: effortLabel.implicitWidth + 24
                    height: 30
                    radius: 15
                    color: on ? Theme.accentSoft : effortHover.hovered ? Theme.hover : "transparent"
                    border.width: 1
                    border.color: on ? Theme.accent : Theme.border

                    Text {
                        id: effortLabel
                        anchors.centerIn: parent
                        text: effort.modelData
                        color: effort.on ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.small + 1
                    }
                    HoverHandler { id: effortHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            App.session.setEffort(sheet.chatId, effort.modelData)
                            const next = Object.assign({}, sheet.options)
                            next.effort = effort.modelData
                            sheet.options = next
                        }
                    }
                }
            }
        }
    }

    // Backend. Switching rebinds the chat, which can take a moment.
    ColumnLayout {
        visible: !sheet.loading && (sheet.options.backends || []).length > 1
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 8

        Text {
            text: qsTr("Backend")
            color: Theme.text
            font.pixelSize: Theme.body
            font.weight: Font.DemiBold
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: sheet.options.backends || []

                Rectangle {
                    id: backend
                    required property var modelData
                    readonly property bool on: modelData.id === sheet.options.backend
                    width: backendRow.implicitWidth + 24
                    height: 30
                    radius: 15
                    color: on ? Theme.accentSoft : backendHover.hovered ? Theme.hover : "transparent"
                    border.width: 1
                    border.color: on ? Theme.accent : Theme.border
                    opacity: sheet.switching !== "" && sheet.switching !== modelData.id ? 0.5 : 1

                    Row {
                        id: backendRow
                        anchors.centerIn: parent
                        spacing: 6

                        Spinner {
                            visible: sheet.switching === backend.modelData.id
                            size: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: backend.modelData.label || backend.modelData.id
                            color: backend.on ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.small + 1
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    HoverHandler { id: backendHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: sheet.switching === "" && !backend.on
                        onTapped: {
                            sheet.switching = backend.modelData.id
                            sheet.backendError = ""
                            App.session.setBackend(sheet.chatId, backend.modelData.id, function (r) {
                                sheet.switching = ""
                                if (r.ok)
                                    sheet.load()
                                else
                                    sheet.backendError = r.error || qsTr("Couldn't switch backend")
                            })
                        }
                    }
                }
            }
        }
        Text {
            visible: sheet.backendError !== ""
            text: sheet.backendError
            color: Theme.danger
            font.pixelSize: Theme.small + 1
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
