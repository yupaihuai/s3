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
