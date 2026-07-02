# ox-ipc-proxy

`ox-ipc-proxy` is a driver-shaped IPC proxy (i.e. driver API in, driver API out, proxied over IPC), built for [ox](https://github.com/ox-runtime/ox).

It is used to decouple the application process from the XR device driver, to protect the application process from driver instability or crashes.

The [ox](https://github.com/ox-runtime/ox) host process uses this to communicate with [ox-runtime](https://github.com/ox-runtime/ox-runtime) (which runs in the XR application process).

## Build

```bash
cmake -B build
cmake --build build --config Release
```

This builds two shared libraries:

- `ox_ipc_client`: exports the low-level [driver API](https://github.com/ox-runtime/ox-runtime/blob/main/include/ox_driver.h) expected by `ox-runtime`
- `ox_ipc_server`: accepts a real driver from `ox.exe`

## Integration

- `ox-runtime` loads `ox_ipc_client` as its default driver when no explicit override is present.
- `ox` host process loads `ox_ipc_server`, calls `ox_ipc_server_set_driver()` with the selected real driver.
