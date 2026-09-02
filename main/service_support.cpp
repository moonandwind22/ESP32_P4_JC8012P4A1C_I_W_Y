#include "service_support.h"

namespace {

service_network_ready_fn_t g_service_network_ready_callback = nullptr;
service_network_fault_fn_t g_service_network_fault_callback = nullptr;

}  // namespace

void service_set_network_ready_callback(service_network_ready_fn_t callback)
{
    g_service_network_ready_callback = callback;
}

bool service_network_ready()
{
    if (g_service_network_ready_callback == nullptr) {
        return false;
    }

    return g_service_network_ready_callback();
}

void service_set_network_fault_callback(service_network_fault_fn_t callback)
{
    g_service_network_fault_callback = callback;
}

void service_report_network_fault(const char *tag, int code)
{
    if (g_service_network_fault_callback == nullptr) {
        return;
    }

    g_service_network_fault_callback(tag, code);
}
