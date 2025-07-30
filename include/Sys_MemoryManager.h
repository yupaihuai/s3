/**
 * @file Sys_MemoryManager.h
 * @brief 系统内存管理器的接口定义 (基于ESP-IDF多堆)
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  该模块采用ESP-IDF v5.x先进的多堆（Multi-heap）内存管理方案。
 *  它在系统启动时，从PSRAM中划分出一块专用区域，并将其注册为一个
 *  隔离的、具有自定义能力（MALLOC_CAP_FRAMEBUFFER）的新堆。
 *  这种方法为关键任务（如摄像头）提供了稳定、隔离的大块内存，
 *  同时保留了标准`heap_caps_malloc`的灵活性。
 *
 * @note  本模块的所有公共方法均为线程安全。
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Sys_LockGuard.h" // 引入RAII锁

/**
 * @class Sys_MemoryManager
 * @brief 一个基于ESP-IDF多堆模型的内存管理器。
 */
class Sys_MemoryManager {
public:
    /**
     * @brief 获取内存管理器的单例实例。
     * @note  必须在系统进入多任务调度前（如在setup()中）完成首次调用。
     * @return Sys_MemoryManager* 指向唯一实例的指针。
     */
    static Sys_MemoryManager* getInstance();

    // 删除拷贝构造函数和赋值操作符，确保单例模式。
    Sys_MemoryManager(const Sys_MemoryManager&) = delete;
    Sys_MemoryManager& operator=(const Sys_MemoryManager&) = delete;

    /**
     * @brief 在系统启动时调用，创建并注册专用的PSRAM堆。
     * @details 此函数将从主PSRAM堆中分配一块大内存，并将其注册为具有
     *          `MALLOC_CAP_FRAMEBUFFER`能力的新堆，供特定任务使用。
     * @return bool `true` 如果专用堆初始化成功, `false` 如果失败。
     */
    bool initializePools();

    /**
     * @brief 释放一个之前通过`heap_caps_malloc`分配的内存块。
     * @details 线程安全。这只是对`heap_caps_free`的简单封装，以保持接口统一。
     * @param block_ptr 指向要释放的内存的指针。
     */
    void releaseMemoryBlock(void* block_ptr);
    
    /**
     * @brief 打印所有关键堆的当前使用状态，用于调试。
     * @details 线程安全。
     */
    void printMemoryInfo();

private:
    // 私有构造函数
    Sys_MemoryManager();

    /** @brief 单例实例指针。*/
    static Sys_MemoryManager* _instance;

    /** @brief 互斥锁，用于保护对内存信息打印等共享操作的并发访问。*/
    SemaphoreHandle_t _mutex = NULL;
};
