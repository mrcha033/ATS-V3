import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15

/**
 * Alert panel widget for displaying system alerts and notifications
 */
ListView {
    id: alertsList
    
    property var alerts: []
    property int maxItems: 10
    property bool showOnlyUnread: false
    
    // Colors for different alert types
    readonly property color infoColor: "#2196F3"
    readonly property color successColor: "#4CAF50"
    readonly property color warningColor: "#FF9800"
    readonly property color errorColor: "#F44336"
    
    clip: true
    spacing: 8
    
    model: getFilteredAlerts()
    
    delegate: MaterialCard {
        width: alertsList.width
        height: alertContent.implicitHeight + 16
        interactive: true
        
        onClicked: markAsRead(modelData.alertId)
        
        RowLayout {
            id: alertContent
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            
            // Alert type indicator
            Rectangle {
                width: 4
                height: parent.height
                color: getAlertColor(modelData.type)
                radius: 2
            }
            
            // Alert icon
            Rectangle {
                width: 32
                height: 32
                radius: 16
                color: getAlertColor(modelData.type)
                opacity: 0.1
                
                Image {
                    anchors.centerIn: parent
                    source: getAlertIcon(modelData.type)
                    width: 16
                    height: 16
                    
                    ColorOverlay {
                        anchors.fill: parent
                        source: parent
                        color: getAlertColor(modelData.type)
                    }
                }
            }
            
            // Alert content
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Label {
                        text: modelData.title
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }
                    
                    // Unread indicator
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: errorColor
                        visible: !modelData.isRead
                    }
                }
                
                Label {
                    text: modelData.message
                    font.pixelSize: 12
                    opacity: 0.8
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                
                RowLayout {
                    spacing: 16
                    
                    Label {
                        text: Qt.formatDateTime(new Date(modelData.timestamp), "MMM dd, hh:mm")
                        font.pixelSize: 11
                        opacity: 0.6
                    }
                    
                    Label {
                        text: modelData.strategy
                        font.pixelSize: 11
                        opacity: 0.6
                        visible: modelData.strategy
                    }
                }
            }
            
            // Action buttons
            RowLayout {
                spacing: 4
                
                Button {
                    flat: true
                    icon.source: "qrc:/resources/icons/close.svg"
                    icon.width: 16
                    icon.height: 16
                    onClicked: dismissAlert(modelData.alertId)
                    
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Dismiss")
                }
            }
        }
    }
    
    // Empty state
    Label {
        anchors.centerIn: parent
        text: showOnlyUnread ? qsTr("No unread alerts") : qsTr("No alerts")
        font.pixelSize: 14
        opacity: 0.6
        visible: alertsList.count === 0
    }
    
    function getFilteredAlerts() {
        var filtered = alerts.slice() // Copy array
        
        if (showOnlyUnread) {
            filtered = filtered.filter(function(alert) {
                return !alert.isRead
            })
        }
        
        // Sort by timestamp (newest first)
        filtered.sort(function(a, b) {
            return new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime()
        })
        
        // Limit items
        if (maxItems > 0) {
            filtered = filtered.slice(0, maxItems)
        }
        
        return filtered
    }
    
    function getAlertColor(type) {
        switch (type) {
            case "info": return infoColor
            case "success": return successColor
            case "warning": return warningColor
            case "error": return errorColor
            default: return infoColor
        }
    }
    
    function getAlertIcon(type) {
        switch (type) {
            case "info": return "qrc:/resources/icons/info.svg"
            case "success": return "qrc:/resources/icons/check.svg"
            case "warning": return "qrc:/resources/icons/warning.svg"
            case "error": return "qrc:/resources/icons/error.svg"
            default: return "qrc:/resources/icons/info.svg"
        }
    }
    
    function markAsRead(alertId) {
        if (dataService) {
            dataService.markAlertAsRead(alertId)
        }
    }
    
    function dismissAlert(alertId) {
        // Remove alert from the list
        for (var i = 0; i < alerts.length; i++) {
            if (alerts[i].alertId === alertId) {
                alerts.splice(i, 1)
                break
            }
        }
        model = getFilteredAlerts()
    }
    
    onAlertsChanged: {
        model = getFilteredAlerts()
    }
}