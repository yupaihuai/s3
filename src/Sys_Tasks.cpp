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
    xCommandQueue = xQueueCreate(10, sizeof(JsonRpcRequest));
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
    JsonRpcRequest request;

    for (;;) {
        // [优化] 每次循环前“喂狗”，表示任务还活着
        esp_task_wdt_reset();

        if (xQueueReceive(xCommandQueue, &request, portMAX_DELAY) == pdPASS) {
            DEBUG_LOG("Worker received RPC method: %s from client #%u", request.method, request.client_id);
            processJsonRpcRequest(request);
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
        // 4. 采集系统状态并打包为JSON RPC 2.0通知
        JsonDocument doc;
        doc["jsonrpc"] = "2.0";
        doc["method"] = "system.stateUpdate";
        JsonObject params = doc["params"].to<JsonObject>();
        params["uptime"] = millis();
        params["free_heap"] = ESP.getFreeHeap();
        params["free_psram"] = ESP.getFreePsram();
        params["wifi_state"] = (int)Sys_WiFiManager::getInstance()->getCurrentState();

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
                // 检查消息是响应还是通知
                JsonDocument doc;
                deserializeJson(doc, messageBuffer);
                if (!doc["id"].isNull()) { // 这是一个响应
                    uint32_t client_id = doc["client_id"];
                    doc.remove("client_id"); // 从最终发送的JSON中移除内部client_id
                    String response;
                    serializeJson(doc, response);
                    webSocket->text(client_id, response);
                } else { // 这是一个通知
                    webSocket->textAll(messageBuffer);
                }
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

/**
 * @brief 响应一个JSON RPC请求的辅助函数。
 * @param request 原始请求，用于获取id和client_id。
 * @param result 要包含在响应中的`result`字段的JSON文档。
 */
static void sendRpcResult(const JsonRpcRequest& request, JsonDocument& result) {
    JsonDocument response_doc;
    response_doc["jsonrpc"] = "2.0";
    response_doc["result"] = result.as<JsonVariant>();
    response_doc["id"] = request.id;
    response_doc["client_id"] = request.client_id; // 内部使用，用于pusher任务路由

    char jsonBuffer[1024];
    serializeJson(response_doc, jsonBuffer, sizeof(jsonBuffer));
    if (xQueueSend(xStateQueue, &jsonBuffer, 0) == pdPASS) {
        xEventGroupSetBits(xDataEventGroup, BIT_STATE_QUEUE_READY);
    } else {
        ESP_LOGW(TASK_WORKER_NAME, "State queue full. RPC response dropped for method %s.", request.method);
    }
}

/**
 * @brief 响应一个JSON RPC错误的辅助函数。
 */
static void sendRpcError(const JsonRpcRequest& request, int code, const char* message) {
    JsonDocument response_doc;
    response_doc["jsonrpc"] = "2.0";
    JsonObject error_obj = response_doc["error"].to<JsonObject>();
    error_obj["code"] = code;
    error_obj["message"] = message;
    response_doc["id"] = request.id;
    response_doc["client_id"] = request.client_id;

    char jsonBuffer[256];
    serializeJson(response_doc, jsonBuffer, sizeof(jsonBuffer));
    if (xQueueSend(xStateQueue, &jsonBuffer, 0) == pdPASS) {
        xEventGroupSetBits(xDataEventGroup, BIT_STATE_QUEUE_READY);
    }
}


void Sys_Tasks::processJsonRpcRequest(const JsonRpcRequest& request) {
    JsonDocument params_doc;
    deserializeJson(params_doc, request.params);

    // --- 系统命令 ---
    if (strcmp(request.method, "system.reboot") == 0) {
        JsonDocument result_doc;
        result_doc["status"] = "rebooting";
        sendRpcResult(request, result_doc);
        delay(1000);
        ESP.restart();
    }
    else if (strcmp(request.method, "system.factoryReset") == 0) {
        JsonDocument result_doc;
        result_doc["status"] = "resetting";
        sendRpcResult(request, result_doc);
        Sys_SettingsManager::getInstance()->factoryReset();
        delay(1000);
        ESP.restart();
    }
    // --- 设置管理 ---
    else if (strcmp(request.method, "settings.get") == 0) {
        const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
        JsonDocument result_doc;
        JsonObject wifi_obj = result_doc["wifi"].to<JsonObject>();
        wifi_obj["ssid"] = settings.wifi_ssid;
        wifi_obj["mode"] = (int)settings.wifi_mode;
        JsonObject bt_obj = result_doc["bluetooth"].to<JsonObject>();
        bt_obj["deviceName"] = settings.bluetooth_name;
        bt_obj["enabled"] = settings.bluetooth_enabled;
        sendRpcResult(request, result_doc);
    }
    else if (strcmp(request.method, "settings.saveWiFi") == 0) {
        const char* ssid = params_doc["ssid"];
        const char* password = params_doc["password"];
        if (ssid) {
            Sys_SettingsManager::getInstance()->setWiFiConfig(ssid, password ? password : "", (SystemSettings::WiFiMode)params_doc["mode"].as<int>());
            Sys_WiFiManager::getInstance()->applySettings();
            JsonDocument result_doc;
            result_doc["status"] = "success";
            sendRpcResult(request, result_doc);
        } else {
            sendRpcError(request, -32602, "Invalid params: missing ssid");
        }
    }
    else if (strcmp(request.method, "settings.saveBluetooth") == 0) {
        const char* name = params_doc["deviceName"];
        if (name) {
            Sys_SettingsManager::getInstance()->setBluetoothConfig(params_doc["enabled"], name);
            Sys_BlueToothManager::getInstance()->applySettings();
            JsonDocument result_doc;
            result_doc["status"] = "success";
            sendRpcResult(request, result_doc);
        } else {
            sendRpcError(request, -32602, "Invalid params: missing deviceName");
        }
    }
    // --- WiFi管理 ---
    else if (strcmp(request.method, "wifi.scan") == 0) {
        JsonDocument result_doc;
        result_doc["status"] = "scanning";
        sendRpcResult(request, result_doc); // 立即响应，告知客户端扫描已开始

        int n = WiFi.scanNetworks(false, true);
        ESP_LOGI(TASK_WORKER_NAME, "Scan finished. Found %d networks.", n);

        JsonDocument scan_result_doc;
        scan_result_doc["jsonrpc"] = "2.0";
        scan_result_doc["method"] = "wifi.scanResult";
        JsonArray networks = scan_result_doc["params"].to<JsonArray>();
        for (int i = 0; i < n; ++i) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["auth"] = WiFi.encryptionType(i);
        }
        
        char jsonBuffer[1024];
        serializeJson(scan_result_doc, jsonBuffer, sizeof(jsonBuffer));
        if (xQueueSend(xStateQueue, &jsonBuffer, 0) == pdPASS) {
            xEventGroupSetBits(xDataEventGroup, BIT_STATE_QUEUE_READY);
        } else {
            ESP_LOGW(TASK_WORKER_NAME, "State queue full. WiFi scan result dropped.");
        }
        WiFi.scanDelete();
    }
    // --- 调试命令 ---
    #if CORE_DEBUG_MODE
    else if (strcmp(request.method, "debug.runDiagnostics") == 0) {
        ESP_LOGI(TASK_WORKER_NAME, "Processing RUN_DIAGNOSTICS command...");
        Sys_Diagnostics::run();
        JsonDocument result_doc;
        result_doc["status"] = "completed";
        sendRpcResult(request, result_doc);
    }
    #endif
    else {
        sendRpcError(request, -32601, "Method not found");
    }
}