import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import ATS.Dashboard 1.0

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1400
    height: 900
    minimumWidth: 1200
    minimumHeight: 700
    title: qsTr("ATS Trading Dashboard") + " - " + appVersion
    
    // Material Design theming
    Material.theme: ThemeManager.isDarkTheme ? Material.Dark : Material.Light
    Material.primary: Material.Blue
    Material.accent: Material.LightBlue
    
    property int currentPageIndex: 0
    property bool isConnected: dataService ? dataService.isConnected : false
    property real totalValue: dataService ? dataService.totalValue : 0.0
    property real totalPnL: dataService ? dataService.totalPnL : 0.0
    property real totalPnLPercentage: dataService ? dataService.totalPnLPercentage : 0.0
    
    // Color scheme
    readonly property color positiveColor: Material.theme === Material.Dark ? "#4CAF50" : "#2E7D32"
    readonly property color negativeColor: Material.theme === Material.Dark ? "#F44336" : "#C62828"
    readonly property color neutralColor: Material.theme === Material.Dark ? "#9E9E9E" : "#616161"
    
    // Connection status
    Connections {
        target: dataService
        function onConnectionStatusChanged() {
            connectionStatusChanged()
        }
        function onTotalValueChanged() {
            totalValueChanged()
        }
        function onNewAlertReceived(alertData) {
            showNotification(alertData.title, alertData.message, alertData.type)
        }
    }
    
    // Header
    header: ToolBar {
        Material.foreground: "white"
        height: 64
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            
            // App logo and title
            RowLayout {
                spacing: 12
                
                Image {
                    source: "qrc:/resources/images/logo.png"
                    width: 32
                    height: 32
                    fillMode: Image.PreserveAspectFit
                }
                
                Label {
                    text: qsTr("ATS Dashboard")
                    font.pixelSize: 20
                    font.weight: Font.Medium
                }
            }
            
            Item { Layout.fillWidth: true }
            
            // Connection status indicator
            RowLayout {
                spacing: 8
                
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: isConnected ? positiveColor : negativeColor
                    
                    SequentialAnimation on opacity {
                        running: !isConnected
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 1000 }
                        NumberAnimation { to: 1.0; duration: 1000 }
                    }
                }
                
                Label {
                    text: isConnected ? qsTr("Connected") : qsTr("Disconnected")
                    font.pixelSize: 14
                }
            }
            
            // Portfolio summary
            RowLayout {
                spacing: 16
                
                Column {
                    Label {
                        text: qsTr("Total Value")
                        font.pixelSize: 12
                        opacity: 0.7
                    }
                    Label {
                        text: "$" + Number(totalValue).toLocaleString(Qt.locale(), 'f', 2)
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }
                }
                
                Column {
                    Label {
                        text: qsTr("Today P&L")
                        font.pixelSize: 12
                        opacity: 0.7
                    }
                    Label {
                        text: (totalPnL >= 0 ? "+" : "") + "$" + Number(totalPnL).toLocaleString(Qt.locale(), 'f', 2) + 
                              " (" + (totalPnLPercentage >= 0 ? "+" : "") + Number(totalPnLPercentage).toLocaleString(Qt.locale(), 'f', 2) + "%)"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: totalPnL >= 0 ? positiveColor : negativeColor
                    }
                }
            }
            
            // Settings button
            ToolButton {
                icon.source: "qrc:/resources/icons/settings.svg"
                icon.width: 24
                icon.height: 24
                onClicked: settingsDialog.open()
                
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings")
            }
        }
    }
    
    // Main content area
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Navigation sidebar
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Material.dialogColor
            
            Column {
                anchors.fill: parent
                anchors.topMargin: 8
                
                NavigationButton {
                    text: qsTr("Dashboard")
                    icon: "qrc:/resources/icons/dashboard.svg"
                    selected: currentPageIndex === 0
                    onClicked: currentPageIndex = 0
                }
                
                NavigationButton {
                    text: qsTr("Trading")
                    icon: "qrc:/resources/icons/trading.svg"
                    selected: currentPageIndex === 1
                    onClicked: currentPageIndex = 1
                }
                
                NavigationButton {
                    text: qsTr("Backtest")
                    icon: "qrc:/resources/icons/backtest.svg"
                    selected: currentPageIndex === 2
                    onClicked: currentPageIndex = 2
                }
                
                NavigationButton {
                    text: qsTr("Analytics")
                    icon: "qrc:/resources/icons/analytics.svg"
                    selected: currentPageIndex === 3
                    onClicked: currentPageIndex = 3
                }
                
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Material.dividerColor
                }
                
                Item { height: 16 }
                
                // Quick actions
                Label {
                    text: qsTr("Quick Actions")
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    opacity: 0.7
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                }
                
                Item { height: 8 }
                
                NavigationButton {
                    text: qsTr("Emergency Stop")
                    icon: "qrc:/resources/icons/stop.svg"
                    iconColor: negativeColor
                    onClicked: TradingController.emergencyStopAll()
                }
                
                NavigationButton {
                    text: qsTr("Export Report")
                    icon: "qrc:/resources/icons/export.svg"
                    onClicked: DashboardController.exportReport()
                }
            }
        }
        
        // Vertical divider
        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Material.dividerColor
        }
        
        // Page content
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentPageIndex
            
            // Dashboard page
            DashboardPage {
                id: dashboardPage
            }
            
            // Trading page
            TradingPage {
                id: tradingPage
            }
            
            // Backtest page
            BacktestPage {
                id: backtestPage
            }
            
            // Analytics page
            Item {
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Analytics page coming soon...")
                    font.pixelSize: 18
                    opacity: 0.6
                }
            }
        }
    }
    
    // Status bar
    footer: ToolBar {
        height: 32
        Material.elevation: 0
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            
            Label {
                text: qsTr("Last update: ") + (dataService ? Qt.formatDateTime(dataService.lastUpdate, "hh:mm:ss") : "Never")
                font.pixelSize: 12
                opacity: 0.7
            }
            
            Item { Layout.fillWidth: true }
            
            Label {
                text: qsTr("Version ") + appVersion
                font.pixelSize: 12
                opacity: 0.7
            }
        }
    }
    
    // Settings dialog
    SettingsDialog {
        id: settingsDialog
    }
    
    // About dialog
    AboutDialog {
        id: aboutDialog
    }
    
    // Notification system
    NotificationArea {
        id: notificationArea
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 80
        anchors.rightMargin: 16
    }
    
    // Global functions
    function showNotification(title, message, type) {
        notificationArea.showNotification(title, message, type)
    }
    
    function connectionStatusChanged() {
        // Handle connection status changes
        if (isConnected) {
            showNotification(qsTr("Connected"), qsTr("Successfully connected to trading system"), "success")
        } else {
            showNotification(qsTr("Disconnected"), qsTr("Lost connection to trading system"), "warning")
        }
    }
    
    function totalValueChanged() {
        // Handle portfolio value changes
        // Could trigger animations or alerts here
    }
    
    // Global keyboard shortcuts
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: currentPageIndex = 0
    }
    
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: currentPageIndex = 1
    }
    
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: currentPageIndex = 2
    }
    
    Shortcut {
        sequence: "F5"
        onActivated: dataService.refreshAllData()
    }
    
    Shortcut {
        sequence: "Escape"
        onActivated: TradingController.emergencyStopAll()
    }
}

