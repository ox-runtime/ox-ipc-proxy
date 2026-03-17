# ox-ipc-proxy

`ox-ipc-proxy` is the driver-shaped IPC layer between `ox-runtime` and the `ox` host process.

It builds two shared libraries:

- `ox_ipc_frontend`: exports the low-level driver ABI expected by `ox-runtime` and acts as the IPC client
- `ox_ipc_backend`: accepts a real driver callback table from `ox.exe` and hosts the IPC server side

## Build

```bash
cmake -S . -B build/win-x64
cmake --build build/win-x64 --config Release
```

## Outputs

Artifacts are written under `build/<platform>/bin`:

- `ox_ipc_frontend.{dll|so|dylib}`
- `ox_ipc_backend.{dll|so|dylib}`

## Integration

- `ox-runtime` loads `ox_ipc_frontend` as its default driver when no explicit override is present.
- `ox.exe` loads `ox_ipc_backend`, calls `set_driver()` with the selected real driver callback table, then calls `initialize()` to start serving clients.
