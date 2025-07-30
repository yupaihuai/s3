/**
 * @file Sys_WiFiManager.cpp
 * @brief WiFi管理器的健壮性实现
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 */
#include "Sys_WiFiManager.h"
#include "Sys_Debug.h"

// [优化] 定义重连相关的常量
static constexpr const uint32_t RECONNECT_INTERVAL_MS = 10000; // 10秒重连间隔
static constexpr const uint8_t MAX_STA_RETRIES = 3;            // 对于永久性错误，最多重试3次

Sys_WiFiManager* Sys_WiFiManager::_instance = nullptr;

Sys_WiFiManager* Sys_WiFiManager::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_WiFiManager();
    }
    return _instance;
}

Sys_WiFiManager::Sys_WiFiManager() {
    // [优化] 在构造函数中创建互斥锁
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        ESP_LOGE("WiFiMan", "FATAL: Failed to create mutex!");
    }
}

void Sys_WiFiManager::begin() {
    // 关键：让静态回调能找到实例。这必须在注册回调之前完成。
    _instance = this; 
    WiFi.onEvent(WiFiEvent); // 注册统一的事件回调
    
    // 首次启动时，应用一次当前配置
    applySettings();
}

void Sys_WiFiManager::applySettings() {
    Sys_LockGuard lock(_mutex); // [优化] 保护整个应用过程，确保原子性

    DEBUG_LOG("Applying new WiFi settings...");
    const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
    
    // 重置永久失败状态和重试计数器，因为我们有了新的配置
    if (_currentState == WiFiState::FAILED_PERMANENTLY) {
        _currentState = WiFiState::WIFI_STATE_DISABLED;
    }
    _sta_retry_count = 0;

    // 设置WiFi模式。这是启动STA/AP的前提。
    WiFi.mode((wifi_mode_t)settings.wifi_mode);

    // 根据新的模式，决定启动或停止STA/AP
    bool should_have_sta = (settings.wifi_mode == SystemSettings::WIFI_MODE_STA || settings.wifi_mode == SystemSettings::WIFI_MODE_AP_STA);
    bool should_have_ap  = (settings.wifi_mode == SystemSettings::WIFI_MODE_AP  || settings.wifi_mode == SystemSettings::WIFI_MODE_AP_STA);
    
    // 触发动作，但不直接改变状态。状态由事件驱动。
    if (should_have_sta) {
        startSTA(settings);
    } else {
        stopSTA();
    }

    if (should_have_ap) {
        startAP(settings);
    } else {
        stopAP();
    }
    
    if (!should_have_sta && !should_have_ap) {
        // 这是唯一一个可以直接设置的状态，因为它不会触发事件
        _currentState = WiFiState::WIFI_STATE_DISABLED;
        ESP_LOGI("WiFiMan", "WiFi is now disabled.");
    }
}

void Sys_WiFiManager::update() {
    // update()只负责一件事情：处理非永久性断开后的超时重连
    if (getCurrentState() == WiFiState::DISCONNECTED) {
        if (millis() - _last_reconnect_attempt_ms > RECONNECT_INTERVAL_MS) {
            Sys_LockGuard lock(_mutex); // [优化] 保护重连动作
            ESP_LOGI("WiFiMan", "Reconnect timeout. Attempting to connect again...");
            const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
            WiFi.begin(settings.wifi_ssid, settings.wifi_password);
            // 不在此处改变状态，等待WIFI_STA_START事件
        }
    }
}

WiFiState Sys_WiFiManager::getCurrentState() {
    // 对于读取volatile变量，虽然是原子操作，但加锁可以保证获取到的是一个更一致的快照
    Sys_LockGuard lock(_mutex);
    return _currentState;
}

