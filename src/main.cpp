/**
 * @file main.cpp
 * @brief 项目主入口文件
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 本文件是整个嵌入式系统的启动程序。它的核心职责是：
 * 1. 在单线程的 `setup()` 环境中，按照严格的依赖顺序初始化所有核心服务模块。
 * 2. 启动FreeRTOS任务调度器，将系统从单线程模式切换到多任务并发模式。
 * 3. `loop()` 函数在多任务启动后将不再被主要使用，系统的心跳和业务逻辑
 *    完全由后台的FreeRTOS任务驱动。
 *
 * 初始化顺序至关重要，遵循以下逻辑：
 *   硬件层 -> 文件系统 -> 内存管理 -> 配置管理 -> 网络服务 -> 任务调度
 */

#include <Arduino.h>

// --- 核心系统模块头文件 ---
// 按照初始化依赖顺序包含
#include "Sys_NvsManager.h"
#include "Sys_Filesystem.h"
#include "Sys_MemoryManager.h"
#include "Sys_SettingsManager.h"
#include "Sys_FlashLogger.h"      // [可选] 如果您希望尽早开始记录日志
#include "Sys_WiFiManager.h"
#include "Sys_BlueToothManager.h"
#include "Sys_WebServer.h"
#include "Sys_Tasks.h"

// --- 调试与诊断工具 ---
#include "Sys_Debug.h"
#include "Sys_Diagnostics.h"

/**
 * @brief 系统启动函数 (setup)
 *
 * 在这里，系统以单线程模式运行，是进行所有模块初始化和资源分配的
 * 安全时机。所有单例模式的 `getInstance()` 首次调用都在此完成。
 */
void setup() {
    // 1. 初始化物理串口，用于日志输出和调试
    Serial.begin(115200);
    delay(1000); // 给予串口监视器足够的时间来连接
    ESP_LOGI("Boot", "\n\n--- ESP32-S3 Modular Management System Booting ---");

    // 2. [阶段0] 初始化硬件抽象层和基础服务
    ESP_LOGI("Boot", "Phase 0: Initializing Hardware Abstraction Layer...");
    Sys_NvsManager::initialize();
    Sys_Filesystem::getInstance()->begin();
    Sys_MemoryManager::getInstance()->initializePools();

    // 3. [可选] 初始化Flash日志记录器。一旦文件系统就绪，就可以开始记录持久化日志。
    Sys_FlashLogger::getInstance()->begin("/media/system.log");
    Sys_FlashLogger::getInstance()->log("System boot sequence started.");

    // 4. [阶段1] 初始化核心服务与配置管理
    ESP_LOGI("Boot", "Phase 1: Initializing Core Services...");
    Sys_SettingsManager::getInstance()->begin();
    Sys_WiFiManager::getInstance()->begin();
    Sys_BlueToothManager::getInstance()->begin(); // 初始化蓝牙管理器

    // 5. [阶段2] 初始化网络通信管道
    ESP_LOGI("Boot", "Phase 2: Initializing Communication Conduit...");
    Sys_WebServer::getInstance()->begin();

    // 6. [阶段3] 启动所有后台任务，将系统转换为多任务模式
    ESP_LOGI("Boot", "Phase 3: Starting all FreeRTOS tasks...");
    // 关键一步：将WebServer中的WebSocket实例指针传递给任务管理器
    Sys_Tasks::begin(Sys_WebServer::getInstance()->getWebSocket());

    ESP_LOGI("Boot", "--- Boot sequence complete. System is now running. ---");

    // [按需调试] 如果需要，可以在这里运行一次诊断报告
    #if CORE_DEBUG_MODE
        DEBUG_LOG("Running post-boot diagnostics report...");
        Sys_Diagnostics::run();
        Sys_MemoryManager::getInstance()->printMemoryInfo();
    #endif

    // setup() 函数到此结束。之后，CPU控制权将完全由FreeRTOS调度器接管。
}

/**
 * @brief 主循环函数 (loop)
 *
 * 在我们的多任务设计中，`loop()` 函数的传统作用已被各个后台任务取代。
 * 它实际上不会被频繁执行，因为所有任务都使用 vTaskDelay 或其他阻塞API
 * 来出让CPU时间。
 *
 * 保留此函数是为了遵循Arduino框架的规范。我们可以让它挂起或进入低功耗状态。
 */
void loop() {
    // 将此“任务”置于最低优先级并长时间挂起，以确保所有资源都留给
    // 我们自己创建的、更重要的后台任务。
    vTaskDelay(pdMS_TO_TICKS(10000)); // 每10秒象征性地运行一次
}