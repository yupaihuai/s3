/**
 * @file Sys_SettingsManager.cpp
 * @brief 系统设置管理器的实现文件
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 * 
 * @details
 * 实现了从NVS加载、保存配置，以及版本控制和默认值恢复等核心逻辑。
 * 它完全依赖`Sys_NvsManager`作为其底层存储引擎，并使用互斥锁确保
 * 所有对配置缓存的读写操作都是线程安全的。
 */
#include "Sys_SettingsManager.h"
#include "Sys_NvsManager.h" // 依赖底层NVS工具类
#include "Sys_Debug.h"

/** @brief 当前固件期望的设置版本号。当`SystemSettings`结构体发生不兼容的改变时，必须增加此版本号。*/
static constexpr const uint32_t CURRENT_SETTINGS_VERSION = 1;

// 初始化静态单例指针
Sys_SettingsManager* Sys_SettingsManager::_instance = nullptr;

/**
 * @brief 私有构造函数，在此创建互斥锁。
 */
Sys_SettingsManager::Sys_SettingsManager() {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        ESP_LOGE("Settings", "FATAL: Failed to create mutex!"); // 严重错误: 创建互斥锁失败！
    }
}

/**
 * @brief 获取Settings管理器的单例实例。
 */
Sys_SettingsManager* Sys_SettingsManager::getInstance() {
    // [优化] 此处采用的简单单例模式依赖于在单线程环境（setup()）中首次调用。
    // 这对于大多数嵌入式项目是足够且高效的。
    if (_instance == nullptr) {
        _instance = new Sys_SettingsManager();
    }
    return _instance;
}

/**
 * @brief 模块初始化，在系统启动时调用。
 */
void Sys_SettingsManager::begin() {
    DEBUG_LOG("Initializing Settings Manager...");
    // 调试日志: 正在初始化设置管理器...
    
    // 在begin()中，系统处于单线程模式，无需加锁即可安全调用load()
    load();
}

/**
 * @brief 从NVS加载配置到内存缓存。
 * @note 这是一个私有方法，假定它总是在一个已获取锁或单线程的上下文中被调用。
 */
void Sys_SettingsManager::load() {
    SystemSettings temp_settings;
    size_t blob_size = sizeof(SystemSettings);

    if (Sys_NvsManager::readBlob(NVS_NAMESPACE, NVS_KEY_BLOB, &temp_settings, &blob_size) && blob_size == sizeof(SystemSettings)) {
        // 读取成功，检查版本
        if (temp_settings.settings_version == CURRENT_SETTINGS_VERSION) {
            ESP_LOGI("Settings", "Settings v%u loaded from NVS.", temp_settings.settings_version);
            // 日志: 从NVS加载了v%u版本的设置。
            _settings = temp_settings;
            _is_dirty = false;
            return;
        } else {
            // 版本不匹配
            ESP_LOGW("Settings", "NVS version mismatch (found v%u, expected v%u). Restoring defaults.", temp_settings.settings_version, CURRENT_SETTINGS_VERSION);
            // 警告: NVS版本不匹配(发现v%u, 期望v%u)。正在恢复默认值。
            loadDefaults();
            save(); // 保存新的默认值
        }
    } else {
        // 读取失败 (通常是首次启动)
        ESP_LOGW("Settings", "Could not read settings. Loading and saving defaults.");
        // 警告: 无法读取设置。正在加载并保存默认值。
        loadDefaults();
        save();
    }
}

/**
 * @brief 将内存缓存中的配置保存到NVS。
 * @note 这是一个私有方法，假定它总是在一个已获取锁的上下文中被调用。
 * @return bool 是否保存成功。
 */
bool Sys_SettingsManager::save() {
    DEBUG_LOG("Saving settings to NVS...");
    // 调试日志: 正在保存设置到NVS...
    _settings.settings_version = CURRENT_SETTINGS_VERSION;

    if (Sys_NvsManager::writeBlob(NVS_NAMESPACE, NVS_KEY_BLOB, &_settings, sizeof(SystemSettings))) {
        ESP_LOGI("Settings", "Settings successfully committed to NVS.");
        // 日志: 设置已成功提交到NVS。
        _is_dirty = false;
        return true;
    } else {
        ESP_LOGE("Settings", "Failed to commit settings to NVS!");
        // 错误: 提交设置到NVS失败！
        return false;
    }
}

/**
 * @brief 提交内存中的修改到NVS（如果需要）。
 */
