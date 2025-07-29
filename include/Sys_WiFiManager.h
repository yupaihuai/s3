/**
 * @file Sys_WiFiManager.h
 * @brief 一个事件驱动的、状态机清晰的、健壮的WiFi管理器。
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 该模块负责根据系统配置来维持WiFi连接，并向外提供清晰的状态。
 * 它不处理业务逻辑（如扫描、保存），只负责应用配置。
 * 优化点：
 * 1. 引入互斥锁，确保所有WiFi操作的原子性，防止竞态条件。
 * 2. 引入智能重连机制，对永久性失败（如密码错误）进行有限次重试，避免无效功耗。
 * 3. 状态转换逻辑全部集中在事件回调中，使状态机模型更纯粹。
 */
#pragma once

#include "WiFi.h"
#include "Sys_SettingsManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Sys_LockGuard.h" // 引入RAII锁

// 定义清晰的WiFi状态，供外部模块查询
enum class WiFiState {
    WIFI_STATE_DISABLED, // WiFi功能被禁用
    DISCONNECTED,      // STA模式下，已断开连接，正在等待重连超时
    CONNECTING,        // STA模式下，正在尝试连接
    CONNECTED_STA,     // STA模式下，已成功连接并获取IP
    HOSTING_AP,        // AP模式下，正在广播热点
    HOSTING_AP_STA,    // AP+STA模式，且STA已连接
    FAILED_PERMANENTLY // [优化] STA连接因配置错误等永久性原因失败，已停止重试
};

class Sys_WiFiManager {
public:
    // 获取单例实例
    static Sys_WiFiManager* getInstance();
    // 禁止拷贝和赋值
    Sys_WiFiManager(const Sys_WiFiManager&) = delete;
    Sys_WiFiManager& operator=(const Sys_WiFiManager&) = delete;

    /**
     * @brief 在系统启动时调用，注册WiFi事件回调并创建同步机制。
     */
    void begin();

    /**
     * @brief 由 Task_SystemMonitor 周期性调用，用于处理非事件驱动的逻辑（如超时重连）。
     */
    void update();
    
    /**
     * @brief 应用最新的系统设置。这是控制WiFi行为的核心入口。
     * @details 这是一个线程安全的操作，它会根据新设置触发相应的WiFi动作。
     */
    void applySettings();

    /**
     * @brief 获取当前WiFi模块的聚合状态。
     * @return WiFiState 当前状态。
     */
    WiFiState getCurrentState();
    
    /**
     * @brief 获取当前设备的IP地址。
     * @return String IP地址字符串，如果未连接则为 "0.0.0.0"。
     */
    String getIPAddress();

private:
    // 私有构造函数
    Sys_WiFiManager();
    
    // WiFi事件的静态回调函数
    static void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info);

    // 内部启动STA和AP的辅助函数
    void startSTA(const SystemSettings& settings);
    void startAP(const SystemSettings& settings);
    void stopSTA();
    void stopAP();

    // 单例实例指针
    static Sys_WiFiManager* _instance;
    
    // 内部状态机
    volatile WiFiState _currentState = WiFiState::DISABLED; // [优化] 使用 volatile 确保多线程可见性
    
    // [优化] 引入互斥锁，保护所有临界区
    SemaphoreHandle_t _mutex = NULL;
    
    // 非阻塞重连逻辑所需的计时器和计数器
    unsigned long _last_reconnect_attempt_ms = 0;
    uint8_t _sta_retry_count = 0; // [优化] STA重试计数器
};
