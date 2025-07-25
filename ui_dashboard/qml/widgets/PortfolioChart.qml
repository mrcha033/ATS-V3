import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtCharts 2.15
import ATS.Dashboard 1.0

/**
 * Portfolio chart widget for displaying equity curves and P&L data
 * Supports multiple chart types: equity, pnl, volume
 */
Item {
    id: root
    
    // Public properties
    property string chartType: "equity" // "equity", "pnl", "volume"
    property string timeframe: "1D"     // "1D", "1W", "1M", "3M", "1Y"
    property bool showLegend: true
    property bool showGrid: true
    property bool animated: true
    
    // Colors
    readonly property color positiveColor: "#4CAF50"
    readonly property color negativeColor: "#F44336"
    readonly property color neutralColor: "#2196F3"
    readonly property color gridColor: Material.theme === Material.Dark ? "#424242" : "#E0E0E0"
    readonly property color textColor: Material.foreground
    
    // Chart view
    ChartView {
        id: chartView
        anchors.fill: parent
        
        antialiasing: true
        legend.visible: showLegend
        legend.alignment: Qt.AlignBottom
        margins.top: 10
        margins.bottom: 10
        margins.left: 10
        margins.right: 10
        
        backgroundColor: "transparent"
        plotAreaColor: "transparent"
        
        // Theme configuration
        theme: Material.theme === Material.Dark ? ChartView.ChartThemeDark : ChartView.ChartThemeLight
        
        // Date/time axis
        DateTimeAxis {
            id: axisX
            format: getTimeFormat()
            titleText: qsTr("Time")
            titleVisible: false
            gridVisible: showGrid
            gridLineColor: gridColor
            labelsColor: textColor
            lineVisible: true
            
            // Auto-range based on timeframe
            min: getMinDateTime()
            max: getMaxDateTime()
        }
        
        // Value axis
        ValueAxis {
            id: axisY
            titleText: getYAxisTitle()
            titleVisible: false
            gridVisible: showGrid
            gridLineColor: gridColor
            labelsColor: textColor
            lineVisible: true
            
            // Auto-scale
            min: 0
            max: 100
        }
        
        // Main data series
        LineSeries {
            id: mainSeries
            name: getSeriesName()
            axisX: axisX
            axisY: axisY
            color: getSeriesColor()
            width: 2
            
            // Animation
            useOpenGL: true
        }
        
        // Secondary series for comparison (if needed)
        LineSeries {
            id: secondarySeries
            name: qsTr("Benchmark")
            axisX: axisX
            axisY: axisY
            color: neutralColor
            width: 1
            style: Qt.DashLine
            visible: chartType === "equity"
        }
        
        // Area series for P&L visualization
        AreaSeries {
            id: areaSeries
            name: getSeriesName()
            axisX: axisX
            axisY: axisY
            visible: chartType === "pnl"
            
            upperSeries: LineSeries {
                id: upperLine
            }
            
            lowerSeries: LineSeries {
                id: lowerLine
            }
            
            color: Qt.rgba(positiveColor.r, positiveColor.g, positiveColor.b, 0.3)
            borderColor: positiveColor
            borderWidth: 2
        }
    }
    
    // Loading indicator
    BusyIndicator {
        id: loadingIndicator
        anchors.centerIn: parent
        visible: false
        Material.accent: neutralColor
    }
    
    // Empty state
    Column {
        id: emptyState
        anchors.centerIn: parent
        spacing: 16
        visible: false
        
        Image {
            source: "qrc:/resources/icons/chart.svg"
            width: 64
            height: 64
            opacity: 0.3
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        Label {
            text: qsTr("No data available")
            font.pixelSize: 16
            opacity: 0.6
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        Button {
            text: qsTr("Refresh")
            flat: true
            onClicked: refresh()
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
    
    // Error state
    Column {
        id: errorState
        anchors.centerIn: parent
        spacing: 16
        visible: false
        
        Image {
            source: "qrc:/resources/icons/error.svg"
            width: 64
            height: 64
            opacity: 0.3
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        Label {
            text: qsTr("Failed to load chart data")
            font.pixelSize: 16
            opacity: 0.6
            anchors.horizontalCenter: parent.horizontalCenter
        }
        
        Button {
            text: qsTr("Retry")
            flat: true
            onClicked: refresh()
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
    
    // Public methods
    function refresh() {
        updateData(timeframe)
    }
    
    function updateData(newTimeframe) {
        if (newTimeframe) {
            timeframe = newTimeframe
        }
        
        loadingIndicator.visible = true
        emptyState.visible = false
        errorState.visible = false
        
        // Get data from data service
        if (dataService) {
            loadChartData()
        } else {
            // Generate mock data for development
            generateMockData()
        }
    }
    
    // Private methods
    function loadChartData() {
        try {
            var data = []
            var fromDate = getFromDate()
            var toDate = new Date()
            
            switch (chartType) {
                case "equity":
                    data = dataService.getEquityCurveData(fromDate, toDate)
                    break
                case "pnl":
                    data = dataService.getPnLChartData(timeframe)
                    break
                case "volume":
                    data = dataService.getVolumeChartData(timeframe)
                    break
            }
            
            if (data && data.length > 0) {
                populateChart(data)
                loadingIndicator.visible = false
            } else {
                showEmptyState()
            }
            
        } catch (error) {
            console.error("Error loading chart data:", error)
            showErrorState()
        }
    }
    
    function populateChart(data) {
        // Clear existing data
        mainSeries.clear()
        if (areaSeries.visible) {
            upperLine.clear()
            lowerLine.clear()
        }
        
        // Calculate axis ranges
        var minValue = Number.MAX_VALUE
        var maxValue = Number.MIN_VALUE
        var minTime = new Date(data[0].timestamp)
        var maxTime = new Date(data[data.length - 1].timestamp)
        
        // Populate main series
        for (var i = 0; i < data.length; i++) {
            var point = data[i]
            var timestamp = new Date(point.timestamp)
            var value = point.value
            
            mainSeries.append(timestamp.getTime(), value)
            
            if (areaSeries.visible) {
                // For P&L charts, show positive/negative areas
                upperLine.append(timestamp.getTime(), Math.max(0, value))
                lowerLine.append(timestamp.getTime(), Math.min(0, value))
            }
            
            minValue = Math.min(minValue, value)
            maxValue = Math.max(maxValue, value)
        }
        
        // Update axis ranges
        axisX.min = minTime
        axisX.max = maxTime
        
        // Add some padding to Y axis
        var padding = (maxValue - minValue) * 0.1
        axisY.min = minValue - padding
        axisY.max = maxValue + padding
        
        // Update area series color based on overall P&L
        if (areaSeries.visible) {
            var finalValue = data[data.length - 1].value
            areaSeries.color = Qt.rgba(
                finalValue >= 0 ? positiveColor.r : negativeColor.r,
                finalValue >= 0 ? positiveColor.g : negativeColor.g,
                finalValue >= 0 ? positiveColor.b : negativeColor.b,
                0.3
            )
            areaSeries.borderColor = finalValue >= 0 ? positiveColor : negativeColor
        }
    }
    
    function generateMockData() {
        // Generate sample data for development
        var data = []
        var now = new Date()
        var startTime = getFromDate()
        var interval = getDataInterval()
        
        var currentTime = startTime
        var baseValue = chartType === "equity" ? 100000 : 0
        var currentValue = baseValue
        
        while (currentTime <= now) {
            // Add some random variation
            var change = (Math.random() - 0.5) * (chartType === "equity" ? 1000 : 100)
            currentValue += change
            
            data.push({
                timestamp: new Date(currentTime),
                value: currentValue
            })
            
            currentTime = new Date(currentTime.getTime() + interval)
        }
        
        populateChart(data)
        loadingIndicator.visible = false
    }
    
    function showEmptyState() {
        loadingIndicator.visible = false
        emptyState.visible = true
        errorState.visible = false
    }
    
    function showErrorState() {
        loadingIndicator.visible = false
        emptyState.visible = false
        errorState.visible = true
    }
    
    // Helper functions
    function getTimeFormat() {
        switch (timeframe) {
            case "1D": return "hh:mm"
            case "1W": return "MMM dd"
            case "1M": return "MMM dd"
            case "3M": return "MMM dd"
            case "1Y": return "MMM yyyy"
            default: return "MMM dd hh:mm"
        }
    }
    
    function getFromDate() {
        var now = new Date()
        switch (timeframe) {
            case "1D": return new Date(now.getTime() - 24 * 60 * 60 * 1000)
            case "1W": return new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)
            case "1M": return new Date(now.getTime() - 30 * 24 * 60 * 60 * 1000)
            case "3M": return new Date(now.getTime() - 90 * 24 * 60 * 60 * 1000)
            case "1Y": return new Date(now.getTime() - 365 * 24 * 60 * 60 * 1000)
            default: return new Date(now.getTime() - 24 * 60 * 60 * 1000)
        }
    }
    
    function getMinDateTime() {
        return getFromDate()
    }
    
    function getMaxDateTime() {
        return new Date()
    }
    
    function getDataInterval() {
        switch (timeframe) {
            case "1D": return 5 * 60 * 1000    // 5 minutes
            case "1W": return 60 * 60 * 1000   // 1 hour
            case "1M": return 4 * 60 * 60 * 1000   // 4 hours
            case "3M": return 24 * 60 * 60 * 1000  // 1 day
            case "1Y": return 7 * 24 * 60 * 60 * 1000  // 1 week
            default: return 60 * 60 * 1000
        }
    }
    
    function getYAxisTitle() {
        switch (chartType) {
            case "equity": return qsTr("Portfolio Value ($)")
            case "pnl": return qsTr("P&L ($)")
            case "volume": return qsTr("Volume")
            default: return qsTr("Value")
        }
    }
    
    function getSeriesName() {
        switch (chartType) {
            case "equity": return qsTr("Portfolio Value")
            case "pnl": return qsTr("Profit & Loss")
            case "volume": return qsTr("Trading Volume")
            default: return qsTr("Data")
        }
    }
    
    function getSeriesColor() {
        switch (chartType) {
            case "equity": return neutralColor
            case "pnl": return positiveColor
            case "volume": return neutralColor
            default: return neutralColor
        }
    }
    
    // Initialize chart on component completion
    Component.onCompleted: {
        updateData()
    }
    
    // Update when timeframe changes
    onTimeframeChanged: {
        updateData()
    }
    
    // Update when chart type changes
    onChartTypeChanged: {
        // Show/hide appropriate series
        mainSeries.visible = chartType !== "pnl"
        areaSeries.visible = chartType === "pnl"
        updateData()
    }
}