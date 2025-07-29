/**
 * @file Sys_BlueToothManager.h
 * @brief 系统蓝牙管理器的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块采用事件驱动和状态机模式，负责管理蓝牙协议栈的生命周期（启用/禁用）
 * 和基本配置（如设备名称）。它的行为完全由`Sys_SettingsManager`中的配置驱动，
 * 并将复杂的蓝牙底层API调用封装在内部，对外提供简洁的控制接口。
 */
#pragma once

#include <Arduino.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"  //负责定义蓝牙经典模式的 GAP 相关的 API、事件和参数类型。
#include "Sys_SettingsManager.h" // 需要读取蓝牙配置

/**
 * @enum BlueToothState
 * @brief 定义了蓝牙模块的清晰、有限的运行状态。
 */
enum class BlueToothState {
    UNINITIALIZED,  // 未初始化，或初始化失败
    BLUETOOTH_STATE_DISABLED, // 已初始化但被用户配置禁用
    ENABLING,       // 正在启用中（异步过程）
    ENABLED,        // 已成功启用并正常运行
    DISABLING       // 正在禁用中（异步过程）
};

/**
 * @class Sys_BlueToothManager
 * @brief 系统蓝牙管理器，负责蓝牙的生命周期和配置应用。
 */
class Sys_BlueToothManager {
public:
    /**
     * @brief 获取蓝牙管理器的单例实例。
     * @return Sys_BlueToothManager* 指向唯一实例的指针。
     */
    static Sys_BlueToothManager* getInstance();
    // 删除拷贝构造函数和赋值操作符，确保单例模式。
    Sys_BlueToothManager(const Sys_BlueToothManager&) = delete;
    Sys_BlueToothManager& operator=(const Sys_BlueToothManager&) = delete;

    /**
     * @brief 在系统启动时调用，初始化蓝牙底层控制器和Bluedroid协议栈。
     */
    void begin();

    /**
     * @brief 由 Task_SystemMonitor 周期性调用。
     * @details 目前为空，为未来扩展预留，例如处理A2DP连接超时、自动重连等逻辑。
     */
    void update();

    /**
     * @brief 应用最新的系统设置。这是控制蓝牙行为的核心入口。
     * @details 当用户在Web界面保存配置后，由Task_Worker调用此方法。
     *          它会比较新旧设置，并执行必要的启用、禁用或重命名操作。
     */
    void applySettings();

    /**
     * @brief 获取当前蓝牙模块的状态。
     * @return BlueToothState 当前状态。
     */
    BlueToothState getCurrentState() const;

private:
    // 私有构造函数，由`getInstance()`调用。
    Sys_BlueToothManager() = default;

    // --- 内部状态转换的核心函数 ---
    /** @brief 异步启用Bluedroid协议栈。*/
    bool enableBlueTooth();
    /** @brief 异步禁用Bluedroid协议栈。*/
    bool disableBlueTooth();
    /** @brief 设置蓝牙设备名称。*/
    bool setDeviceName(const char* name);

    /**
     * @brief ESP-IDF蓝牙GAP事件的静态回调函数。
     * @details 这是实现事件驱动状态机的关键。所有蓝牙底层的异步事件（如认证完成、扫描模式设置等）
     *          都会在这里被捕获和处理。
     */
    static void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);

    /** @brief 单例实例指针。*/
    static Sys_BlueToothManager* _instance;
    /** @brief 当前蓝牙模块的状态机状态。*/
    BlueToothState _currentState = BlueToothState::UNINITIALIZED;
    /** @brief 内存中缓存的当前设备名称，用于比较配置是否变更。*/
    char _currentDeviceName[33] = "";
};