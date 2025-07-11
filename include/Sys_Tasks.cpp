/**
 * @file Sys_Tasks.cpp
 * @brief 系统核心任务的实现文件
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 *
 * @details
 * 该文件集中实现了项目中所有的后台FreeRTOS任务，并引入了任务看门狗(TWDT)
 * 机制来监控关键任务的健康状况，提升系统的健壮性。
 * 它扮演着系统“神经中枢”的角色，负责：
 * 1. 初始化任务间通信所需的所有句柄（Queues, Event Groups）。
 * 2. 创建并启动所有后台任务，并将它们固定在指定的CPU核心上。
 * 3. 定义每个任务的核心循环逻辑，协调各个管理器模块完成具体工作。
 */

#include "Sys_Tasks.h"
#include "types.h"
#include "Sys_Debug.h"

// --- 模块依赖 ---
#include "Sys_SettingsManager.h"
#include "Sys_WiFiManager.h"
#include "Sys_BlueToothManager.h"
#include "Sys_Diagnostics.h"

// --- 第三方库依赖 ---
#include "ArduinoJson.h"

// --- ESP-IDF 核心依赖 ---
#include "esp_task_wdt.h" // [优化] 引入任务看门狗头文件

// --- 全局通信句柄的定义 ---
QueueHandle_t xCommandQueue = NULL;
QueueHandle_t xStateQueue = NULL;
EventGroupHandle_t xDataEventGroup = NULL;

// [优化] 定义看门狗超时时间（秒）
static constexpr const uint32_t TASK_WDT_TIMEOUT_S = 15;

/**
 * @brief 初始化所有通信句柄、看门狗并创建所有系统任务。
 */
void Sys_Tasks::begin(AsyncWebSocket* webSocket) {
    DEBUG_LOG("Initializing system tasks and communication handles...");

    // 步骤 1: 创建通信句柄
    xCommandQueue = xQueueCreate(10, sizeof(Command));
    xStateQueue = xQueueCreate(20, sizeof(char[1024]));
    xDataEventGroup = xEventGroupCreate();

    if (!xCommandQueue || !xStateQueue || !xDataEventGroup) {
        ESP_LOGE("Tasks", "FATAL: Failed to create communication handles!");
        return;
    }

    // 步骤 2: [优化] 初始化任务看门狗
    ESP_LOGI("Tasks", "Initializing Task Watchdog Timer with %d seconds timeout.", TASK_WDT_TIMEOUT_S);
    ESP_ERROR_CHECK(esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true)); // true 表示 panic on timeout
    
    // 订阅当前任务(setup/loop)到看门狗，防止在任务启动前超时
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); 
    esp_task_wdt_reset();

    // 步骤 3: 创建并启动所有后台任务
    TaskHandle_t worker_handle;
    xTaskCreatePinnedToCore(taskWorkerLoop, TASK_WORKER_NAME, TASK_WORKER_STACK_SIZE, NULL, TASK_WORKER_PRIORITY, &worker_handle, TASK_WORKER_CORE);
    xTaskCreatePinnedToCore(taskSystemMonitorLoop, TASK_MONITOR_NAME, TASK_MONITOR_STACK_SIZE, NULL, TASK_MONITOR_PRIORITY, NULL, TASK_MONITOR_CORE);
    xTaskCreatePinnedToCore(taskWebSocketPusherLoop, TASK_PUSHER_NAME, TASK_PUSHER_STACK_SIZE, (void*)webSocket, TASK_PUSHER_PRIORITY, NULL, TASK_PUSHER_CORE);
    
    // [优化] 将关键的Task_Worker注册到看门狗
    ESP_ERROR_CHECK(esp_task_wdt_add(worker_handle));

    ESP_LOGI("Tasks", "All system tasks created successfully.");

    // 取消订阅当前任务(setup/loop)，因为它的生命周期即将结束
    esp_task_wdt_delete(NULL);
}

// =================================================================================================
// 任务循环实现 (Task Loop Implementations)
// =================================================================================================

/**
 * @brief Task_Worker 的核心循环函数。
 * @details
 *  - 这是一个被看门狗监控的关键任务。
 *  - 它永远阻塞等待新命令，收到后分发给具体的处理函数。
 */
void Sys_Tasks::taskWorkerLoop(void* parameter) {
    ESP_LOGI(TASK_WORKER_NAME, "Task starting... Now monitored by TWDT.");
    Command cmd;

    for (;;) {
        // [优化] 每次循环前“喂狗”，表示任务还活着
        esp_task_wdt_reset();

        if (xQueueReceive(xCommandQueue, &cmd, portMAX_DELAY) == pdPASS) {
            DEBUG_LOG("Worker received command type: %d", cmd.type);

            // [优化] 将处理逻辑分发到独立的函数中
            switch (cmd.type) {
                case Command::SAVE_WIFI:      processSaveWifi(cmd.payload); break;
                case Command::SAVE_BLE:       processSaveBle(cmd.payload); break;
                case Command::SCAN_WIFI:      processScanWifi(); break;
                case Command::REBOOT:         processReboot(); break;
                case Command::FACTORY_RESET:  processFactoryReset(); break;
                #if CORE_DEBUG_MODE
                case Command::RUN_DIAGNOSTICS: processRunDiagnostics(); break;
                #endif
                default:
                    ESP_LOGW(TASK_WORKER_NAME, "Unknown command received: %d", cmd.type);
                    break;
            }
        }
    }
}


/**
 * @brief Task_SystemMonitor 的核心循环函数。
 */
