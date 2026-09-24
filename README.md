# rbxWsocket

A simple WebSocket library for Roblox exploits (DLL).

## Features

- Supports `ws://` and `wss://` connections
- Send and receive text and binary messages
- `OnMessage` and `OnClose` events (BindableEvent)
- Works via WinHTTP (secure) and raw sockets
- Thread-safe Lua state access via spinlock

## Installation

This library is intended for use inside an **internal DLL**.  
Add the file to your project and register the `WebSocket::connect` function in your environment.

## API

### `WebSocket.connect(url: string): WebSocket`

Creates a connection to the specified WebSocket server.

```lua
local ws = WebSocket.connect("wss://example.com/socket")
```

### Properties

| Property | Type | Description |
|---|---|---|
| `OnMessage` | BindableEvent | Fired when a message is received. Args: `(message: string, is_binary: boolean)` |
| `OnClose` | BindableEvent | Fired when the connection is closed |

### Methods

| Method | Description |
|---|---|
| `ws:Send(data: string, is_binary: boolean?)` | Send a message |
| `ws:Close()` | Close the connection |

## Example

```lua
local ws = WebSocket.connect("wss://echo.websocket.events")

ws.OnMessage.Event:Connect(function(msg, is_binary)
    print("[MESSAGE]", is_binary and "binary" or "text", msg)
end)

ws.OnClose.Event:Connect(function()
    print("[CLOSED]")
end)

ws:Send("hello world")

task.wait(5)
ws:Close()
```

## Dependencies

- Windows (WinHTTP, Winsock2)
- Luau 730+
- `xorstr_` — for string obfuscation

## Notes

- `wss://` uses WinHTTP with `WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET`.
- `ws://` uses a custom frame parser (RFC 6455) with masking.
- All Lua calls from the background thread are protected by a `SpinLock`.
- Event and thread references are released on close.
