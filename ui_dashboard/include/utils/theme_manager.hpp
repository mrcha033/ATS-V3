#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Theme_manager : public QObject {
    Q_OBJECT
public:
    explicit Theme_manager(QObject *parent = nullptr) : QObject(parent) {}
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme CONSTANT)
    bool isDarkTheme() const { return true; }
    Q_INVOKABLE void setTheme(const QString& theme) { Q_UNUSED(theme) }
};
}} // namespace ats::ui
