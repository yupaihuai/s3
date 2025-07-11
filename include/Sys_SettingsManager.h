/**
 * @file Sys_SettingsManager.h
 * @brief 系统设置管理器的接口定义
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 *
 * @details
 * 该模块是系统配置的“唯一事实来源 (Single Source of Truth)”。
 * 它采用内存缓存和“脏标记”机制，实现了配置的快速读取和对Flash寿命友好的延迟写入。
 * 所有其他模块需要获取配置时，都应通过此管理器进行，以确保数据的一致性。
 *
 * @note  本模块的所有公共方法均为线程安全，内部通过互斥锁实现同步。
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Sys_LockGuard.h" // 引入RAII锁

/**
 * @struct SystemSettings
 * @brief 一个集中包含了所有系统可配置项的结构体。
 *
 * @details
 * 将所有配置项聚合在一个结构体中，便于作为一个整体从NVS中加载和保存(BLOB操作)。
 * 成员的默认值在此处通过C++11的成员初始化器定义，用于`loadDefaults()`。
 */
struct SystemSettings {
    // --- 版本控制 ---
    /** @brief 配置结构体的版本号，用于固件升级时的数据迁移。*/
    uint32_t settings_version = 1;

    // --- WiFi 设置 ---
    /** @brief Station模式下连接的WiFi SSID。*/
    char wifi_ssid[33] = "esp32s3";
    /** @brief Station模式下连接的WiFi 密码。*/
    char wifi_password[65] = "12345678";
    /** @brief WiFi工作模式的枚举。*/
    enum WiFiMode { WIFI_MODE_NULL = 0, WIFI_MODE_STA = 1, WIFI_MODE_AP = 2, WIFI_MODE_AP_STA = 3 };
    /** @brief 当前的WiFi工作模式。*/
    WiFiMode wifi_mode = WIFI_MODE_AP_STA;
    /** @brief 是否启用静态IP。*/
    bool wifi_static_ip_enabled = false;
    /** @brief 静态IP地址。*/
    char wifi_static_ip[16] = "";
    /** @brief 子网掩码。*/
    char wifi_subnet[16] = "";
    /** @brief 网关地址。*/
    char wifi_gateway[16] = "";

    // --- 蓝牙设置 ---
    /** @brief 是否启用蓝牙功能。*/
    bool bluetooth_enabled = true;
    /** @brief 蓝牙广播的设备名称。*/
    char bluetooth_name[33] = "ESP32S3-Device";

    // --- 调试设置 ---
    /** @brief 运行时的调试日志开关。*/
    bool debug_mode_enabled = true;
};


/**
 * @class Sys_SettingsManager
 * @brief 系统设置管理器，实现了配置的缓存、持久化、版本控制和线程安全访问。
 */
class Sys_SettingsManager {
public:
    /**
     * @brief 获取Settings管理器的单例实例。
     * @note  为保证线程安全，此方法应在系统进入多任务调度前（如在setup()中）完成首次调用。
     * @return Sys_SettingsManager* 指向唯一实例的指针。
     */
    static Sys_SettingsManager* getInstance();
    
    // 删除拷贝构造函数和赋值操作符，确保单例模式。
    Sys_SettingsManager(const Sys_SettingsManager&) = delete;
    Sys_SettingsManager& operator=(const Sys_SettingsManager&) = delete;

    /**
     * @brief 在系统启动时调用，从NVS加载配置或初始化为默认值。
     * @note 必须在`Sys_NvsManager::initialize()`之后调用。
     */
    void begin();

    /**
     * @brief 获取当前内存中所有配置的一份线程安全快照。
     * @details 此方法返回一个配置的**副本**，适用于需要同时访问多个配置项，
     *          并需要保证这些配置项在操作期间一致的场景。
     * @return SystemSettings 配置结构体的副本。
     */
    SystemSettings getSettings();

    // --- 线程安全的单个配置项 Getter ---
    // 为频繁访问的单个配置项提供高效、线程安全的只读方法，避免不必要的结构体拷贝。
    
    /** @brief 线程安全地获取运行时调试模式状态。*/
    bool isDebugModeEnabled();
    /** @brief 线程安全地获取WiFi模式。*/
    SystemSettings::WiFiMode getWiFiMode();
    /** @brief 线程安全地获取蓝牙设备名称。*/
    String getBluetoothName();
    // ... 未来可为其他需要单独访问的配置项添加getter ...
    
    // --- 修改与持久化 ---

    /**
     * @brief 将内存中的修改提交到NVS。
     * @details 只有当`isDirty()`返回`true`时，才会执行实际的写入操作。
     *          这个函数被`Task_SystemMonitor`周期性调用，实现了延迟写入。
     * @return bool `true` 如果提交成功或无需提交，`false` 如果写入NVS失败。
     */
    bool commit();
    
    /**
     * @brief 强制将当前内存中的配置立即保存到NVS，无论是否“脏”。
     * @details 用于在系统重启等关键操作前，确保所有配置都已持久化。
     */
    void forceSave();

    /**
     * @brief 检查是否有未保存的修改。
     * @return bool `true` 如果有未提交的修改，否则为 `false`。
     */
    bool isDirty();
    
    /**
     * @brief 将所有设置恢复到出厂默认值，并立即保存到NVS。
     */
    void factoryReset();

    // --- 线程安全的 Setter 接口 ---
    // 每个setter都会修改内存中的值，并设置“脏”标记，以待`commit()`。
    
    /** @brief 设置WiFi相关配置。*/
    void setWiFiConfig(const char* ssid, const char* password, SystemSettings::WiFiMode mode);
    /** @brief 设置蓝牙相关配置。*/
    void setBluetoothConfig(bool enabled, const char* name);
    /** @brief 设置运行时调试模式开关。*/
    void setDebugMode(bool enabled);

private:
    // 私有构造函数，在其中创建同步机制。
    Sys_SettingsManager();
    
    // 从NVS加载配置到内存缓存
    void load();
    // 将内存缓存中的配置保存到NVS
    bool save(); // [优化] 返回bool值，便于上层判断是否成功
    // 将出厂默认配置加载到内存缓存
    void loadDefaults();
    // 将“脏”标记设为true
    void markAsDirty();

    /** @brief 单例实例指针。*/
    static Sys_SettingsManager* _instance;
    /** @brief 内存中的配置缓存，是系统的“唯一事实来源”。*/
    SystemSettings _settings;
    /** @brief “脏”标记，指示内存中的配置是否与NVS中的不一致。*/
    bool _is_dirty = false;
    
    /** @brief 互斥锁，用于保护对 _settings 和 _is_dirty 的并发访问。*/
    SemaphoreHandle_t _mutex = NULL;
    
    /** @brief 用于存储配置的NVS命名空间。*/
    static constexpr const char* NVS_NAMESPACE = "sys_config";
    /** @brief 用于存储配置BLOB的NVS键名。*/
    static constexpr const char* NVS_KEY_BLOB = "settings_v1";
};
