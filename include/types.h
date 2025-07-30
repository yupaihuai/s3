/**
 * @file types.h
 * @brief 定义项目中跨模块共享的公共数据类型、结构体和枚举。
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 这个文件旨在解决模块间的类型依赖问题。任何被多个模块
 * 使用的数据结构都应该在这里定义，以提供一个“单一事实来源”，
 * 避免重复定义和不明确的`extern`声明。
 */
#pragma once
#include <stdint.h> // For uint32_t
#include <functional> // For std::function

/**
 * @struct JsonRpcRequest
 * @brief 定义了从前端接口到Task_Worker的JSON RPC 2.0请求的内部表示。
 *        它被放入命令队列中，由Task_Worker消费。
 */
struct JsonRpcRequest {
    /** @brief JSON RPC 请求的ID，用于匹配响应。对于通知，此ID可能无效或为0。*/
    uint32_t id = 0;

    /** @brief 发起请求的WebSocket客户端ID，用于定向响应。*/
    uint32_t client_id = 0;

    /** @brief JSON RPC 的方法名 (例如 "system.reboot", "settings.saveWiFi")。*/
    char method[64] = "";

    /** @brief 携带JSON RPC的`params`对象的JSON字符串。*/
    char params[512] = "";
    /**
     * @brief 响应闭包，用于Task_Worker直接回调，将响应发回给正确的客户端。
     * @details 定义一个响应函数类型：参数为JSON字符串，返回值为void。
     */
    using ResponseCallback = std::function<void(const char* json_response)>;
    ResponseCallback response_cb;
};

// --- 未来可以添加其他共享类型 ---

/**
 * @struct LogEntry_t
 * @brief 定义了放入日志队列的轻量级日志条目。
 * @details 仅包含格式化后的原始日志消息，将JSON打包工作转移到消费者任务。
 */
struct LogEntry_t {
    char message[256]; // 存储格式化后的原始日志消息
};

/*
struct SystemEvent {
    enum class Source { WIFI, BLUETOOTH, SENSOR };
    Source source;
    int event_code;
};
*/