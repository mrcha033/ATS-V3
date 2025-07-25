#include "../../include/services/grpc_client_service.hpp"

namespace ats { namespace ui {

GrpcClientService::GrpcClientService(QObject *parent) : QObject(parent) {}

bool GrpcClientService::initialize(const QString& serverUrl) {
    Q_UNUSED(serverUrl)
    m_connected = true;
    emit connectionStatusChanged(m_connected);
    return true;
}

}} // namespace ats::ui