String Sys_WiFiManager::getIPAddress() {
    Sys_LockGuard lock(_mutex);
    if (_currentState == WiFiState::CONNECTED_STA || _currentState == WiFiState::HOSTING_AP_STA) {
        return WiFi.localIP().toString();
    }
    if (_currentState == WiFiState::HOSTING_AP) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

// --- Private Helper Methods (仅由 applySettings 调用，已在锁的保护下) ---

void Sys_WiFiManager::startSTA(const SystemSettings& settings) {
    if (strlen(settings.wifi_ssid) > 0) {
        ESP_LOGI("WiFiMan", "Triggering STA mode for SSID: %s", settings.wifi_ssid);
        if (settings.wifi_static_ip_enabled) {
            IPAddress staticIP, gateway, subnet;
            if (staticIP.fromString(settings.wifi_static_ip) && gateway.fromString(settings.wifi_gateway) && subnet.fromString(settings.wifi_subnet)) {
                WiFi.config(staticIP, gateway, subnet);
                 ESP_LOGI("WiFiMan", "Using static IP configuration.");
            } else {
                ESP_LOGW("WiFiMan", "Invalid static IP configuration, falling back to DHCP.");
            }
        }
        WiFi.begin(settings.wifi_ssid, settings.wifi_password);
    } else {
        ESP_LOGW("WiFiMan", "STA mode enabled, but no SSID configured.");
        stopSTA();
    }
}

void Sys_WiFiManager::startAP(const SystemSettings& settings) {
    const char* ap_ssid = "ESP32S3-Device"; // Can be read from settings in the future
    ESP_LOGI("WiFiMan", "Triggering AP mode with SSID: %s", ap_ssid);
    WiFi.softAP(ap_ssid);
}

void Sys_WiFiManager::stopSTA() {
    if (WiFi.isConnected()) {
        WiFi.disconnect(true);
    }
}

void Sys_WiFiManager::stopAP() {
    WiFi.softAPdisconnect(true);
}

// --- Static Event Handler ---
// 这是整个模块的核心驱动力，所有状态转换都在这里发生
void Sys_WiFiManager::WiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
    if (!_instance) return;

    // 保护事件处理的全过程，防止与 applySettings 等发生冲突
    Sys_LockGuard lock(_instance->_mutex); 

    DEBUG_LOG("WiFi Event received: %d", event);

    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            ESP_LOGI("WiFiMan", "AP Mode Started. IP: %s", WiFi.softAPIP().toString().c_str());
            _instance->_currentState = WiFi.isConnected() ? WiFiState::HOSTING_AP_STA : WiFiState::HOSTING_AP;
            break;

        case ARDUINO_EVENT_WIFI_AP_STOP:
            ESP_LOGI("WiFiMan", "AP Mode Stopped.");
            // 如果STA也未连接，则为完全禁用
            if (!WiFi.isConnected()) {
                _instance->_currentState = WiFiState::WIFI_STATE_DISABLED;
            } else {
                 _instance->_currentState = WiFiState::CONNECTED_STA;
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_START:
            ESP_LOGI("WiFiMan", "STA Mode Started. Connecting...");
            _instance->_currentState = WiFiState::CONNECTING;
            _instance->_last_reconnect_attempt_ms = millis();
            break;
            
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI("WiFiMan", "STA Connected to SSID: %s. Waiting for IP...", (char*)info.wifi_sta_connected.ssid);
            // 状态仍然是CONNECTING，直到获取IP
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI("WiFiMan", "STA Got IP: %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
            _instance->_sta_retry_count = 0; // 连接成功，重置重试计数器
            _instance->_currentState = WiFi.softAPgetStationNum() > 0 ? WiFiState::HOSTING_AP_STA : WiFiState::CONNECTED_STA;
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            wifi_err_reason_t reason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
            ESP_LOGW("WiFiMan", "STA Disconnected. Reason: %d (%s)", reason, WiFi.disconnectReasonName(reason));

            // [优化] 智能重连逻辑
            // 检查是否是“永久性”错误
            if (reason == WIFI_REASON_NO_AP_FOUND || reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_AUTH_FAIL) {
                _instance->_sta_retry_count++;
                ESP_LOGW("WiFiMan", "Permanent-like failure. Retry attempt %d/%d.", _instance->_sta_retry_count, MAX_STA_RETRIES);
                if (_instance->_sta_retry_count >= MAX_STA_RETRIES) {
                    ESP_LOGE("WiFiMan", "Max retries reached. Entering permanent failure state.");
                    _instance->_currentState = WiFiState::FAILED_PERMANENTLY;
                    // 在这里停止，不再尝试重连
                    break; 
                }
            }
            // 对于其他临时性断开或未达到重试上限的永久性错误，进入常规重连流程
            _instance->_currentState = WiFiState::DISCONNECTED;
            _instance->_last_reconnect_attempt_ms = millis(); 
            break;
        }
        
        default:
            break;
    }
}
