/**
 * @file Sys_Filesystem.h
 * @brief 系统文件系统管理器的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块负责初始化和管理设备上的所有文件系统，包括用于Web UI的LittleFS
 * 和用于大文件存储的FFat。它封装了文件系统的挂载逻辑，并向上层
 * 模块提供对已挂载文件系统的访问。
 */
#pragma once

#include <Arduino.h>
#include "FS.h"
#include "LittleFS.h"
#include "FFat.h"

/**
 * @class Sys_Filesystem
 * @brief 文件系统管理器，负责挂载和提供对LittleFS和FFat的访问。
 */
class Sys_Filesystem {
public:
    /**
     * @brief 获取文件系统管理器的单例实例。
     * @note  必须在系统进入多任务调度前（如在setup()中）完成首次调用。
     * @return Sys_Filesystem* 指向唯一实例的指针。
     */
    static Sys_Filesystem* getInstance();
    // 删除拷贝构造函数和赋值操作符。
    Sys_Filesystem(const Sys_Filesystem&) = delete;
    Sys_Filesystem& operator=(const Sys_Filesystem&) = delete;

    /**
     * @brief 在系统启动时调用，挂载所有已定义的文件系统。
     * @details 采用“失败时格式化”策略，只在首次启动或文件系统损坏时进行格式化。
     * @return bool `true` 如果所有文件系统都成功挂载，否则为 `false`。
     */
    bool begin();

    // --- 文件系统状态查询接口 ---
    
    /** @brief 检查LittleFS是否已成功挂载。*/
    bool isLittleFSMounted() const { return _littlefs_mounted; }
    /** @brief 检查FFat是否已成功挂载。*/
    bool isFFatMounted() const { return _ffat_mounted; }

    // --- 文件系统信息获取接口 ---
    // 这些接口供诊断或系统状态监控使用。

    /** @brief 获取LittleFS的总字节数。*/
    uint64_t getLittleFSTotalBytes();
    /** @brief 获取LittleFS已使用的字节数。*/
    uint64_t getLittleFSUsedBytes();
    /** @brief 获取FFat的总字节数。*/
    uint64_t getFFatTotalBytes();
    /** @brief 获取FFat已使用的字节数。*/
    uint64_t getFFatUsedBytes();

private:
    // 私有构造函数，由`getInstance()`调用。
    Sys_Filesystem() = default;

    /**
     * @brief 一个通用的、挂载单个文件系统的私有辅助函数。
     * @param fs 文件系统对象的引用 (如 LittleFS, FFat)。
     * @param partition_label 分区表中的标签名。
     * @param mount_point 挂载点路径。
     * @return bool 是否成功挂载。
     */
    bool mountFs(fs::FS& fs, const char* partition_label, const char* mount_point);
    
    /** @brief 单例实例指针。*/
    static Sys_Filesystem* _instance;

    /** @brief 标志位，记录LittleFS的挂载状态。*/
    bool _littlefs_mounted = false;
    /** @brief 标志位，记录FFat的挂载状态。*/
    bool _ffat_mounted = false;
};
