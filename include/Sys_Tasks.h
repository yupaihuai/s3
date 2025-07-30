/**
 * @file Sys_Tasks.h
 * @brief 系统核心任务管理器的接口定义
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 *
 * @details
 * 该文件是系统中所有FreeRTOS后台任务的“蓝图”。
 * 它集中定义了每个任务的属性（名称、堆栈、优先级、核心），
 * 并声明了任务间通信所需的全局句柄，为整个系统的并发模型
 * 提供了清晰、统一的视图。
 * 优化点：
 * 1. 明确了任务看门狗(TWDT)的配置和使用。
 * 2. 增加了对未来任务(Task_ImageProcessor)的规划。
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "ESPAsyncWebServer.h" // 需要 AsyncWebSocket 类型

// --- 定义全局通信句柄 ---
// 这些句柄在Sys_Tasks.cpp中被实际创建，在此处用`extern`声明以便其他模块可以引用。
// 它们是任务间通信的桥梁，实现了模块间的解耦。

/** @brief 命令队列：用于从前端接口（如WebServer）接收待处理的命令，由Task_Worker消费。*/
extern QueueHandle_t xCommandQueue;
/** @brief 状态队列：用于从后台任务（如Task_SystemMonitor）收集需要推送到前端的状态信息。*/
extern QueueHandle_t xStateQueue;
/** @brief 日志队列：用于从日志系统收集需要推送到前端的日志消息。*/
extern QueueHandle_t xLogQueue;
/** @brief 数据事件组：用于高效地通知Task_WebSocketPusher有新的数据需要推送，避免轮询队列。*/
extern EventGroupHandle_t xDataEventGroup;

// --- 事件组中的事件位定义 ---
/** @brief 标记`xStateQueue`中有新数据的事件位。*/
const EventBits_t BIT_STATE_QUEUE_READY = (1 << 0);
/** @brief 标记日志队列（未来扩展）中有新数据的事件位。*/
const EventBits_t BIT_LOG_QUEUE_READY   = (1 << 1);
// ... 未来可在此添加其他事件位 ...


/**
 * @class Sys_Tasks
 * @brief 集中创建和管理系统中的所有FreeRTOS任务。
 *        它是一个任务工厂和管理器，负责启动系统的并发核心。
 */
class Sys_Tasks {
public:
    /**
     * @brief 初始化所有通信句柄并创建所有系统任务。
     * @details 这是从单线程`setup()`到多任务系统的切换点。
     *          此函数还会初始化并启动任务看门狗。
     * @param webSocket 异步WebSocket服务器的实例指针，用于安全地传递给需要它的后台任务。
     */
    static void begin(AsyncWebSocket* webSocket);

private:
    // --- 任务参数定义 ---
    // 将所有任务的配置参数集中在此处，便于统一调整和管理。

    /** @brief Task_Worker: 命令处理器任务 */
    static constexpr const char* TASK_WORKER_NAME = "Task_Worker";
    static constexpr uint32_t TASK_WORKER_STACK_SIZE = 4096;
    static constexpr UBaseType_t TASK_WORKER_PRIORITY = 1;
    static constexpr BaseType_t TASK_WORKER_CORE = 1;

    /** @brief Task_SystemMonitor: 系统监视器任务 */
    static constexpr const char* TASK_MONITOR_NAME = "Task_SystemMonitor";
    static constexpr uint32_t TASK_MONITOR_STACK_SIZE = 4096;
    static constexpr UBaseType_t TASK_MONITOR_PRIORITY = 1;
    static constexpr BaseType_t TASK_MONITOR_CORE = 1;

    /** @brief Task_WebSocketPusher: WebSocket推送器任务 */
    static constexpr const char* TASK_PUSHER_NAME = "Task_WebSocketPusher";
    static constexpr uint32_t TASK_PUSHER_STACK_SIZE = 4096;
    static constexpr UBaseType_t TASK_PUSHER_PRIORITY = 2; // 较高优先级，确保数据实时推送
    static constexpr BaseType_t TASK_PUSHER_CORE = 1;      // 在应用核心上运行

    // --- 任务的静态循环函数 ---
    // 声明为私有，防止外部直接调用，其地址被传递给`xTaskCreatePinnedToCore`。
    
    /** @brief Task_Worker 的核心循环函数。*/
    static void taskWorkerLoop(void* parameter);
    /** @brief Task_SystemMonitor 的核心循环函数。*/
    static void taskSystemMonitorLoop(void* parameter);
    /** @brief Task_WebSocketPusher 的核心循环函数。*/
    static void taskWebSocketPusherLoop(void* parameter);

    // --- [重构] JSON RPC 请求的统一处理入口 ---
    /**
     * @brief 解析并分发JSON RPC请求到具体的处理逻辑。
     * @param request 包含RPC方法和参数的请求对象。
     */
    static void processJsonRpcRequest(const struct JsonRpcRequest& request);
};
