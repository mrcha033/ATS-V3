#include "../../include/controllers/dashboard_controller.hpp"

namespace ats {
namespace ui {

DashboardController::DashboardController(DataService* dataService, 
                                       NotificationService* notificationService,
                                       QObject *parent)
    : QObject(parent)
    , m_dataService(dataService)
    , m_notificationService(notificationService)
{
}

void DashboardController::exportReport()
{
    // TODO: Implement PDF report export
}

void DashboardController::refreshData()
{
    if (m_dataService) {
        m_dataService->refreshAllData();
    }
}

bool DashboardController::startStrategy(const QString& strategyName)
{
    // TODO: Implement strategy start
    Q_UNUSED(strategyName)
    return true;
}

bool DashboardController::stopStrategy(const QString& strategyName)
{
    // TODO: Implement strategy stop
    Q_UNUSED(strategyName)
    return true;
}

} // namespace ui
} // namespace ats