// Custom navigation button component
component NavigationButton: ItemDelegate {
    property string text
    property string icon
    property bool selected: false
    property color iconColor: Material.foreground
    
    width: parent.width
    height: 48
    
    background: Rectangle {
        color: selected ? Material.accent : (parent.hovered ? Material.buttonHoveredColor : "transparent")
        opacity: selected ? 0.12 : (parent.hovered ? 0.08 : 0)
        
        Rectangle {
            width: 4
            height: parent.height
            color: Material.accent
            visible: selected
        }
    }
    
    contentItem: RowLayout {
        spacing: 16
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        
        Image {
            source: icon
            width: 24
            height: 24
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            
            ColorOverlay {
                anchors.fill: parent
                source: parent
                color: selected ? Material.accent : iconColor
            }
        }
        
        Label {
            text: parent.parent.text
            font.pixelSize: 14
            font.weight: selected ? Font.Medium : Font.Normal
            color: selected ? Material.accent : Material.foreground
        }
    }
}

// Notification area component
component NotificationArea: Column {
    spacing: 8
    width: 320
    
    function showNotification(title, message, type) {
        var component = Qt.createComponent("components/NotificationCard.qml")
        if (component.status === Component.Ready) {
            var notification = component.createObject(this, {
                "title": title,
                "message": message,
                "type": type
            })
        }
    }
}