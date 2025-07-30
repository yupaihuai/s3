/**
 * @file Sys_MemoryManager.h
 * @brief PSRAM固定块内存池管理器的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  该模块在应用层实现了一个专用的、固定块大小的内存池，以取代
 *  底层复杂的堆注册方案。它在系统启动时，从PSRAM中预留一块大容量
 *  内存（如4MB），并将其手动划分为若干个大块（如每块1MB）。
 *  这种“真池”方案通过简单的状态向量进行管理，实现了零碎片、高性能的
 *  大块内存分配，专门用于摄像头帧缓冲等关键任务。
 *
 * @note  本模块的所有公共方法均为线程安全。
 */
#pragma once

#include <Arduino.h>
#include <vector> // 引入 vector
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Sys_LockGuard.h" // 引入RAII锁

/**
 * @class Sys_MemoryManager
 * @brief 一个用于PSRAM的、手动的、固定块内存池管理器。
 */
class Sys_MemoryManager {
public:
    /**
     * @brief 获取内存管理器的单例实例。
     */
    static Sys_MemoryManager* getInstance();

    // 删除拷贝构造函数和赋值操作符，确保单例模式。
    Sys_MemoryManager(const Sys_MemoryManager&) = delete;
    Sys_MemoryManager& operator=(const Sys_MemoryManager&) = delete;

    /**
     * @brief 在系统启动时调用，从PSRAM分配大块内存并初始化内存池。
     * @return bool `true` 如果内存池初始化成功, `false` 如果失败。
     */
    bool initializePools();

    /**
     * @brief 从专用的摄像头帧缓冲池中获取一个内存块。
     * @return void* 指向可用内存块的指针，若无可用块则返回nullptr。
     */
    void* getFrameBuffer();

    /**
     * @brief 将一个内存块释放回帧缓冲池。
     * @param buffer 指向要释放的内存块的指针。
     */
    void releaseFrameBuffer(void* buffer);
    
    /**
     * @brief 打印内存池的当前使用状态，用于调试。
     */
    void printMemoryInfo();

private:
    // 私有构造函数
    Sys_MemoryManager();

    /** @brief 单例实例指针。*/
    static Sys_MemoryManager* _instance;

    /** @brief 互斥锁，用于保护对内存池状态的并发访问。*/
    SemaphoreHandle_t _mutex = NULL;

    // --- 内存池核心数据结构 ---
    /** @brief 预分配的4MB内存区域的起始地址。*/
    static void* _framebuffer_heap_start;
    /** @brief 用于跟踪每个内存块使用状态的标志位向量。*/
    static std::vector<bool> _block_is_used;
    /** @brief 池中每个内存块的大小（字节）。*/
    static size_t _block_size;
    /** @brief 池中内存块的总数。*/
    static size_t _block_count;
};
