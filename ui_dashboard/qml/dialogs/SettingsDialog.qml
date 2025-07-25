import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

Dialog {
    title: qsTr("Settings")
    width: 400
    height: 300
    modal: true
    
    Label {
        anchors.centerIn: parent
        text: qsTr("Settings dialog - Coming Soon")
    }
}