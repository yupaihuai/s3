/**
 * @file Sys_BlueToothManager.cpp
 * @brief 系统蓝牙(BLE)管理器的实现文件 (基于NimBLE)
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  实现了基于NimBLE库的BLE协议栈初始化、广播管理和连接回调处理。
 *  ESP32-S3仅支持BLE，此模块专为此设计。
 */
#include "Sys_BlueToothManager.h"
#include "Sys_Debug.h"
#include "Sys_FlashLogger.h" // [新增] 引入闪存日志模块

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
    _instance = this; // 将this指针赋给静态实例，以便回调函数能访问到非静态成员
    
    DEBUG_LOG("Initializing NimBLE stack...");
    
    // 步骤1: 初始化NimBLE库
    // true表示初始化ESP控制器，false表示不初始化（如果已由其他组件初始化）
    NimBLEDevice::init(""); // 设备名称将在applySettings中设置
    
    // 步骤2: 创建一个新的BLE服务器
    _pServer = NimBLEDevice::createServer();
    if (!_pServer) {
        ESP_LOGE("BTMan", "Failed to create BLE Server!");
        _currentState = BlueToothState::UNINITIALIZED;
        return;
    }
    
    // 步骤3: 设置服务器回调
    // this指针指向的对象（即Sys_BlueToothManager实例）将处理连接和断开事件
    _pServer->setCallbacks(this);

    // 步骤4: 获取广播实例
    _pAdvertising = NimBLEDevice::getAdvertising();
    if (!_pAdvertising) {
        ESP_LOGE("BTMan", "Failed to get BLE Advertising instance!");
        _currentState = BlueToothState::UNINITIALIZED;
        return;
    }
    
    // [示例] 添加一个设备信息服务 (Device Information Service)
    // 这有助于BLE扫描器识别设备类型
    _pAdvertising->addServiceUUID(NimBLEUUID("180A"));

    _currentState = BlueToothState::BT_DISABLED; // 初始状态为已初始化但禁用

    // 步骤5: 初始化完成后，立即应用一次当前的系统配置
    applySettings();
}

/**
 * @brief 周期性更新函数。
 */
void Sys_BlueToothManager::update() {
    // NimBLE是事件驱动的，此函数暂时为空。
}

/**
 * @brief 应用最新的系统设置。
 */
void Sys_BlueToothManager::applySettings() {
    DEBUG_LOG("Applying new BLE settings...");
    const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
    
    bool shouldBeEnabled = settings.bluetooth_enabled;
    bool isAdvertising = (_currentState == BlueToothState::ADVERTISING);

    // 更新设备名称
    setDeviceName(settings.bluetooth_name);

    if (shouldBeEnabled && !isAdvertising) {
        startAdvertising();
    } else if (!shouldBeEnabled && isAdvertising) {
        stopAdvertising();
    }
}

/**
 * @brief 获取当前状态。
 */
BlueToothState Sys_BlueToothManager::getCurrentState() const {
    return _currentState;
}

// --- NimBLEServerCallbacks 回调函数 ---

/**
 * @brief 处理客户端连接事件。
 */
void Sys_BlueToothManager::onConnect(NimBLEServer* pServer) {
    ESP_LOGI("BTMan", "BLE Client Connected.");
    // [日志] 记录客户端连接事件
    Sys_FlashLogger::getInstance()->log("[Bluetooth]", "Client connected.");
    _currentState = BlueToothState::CONNECTED;
}

/**
 * @brief 处理客户端断开连接事件。
 */
void Sys_BlueToothManager::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI("BTMan", "BLE Client Disconnected.");
    // [日志] 记录客户端断开连接事件
    Sys_FlashLogger::getInstance()->log("[Bluetooth]", "Client disconnected.");
    // 断开连接后，根据配置决定是返回禁用状态还是重新开始广播
    const auto& settings = Sys_SettingsManager::getInstance()->getSettings();
    if (settings.bluetooth_enabled) {
        // 延迟一小段时间后重新广播，给客户端一些时间来处理断开
        vTaskDelay(pdMS_TO_TICKS(100));
        startAdvertising();
    } else {
        _currentState = BlueToothState::BT_DISABLED;
    }
}

// --- 私有辅助方法 (Private Methods) ---

/**
 * @brief 启动BLE广播。
 */
bool Sys_BlueToothManager::startAdvertising() {
    if (_currentState == BlueToothState::ADVERTISING || !_pAdvertising) {
        return true;
    }
    ESP_LOGI("BTMan", "Starting BLE advertising...");
    if (_pAdvertising->start()) {
        _currentState = BlueToothState::ADVERTISING;
        return true;
    } else {
        ESP_LOGE("BTMan", "Failed to start advertising.");
        return false;
    }
}

/**
 * @brief 停止BLE广播。
 */
bool Sys_BlueToothManager::stopAdvertising() {
    if (_currentState != BlueToothState::ADVERTISING || !_pAdvertising) {
        return true;
    }
    ESP_LOGI("BTMan", "Stopping BLE advertising...");
    if (_pAdvertising->stop()) {
        _currentState = BlueToothState::BT_DISABLED;
        return true;
    } else {
        ESP_LOGE("BTMan", "Failed to stop advertising.");
        return false;
    }
}

/**
 * @brief 设置BLE设备名称。
 */
void Sys_BlueToothManager::setDeviceName(const char* name) {
    if (!name || strlen(name) == 0 || strcmp(_currentDeviceName, name) == 0) {
        return; // 如果名称为空或未改变，则不执行任何操作
    }

    ESP_LOGI("BTMan", "Setting BLE device name to: '%s'", name);
    // 更新NimBLE设备名称
    NimBLEDevice::setDeviceName(name);
    
    // 如果正在广播，需要重启广播以应用新名称
    if (_currentState == BlueToothState::ADVERTISING) {
        _pAdvertising->stop();
        _pAdvertising->start();
    }
    
    // 更新内部缓存
    strncpy(_currentDeviceName, name, sizeof(_currentDeviceName) - 1);
    _currentDeviceName[sizeof(_currentDeviceName) - 1] = '\0';
}