bool Sys_SettingsManager::commit() {
    Sys_LockGuard lock(_mutex);
    if (_is_dirty) {
        return save();
    }
    return true; // 没有修改，视为提交成功
}

/**
 * @brief 强制将内存配置写入NVS。
 */
void Sys_SettingsManager::forceSave() {
    Sys_LockGuard lock(_mutex);
    save();
}

/**
 * @brief 将出厂默认配置加载到内存缓存。
 * @note 这是一个私有方法，假定它总是在一个已获取锁的上下文中被调用。
 */
void Sys_SettingsManager::loadDefaults() {
    ESP_LOGI("Settings", "Loading default settings into memory.");
    // 日志: 正在加载默认设置到内存。
    _settings = SystemSettings(); // 使用默认构造函数重置
    markAsDirty();
}

/**
 * @brief 恢复出厂设置。
 */
void Sys_SettingsManager::factoryReset() {
    Sys_LockGuard lock(_mutex);
    ESP_LOGW("Settings", "Performing factory reset!");
    // 警告: 正在执行恢复出厂设置！
    Sys_NvsManager::eraseNamespace(NVS_NAMESPACE);
    loadDefaults();
    save();
}

/**
 * @brief 获取对配置缓存的安全拷贝。
 */
SystemSettings Sys_SettingsManager::getSettings() {
    Sys_LockGuard lock(_mutex);
    return _settings;
}

/**
 * @brief 检查配置是否“脏”。
 */
bool Sys_SettingsManager::isDirty() {
    Sys_LockGuard lock(_mutex);
    return _is_dirty;
}

/**
 * @brief 标记配置为“脏”。
 * @note 这是一个私有方法，假定它总是在一个已获取锁的上下文中被调用。
 */
void Sys_SettingsManager::markAsDirty() {
    if (!_is_dirty) {
        DEBUG_LOG("Settings marked as dirty.");
        _is_dirty = true;
    }
}

// --- 线程安全的 Getter 实现 ---

bool Sys_SettingsManager::isDebugModeEnabled() {
    Sys_LockGuard lock(_mutex);
    return _settings.debug_mode_enabled;
}

SystemSettings::WiFiMode Sys_SettingsManager::getWiFiMode() {
    Sys_LockGuard lock(_mutex);
    return _settings.wifi_mode;
}

String Sys_SettingsManager::getBluetoothName() {
    Sys_LockGuard lock(_mutex);
    return String(_settings.bluetooth_name);
}

// --- 线程安全的 Setter 实现 ---

void Sys_SettingsManager::setWiFiConfig(const char* ssid, const char* password, SystemSettings::WiFiMode mode) {
    Sys_LockGuard lock(_mutex);
    // 只有当配置实际发生变化时，才进行修改并标记为脏
    if (strcmp(_settings.wifi_ssid, ssid) != 0 || 
        strcmp(_settings.wifi_password, password) != 0 || 
        _settings.wifi_mode != mode) {
        
        strncpy(_settings.wifi_ssid, ssid, sizeof(_settings.wifi_ssid) - 1);
        _settings.wifi_ssid[sizeof(_settings.wifi_ssid) - 1] = '\0'; // 确保null结尾
        
        strncpy(_settings.wifi_password, password, sizeof(_settings.wifi_password) - 1);
        _settings.wifi_password[sizeof(_settings.wifi_password) - 1] = '\0';

        _settings.wifi_mode = mode;
        
        markAsDirty();
    }
}

/**
 * @brief 设置蓝牙配置。
 */
void Sys_SettingsManager::setBluetoothConfig(bool enabled, const char* name) {
    Sys_LockGuard lock(_mutex);
    if (_settings.bluetooth_enabled != enabled || strcmp(_settings.bluetooth_name, name) != 0) {
        _settings.bluetooth_enabled = enabled;
        strncpy(_settings.bluetooth_name, name, sizeof(_settings.bluetooth_name) - 1);
        _settings.bluetooth_name[sizeof(_settings.bluetooth_name) - 1] = '\0';
        markAsDirty();
    }
}

/**
 * @brief 设置运行时调试模式。
 */
void Sys_SettingsManager::setDebugMode(bool enabled) {
    Sys_LockGuard lock(_mutex);
    if (_settings.debug_mode_enabled != enabled) {
        _settings.debug_mode_enabled = enabled;
        markAsDirty();
    }
}
