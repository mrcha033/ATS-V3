# Notification Service Module

## Purpose

The `notification_service` module is responsible for managing user notification preferences and delivering various types of alerts and messages to users through multiple communication channels. It ensures that users are informed about critical system events, trading activities, and risk alerts in a timely and customizable manner.

## Key Components

-   **`NotificationSettingsService` (`include/notification_settings_service.hpp`, `src/notification_settings_service.cpp`)**:
    The central component for managing user notification preferences and routing notifications. Its responsibilities include:
    -   **User Profiles**: Stores and manages `UserNotificationProfile`s for each user, including their contact information (email, phone), preferred timezone, global notification enablement, and quiet mode settings.
    -   **Notification Rules**: Allows users to define custom `NotificationRule`s based on criteria such as notification category (e.g., "risk", "trade", "system"), minimum severity level, enabled communication channels, and desired frequency (immediate, batched, daily digest).
    -   **Device Management**: Manages `DeviceRegistration` for push notifications, linking FCM (Firebase Cloud Messaging) tokens to user IDs.
    -   **Notification Processing**: The `process_notification` method is the core logic. It receives an `exchange::NotificationMessage` (from `shared/exchange/exchange_notification_system.hpp`) and, based on user profiles and rules, determines which users should receive the notification, on which channels, and at what frequency.
    -   **Batching**: Supports batching notifications (e.g., hourly or daily digests) to reduce notification fatigue. A background thread (`batch_processor_loop`) handles the scheduled sending of these batched notifications.
    -   **Integration with `ExchangeNotificationSystem`**: Provides a settings-aware handler that can be registered with the `ExchangeNotificationSystem` to filter and route notifications based on individual user preferences.
    -   **Statistics**: Tracks various statistics related to notification processing, such as the number of notifications filtered, batched, or sent immediately, and usage statistics per channel and frequency.

-   **`PushNotificationService` (`include/push_notification_service.hpp`, `src/push_notification_service.cpp`)**:
    Handles the sending of push notifications, primarily via Firebase Cloud Messaging (FCM). Key features include:
    -   **FCM Integration**: Uses `libcurl` to send HTTP POST requests to the FCM API for delivering messages to mobile devices or web browsers.
    -   **Device Registration**: Manages the registration and unregistration of devices, storing and updating FCM tokens.
    -   **Notification History**: Maintains a history of sent push notifications, including delivery status and any errors encountered.
    -   **Statistics**: Tracks total sent, delivered, failed, and retried push notifications.
    -   **InfluxDB Integration**: Persists push notification history and metrics to InfluxDB for long-term storage and analysis.
    -   **Pre-defined Messages**: Includes helper functions for creating common push notification messages (e.g., risk limit exceeded, trade failure, system health alerts).

-   **`EmailNotificationService` (`include/email_notification_service.hpp`, `src/email_notification_service.cpp`)**:
    Manages the sending of email notifications via SMTP. Its functionalities include:
    -   **SMTP Integration**: Uses `libcurl` for secure SMTP (Simple Mail Transfer Protocol) communication to send emails.
    -   **Recipient Management**: Manages `EmailRecipient` profiles, including their email addresses and subscription preferences (e.g., subscribed notification levels and categories).
    -   **Email Templates**: Supports `EmailTemplate`s for generating dynamic email content (subject and body) using placeholders, allowing for personalized and structured messages.
    -   **Delivery History**: Stores a history of sent emails, including delivery status, SMTP responses, and errors.
    -   **Statistics**: Tracks total sent, delivered, failed, and retried emails.
    -   **InfluxDB Integration**: Persists email delivery history and metrics to InfluxDB.
    -   **Pre-defined Templates**: Includes helper functions for creating common email templates (e.g., risk alert, trade failure, system health).

-   **`NotificationInfluxDBStorage` (`include/notification_influxdb_storage.hpp`, `src/notification_influxdb_storage.cpp`)**:
    Provides a centralized mechanism for storing all types of notification metrics and history in InfluxDB. Its features include:
    -   **Unified Metrics**: Defines `NotificationMetrics` to capture comprehensive details about each notification event (user, channel, level, timing, delivery status, errors).
    -   **Batching**: Supports efficient batching of metrics for writing to InfluxDB, optimizing database performance.
    -   **Aggregates**: Calculates and stores hourly and daily aggregate metrics (e.g., total notifications by level/channel, average delivery time), providing summarized insights.
    -   **Querying**: Offers methods to query historical notification data and aggregates for reporting and dashboard visualization.

-   **Utility Functions (`src/notification_settings_utils.cpp`)**:
    Contains helper functions for common tasks such as converting between notification level/channel/frequency enums and strings, parsing time strings, and generating default user profiles and notification rules.

## Data Flow

1.  **Event Generation**: System events, trading alerts, and risk warnings are generated by other modules (e.g., `trading_engine`, `risk_manager`) and sent to the `ExchangeNotificationSystem` (from `shared`).
2.  **Initial Filtering**: The `ExchangeNotificationSystem` passes these messages to the `NotificationSettingsService` via a registered handler.
3.  **User-Specific Processing**: The `NotificationSettingsService` evaluates each notification against individual user profiles and custom rules to determine if, how, and when it should be sent.
4.  **Channel Routing**: Based on user preferences, notifications are routed to the appropriate channel-specific service (`PushNotificationService` or `EmailNotificationService`).
5.  **Message Delivery**: The channel services format the messages and send them using external APIs (FCM for push, SMTP for email).
6.  **History & Metrics**: All notification events, delivery statuses, and errors are logged by `NotificationInfluxDBStorage` to InfluxDB for auditing, analysis, and dashboard display.
7.  **Batch Processing**: For batched notifications, messages are queued by `NotificationSettingsService` and sent at scheduled intervals by a background processor.

## Integration with Other Modules

-   **`shared`**: Provides core data types, logging, and the `ExchangeNotificationSystem` for event ingestion.
-   **`trading_engine`**: Generates trade-related events and alerts that are consumed by the notification service.
-   **`risk_manager`**: Generates risk alerts and warnings that are delivered to users.
-   **`ui_dashboard`**: Displays notification history and allows users to configure their notification settings.

## Design Philosophy

The `notification_service` module is designed with a focus on:
-   **User-Centricity**: Highly customizable user preferences to prevent notification fatigue.
-   **Reliability**: Robust delivery mechanisms with retry logic and error handling.
-   **Extensibility**: Easy to add new communication channels or notification rules.
-   **Observability**: Comprehensive logging and metrics for monitoring notification delivery performance.
-   **Scalability**: Supports batch processing to handle large volumes of notifications efficiently.

