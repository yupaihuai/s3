/**
 * @file Sys_WebServer.cpp
 * @brief 系统Web服务器模块的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 实现了所有HTTP路由和WebSocket事件处理的具体逻辑。
 * 它作为前端和后端业务逻辑之间的“翻译官”和“调度员”。
 */
#include "Sys_WebServer.h"
#include "types.h"
#include "Sys_Debug.h"
#include "Sys_Tasks.h"        // 需要访问 xCommandQueue 和 Command 结构体
#include "Sys_Filesystem.h"   // 需要访问 LittleFS 和 FFat
#include "Sys_SettingsManager.h"

// 初始化静态单例指针
Sys_WebServer* Sys_WebServer::_instance = nullptr;

/**
 * @brief 获取WebServer的单例实例。
 */
Sys_WebServer* Sys_WebServer::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_WebServer();
    }
    return _instance;
}

/**
 * @brief 构造函数，初始化服务器监听80端口，WebSocket服务路径为/ws。
 */
Sys_WebServer::Sys_WebServer() : _server(80), _ws("/ws") {}

/**
 * @brief 启动服务器。
 */
void Sys_WebServer::begin() {
    DEBUG_LOG("Initializing Web Server...");

    // 步骤1：将WebSocket事件回调函数绑定到WebSocket实例。
    // 使用C++ lambda表达式和[this]捕获，使得我们可以在类的成员函数中处理事件。
    _ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws); // 将WebSocket处理器添加到Web服务器

    // 步骤2：设置所有HTTP路由
    setupHttpRoutes();

    // 步骤3：启动Web服务器
    _server.begin();
    ESP_LOGI("WebServer", "HTTP and WebSocket server started.");
}

/**
 * @brief 集中设置所有HTTP路由。
 */
void Sys_WebServer::setupHttpRoutes() {
    // --- API Endpoints ---
    
    // 注册一个处理JSON body的POST请求处理器
    // 这是处理前端发来的JSON数据的标准、高效方式
    auto jsonBodyHandler = new AsyncCallbackJsonWebHandler("/api/settings/save", handleSaveSettings);
    _server.addHandler(jsonBodyHandler);
    
    // 注册GET请求API
    _server.on("/api/settings/get", HTTP_GET, handleGetSettings);
    _server.on("/api/wifi/scan", HTTP_GET, handleScanWiFi);
    
    // --- 文件上传处理 ---
    // 所有POST到/upload的请求都会被这个处理器处理
    _server.on("/upload", HTTP_POST, 
        // 上传成功后的响应回调
        [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "Upload OK");
        }, 
        // 文件块数据处理回调
        handleFileUpload
    );

    // --- 静态文件服务 ---
    // 根目录 ("/") 的文件从LittleFS的根目录提供
    _server.serveStatic("/", LittleFS, "/")
           .setCacheControl("max-age=600")      // 设置浏览器缓存600秒
           .setDefaultFile("index.html");       // 默认文件为index.html

    // "/media" 路径的文件从FFat的根目录提供
    _server.serveStatic("/media", FFat, "/");

    // --- Gzip内容协商优化 ---
    // 当浏览器请求一个.css文件时，我们检查是否存在对应的.css.gz文件。
    // 如果存在，则直接发送压缩过的版本，并告知浏览器内容是gzip编码的。
    _server.on("*.js", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = request->url();
        if (LittleFS.exists(path + ".gz")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, path + ".gz", "application/javascript");
            response->addHeader("Content-Encoding", "gzip");
            request->send(response);
        } else if (LittleFS.exists(path)) {
            request->send(LittleFS, path, "application/javascript");
        }
        else {
            request->send(404);
        }
    });
    // (可以为css, html等其他文件类型添加类似的规则)

    // --- 404 Not Found 处理器 ---
    // 当以上所有路由都不匹配时，此处理器将被调用
    _server.onNotFound(handleNotFound);
}

AsyncWebSocket* Sys_WebServer::getWebSocket() {
    return &_ws;
}

void Sys_WebServer::cleanupClients() {
    _ws.cleanupClients();
}

// --- HTTP Route Handlers (静态方法实现) ---

void Sys_WebServer::handleGetSettings(AsyncWebServerRequest *request) {
    // 只读操作，可以直接从SettingsManager的缓存中快速获取数据并返回，无需经过Task_Worker
    const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
    
    JsonDocument doc; // 使用ArduinoJson创建JSON
    // 序列化所有需要返回给前端的设置
    doc["wifi_ssid"] = settings.wifi_ssid;
    doc["wifi_mode"] = (int)settings.wifi_mode;
    doc["bluetooth_enabled"] = settings.bluetooth_enabled;
    doc["bluetooth_name"] = settings.bluetooth_name;
    // ...

    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
}

