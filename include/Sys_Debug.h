/**
 * @file Sys_Debug.h
 * @brief 全局调试日志宏定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 本文件提供了优雅的、可配置的调试日志宏。它不包含任何.cpp实现，
 * 只定义全局可用的宏，依赖`Sys_SettingsManager`获取运行时调试状态。
 * 在任何需要打印调试信息的.cpp文件中，只需包含本文件即可。
 */
#pragma once

#include "esp_log.h"
#include "Sys_SettingsManager.h" // 关键：引入Settings管理器以查询状态

// --- 编译时开关 ---
// 这个宏从 platformio.ini 的 build_flags 中获取 (-DCORE_DEBUG_MODE=1)
// 如果没有定义，则默认为0（关闭）
#ifndef CORE_DEBUG_MODE
#define CORE_DEBUG_MODE 0
#endif

// --- 统一的日志标签 ---
// 为所有调试日志定义一个统一的TAG，便于在串口监视器中过滤
static const char* DEBUG_TAG = "CoreDebug";

/**
 * @brief 优雅的调试日志宏 (DEBUG_LOG)
 *
 * - 在编译时: 如果 CORE_DEBUG_MODE=0, 此宏展开为空，不产生任何代码，实现零开销。
 * - 在运行时: 如果 CORE_DEBUG_MODE=1, 此宏会检查 Sys_SettingsManager 中的
 *             运行时开关。只有当运行时开关也为true时，才会打印日志。
 *
 * 用法: DEBUG_LOG("Value is: %d", my_variable);
 */
#if CORE_DEBUG_MODE
    #define DEBUG_LOG(fmt, ...) \
        do { \
            /* [优化] 实时查询专用getter方法，避免不必要的结构体拷贝，性能更佳。 */ \
            if (Sys_SettingsManager::getInstance()->isDebugModeEnabled()) { \
                /* 使用 ESP-IDF 的 DEBUG 级别日志宏，输出带格式的日志 */ \
                ESP_LOGD(DEBUG_TAG, fmt, ##__VA_ARGS__); \
            } \
        } while(0)
#else
    // 如果编译时就禁用了调试模式，宏展开为空，实现零开销。
    #define DEBUG_LOG(fmt, ...)
#endif

/**
 * @brief 无条件调试日志宏 (DEBUG_LOG_ALWAYS) (可选)
 *
 * 这个宏忽略运行时的开关，只要编译时 CORE_DEBUG_MODE=1，它就会打印。
 * 用于那些即使在运行时关闭了普通调试，也希望看到的关键调试信息。
 */
#if CORE_DEBUG_MODE
    #define DEBUG_LOG_ALWAYS(fmt, ...) ESP_LOGD(DEBUG_TAG, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_LOG_ALWAYS(fmt, ...)
#endif
