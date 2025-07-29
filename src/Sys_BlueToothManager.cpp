/**
 * @file Sys_BlueToothManager.cpp
 * @brief 系统蓝牙管理器的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 实现了蓝牙协议栈的初始化、配置应用逻辑和事件回调处理。
 * 它演示了如何与ESP-IDF的蓝牙API进行交互。
 */
#include "Sys_BlueToothManager.h"
#include "Sys_Debug.h"
#include "esp_gap_bt_api.h" // 引入经典蓝牙(BT Classic)的GAP API

// 初始化静态单例指针
Sys_BlueToothManager* Sys_BlueToothManager::_instance = nullptr;

/**
 * @brief 获取蓝牙管理器的单例实例。
 */
Sys_BlueToothManager* Sys_BlueToothManager::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_BlueToothManager();
    }
    return _instance;
}

/**
 * @brief 模块初始化。
 */
void Sys_BlueToothManager::begin() {
    _instance = this; // 关键一步：将this指针赋给静态实例，以便静态回调函数能访问到非静态成员

    // 步骤1：初始化蓝牙底层控制器(Controller)
    // 这是所有蓝牙操作（包括BLE和Classic）的基础
    if (btStarted()) {
        DEBUG_LOG("Bluetooth controller already started.");
    } else {
        if (!btStart()) {
            ESP_LOGE("BTMan", "Failed to start Bluetooth controller!"); // 错误: 启动蓝牙控制器失败！
            _currentState = BlueToothState::UNINITIALIZED;
            return;
        }
    }
    
    // 步骤2：初始化Bluedroid协议栈
    // Bluedroid是支持Classic BT和BLE的功能完整的协议栈
    esp_err_t ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE("BTMan", "Failed to initialize Bluedroid: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE("BTMan", "Failed to enable Bluedroid: %s", esp_err_to_name(ret));
        return;
    }
    
    // 步骤3：注册GAP事件回调函数
    // GAP(Generic Access Profile)事件处理设备发现、连接和安全等
    esp_bt_gap_register_callback(esp_bt_gap_cb);

    // 步骤4：初始化完成后，立即应用一次当前的系统配置
    applySettings();
}

/**
 * @brief 周期性更新函数。
 */
void Sys_BlueToothManager::update() {
    // 当前版本，所有逻辑都是事件驱动的，此函数暂时为空。
    // 未来可在这里添加需要轮询处理的逻辑，例如：
    // - 检查A2DP流的超时
    // - 管理一个懒加载的连接池
}

/**
 * @brief 应用最新的系统设置。
 */
void Sys_BlueToothManager::applySettings() {
    DEBUG_LOG("Applying new Bluetooth settings...");
    const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
    
    // --- 逻辑1: 处理蓝牙总开关 ---
    bool shouldBeEnabled = settings.bluetooth_enabled;
    bool isEnabled = (_currentState == BlueToothState::ENABLED);

    if (shouldBeEnabled && !isEnabled) {
        // 配置要求启用，但当前未启用 -> 执行启用流程
        enableBlueTooth();
    } else if (!shouldBeEnabled && isEnabled) {
        // 配置要求禁用，但当前已启用 -> 执行禁用流程
        disableBlueTooth();
    }

    // --- 逻辑2: 如果蓝牙是启用的，则更新设备名称 ---
    if (shouldBeEnabled) {
        // 比较内存中的名称和配置中的名称，只有不同时才更新，避免不必要的操作
        if (strcmp(_currentDeviceName, settings.bluetooth_name) != 0) {
            setDeviceName(settings.bluetooth_name);
        }
    }
}

/**
 * @brief 获取当前状态。
 */
BlueToothState Sys_BlueToothManager::getCurrentState() const {
    return _currentState;
}

// --- 私有辅助方法 (Private Methods) ---

/**
 * @brief 异步启用蓝牙。
 */
bool Sys_BlueToothManager::enableBlueTooth() {
    if (_currentState == BlueToothState::ENABLED || _currentState == BlueToothState::ENABLING) {
        return true; // 已经是启用或正在启用状态，无需重复操作
    }
    ESP_LOGI("BTMan", "Enabling Bluedroid stack...");
    _currentState = BlueToothState::ENABLING; // 进入“正在启用”状态

    // 这是一个异步操作，结果将在事件回调中确认
    esp_err_t err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE("BTMan", "Failed to enable Bluedroid: %s", esp_err_to_name(err));
        _currentState = BlueToothState::BLUETOOTH_STATE_DISABLED; // 启用失败，回到禁用状态
        return false;
    }
    return true;
}

/**
 * @brief 异步禁用蓝牙。
 */
bool Sys_BlueToothManager::disableBlueTooth() {
    if (_currentState == BlueToothState::BLUETOOTH_STATE_DISABLED || _currentState == BlueToothState::DISABLING) {
        return true;
    }
    ESP_LOGI("BTMan", "Disabling Bluedroid stack...");
    _currentState = BlueToothState::DISABLING;

    esp_err_t err = esp_bluedroid_disable();
    if (err != ESP_OK) {
        ESP_LOGE("BTMan", "Failed to disable Bluedroid: %s", esp_err_to_name(err));
        _currentState = BlueToothState::ENABLED; // 禁用失败，回到启用状态
        return false;
    }
    // 成功发起禁用请求，状态将在回调中更新为DISABLED
    return true;
}

/**
 * @brief 设置蓝牙设备名称。
 */
bool Sys_BlueToothManager::setDeviceName(const char* name) {
    if (!name || strlen(name) == 0) return false;

    ESP_LOGI("BTMan", "Setting Bluetooth device name to: '%s'", name);
    esp_err_t err = esp_bt_dev_set_device_name(name); // 调用ESP-IDF API
    if (err == ESP_OK) {
        // 设置成功，更新内存缓存的名称
        strncpy(_currentDeviceName, name, sizeof(_currentDeviceName) - 1);
        _currentDeviceName[sizeof(_currentDeviceName) - 1] = '\0'; // 确保null结尾
        return true;
    } else {
        ESP_LOGE("BTMan", "Failed to set device name: %s", esp_err_to_name(err));
        return false;
    }
}

/**
 * @brief 静态GAP事件回调函数。
 */
void Sys_BlueToothManager::esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    if (!_instance) return; // 安全检查

    switch (event) {
        // 这是一个示例事件，当蓝牙协议栈准备好，并设置好扫描模式后触发
        // 我们可以以此为标志，认为蓝牙已完全启用
        case ESP_BT_GAP_SET_SCAN_MODE_EVT: {
            DEBUG_LOG("GAP Event: Scan mode set, Bluetooth is fully enabled.");
            _instance->_currentState = BlueToothState::ENABLED;
            break;
        }
        
        // 另一个示例：认证完成事件
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI("BTMan", "Authentication successful with a device.");
            } else {
                ESP_LOGE("BTMan", "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;
        }
        
        // 注意：ESP-IDF中，Bluedroid的启用/禁用完成没有一个特定的GAP事件。
        // 通常，调用enable/disable后，可以通过检查esp_bluedroid_get_status()或
        // 等待其他依赖的服务（如A2DP）的启动事件来确认。
        // 为简化，我们在这里主要依赖于调用函数的返回值和后续事件来管理状态。

        default:
            DEBUG_LOG("Unhandled GAP event: %d", event);
            break;
    }
}