void Sys_Tasks::taskSystemMonitorLoop(void* parameter) {
    ESP_LOGI(TASK_MONITOR_NAME, "Task starting...");
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        DEBUG_LOG("System Monitor tick...");

        // 1. 更新WiFi状态机
        Sys_WiFiManager::getInstance()->update();
        
        // 2. 更新蓝牙状态机
        Sys_BlueToothManager::getInstance()->update();

        // 3. 提交“脏”的设置
        Sys_SettingsManager::getInstance()->commit();

        // 4. 采集系统状态并打包JSON
        JsonDocument doc;
        doc["type"] = "system_status";
        JsonObject data = doc["data"].to<JsonObject>();
        data["uptime"] = millis();
        data["free_heap"] = ESP.getFreeHeap();
        data["free_psram"] = ESP.getFreePsram();
        data["wifi_state"] = (int)Sys_WiFiManager::getInstance()->getCurrentState();

        char jsonBuffer[512];
        serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

        // [优化] 检查队列发送结果
        if (xQueueSend(xStateQueue, &jsonBuffer, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGW(TASK_MONITOR_NAME, "State queue is full. Status update dropped.");
        } else {
            xEventGroupSetBits(xDataEventGroup, BIT_STATE_QUEUE_READY);
        }
    }
}

/**
 * @brief Task_WebSocketPusher 的核心循环函数。
 */
void Sys_Tasks::taskWebSocketPusherLoop(void* parameter) {
    ESP_LOGI(TASK_PUSHER_NAME, "Task starting...");
    AsyncWebSocket* webSocket = (AsyncWebSocket*)parameter;

    if (!webSocket) {
        ESP_LOGE(TASK_PUSHER_NAME, "FATAL: WebSocket instance is NULL!");
        vTaskDelete(NULL);
    }
    
    char messageBuffer[1024];

    for (;;) {
        const EventBits_t bits = xEventGroupWaitBits(
            xDataEventGroup,
            BIT_STATE_QUEUE_READY | BIT_LOG_QUEUE_READY,
            pdTRUE, pdFALSE, portMAX_DELAY);

        // [优化] 只有在有客户端连接时才处理推送
        if (webSocket->count() == 0) {
            // 清空队列，防止消息堆积
            while (xQueueReceive(xStateQueue, &messageBuffer, 0) == pdPASS) {}
            continue; // 跳过本次推送
        }

        if (bits & BIT_STATE_QUEUE_READY) {
            DEBUG_LOG("Pusher woken by state queue event.");
            while (xQueueReceive(xStateQueue, &messageBuffer, 0) == pdPASS) {
                webSocket->textAll(messageBuffer);
            }
        }

        if (bits & BIT_LOG_QUEUE_READY) {
            // ... 未来的日志队列处理逻辑 ...
        }
    }
}


// =================================================================================================
// 命令处理函数实现 (Command Handler Implementations)
// =================================================================================================

void Sys_Tasks::processSaveWifi(const char* payload) {
    ESP_LOGI(TASK_WORKER_NAME, "Processing SAVE_WIFI command...");
    JsonDocument doc;
    deserializeJson(doc, payload);
    
    // 使用 .c_str() 确保传递的是 const char*
    Sys_SettingsManager::getInstance()->setWiFiConfig(doc["ssid"], doc["pass"], (SystemSettings::WiFiMode)doc["mode"].as<int>());
    Sys_WiFiManager::getInstance()->applySettings();
}

void Sys_Tasks::processSaveBle(const char* payload) {
    ESP_LOGI(TASK_WORKER_NAME, "Processing SAVE_BLE command...");
    JsonDocument doc;
    deserializeJson(doc, payload);
    Sys_SettingsManager::getInstance()->setBluetoothConfig(doc["enabled"], doc["name"]);
    Sys_BlueToothManager::getInstance()->applySettings();
}

void Sys_Tasks::processScanWifi() {
    ESP_LOGI(TASK_WORKER_NAME, "Processing SCAN_WIFI command...");
    int n = WiFi.scanNetworks(false, true);
    ESP_LOGI(TASK_WORKER_NAME, "Scan finished. Found %d networks.", n);

    JsonDocument doc;
    doc["type"] = "wifi_scan_result";
    JsonArray networks = doc["data"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["auth"] = WiFi.encryptionType(i);
    }
    
    char jsonBuffer[1024];
    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    if (xQueueSend(xStateQueue, &jsonBuffer, 0) == pdPASS) {
        xEventGroupSetBits(xDataEventGroup, BIT_STATE_QUEUE_READY);
    } else {
        ESP_LOGW(TASK_WORKER_NAME, "State queue full. WiFi scan result dropped.");
    }
    WiFi.scanDelete();
}

void Sys_Tasks::processReboot() {
    ESP_LOGW(TASK_WORKER_NAME, "Rebooting system now...");
    Sys_SettingsManager::getInstance()->forceSave(); // 确保所有挂起的设置都已保存
    delay(1000);
    ESP.restart();
}

void Sys_Tasks::processFactoryReset() {
    ESP_LOGW(TASK_WORKER_NAME, "Performing factory reset...");
    Sys_SettingsManager::getInstance()->factoryReset();
    delay(1000);
    ESP.restart();
}

#if CORE_DEBUG_MODE
void Sys_Tasks::processRunDiagnostics() {
    ESP_LOGI(TASK_WORKER_NAME, "Processing RUN_DIAGNOSTICS command...");
    Sys_Diagnostics::run();
}
#endif