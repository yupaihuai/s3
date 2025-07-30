# API Specification (JSON RPC 2.0)

本文件定义了ESP32管理系统的前后端通信契约。所有通信均基于JSON RPC 2.0规范，通过WebSocket传输。

## 基本原则

- **请求 (Request)**: 客户端向服务器发送包含 `jsonrpc`, `method`, `params`, `id` 的请求对象。
- **响应 (Response)**: 服务器返回包含 `jsonrpc`, `result` 或 `error`, `id` 的响应对象。
- **通知 (Notification)**: 服务器主动向客户端推送不带 `id` 的消息，用于状态更新和日志。

---

## 1. 系统命令 (System Commands)

### Method: `system.reboot`
- **Description**: 请求系统重启。
- **Params**: `null`
- **Result**: `{"status": "rebooting"}`
- **Example**:
  - **Request**: `{"jsonrpc": "2.0", "method": "system.reboot", "id": 1}`
  - **Response**: `{"jsonrpc": "2.0", "result": {"status": "rebooting"}, "id": 1}`

### Method: `system.factoryReset`
- **Description**: 执行恢复出厂设置，将擦除所有用户配置。
- **Params**: `null`
- **Result**: `{"status": "resetting"}`

---

## 2. 设置管理 (Settings Management)

### Method: `settings.get`
- **Description**: 获取当前所有系统设置。
- **Params**: `null`
- **Result**: `Object` - 包含所有设置项的JSON对象。
- **Example**:
  - **Response**: `{"jsonrpc": "2.0", "result": {"wifi": {"ssid": "MyNet"}, "bluetooth": {"deviceName": "ESP32-Sys"}}, "id": 2}`

### Method: `settings.saveWiFi`
- **Description**: 保存新的WiFi凭据。
- **Params**:
  - `ssid` (string, required): WiFi的SSID。
  - `password` (string, optional): WiFi的密码。
- **Result**: `{"status": "success"}`

### Method: `settings.saveBluetooth`
- **Description**: 保存新的蓝牙设备名称。
- **Params**:
  - `deviceName` (string, required): 新的蓝牙广播名称。
- **Result**: `{"status": "success"}`

---

## 3. WiFi管理 (WiFi Management)

### Method: `wifi.scan`
- **Description**: 请求进行一次WiFi扫描。结果将通过`wifi.scanResult`通知异步推送。
- **Params**: `null`
- **Result**: `{"status": "scanning"}`

---

## 4. 服务器推送通知 (Server Notifications)

### Method: `log.batch`
- **Description**: 服务器推送的一批（一个或多个）日志消息。
- **Params**: `Array` - 一个JSON对象的数组，每个对象代表一条日志。
  - `msg` (string): 日志内容。
  - `lvl` (string, optional): 日志级别 (e.g., "I", "W", "E")。
  - `ts` (number, optional): 时间戳。
- **Example**:
  ```json
  {
    "jsonrpc": "2.0",
    "method": "log.batch",
    "params": [
      { "msg": "I (1234) WiFi: STA Connected. IP: 192.168.1.100" },
      { "msg": "I (5678) Settings: Settings committed to NVS." }
    ]
  }
  ```

### Method: `system.stateUpdate`
- **Description**: 服务器推送的系统状态更新。
- **Params**: `Object` - 包含变化的状态键值对，例如 `{"heap_free": 123456, "uptime": 7200}`。

### Method: `wifi.scanResult`
- **Description**: 推送WiFi扫描结果。
- **Params**: `Array` - 扫描到的WiFi网络对象数组 `[{"ssid": "Net1", "rssi": -50}, ...]`。