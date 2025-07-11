/**
 * @file Sys_WebServer.h
 * @brief 系统Web服务器模块的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块基于ESPAsyncWebServer库，创建并管理一个高效的异步Web服务器。
 * 它负责处理两类核心通信：
 * 1. HTTP RESTful API：用于前端发起的、请求-响应模式的操作（如获取设置）。
 * 2. WebSocket：用于实现服务器主动推送数据（如系统状态）和客户端的简单命令。
 *
 * 本模块遵循关注点分离原则，不执行任何耗时业务逻辑，而是将收到的请求
 * 转化为命令，通过命令队列发送给后台任务处理。
 */
#pragma once

#include <Arduino.h>
#include "ESPAsyncWebServer.h"
#include "ArduinoJson.h" // 需要JsonVariant

class Sys_WebServer {
public:
    /**
     * @brief 获取WebServer的单例实例。
     * @return Sys_WebServer* 指向唯一实例的指针。
     */
    static Sys_WebServer* getInstance();

    // 删除拷贝构造函数和赋值操作符，确保单例模式。
    Sys_WebServer(const Sys_WebServer&) = delete;
    Sys_WebServer& operator=(const Sys_WebServer&) = delete;

    /**
     * @brief 初始化并启动Web服务器和WebSocket服务。
     * @details 会设置好所有的HTTP路由和WebSocket事件回调。
     */
    void begin();

    /**
     * @brief 获取内部的WebSocket服务器实例指针。
     * @details 主要用于将其实例传递给`Task_WebSocketPusher`，以便该任务能推送数据。
     * @return AsyncWebSocket* 指向WebSocket服务器的指针。
     */
    AsyncWebSocket* getWebSocket();
    
    /**
     * @brief 清理所有已断开连接的WebSocket客户端。
     * @details `ESPAsyncWebServer`库要求定期调用此函数以释放资源。
     *          通常可以由`Task_SystemMonitor`周期性调用。
     */
    void cleanupClients();

private:
    // 私有构造函数，在其中初始化服务器和WebSocket对象。
    Sys_WebServer();

    // --- HTTP API 路由设置 ---
    /**
     * @brief 集中设置所有HTTP路由的私有辅助函数。
     */
    void setupHttpRoutes();

    // --- HTTP Route Handlers (静态方法) ---
    // 这些是处理具体HTTP请求的函数。
    
    /** @brief 处理获取系统设置的GET请求。*/
    static void handleGetSettings(AsyncWebServerRequest *request);
    /** @brief 处理保存系统设置的POST请求（带JSON Body）。*/
    static void handleSaveSettings(AsyncWebServerRequest *request, JsonVariant &json);
    /** @brief 处理WiFi扫描的GET请求。*/
    static void handleScanWiFi(AsyncWebServerRequest *request);
    /** @brief 处理文件上传。*/
    static void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
    /** @brief 处理所有未找到的路由 (404)。*/
    static void handleNotFound(AsyncWebServerRequest *request);

    // --- WebSocket 事件处理 ---
    /**
     * @brief WebSocket的核心事件回调函数。
     * @details 处理客户端的连接、断开、数据接收等所有WebSocket事件。
     */
    void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    /** @brief 单例实例指针。*/
    static Sys_WebServer* _instance;
    
    /** @brief 异步Web服务器实例。*/
    AsyncWebServer _server;
    /** @brief 异步WebSocket实例，路径为 "/ws"。*/
    AsyncWebSocket _ws;
};
