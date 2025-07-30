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
#include "Sys_Tasks.h"        // 需要访问 xCommandQueue 和 JsonRpcRequest 结构体
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

    // --- 静态文件服务 (Gzip内容协商优化) ---
    // 对所有静态资源请求进行拦截，优先提供.gz版本
    _server.on("/*", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = request->url();
        if (path.endsWith("/")) path += "index.html";
        
        String contentType = "text/plain";
        if (path.endsWith(".html")) contentType = "text/html";
        else if (path.endsWith(".css")) contentType = "text/css";
        else if (path.endsWith(".js")) contentType = "application/javascript";
        
        if (LittleFS.exists(path + ".gz")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, path + ".gz", contentType);
            response->addHeader("Content-Encoding", "gzip");
            request->send(response);
        } else if (LittleFS.exists(path)) {
            request->send(LittleFS, path, contentType);
        } else {
            handleNotFound(request);
        }
    });

    // --- 媒体文件服务 ---
    // "/media" 路径的文件从FFat的根目录提供
    _server.serveStatic("/media", FFat, "/");

    // --- 404 Not Found 处理器 ---
    _server.onNotFound(handleNotFound);
}

AsyncWebSocket* Sys_WebServer::getWebSocket() {
    return &_ws;
}

void Sys_WebServer::cleanupClients() {
    _ws.cleanupClients();
}

// --- HTTP Route Handlers (静态方法实现) ---

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

// --- WebSocket 事件处理器 (JSON RPC 2.0) ---
void Sys_WebServer::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            ESP_LOGI("WebSocket", "Client #%u connected from %s", client->id(), client->remoteIP().toString().c_str());
            client->text("{\"jsonrpc\":\"2.0\",\"method\":\"server.welcome\",\"params\":{\"message\":\"Connection established!\"}}");
            break;

        case WS_EVT_DISCONNECT:
            ESP_LOGI("WebSocket", "Client #%u disconnected", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, (const char*)data, len);

                if (error) {
                    client->text("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}");
                    return;
                }

                // 验证JSON RPC 2.0格式
                if (strcmp(doc["jsonrpc"], "2.0") != 0 || doc["method"].isNull()) {
                    client->text("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"},\"id\":null}");
                    return;
                }

                // 封装为内部命令结构体
                JsonRpcRequest rpcRequest;
                rpcRequest.id = doc["id"] | 0; // 如果id不存在，默认为0
                rpcRequest.client_id = client->id();
                
                strncpy(rpcRequest.method, doc["method"], sizeof(rpcRequest.method) - 1);
                
                if (!doc["params"].isNull()) {
                    serializeJson(doc["params"], rpcRequest.params, sizeof(rpcRequest.params));
                }

                // 发送到命令队列
                if (xQueueSend(xCommandQueue, &rpcRequest, pdMS_TO_TICKS(10)) != pdPASS) {
                    ESP_LOGE("WebServer", "Command queue full, dropping RPC request.");
                    client->text("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"Server busy, command queue full\"},\"id\":rpcRequest.id}");
                }
            }
            break;
        }
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}
