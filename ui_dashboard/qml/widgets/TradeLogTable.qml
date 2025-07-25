import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15

/**
 * Trading log table widget for displaying trade history
 * Supports filtering, sorting, and real-time updates
 */
Item {
    id: root
    
    // Public properties
    property var trades: []
    property bool showHeader: true
    property int maxRows: -1 // -1 for unlimited
    property string sortColumn: "timestamp"
    property bool sortAscending: false
    property string filterText: ""
    property string filterStrategy: ""
    property string filterSymbol: ""
    
    // Colors
    readonly property color positiveColor: "#4CAF50"
    readonly property color negativeColor: "#F44336"
    readonly property color neutralColor: "#9E9E9E"
    readonly property color headerColor: Material.theme === Material.Dark ? "#424242" : "#F5F5F5"
    
    // Column configuration
    readonly property var columns: [
        { key: "timestamp", title: qsTr("Time"), width: 140, type: "datetime" },
        { key: "symbol", title: qsTr("Symbol"), width: 100, type: "text" },
        { key: "exchange", title: qsTr("Exchange"), width: 80, type: "text" },
        { key: "side", title: qsTr("Side"), width: 60, type: "text" },
        { key: "quantity", title: qsTr("Quantity"), width: 100, type: "number" },
        { key: "price", title: qsTr("Price"), width: 100, type: "currency" },
        { key: "pnl", title: qsTr("P&L"), width: 100, type: "currency" },
        { key: "strategy", title: qsTr("Strategy"), width: 120, type: "text" },
        { key: "status", title: qsTr("Status"), width: 80, type: "status" }
    ]
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Header row
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: showHeader ? 40 : 0
            color: headerColor
            visible: showHeader
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 0
                
                Repeater {
                    model: columns
                    
                    Rectangle {
                        Layout.preferredWidth: modelData.width
                        Layout.fillHeight: true
                        color: "transparent"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 4
                            
                            Label {
                                text: modelData.title
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: Material.foreground
                                Layout.fillWidth: true
                            }
                            
                            // Sort indicator
                            Image {
                                source: sortAscending ? "qrc:/resources/icons/arrow-up.svg" : "qrc:/resources/icons/arrow-down.svg"
                                width: 12
                                height: 12
                                visible: sortColumn === modelData.key
                                opacity: 0.7
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (sortColumn === modelData.key) {
                                    sortAscending = !sortAscending
                                } else {
                                    sortColumn = modelData.key
                                    sortAscending = false
                                }
                                sortTrades()
                            }
                        }
                        
                        // Column separator
                        Rectangle {
                            width: 1
                            height: parent.height * 0.6
                            color: Material.dividerColor
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: index < columns.length - 1
                        }
                    }
                }
            }
            
            // Header border
            Rectangle {
                width: parent.width
                height: 1
                color: Material.dividerColor
                anchors.bottom: parent.bottom
            }
        }
        
        // Data rows
        ListView {
            id: tradesListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            model: getFilteredTrades()
            
            delegate: ItemDelegate {
                width: tradesListView.width
                height: 48
                
                background: Rectangle {
                    color: index % 2 === 0 ? "transparent" : Material.dialogColor
                    opacity: 0.5
                    
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Material.dividerColor
                        anchors.bottom: parent.bottom
                        opacity: 0.3
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 0
                    
                    Repeater {
                        model: columns
                        
                        Rectangle {
                            Layout.preferredWidth: modelData.width
                            Layout.fillHeight: true
                            color: "transparent"
                            
                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                
                                text: formatCellValue(modelData.key, modelData.type, getCellValue(modelData.key))
                                font.pixelSize: 13
                                color: getCellColor(modelData.key, modelData.type)
                                elide: Text.ElideRight
                                
                                // Alignment based on type
                                horizontalAlignment: {
                                    switch (modelData.type) {
                                        case "number":
                                        case "currency":
                                            return Text.AlignRight
                                        case "datetime":
                                            return Text.AlignLeft
                                        default:
                                            return Text.AlignLeft
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Row hover effect
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: showTradeDetails(modelData)
                    
                    onEntered: parent.background.opacity = 0.8
                    onExited: parent.background.opacity = 0.5
                }
            }
            
            // Empty state
            Label {
                anchors.centerIn: parent
                text: qsTr("No trades to display")
                font.pixelSize: 14
                opacity: 0.6
                visible: tradesListView.count === 0
            }
            
            // Loading indicator
            BusyIndicator {
                anchors.centerIn: parent
                visible: false
                Material.accent: Material.primary
            }
        }
        
        // Footer with pagination/summary
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: headerColor
            visible: trades.length > 0
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                
                Label {
                    text: qsTr("Total: ") + getFilteredTrades().length + qsTr(" trades")
                    font.pixelSize: 12
                    opacity: 0.7
                }
                
                Item { Layout.fillWidth: true }
                
                Label {
                    text: qsTr("Total P&L: ") + formatCurrency(getTotalPnL())
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: getTotalPnL() >= 0 ? positiveColor : negativeColor
                }
            }
        }
    }
    
    // Trade details popup
    Popup {
        id: tradeDetailsPopup
        width: 400
        height: 300
        anchors.centerIn: parent
        modal: true
        
        property var selectedTrade: null
        
        MaterialCard {
            anchors.fill: parent
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                
                Label {
                    text: qsTr("Trade Details")
                    font.pixelSize: 18
                    font.weight: Font.Medium
                }
                
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    Column {
                        width: parent.width
                        spacing: 8
                        
                        Repeater {
                            model: columns
                            
                            RowLayout {
                                width: parent.width
                                
                                Label {
                                    text: modelData.title + ":"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    Layout.preferredWidth: 100
                                }
                                
                                Label {
                                    text: tradeDetailsPopup.selectedTrade ? 
                                         formatCellValue(modelData.key, modelData.type, 
                                                       tradeDetailsPopup.selectedTrade[modelData.key]) : ""
                                    font.pixelSize: 13
                                    color: getCellColor(modelData.key, modelData.type, tradeDetailsPopup.selectedTrade)
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                
                RowLayout {
                    Button {
                        text: qsTr("Close")
                        onClicked: tradeDetailsPopup.close()
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Button {
                        text: qsTr("Export")
                        flat: true
                        onClicked: {
                            // Export single trade
                            exportTrade(tradeDetailsPopup.selectedTrade)
                        }
                    }
                }
            }
        }
    }
    
    // Public methods
    function refresh() {
        tradesListView.model = getFilteredTrades()
    }
    
    function exportTrades() {
        // Export visible trades to CSV
        var filteredTrades = getFilteredTrades()
        // Implementation would call C++ export function
        console.log("Exporting", filteredTrades.length, "trades")
    }
    
    function exportTrade(trade) {
        // Export single trade details
        console.log("Exporting trade:", trade.tradeId)
    }
    
    function addTrade(trade) {
        // Add new trade and refresh display
        trades.push(trade)
        refresh()
        
        // Animate new row if visible
        animateNewRow()
    }
    
    // Private methods
    function getFilteredTrades() {
        var filtered = trades.slice() // Copy array
        
        // Apply text filter
        if (filterText) {
            filtered = filtered.filter(function(trade) {
                return trade.symbol.toLowerCase().includes(filterText.toLowerCase()) ||
                       trade.exchange.toLowerCase().includes(filterText.toLowerCase()) ||
                       trade.strategy.toLowerCase().includes(filterText.toLowerCase())
            })
        }
        
        // Apply strategy filter
        if (filterStrategy) {
            filtered = filtered.filter(function(trade) {
                return trade.strategy === filterStrategy
            })
        }
        
        // Apply symbol filter
        if (filterSymbol) {
            filtered = filtered.filter(function(trade) {
                return trade.symbol === filterSymbol
            })
        }
        
        // Apply sorting
        filtered.sort(function(a, b) {
            var aVal = a[sortColumn]
            var bVal = b[sortColumn]
            
            // Handle different data types
            if (sortColumn === "timestamp") {
                aVal = new Date(aVal).getTime()
                bVal = new Date(bVal).getTime()
            } else if (typeof aVal === "string") {
                aVal = aVal.toLowerCase()
                bVal = bVal.toLowerCase()
            }
            
            var result = aVal < bVal ? -1 : (aVal > bVal ? 1 : 0)
            return sortAscending ? result : -result
        })
        
        // Apply row limit
        if (maxRows > 0) {
            filtered = filtered.slice(0, maxRows)
        }
        
        return filtered
    }
    
    function getCellValue(key) {
        return modelData[key] || ""
    }
    
    function formatCellValue(key, type, value) {
        switch (type) {
            case "datetime":
                return Qt.formatDateTime(new Date(value), "MM/dd hh:mm:ss")
            case "currency":
                return formatCurrency(value)
            case "number":
                return Number(value).toLocaleString(Qt.locale(), 'f', 6)
            case "status":
                return formatStatus(value)
            default:
                return value ? value.toString() : ""
        }
    }
    
    function formatCurrency(value) {
        if (value === undefined || value === null) return "$0.00"
        var prefix = value >= 0 ? "" : "-"
        return prefix + "$" + Number(Math.abs(value)).toLocaleString(Qt.locale(), 'f', 2)
    }
    
    function formatStatus(status) {
        switch (status) {
            case "filled": return qsTr("Filled")
            case "pending": return qsTr("Pending")
            case "cancelled": return qsTr("Cancelled")
            case "rejected": return qsTr("Rejected")
            default: return status
        }
    }
    
    function getCellColor(key, type, trade) {
        switch (key) {
            case "pnl":
                var pnl = trade ? trade[key] : getCellValue(key)
                return pnl >= 0 ? positiveColor : negativeColor
            case "side":
                var side = trade ? trade[key] : getCellValue(key)
                return side === "buy" ? positiveColor : negativeColor
            case "status":
                var status = trade ? trade[key] : getCellValue(key)
                switch (status) {
                    case "filled": return positiveColor
                    case "pending": return neutralColor
                    case "cancelled": 
                    case "rejected": return negativeColor
                    default: return Material.foreground
                }
            default:
                return Material.foreground
        }
    }
    
    function getTotalPnL() {
        var total = 0
        var filtered = getFilteredTrades()
        for (var i = 0; i < filtered.length; i++) {
            total += filtered[i].pnl || 0
        }
        return total
    }
    
    function sortTrades() {
        refresh()
    }
    
    function showTradeDetails(trade) {
        tradeDetailsPopup.selectedTrade = trade
        tradeDetailsPopup.open()
    }
    
    function animateNewRow() {
        // Could add a highlight animation for new trades
        // For now, just scroll to top
        if (tradesListView.count > 0) {
            tradesListView.positionViewAtBeginning()
        }
    }
    
    // Watch for trades changes
    onTradesChanged: {
        refresh()
    }
}