#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Chart_data_provider : public QObject {
    Q_OBJECT
public:
    explicit Chart_data_provider(QObject *parent = nullptr) : QObject(parent) {}
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme CONSTANT)
    bool isDarkTheme() const { return true; }
    Q_INVOKABLE void setTheme(const QString& theme) { Q_UNUSED(theme) }
};
}} // namespace ats::ui
