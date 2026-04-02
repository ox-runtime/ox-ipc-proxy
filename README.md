# ox-ipc-proxy

`ox-ipc-proxy` is the driver-shaped IPC layer between `ox-runtime` and the `ox` host process.

It builds two shared libraries:

- `ox_ipc_client`: exports the low-level driver ABI expected by `ox-runtime` and acts as the IPC client
- `ox_ipc_server`: accepts a real driver callback table from `ox.exe` and hosts the IPC server side

## Build

```bash
cmake -S . -B build/win-x64
cmake --build build/win-x64 --config Release
```

## Outputs

Artifacts are written under `build/<platform>/bin`:

- `ox_ipc_client.dll` (Windows), `libox_ipc_client.so` (Linux), `libox_ipc_client.dylib` (macOS)
- `ox_ipc_server.dll` (Windows), `libox_ipc_server.so` (Linux), `libox_ipc_server.dylib` (macOS)

The libraries use OS-native naming conventions and do not include version suffixes.

## Integration

- `ox-runtime` loads `ox_ipc_client` as its default driver when no explicit override is present.
- `ox.exe` loads `ox_ipc_server`, calls `ox_ipc_server_set_driver()` with the selected real driver callback table, then calls `ox_ipc_server_initialize()` to start serving clients.
