#pragma once

#include <QObject>
#include <QString>

namespace ats {
namespace ui {

class DataService;
class NotificationService;

class DashboardController : public QObject
{
    Q_OBJECT

public:
    explicit DashboardController(DataService* dataService = nullptr, 
                               NotificationService* notificationService = nullptr,
                               QObject *parent = nullptr);

    Q_INVOKABLE void exportReport();
    Q_INVOKABLE void refreshData();
    Q_INVOKABLE bool startStrategy(const QString& strategyName);
    Q_INVOKABLE bool stopStrategy(const QString& strategyName);

private:
    DataService* m_dataService;
    NotificationService* m_notificationService;
};

} // namespace ui
} // namespace ats