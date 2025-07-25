#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Notification_service : public QObject {
    Q_OBJECT
public:
    explicit Notification_service(QObject *parent = nullptr) : QObject(parent) {}
};
}} // namespace ats::ui
