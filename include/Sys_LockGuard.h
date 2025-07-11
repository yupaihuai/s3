/**
 * @file Sys_LockGuard.h
 * @brief 一个通用的、基于RAII模式的FreeRTOS互斥锁守护者
 * @author [ANEAK] & AI Assistant
 * @date [2025/7]
 *
 * @details
 * 该类遵循RAII (Resource Acquisition Is Initialization) 模式。
 * 在其构造函数中获取互斥锁，在其析构函数中释放互斥锁。
 * 只需在需要保护的代码块的开头，在栈上创建一个此类的对象，
 * 即可保证无论函数如何退出（正常返回、异常抛出），锁都一定会被释放，
 * 从而彻底避免忘记解锁导致的死锁问题。
 *
 * @example
 * void threadSafeFunction() {
 *     Sys_LockGuard lock(_my_mutex); // 在构造时加锁
 *     // ... 此处是受保护的临界区代码 ...
 * } // 函数结束时，lock对象被销毁，析构函数自动解锁
 *
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Sys_LockGuard {
public:
    /**
     * @brief 构造函数，立即获取互斥锁并阻塞等待。
     * @param mutex 要管理的FreeRTOS互斥信号量句柄。
     */
    explicit Sys_LockGuard(SemaphoreHandle_t& mutex) : _mutex(mutex) {
        // [优化] 增加对NULL句柄的检查，提高健壮性。
        if (_mutex != NULL) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
        }
    }

    /**
     * @brief 析构函数，自动释放互斥锁。
     */
    ~Sys_LockGuard() {
        // [优化] 增加对NULL句柄的检查。
        if (_mutex != NULL) {
            xSemaphoreGive(_mutex);
        }
    }

    // 删除拷贝构造函数和赋值操作符，确保锁的所有权不被转移。
    // 这是保证RAII模式正确性的关键。
    Sys_LockGuard(const Sys_LockGuard&) = delete;
    Sys_LockGuard& operator=(const Sys_LockGuard&) = delete;

private:
    /** @brief 对要管理的互斥锁的引用。*/
    SemaphoreHandle_t& _mutex;
};
