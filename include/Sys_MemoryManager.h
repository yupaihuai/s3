/**
 * @file Sys_MemoryManager.h
 * @brief PSRAM多池内存管理器的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块旨在解决PSRAM因频繁、零散的malloc/free操作而导致的内存碎片化问题。
 * 它通过在系统启动时，从PSRAM中预先分配若干个大小固定的“内存池”，
 * 将动态内存分配转换为对这些预分配块的复用管理，从而保证了需要大块连续内存的
 * 任务（如摄像头、文件上传）总能获得可靠的内存资源。
 *
 * @note  本模块的所有公共方法均为线程安全。
 */
#pragma once

#include <Arduino.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Sys_LockGuard.h" // 引入RAII锁

/**
 * @struct MemoryPool
 * @brief 描述一个内存池的内部结构。
 * @note  这是一个内部数据结构，对外部模块隐藏。
 */
struct MemoryPool {
    void* start_ptr = nullptr;      // 池的起始内存地址
    size_t total_size = 0;          // 池的总大小（字节）
    size_t block_size = 0;          // 池中每个内存块的大小（字节）
    int block_count = 0;            // 池中内存块的数量
    std::vector<bool> used_flags;   // 用于跟踪每个块是否被使用的标志位向量
    const char* name = "Unnamed";   // 池的名称，便于调试
};

/**
 * @class Sys_MemoryManager
 * @brief 一个主动管理PSRAM的多池内存管理器。
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
     * @brief 在系统启动时调用，根据预定义的配置初始化所有内存池。
     * @note 必须在PSRAM可用后调用。
     * @return bool `true` 如果初始化成功, `false` 如果失败。
     */
    bool initializePools();

    /**
     * @brief 根据请求的大小，从最合适的池中获取一个内存块。
     * @details 线程安全。会优先从尺寸最接近且有空闲块的池中分配。
     * @param required_size 期望的内存大小（字节）。
     * @return void* 指向已分配内存块的指针，如果无可用块则返回 `nullptr`。
     */
    void* getMemoryBlock(size_t required_size);
    
    /**
     * @brief 根据池的索引，直接从指定的池中获取一个内存块。
     * @details 线程安全。这对于用途固定的内存请求（如摄像头帧缓冲）更高效、更明确。
     * @param pool_index 池的索引 (从0开始)。
     * @return void* 指向已分配内存块的指针，如果该池无可用块则返回 `nullptr`。
     */
    void* getMemoryBlockFromPool(size_t pool_index);

    /**
     * @brief 释放一个之前分配的内存块。
     * @details 线程安全。管理器会自动判断该内存块属于哪个池并将其回收。
     * @param block_ptr 通过 `getMemoryBlock` 或 `getMemoryBlockFromPool` 获取的指针。
     */
    void releaseMemoryBlock(void* block_ptr);
    
    /**
     * @brief 打印所有内存池的当前使用状态，用于调试。
     * @details 线程安全。
     */
    void printMemoryInfo();

private:
    // 私有构造函数
    Sys_MemoryManager();

    /** @brief 存储所有已初始化内存池的向量。*/
    std::vector<MemoryPool> _pools;

    /** @brief 单例实例指针。*/
    static Sys_MemoryManager* _instance;

    /** @brief 互斥锁，用于保护对内存池状态的并发访问。*/
    SemaphoreHandle_t _mutex = NULL;
};
