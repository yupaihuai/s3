/**
 * @file Sys_Filesystem.cpp
 * @brief 系统文件系统管理器的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 实现了LittleFS和FFat的挂载逻辑。
 * 采用了“失败时格式化”的健壮策略。
 */
#include "Sys_Filesystem.h"
#include "Sys_Debug.h"

// 初始化静态单例指针
Sys_Filesystem* Sys_Filesystem::_instance = nullptr;

namespace {
/**
 * @brief 一个通用的、挂载单个文件系统的私有辅助模板函数。
 * @tparam FSType 文件系统类的具体类型 (e.g., LittleFSFS, FFatFS)
 * @param fs 文件系统对象的引用 (如 LittleFS, FFat)。
 * @param partition_label 分区表中的标签名。
 * @param mount_point 挂载点路径。
 * @return bool 是否成功挂载。
 */
template<typename FSType>
bool mountFs(FSType& fs, const char* partition_label, const char* mount_point) {
    ESP_LOGI("FS", "Mounting '%s' partition to '%s'...", partition_label, mount_point);
    
    if (fs.begin(false, mount_point, 10, partition_label)) {
        ESP_LOGI("FS", "'%s' mounted successfully.", partition_label);
        return true;
    }

    ESP_LOGE("FS", "'%s' mount failed! Attempting to format...", partition_label);
    if (fs.format()) {
        ESP_LOGI("FS", "'%s' partition formatted successfully. Remounting...", partition_label);
        if (fs.begin(false, mount_point, 10, partition_label)) {
            ESP_LOGI("FS", "'%s' remounted successfully after format.", partition_label);
            return true;
        } else {
            ESP_LOGE("FS", "FATAL: '%s' remount failed after format!", partition_label);
            return false;
        }
    } else {
        ESP_LOGE("FS", "FATAL: Formatting '%s' partition failed!", partition_label);
        return false;
    }
}
} // namespace

/**
 * @brief 获取文件系统管理器的单例实例。
 */
Sys_Filesystem* Sys_Filesystem::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_Filesystem();
    }
    return _instance;
}

/**
 * @brief 初始化并挂载所有文件系统。
 */
bool Sys_Filesystem::begin() {
    DEBUG_LOG("Initializing filesystems...");

    _littlefs_mounted = mountFs(LittleFS, "littlefs", "/");
    _ffat_mounted = mountFs(FFat, "ffat", "/media");

    return _littlefs_mounted && _ffat_mounted;
}

/**
 * @brief 获取LittleFS的总空间。
 */
uint64_t Sys_Filesystem::getLittleFSTotalBytes() {
    return _littlefs_mounted ? LittleFS.totalBytes() : 0;
}

/**
 * @brief 获取LittleFS已用空间。
 */
uint64_t Sys_Filesystem::getLittleFSUsedBytes() {
    return _littlefs_mounted ? LittleFS.usedBytes() : 0;
}

/**
 * @brief 获取FFat的总空间。
 */
uint64_t Sys_Filesystem::getFFatTotalBytes() {
    return _ffat_mounted ? FFat.totalBytes() : 0;
}

/**
 * @brief 获取FFat已用空间。
 */
uint64_t Sys_Filesystem::getFFatUsedBytes() {
    return _ffat_mounted ? FFat.usedBytes() : 0;
}

// The original mountFs member function is no longer needed.
