#pragma once
#include <QObject>
#include <QString>

namespace ats { namespace ui {

class GrpcClientService : public QObject
{
    Q_OBJECT
public:
    explicit GrpcClientService(QObject *parent = nullptr);
    bool initialize(const QString& serverUrl);
    bool isConnected() const { return m_connected; }

signals:
    void connectionStatusChanged(bool connected);

private:
    bool m_connected = false;
};

}} // namespace ats::ui