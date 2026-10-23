import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ApplicationWindow {
    width: 400
    height: 720
    visible: true
    title: qsTr("Mahali")

    Material.theme: Material.Light
    Material.accent: Material.Blue

    ProductListPage {
        anchors.fill: parent
    }
}
