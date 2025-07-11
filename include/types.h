/**
 * @file types.h
 * @brief 定义项目中跨模块共享的公共数据类型、结构体和枚举。
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 这个文件旨在解决模块间的类型依赖问题。任何被多个模块
 * 使用的数据结构都应该在这里定义，以提供一个“单一事实来源”，
 * 避免重复定义和不明确的`extern`声明。
 */
#pragma once

/**
 * @struct Command
 * @brief 定义了从前端接口到Task_Worker的命令格式。
 */
struct Command {
    // 命令类型枚举，清晰地定义了所有支持的命令。
    enum Type {
        SAVE_WIFI,          // 保存WiFi设置
        SAVE_BLE,           // 保存蓝牙设置
        SCAN_WIFI,          // 扫描WiFi网络
        RUN_DIAGNOSTICS,    // 运行系统诊断
        REBOOT,             // 重启设备
        FACTORY_RESET       // 恢复出厂设置
        // ... 未来可在此添加新命令 ...
    };

    Type type;
    char payload[128] = ""; // 用于携带命令的附加数据，例如一个简短的JSON字符串。
};

// --- 未来可以添加其他共享类型 ---
/*
struct SystemEvent {
    enum class Source { WIFI, BLUETOOTH, SENSOR };
    Source source;
    int event_code;
};
*/