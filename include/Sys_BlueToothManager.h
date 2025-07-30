/**
 * @file Sys_BlueToothManager.h
 * @brief 系统蓝牙(BLE)管理器的接口定义 (基于NimBLE)
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  该模块基于NimBLE库，负责管理BLE协议栈的生命周期、广播行为和连接状态。
 *  它的行为完全由`Sys_SettingsManager`中的配置驱动，并将复杂的BLE底层API
 *  封装在内部，对外提供简洁的控制接口和清晰的状态机。
 *  ESP32-S3仅支持BLE，不支持经典蓝牙。
 */
#pragma once

#include <Arduino.h>
#include "NimBLEDevice.h"
#include "Sys_SettingsManager.h" // 需要读取蓝牙配置

/**
 * @enum BlueToothState
 * @brief 定义了BLE模块的清晰、有限的运行状态。
 */
enum class BlueToothState {
    UNINITIALIZED,  // 未初始化，或初始化失败
    BT_DISABLED,    // 已初始化但被用户配置禁用 (添加前缀以避免宏冲突)
    ADVERTISING,    // 正在广播
    CONNECTED       // 已有客户端连接
};

/**
 * @class Sys_BlueToothManager
 * @brief 系统BLE管理器，负责BLE的广播、连接和配置应用。
 */
class Sys_BlueToothManager : public NimBLEServerCallbacks {
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
     * @brief 在系统启动时调用，初始化NimBLE协议栈。
     */
    void begin();

    /**
     * @brief 由 Task_SystemMonitor 周期性调用。
     * @details 目前为空，为未来扩展预留。
     */
    void update();

    /**
     * @brief 应用最新的系统设置。这是控制BLE行为的核心入口。
     * @details 当用户在Web界面保存配置后，由Task_Worker调用此方法。
     *          它会比较新旧设置，并执行必要的启动广播、停止广播或重命名操作。
     */
    void applySettings();

    /**
     * @brief 获取当前BLE模块的状态。
     * @return BlueToothState 当前状态。
     */
    BlueToothState getCurrentState() const;

    // --- NimBLEServerCallbacks 回调函数 ---
    /** @brief 处理客户端连接事件。*/
    void onConnect(NimBLEServer* pServer) override;
    /** @brief 处理客户端断开连接事件。*/
    void onDisconnect(NimBLEServer* pServer) override;

private:
    // 私有构造函数，由`getInstance()`调用。
    Sys_BlueToothManager() = default;

    // --- 内部状态转换的核心函数 ---
    /** @brief 启动BLE广播。*/
    bool startAdvertising();
    /** @brief 停止BLE广播。*/
    bool stopAdvertising();
    /** @brief 设置BLE设备名称。*/
    void setDeviceName(const char* name);

    /** @brief 单例实例指针。*/
    static Sys_BlueToothManager* _instance;
    /** @brief 当前BLE模块的状态机状态。*/
    volatile BlueToothState _currentState = BlueToothState::UNINITIALIZED;
    /** @brief 内存中缓存的当前设备名称，用于比较配置是否变更。*/
    char _currentDeviceName[33] = "";

    /** @brief 指向NimBLE服务器实例的指针。*/
    NimBLEServer* _pServer = nullptr;
    /** @brief 指向NimBLE广播实例的指针。*/
    NimBLEAdvertising* _pAdvertising = nullptr;
};