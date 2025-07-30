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
 *   硬件层 -> NVS -> 设置 -> 内存 -> 文件系统 -> 日志 -> 网络 -> 任务调度
 */

#include <Arduino.h>

// --- 核心系统模块头文件 ---
// 按照初始化依赖顺序包含
#include "Sys_NvsManager.h"
#include "Sys_SettingsManager.h"
#include "Sys_MemoryManager.h"
#include "Sys_Filesystem.h"
#include "Sys_FlashLogger.h"
#include "Sys_WiFiManager.h"
#include "Sys_BlueToothManager.h"
#include "Sys_WebServer.h"
#include "Sys_Tasks.h"

// --- 调试与诊断工具 ---
#include "Sys_Debug.h"
#include "Sys_Diagnostics.h"

// 定义固件版本号
#define FIRMWARE_VERSION "5.5.1"

/**
 * @brief 系统启动函数 (setup)
 *
 * 在这里，系统以单线程模式运行，是进行所有模块初始化和资源分配的
 * 安全时机。所有单例模式的 `getInstance()` 首次调用都在此完成。
 */
void setup() {
    // 步骤 1: 初始化物理串口，用于早期调试日志输出
    Serial.begin(115200);
    delay(1000); // 给予串口监视器足够的时间来连接
    ESP_LOGI("Boot", "\n\n--- ESP32-S3 Modular Management System Booting ---");

    // 步骤 2: 初始化NVS（非易失性存储）
    // 这是所有配置管理的基础，必须最先执行。
    ESP_LOGI("Boot", "[1/9] Initializing NVS Manager...");
    Sys_NvsManager::initialize();

    // 步骤 3: 初始化设置管理器
    // 它依赖NVS来加载配置，并执行健壮的迁移逻辑。
    ESP_LOGI("Boot", "[2/9] Initializing Settings Manager...");
    Sys_SettingsManager::getInstance()->begin();

    // 步骤 4: 初始化内存管理器
    // 为文件系统和其他模块准备内存池和PSRAM堆。
    ESP_LOGI("Boot", "[3/9] Initializing Memory Manager...");
    Sys_MemoryManager::getInstance()->initializePools();

    // 步骤 5: 初始化文件系统
    // 挂载LittleFS和FFat，为Web服务器和日志系统做准备。
    ESP_LOGI("Boot", "[4/9] Initializing Filesystem...");
    Sys_Filesystem::getInstance()->begin();

    // 步骤 6: 初始化闪存日志系统
    // 它依赖文件系统来存储日志。
    ESP_LOGI("Boot", "[5/9] Initializing Flash Logger...");
    Sys_FlashLogger::getInstance()->begin("/sys/system.log");

    // 步骤 7: 初始化WiFi管理器
    // 它会读取NVS中的WiFi配置并尝试自动连接。
    ESP_LOGI("Boot", "[6/9] Initializing WiFi Manager...");
    Sys_WiFiManager::getInstance()->begin();
    
    // 步骤 8: 初始化Web服务器
    // 它依赖文件系统和网络服务。
    ESP_LOGI("Boot", "[7/9] Initializing Web Server...");
    Sys_WebServer::getInstance()->begin();

    // 步骤 9: 初始化蓝牙管理器 (如果启用)
    ESP_LOGI("Boot", "[8/9] Initializing BlueTooth Manager...");
    Sys_BlueToothManager::getInstance()->begin();

    // 步骤 10: 创建所有后台服务任务
    // 这是最后一步，在所有基础服务都初始化完毕后，启动系统的“大脑”。
    ESP_LOGI("Boot", "[9/9] Creating all background tasks...");
    Sys_Tasks::begin(Sys_WebServer::getInstance()->getWebSocket());

    ESP_LOGI("Boot", "--- System Initialization Complete. Handing over to FreeRTOS... ---");

    // [日志] 记录系统启动成功事件
    Sys_FlashLogger::getInstance()->log("[Main]", "System booted successfully. Version: %s", FIRMWARE_VERSION);

    // [按需调试] 如果需要，可以在这里运行一次诊断报告
    #if CORE_DEBUG_MODE
        DEBUG_LOG("Running post-boot diagnostics report...");
        Sys_Diagnostics::run();
    #endif
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
    // 在我们的多任务设计中，loop() 函数的传统作用已被各个后台任务取代。
    // 它实际上不会被频繁执行。让它挂起，将CPU控制权完全让渡给FreeRTOS调度器。
    vTaskDelay(portMAX_DELAY);
}