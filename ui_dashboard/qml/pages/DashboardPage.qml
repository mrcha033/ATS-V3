import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import ATS.Dashboard 1.0

ScrollView {
    id: dashboardRoot
    clip: true
    
    property real totalValue: dataService ? dataService.totalValue : 0.0
    property real totalPnL: dataService ? dataService.totalPnL : 0.0
    property real totalPnLPercentage: dataService ? dataService.totalPnLPercentage : 0.0
    property real dayPnL: dataService ? dataService.dayPnL : 0.0
    property real dayPnLPercentage: dataService ? dataService.dayPnLPercentage : 0.0
    
    readonly property color positiveColor: "#4CAF50"
    readonly property color negativeColor: "#F44336"
    readonly property color neutralColor: "#9E9E9E"
    
    Flickable {
        contentHeight: mainLayout.implicitHeight + 32
        
        ColumnLayout {
            id: mainLayout
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16
            
            // Portfolio summary cards
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 16
                rowSpacing: 16
                
                // Total Portfolio Value
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        
                        RowLayout {
                            Label {
                                text: qsTr("Total Portfolio Value")
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                opacity: 0.7
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Button {
                                flat: true
                                icon.source: "qrc:/resources/icons/refresh.svg"
                                icon.width: 16
                                icon.height: 16
                                onClicked: dataService.refreshAllData()
                            }
                        }
                        
                        Label {
                            text: "$" + Number(totalValue).toLocaleString(Qt.locale(), 'f', 2)
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        
                        RowLayout {
                            spacing: 8
                            
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: totalPnL >= 0 ? positiveColor : negativeColor
                            }
                            
                            Label {
                                text: (totalPnL >= 0 ? "+" : "") + "$" + Number(Math.abs(totalPnL)).toLocaleString(Qt.locale(), 'f', 2)
                                font.pixelSize: 14
                                color: totalPnL >= 0 ? positiveColor : negativeColor
                            }
                            
                            Label {
                                text: "(" + (totalPnLPercentage >= 0 ? "+" : "") + Number(totalPnLPercentage).toLocaleString(Qt.locale(), 'f', 2) + "%)"
                                font.pixelSize: 14
                                color: totalPnL >= 0 ? positiveColor : negativeColor
                            }
                        }
                    }
                }
                
                // Today's P&L
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        
                        Label {
                            text: qsTr("Today's P&L")
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            opacity: 0.7
                        }
                        
                        Label {
                            text: (dayPnL >= 0 ? "+" : "") + "$" + Number(Math.abs(dayPnL)).toLocaleString(Qt.locale(), 'f', 2)
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            color: dayPnL >= 0 ? positiveColor : negativeColor
                            Layout.fillWidth: true
                        }
                        
                        Label {
                            text: (dayPnLPercentage >= 0 ? "+" : "") + Number(dayPnLPercentage).toLocaleString(Qt.locale(), 'f', 2) + "%"
                            font.pixelSize: 14
                            color: dayPnL >= 0 ? positiveColor : negativeColor
                        }
                    }
                }
                
                // Active Strategies
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        
                        Label {
                            text: qsTr("Active Strategies")
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            opacity: 0.7
                        }
                        
                        Label {
                            text: "3" // TODO: Get from data service
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        
                        RowLayout {
                            spacing: 8
                            
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: positiveColor
                            }
                            
                            Label {
                                text: qsTr("All Running")
                                font.pixelSize: 14
                                color: positiveColor
                            }
                        }
                    }
                }
                
                // Alerts
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        
                        RowLayout {
                            Label {
                                text: qsTr("Alerts")
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                opacity: 0.7
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Button {
                                flat: true
                                text: qsTr("View All")
                                font.pixelSize: 12
                                onClicked: alertPanel.visible = !alertPanel.visible
                            }
                        }
                        
                        Label {
                            text: dataService ? dataService.getUnreadAlertsCount() : 0
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            color: dataService && dataService.getUnreadAlertsCount() > 0 ? negativeColor : neutralColor
                            Layout.fillWidth: true
                        }
                        
                        Label {
                            text: qsTr("Unread")
                            font.pixelSize: 14
                            opacity: 0.7
                        }
                    }
                }
            }
            
            // Charts section
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 400
                spacing: 16
                
                // Portfolio equity curve
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        
                        RowLayout {
                            Label {
                                text: qsTr("Portfolio Equity Curve")
                                font.pixelSize: 16
                                font.weight: Font.Medium
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            ComboBox {
                                id: timeframeCombo
                                model: [qsTr("1D"), qsTr("1W"), qsTr("1M"), qsTr("3M"), qsTr("1Y")]
                                currentIndex: 0
                                implicitWidth: 80
                                onCurrentTextChanged: equityChart.updateData(currentText)
                            }
                        }
                        
                        PortfolioChart {
                            id: equityChart
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            chartType: "equity"
                            timeframe: timeframeCombo.currentText
                        }
                    }
                }
                
                // P&L breakdown
                MaterialCard {
                    Layout.preferredWidth: 350
                    Layout.fillHeight: true
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        
                        Label {
                            text: qsTr("P&L Breakdown")
                            font.pixelSize: 16
                            font.weight: Font.Medium
                        }
                        
                        PortfolioChart {
                            id: pnlChart
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            chartType: "pnl"
                            timeframe: "1D"
                        }
                    }
                }
            }
            
            // Trading activity and positions
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                spacing: 16
                
                // Recent trades
                MaterialCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        
                        RowLayout {
                            Label {
                                text: qsTr("Recent Trades")
                                font.pixelSize: 16
                                font.weight: Font.Medium
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            Button {
                                flat: true
                                text: qsTr("View All")
                                font.pixelSize: 12
                                onClicked: mainWindow.currentPageIndex = 1 // Switch to trading page
                            }
                        }
                        
                        TradeLogTable {
                            id: recentTradesTable
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            showHeader: true
                            maxRows: 8
                            trades: dataService ? dataService.getRecentTrades(8) : []
                        }
                    }
                }
                
                // Current positions
                MaterialCard {
                    Layout.preferredWidth: 400
                    Layout.fillHeight: true
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        
                        Label {
                            text: qsTr("Current Positions")
                            font.pixelSize: 16
                            font.weight: Font.Medium
                        }
                        
                        ListView {
                            id: positionsListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            
                            model: dataService ? dataService.getPortfolioPositions() : []
                            
                            delegate: ItemDelegate {
                                width: positionsListView.width
                                height: 60
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 12
                                    
                                    Column {
                                        spacing: 2
                                        
                                        Label {
                                            text: modelData.symbol + "/" + modelData.exchange
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                        }
                                        
                                        Label {
                                            text: Number(modelData.quantity).toLocaleString(Qt.locale(), 'f', 6)
                                            font.pixelSize: 12
                                            opacity: 0.7
                                        }
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Column {
                                        spacing: 2
                                        
                                        Label {
                                            text: "$" + Number(modelData.marketValue).toLocaleString(Qt.locale(), 'f', 2)
                                            font.pixelSize: 14
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        
                                        Label {
                                            text: (modelData.unrealizedPnL >= 0 ? "+" : "") + 
                                                 "$" + Number(Math.abs(modelData.unrealizedPnL)).toLocaleString(Qt.locale(), 'f', 2) +
                                                 " (" + (modelData.unrealizedPnLPercentage >= 0 ? "+" : "") + 
                                                 Number(modelData.unrealizedPnLPercentage).toLocaleString(Qt.locale(), 'f', 2) + "%)"
                                            font.pixelSize: 12
                                            color: modelData.unrealizedPnL >= 0 ? positiveColor : negativeColor
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                }
                                
                                Rectangle {
                                    width: parent.width
                                    height: 1
                                    color: Material.dividerColor
                                    anchors.bottom: parent.bottom
                                }
                            }
                            
                            // Empty state
                            Label {
                                anchors.centerIn: parent
                                text: qsTr("No active positions")
                                font.pixelSize: 14
                                opacity: 0.6
                                visible: positionsListView.count === 0
                            }
                        }
                    }
                }
            }
            
            // Alert panel (collapsible)
            MaterialCard {
                id: alertPanel
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 200 : 0
                visible: false
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
                }
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    
                    RowLayout {
                        Label {
                            text: qsTr("Recent Alerts")
                            font.pixelSize: 16
                            font.weight: Font.Medium
                        }
                        
                        Item { Layout.fillWidth: true }
                        
                        Button {
                            flat: true
                            text: qsTr("Mark All Read")
                            font.pixelSize: 12
                            enabled: dataService && dataService.getUnreadAlertsCount() > 0
                            onClicked: dataService.markAllAlertsAsRead()
                        }
                        
                        Button {
                            flat: true
                            icon.source: "qrc:/resources/icons/close.svg"
                            icon.width: 16
                            icon.height: 16
                            onClicked: alertPanel.visible = false
                        }
                    }
                    
                    AlertPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        alerts: dataService ? dataService.getAlerts() : []
                        maxItems: 6
                    }
                }
            }
        }
    }
    
    // Auto-refresh timer
    Timer {
        interval: dataService ? dataService.updateInterval : 1000
        running: true
        repeat: true
        onTriggered: {
            if (dataService) {
                dataService.updateData()
            }
        }
    }
    
    // Data connections
    Connections {
        target: dataService
        function onDataUpdated() {
            // Refresh charts and data displays
            equityChart.refresh()
            pnlChart.refresh()
        }
        
        function onNewTradeReceived(tradeData) {
            // Could show a brief animation or notification
            showTradeNotification(tradeData)
        }
    }
    
    function showTradeNotification(tradeData) {
        var message = qsTr("Executed trade: ") + tradeData.side + " " + tradeData.quantity + " " + tradeData.symbol
        mainWindow.showNotification(qsTr("Trade Executed"), message, "info")
    }
}