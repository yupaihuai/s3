/**
 * @file Sys_Diagnostics.h
 * @brief 系统诊断服务模块的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块提供了一个静态的`run()`方法，用于执行一系列全面的系统检查，
 * 包括硬件信息、内存状态、分区表和文件系统。
 *
 * 它是专为开发和调试设计的工具，通过编译时宏`CORE_DEBUG_MODE`控制，
 * 在发布固件中将被完全移除，不占用任何资源。
 */
#pragma once

// 引入编译时开关的定义
#ifndef CORE_DEBUG_MODE
#define CORE_DEBUG_MODE 0 // 默认关闭
#endif

// 关键：整个模块都由编译时开关控制
#if CORE_DEBUG_MODE

#include "FS.h" // 需要 fs::FS 类型

/**
 * @class Sys_Diagnostics
 * @brief 一个用于硬件、软件配置的按需诊断服务模块。
 */
class Sys_Diagnostics {
public:
    /**
     * @brief 运行所有诊断检查，并将格式化的报告打印到串口。
     * @details 使用`ESP_LOGI`级别，确保其输出总是可见，不受运行时调试日志级别的影响。
     */
    static void run();

private:
    // 将每个诊断部分拆分为独立的私有方法，增加可读性和可维护性。
    /** @brief 检查并打印芯片、CPU、固件版本等系统信息。*/
    static void checkSystemInfo();
    /** @brief 检查并打印Flash、PSRAM、堆内存的使用情况。*/
    static void checkMemory();
    /** @brief 检查并打印实际加载的分区表信息。*/
    static void checkPartitions();
    /** @brief 检查并打印已挂载文件系统的状态和内容。*/
    static void checkFileSystems();
    /** @brief 递归列出指定目录下的文件和文件夹，是一个辅助函数。*/
    static void listDir(fs::FS& fs, const char* dirname, uint8_t levels = 0);
};

#endif // CORE_DEBUG_MODE
