#pragma once
#include <QObject>
#include <QString>
namespace ats { namespace ui {
class Pdf_report_service : public QObject {
    Q_OBJECT
public:
    explicit Pdf_report_service(QObject *parent = nullptr) : QObject(parent) {}
};
}} // namespace ats::ui
