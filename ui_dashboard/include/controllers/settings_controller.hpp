#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Settings_controller : public QObject {
    Q_OBJECT
public:
    explicit Settings_controller(QObject* service1 = nullptr, QObject* service2 = nullptr, QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void emergencyStopAll() {}
};
}} // namespace ats::ui
