#include "notification_settings_service.hpp"
#include "exchange/exchange_notification_system.hpp"
#include <iostream>
#include <memory>

using namespace ats::notification;
using namespace ats::exchange;

int main() {
    std::cout << "Simple Notification Service Test" << std::endl;
    std::cout << "===============================" << std::endl;
    
    try {
        // Test basic notification settings without HTTP dependencies
        std::cout << "\n1. Testing notification settings creation..." << std::endl;
        
        // Create user profile
        UserNotificationProfile test_user("test_user_001");
        test_user.email = "test@example.com";
        test_user.global_enabled = true;
        
        std::cout << "✓ Created user profile: " << test_user.user_id << std::endl;
        std::cout << "  Email: " << test_user.email << std::endl;
        std::cout << "  Global enabled: " << (test_user.global_enabled ? "Yes" : "No") << std::endl;
        
        // Test notification rules
        std::cout << "\n2. Testing notification rules..." << std::endl;
        
        NotificationRule risk_rule;
        risk_rule.rule_id = "test_risk_rule";
        risk_rule.user_id = test_user.user_id;
        risk_rule.category = "risk";
        risk_rule.min_level = NotificationLevel::WARNING;
        risk_rule.enabled_channels = {notification::NotificationChannel::EMAIL, notification::NotificationChannel::PUSH};
        risk_rule.frequency = NotificationFrequency::IMMEDIATE;
        risk_rule.enabled = true;
        
        std::cout << "✓ Created risk notification rule" << std::endl;
        std::cout << "  Rule ID: " << risk_rule.rule_id << std::endl;
        std::cout << "  Category: " << risk_rule.category << std::endl;
        std::cout << "  Min Level: " << static_cast<int>(risk_rule.min_level) << std::endl;
        std::cout << "  Channels: " << risk_rule.enabled_channels.size() << std::endl;
        
        // Test device registration
        std::cout << "\n3. Testing device registration..." << std::endl;
        
        DeviceRegistration test_device;
        test_device.device_id = "test_device_001";
        test_device.fcm_token = "test_fcm_token_abcdef";
        test_device.channel = PushNotificationChannel::FCM_ANDROID;
        test_device.user_id = test_user.user_id;
        test_device.is_active = true;
        
        test_user.registered_devices.push_back(test_device);
        
        std::cout << "✓ Registered test device" << std::endl;
        std::cout << "  Device ID: " << test_device.device_id << std::endl;
        std::cout << "  FCM Token: " << test_device.fcm_token << std::endl;
        std::cout << "  Channel: " << static_cast<int>(test_device.channel) << std::endl;
        std::cout << "  Active: " << (test_device.is_active ? "Yes" : "No") << std::endl;
        
        // Test exchange notification
        std::cout << "\n4. Testing exchange notification creation..." << std::endl;
        
        NotificationMessage test_notification;
        test_notification.id = "test_notification_001";
        test_notification.level = NotificationLevel::WARNING;
        test_notification.title = "Test Risk Alert";
        test_notification.message = "This is a test risk alert message for BTC/USD position";
        test_notification.exchange_id = "binance";
        test_notification.timestamp = std::chrono::system_clock::now();
        test_notification.acknowledged = false;
        
        std::cout << "✓ Created test notification" << std::endl;
        std::cout << "  ID: " << test_notification.id << std::endl;
        std::cout << "  Level: " << static_cast<int>(test_notification.level) << std::endl;
        std::cout << "  Title: " << test_notification.title << std::endl;
        std::cout << "  Exchange: " << test_notification.exchange_id << std::endl;
        
        // Test notification message conversion to JSON
        std::cout << "\n5. Testing notification serialization..." << std::endl;
        
        std::string json_string = test_notification.to_json();
        std::cout << "✓ Serialized notification to JSON" << std::endl;
        std::cout << "  JSON length: " << json_string.length() << " characters" << std::endl;
        
        // Test JSON deserialization
        NotificationMessage deserialized = NotificationMessage::from_json(json_string);
        std::cout << "✓ Deserialized notification from JSON" << std::endl;
        std::cout << "  Deserialized ID: " << deserialized.id << std::endl;
        std::cout << "  Deserialized Title: " << deserialized.title << std::endl;
        
        // Test utility functions
        std::cout << "\n6. Testing utility functions..." << std::endl;
        
        std::string level_str = settings_utils::notification_level_to_string(NotificationLevel::WARNING);
        std::cout << "✓ Converted level to string: " << level_str << std::endl;
        
        NotificationLevel parsed_level = settings_utils::string_to_notification_level(level_str);
        std::cout << "✓ Parsed level from string: " << static_cast<int>(parsed_level) << std::endl;
        
        std::string channel_str = settings_utils::notification_channel_to_string(notification::NotificationChannel::PUSH);
        std::cout << "✓ Converted channel to string: " << channel_str << std::endl;
        
        // Test time utilities
        std::string current_time = settings_utils::current_time_string();
        std::cout << "✓ Current time: " << current_time << std::endl;
        
        auto time_minutes = settings_utils::parse_time_string("14:30");
        std::cout << "✓ Parsed time '14:30' to minutes: " << time_minutes.count() << std::endl;
        
        // Test default profile creation
        std::cout << "\n7. Testing default profile creation..." << std::endl;
        
        auto default_profile = settings_utils::create_default_user_profile("default_user", "default@example.com");
        std::cout << "✓ Created default user profile" << std::endl;
        std::cout << "  User ID: " << default_profile.user_id << std::endl;
        std::cout << "  Email: " << default_profile.email << std::endl;
        std::cout << "  Channel count: " << default_profile.channel_enabled.size() << std::endl;
        
        auto default_rules = settings_utils::create_default_notification_rules("default_user");
        std::cout << "✓ Created default notification rules: " << default_rules.size() << " rules" << std::endl;
        
        std::cout << "\n🎉 Simple notification service test completed successfully!" << std::endl;
        std::cout << "\nTested Components:" << std::endl;
        std::cout << "• User notification profiles" << std::endl;
        std::cout << "• Notification rules and preferences" << std::endl;
        std::cout << "• Device registration structures" << std::endl;
        std::cout << "• Exchange notification messages" << std::endl;
        std::cout << "• JSON serialization/deserialization" << std::endl;
        std::cout << "• Utility functions" << std::endl;
        std::cout << "• Default configuration generation" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in simple test: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}