void Sys_WebServer::handleSaveSettings(AsyncWebServerRequest *request, JsonVariant &json) {
    // 写操作，可能会触发耗时动作（如重启WiFi），因此必须通过命令队列发给Task_Worker
    JsonDocument doc = json.as<JsonDocument>();
    Command cmd;

    // 根据JSON负载的内容，判断是哪种保存操作
    if (doc.containsKey("wifi_ssid")) {
        cmd.type = Command::SAVE_WIFI;
    } else if (doc.containsKey("bluetooth_name")) {
        cmd.type = Command::SAVE_BLE;
    } else {
        request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid settings format.\"}");
        return;
    }
        
    // 将收到的完整JSON作为字符串负载发送给Worker
    serializeJson(doc, cmd.payload, sizeof(cmd.payload));
    
    if (xQueueSend(xCommandQueue, &cmd, 0) == pdPASS) {
        request->send(202, "application/json", "{\"success\":true, \"message\":\"Settings update command queued.\"}");
    } else {
        request->send(503, "application/json", "{\"success\":false, \"message\":\"Command queue full.\"}");
    }
}

void Sys_WebServer::handleScanWiFi(AsyncWebServerRequest *request) {
    // WiFi扫描是典型的耗时操作，必须交由Task_Worker处理
    Command cmd;
    cmd.type = Command::SCAN_WIFI;
    
    if (xQueueSend(xCommandQueue, &cmd, 0) == pdPASS) {
        // 立即返回“202 Accepted”，表示请求已被接受，结果将异步推送
        request->send(202, "application/json", "{\"success\":true, \"message\":\"WiFi scan initiated.\"}");
    } else {
        request->send(503, "application/json", "{\"success\":false, \"message\":\"Command queue full.\"}");
    }
}

void Sys_WebServer::handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    // 安全地拼接路径，防止路径遍历攻击
    String path = "/media/" + filename;

    if (!index) { // 文件开始上传 (index=0)
        DEBUG_LOG("Upload Start: %s", path.c_str());
        if (FFat.exists(path)) { FFat.remove(path); }
        uploadFile = FFat.open(path, "w");
        if (!uploadFile) {
            ESP_LOGE("WebServer", "Failed to open file for writing: %s", path.c_str());
            return;
        }
    }

    if (len > 0 && uploadFile) {
        uploadFile.write(data, len); // 将接收到的数据块写入文件
    }

    if (final) { // 文件上传结束
        DEBUG_LOG("Upload End: %s, Total Size: %u", path.c_tr(), index + len);
        if (uploadFile) { uploadFile.close(); }
    }
}

void Sys_WebServer::handleNotFound(AsyncWebServerRequest *request) {
    // 根据请求的URL类型，返回不同的404响应
    if (request->url().startsWith("/api/")) {
        // API请求返回JSON错误
        request->send(404, "application/json", "{\"error\":\"API endpoint not found\"}");
    } else {
        // 普通页面请求返回HTML错误页
        request->send(404, "text/html", "<h1>404 Not Found</h1><p>The requested resource was not found on this server.</p>");
    }
}

// --- WebSocket 事件处理器 ---
void Sys_WebServer::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            // 客户端连接成功
            ESP_LOGI("WebSocket", "Client #%u connected from %s", client->id(), client->remoteIP().toString().c_str());
            client->text("{\"type\":\"welcome\",\"message\":\"Connection established!\"}");
            break;
        case WS_EVT_DISCONNECT:
            // 客户端断开连接
            ESP_LOGI("WebSocket", "Client #%u disconnected", client->id());
            break;
        case WS_EVT_DATA: {
            // 收到数据帧
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            // 只处理完整的TEXT类型数据帧
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // 这是处理从前端发来的简单WebSocket命令的地方
                JsonDocument doc;
                if (deserializeJson(doc, (const char*)data, len) == DeserializationError::Ok) {
                    const char* command_str = doc["command"];
                    if (command_str) {
                         if (strcmp(command_str, "reboot") == 0) {
                            Command cmd;
                            cmd.type = Command::REBOOT;
                            xQueueSend(xCommandQueue, &cmd, 0);
                        }
                        // ... 处理其他简单的WebSocket命令 ...
                    }
                } else {
                    client->text("{\"type\":\"error\",\"message\":\"Invalid JSON received\"}");
                }
            }
            break;
        }
        case WS_EVT_PONG: // 收到PONG帧，可用于心跳机制
        case WS_EVT_ERROR: // 发生错误
            break;
    }
}
