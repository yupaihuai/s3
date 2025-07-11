/**
 * @file Sys_FlashLogger.h
 * @brief Flash友好型日志记录器的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块旨在解决直接、频繁地向Flash写入小数据块而导致的性能和寿命问题。
 * 它实现了一个基于PSRAM环形缓冲区的二级日志系统：
 * 1. 日志首先被快速、非阻塞地写入PSRAM中的`Ringbuf`。
 * 2. 一个低优先级的后台任务负责在满足条件（缓冲区满或超时）时，
 *    将缓冲区中的数据一次性、批量地写入Flash上的日志文件。
 *
 * 这种机制极大地减少了Flash的擦写次数，是构建可靠、长寿命产品的关键。
 *
 * @note 本模块所有公共方法均设计为线程安全。
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"   // 使用FreeRTOS高效的环形缓冲区
#include "freertos/semphr.h"    // 引入信号量头文件，用于线程同步
#include "Sys_LockGuard.h"      // 引入RAII锁

/**
 * @class Sys_FlashLogger
 * @brief 一个对Flash友好的、基于PSRAM环形缓冲区的日志记录器。
 */
class Sys_FlashLogger {
public:
    /**
     * @brief 获取日志记录器的单例实例。
     * @note  必须在系统进入多任务调度前（如在setup()中）完成首次调用。
     * @return Sys_FlashLogger* 指向唯一实例的指针。
     */
    static Sys_FlashLogger* getInstance();
    // 删除拷贝构造函数和赋值操作符。
    Sys_FlashLogger(const Sys_FlashLogger&) = delete;
    Sys_FlashLogger& operator=(const Sys_FlashLogger&) = delete;

    /**
     * @brief 初始化日志记录器，创建PSRAM缓冲区和后台写入任务。
     * @details 必须在文件系统模块初始化之后调用。
     * @param log_filepath 日志文件的完整路径 (例如, "/media/system.log")。
     * @param buffer_size PSRAM中环形缓冲区的大小 (字节)。建议为4KB的倍数。
     * @param flush_interval_ms 后台任务定时强制刷写缓冲区的间隔 (毫秒)。
     * @return bool `true` 表示初始化成功, `false` 表示失败。
     */
    bool begin(const char* log_filepath, size_t buffer_size = 8192, uint32_t flush_interval_ms = 60000);

    /**
     * @brief 记录一条格式化的日志。
     * @details 这是一个快速的、线程安全的操作，它只将数据写入PSRAM缓冲区，然后立即返回。
     * @param format `printf`风格的格式化字符串。
     * @param ... 可变参数。
     */
    void log(const char* format, ...);

    /**
     * @brief 强制将缓冲区中所有待处理的日志立即写入Flash。
     * @details 这是一个线程安全的异步请求。它会通知后台任务尽快执行刷写操作。
     *          用于在系统重启或发生严重错误前，确保所有日志都被保存。
     */
    void flush();
    
    /**
     * @brief 线程安全地删除当前的日志文件。
     * @details 可用于实现日志的轮转(log rotation)或手动清理。
     *          此操作会等待任何正在进行的写操作完成后再执行。
     */
    void clearLogFile();

private:
    // 私有构造函数
    Sys_FlashLogger();

    /**
     * @brief 后台任务的核心循环函数，负责将缓冲区数据写入文件。
     */
    static void flushTask(void* parameter);
    
    /**
     * @brief 实际执行从缓冲区读取并写入文件操作的函数。
     * @note  这个方法内部实现了对文件I/O的互斥访问。
     */
    void writeBufferToFile();

    /** @brief 单例实例指针。*/
    static Sys_FlashLogger* _instance;
    
    /** @brief FreeRTOS环形缓冲区的句柄。*/
    RingbufHandle_t _ring_buffer_handle = NULL;
    /** @brief 后台刷写任务的句柄。*/
    TaskHandle_t _flush_task_handle = NULL;
    /** @brief 用于手动触发立即刷写的二进制信号量。*/
    SemaphoreHandle_t _flush_semaphore = NULL;
    /** 
     * @brief 用于保护文件I/O操作的互斥锁 (Mutex)。
     * @details 这是确保多任务环境下文件系统访问安全的关键。
     *          任何对日志文件的`open`, `write`, `remove`等操作都必须先获取此锁。
     */
    SemaphoreHandle_t _file_mutex = NULL;
    
    /** @brief 日志文件的路径。*/
    String _log_filepath;
    /** @brief 定时刷写的间隔。*/
    uint32_t _flush_interval_ms;
};
