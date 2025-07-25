import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

Dialog {
    title: qsTr("About")
    width: 300
    height: 200
    modal: true
    
    Label {
        anchors.centerIn: parent
        text: qsTr("ATS Dashboard v1.0")
    }
}