import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 400
    height: 720
    visible: true
    title: qsTr("Mahali")

    ProductListPage {
        anchors.fill: parent
    }
}
