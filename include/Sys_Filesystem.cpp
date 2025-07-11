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

    // [优化] 增加注释，强调分区标签需要与.csv文件匹配。
    // 根据您的设计蓝图，第一个数据分区标签为 "littlefs"，用于存储Web UI。
    // ESP-IDF的`esp_vfs_fat_spiflash_mount_rw_wl`会先尝试挂载ffat分区，
    // 因此分区表顺序应为 nvs, otadata, app0, app1, ffat, littlefs, coredump。
    // 在代码中，我们先挂载 littlefs，再挂载 ffat。
    _littlefs_mounted = mountFs(LittleFS, "littlefs", "/");

    // 挂载FFat，分区标签为"ffat"，用于用户文件。
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

// --- 私有辅助方法 (Private Methods) ---

/**
 * @brief 通用的文件系统挂载函数。
 */
bool Sys_Filesystem::mountFs(fs::FS& fs, const char* partition_label, const char* mount_point) {
    ESP_LOGI("FS", "Mounting '%s' partition to '%s'...", partition_label, mount_point);
    
    // 尝试挂载文件系统。`format_if_failed`参数设为`false`，我们手动处理格式化逻辑以提供更详细的日志。
    // `max_open_files`可以根据需求调整。
    if (fs.begin(false, mount_point, 10, partition_label)) {
        ESP_LOGI("FS", "'%s' mounted successfully.", partition_label);
        return true;
    }

    // --- 如果挂载失败 ---
    ESP_LOGE("FS", "'%s' mount failed! Attempting to format...", partition_label);
    if (fs.format()) {
        ESP_LOGI("FS", "'%s' partition formatted successfully. Remounting...", partition_label);
        // 格式化后再次尝试挂载
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
