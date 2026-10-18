import QtQuick
import QtQuick.Controls

Page {
    id: root

    header: TextField {
        id: searchField
        placeholderText: qsTr("Code-barres ou nom…")
    }

    // Filtering is client-side on purpose: the backend hands over at most a
    // capped slice of the table, so narrowing it in QML costs nothing and a
    // scan never round-trips to C++ just to hide rows.
    readonly property var visibleProducts: {
        var query = searchField.text.trim().toLowerCase()
        var all = backend.products
        if (query.length === 0)
            return all
        return all.filter(function (product) {
            return product.name.toLowerCase().indexOf(query) !== -1
                || product.barcode.toLowerCase().indexOf(query) !== -1
        })
    }

    ListView {
        id: productList
        anchors.fill: parent
        clip: true
        model: root.visibleProducts

        delegate: Rectangle {
            width: ListView.view.width
            height: 64
            color: "#ffffff"

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 4

                Label {
                    width: parent.width
                    text: modelData.name
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: modelData.barcode
                          + "  ·  " + (modelData.salePriceCents / 100).toFixed(2)
                          + "  ·  " + modelData.quantity
                    font.pixelSize: 12
                    opacity: 0.6
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#e5e5e5"
            }
        }
    }

    RoundButton {
        id: saleButton
        text: "+"
        z: 1
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        onClicked: salePopup.open()
    }

    Popup {
        id: salePopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 260
        padding: 20
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Label {
            anchors.centerIn: parent
            text: qsTr("Vente — à venir")
        }
    }
}
