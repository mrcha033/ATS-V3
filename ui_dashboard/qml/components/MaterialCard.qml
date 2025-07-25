import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

/**
 * Material Design card component
 * Provides consistent styling and elevation for content cards
 */
Rectangle {
    id: card
    
    // Public properties
    property alias elevation: effect.elevation
    property alias radius: card.radius
    property bool highlighted: false
    property bool interactive: false
    
    // Styling
    color: Material.cardColor
    radius: 8
    
    // Default elevation
    Material.elevation: highlighted ? 8 : (interactive && hovered ? 4 : 2)
    
    // Drop shadow effect
    layer.enabled: true
    layer.effect: ElevationEffect {
        id: effect
        elevation: card.Material.elevation
    }
    
    // Hover effects for interactive cards
    property bool hovered: false
    
    MouseArea {
        anchors.fill: parent
        enabled: interactive
        hoverEnabled: interactive
        
        onEntered: parent.hovered = true
        onExited: parent.hovered = false
        
        onClicked: if (interactive) card.clicked()
    }
    
    // Animations
    Behavior on Material.elevation {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }
    }
    
    // Signals
    signal clicked()
}

// Drop shadow effect component
component ElevationEffect: Item {
    property int elevation: 2
    
    Rectangle {
        id: shadow
        anchors.fill: parent
        anchors.topMargin: Math.max(1, elevation * 0.5)
        anchors.bottomMargin: -anchors.topMargin
        
        radius: parent.parent.radius
        color: "#000000"
        opacity: Math.min(0.3, elevation * 0.05)
        
        // Blur effect would be added here in a full implementation
        // For now, we'll use a simple offset shadow
    }
}