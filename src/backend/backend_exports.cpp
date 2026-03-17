#include <ox_driver.h>

#include "ipc_server.h"

#ifdef _WIN32
#define OX_IPC_BACKEND_EXPORT __declspec(dllexport)
#else
#define OX_IPC_BACKEND_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

OX_IPC_BACKEND_EXPORT void set_driver(const OxDriverCallbacks* callbacks) {
    ox::ipc::IpcServer::Instance().SetDriver(callbacks);
}

OX_IPC_BACKEND_EXPORT int initialize() { return ox::ipc::IpcServer::Instance().Initialize() ? 1 : 0; }

OX_IPC_BACKEND_EXPORT void shutdown() { ox::ipc::IpcServer::Instance().Shutdown(); }
}