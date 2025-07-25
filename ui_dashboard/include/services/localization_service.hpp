#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Localization_service : public QObject {
    Q_OBJECT
public:
    explicit Localization_service(QObject *parent = nullptr) : QObject(parent) {}
};
}} // namespace ats::ui
