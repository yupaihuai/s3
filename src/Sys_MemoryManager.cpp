/**
 * @file Sys_MemoryManager.cpp
 * @brief PSRAM固定块内存池管理器的实现
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  实现了应用层的、手动的、固定块内存池管理。
 */
#include "Sys_MemoryManager.h"
#include "Sys_Debug.h"
#include "esp_heap_caps.h"
#include "Sys_FlashLogger.h" // [新增] 引入闪存日志模块
#include <numeric> // For std::iota

// --- 内存池配置 ---
static const size_t FRAMEBUFFER_HEAP_SIZE = 4 * 1024 * 1024; // 预留4MB PSRAM

// --- 静态成员初始化 ---
Sys_MemoryManager* Sys_MemoryManager::_instance = nullptr;
void* Sys_MemoryManager::_framebuffer_heap_start = nullptr;
std::vector<bool> Sys_MemoryManager::_block_is_used;
size_t Sys_MemoryManager::_block_size = 0;
size_t Sys_MemoryManager::_block_count = 0;

/**
 * @brief 获取内存管理器的单例实例。
 */
Sys_MemoryManager* Sys_MemoryManager::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_MemoryManager();
    }
    return _instance;
}

/**
 * @brief 私有构造函数，创建互斥锁。
 */
Sys_MemoryManager::Sys_MemoryManager() {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        ESP_LOGE("MemManager", "Fatal: Failed to create mutex!");
    }
}

/**
 * @brief 初始化内存池。
 */
bool Sys_MemoryManager::initializePools() {
    DEBUG_LOG("Initializing Framebuffer Memory Pool...");

    // 1. 从PSRAM中为我们的内存池预分配一大块内存
    _framebuffer_heap_start = heap_caps_malloc(FRAMEBUFFER_HEAP_SIZE, MALLOC_CAP_SPIRAM);

    if (_framebuffer_heap_start == nullptr) {
        ESP_LOGE("MemManager", "Fatal: Failed to allocate %d bytes for Framebuffer Pool from PSRAM!", FRAMEBUFFER_HEAP_SIZE);
        // [日志] 记录内存分配失败的致命错误
        Sys_FlashLogger::getInstance()->log("[MemManager]", "FATAL: Failed to allocate %d bytes for Framebuffer Pool!", FRAMEBUFFER_HEAP_SIZE);
        return false;
    }
    
    // 2. 定义池的几何形状并初始化状态向量
    _block_size = 1024 * 1024; // 每块1MB
    _block_count = FRAMEBUFFER_HEAP_SIZE / _block_size;
    _block_is_used.assign(_block_count, false); // 初始化所有块为"未使用"

    ESP_LOGI("MemManager", "Framebuffer Pool initialized: %d blocks, %d KB/block.", _block_count, _block_size / 1024);
    printMemoryInfo();
    return true;
}

/**
 * @brief 从帧缓冲池获取一个空闲块。
 */
void* Sys_MemoryManager::getFrameBuffer() {
    Sys_LockGuard lock(_mutex);
    for (size_t i = 0; i < _block_count; ++i) {
        if (!_block_is_used[i]) {
            _block_is_used[i] = true;
            void* block_ptr = static_cast<char*>(_framebuffer_heap_start) + (i * _block_size);
            DEBUG_LOG("Allocated framebuffer block %d at address %p", i, block_ptr);
            return block_ptr;
        }
    }
    ESP_LOGE("MemManager", "No free frame buffers available!");
    return nullptr;
}

/**
 * @brief 释放一个帧缓冲块。
 */
void Sys_MemoryManager::releaseFrameBuffer(void* buffer) {
    if (buffer == nullptr) return;
    
    // 安全检查：确保指针在我们的内存池范围内
    if (buffer < _framebuffer_heap_start || buffer >= static_cast<char*>(_framebuffer_heap_start) + FRAMEBUFFER_HEAP_SIZE) {
        ESP_LOGW("MemManager", "Attempted to release a buffer not managed by this pool.");
        return;
    }

    Sys_LockGuard lock(_mutex);
    // 通过指针运算计算出块的索引
    size_t offset = static_cast<char*>(buffer) - static_cast<char*>(_framebuffer_heap_start);
    size_t index = offset / _block_size;

    if (index < _block_count) {
        if (_block_is_used[index]) {
            _block_is_used[index] = false;
            DEBUG_LOG("Released framebuffer block %d", index);
        } else {
            ESP_LOGW("MemManager", "Attempted to release a block that was already free.");
        }
    } else {
        ESP_LOGE("MemManager", "Calculated invalid block index on release.");
    }
}

/**
 * @brief 打印内存池和系统堆的当前状态。
 */
void Sys_MemoryManager::printMemoryInfo() {
    Sys_LockGuard lock(_mutex);
    ESP_LOGI("MemManager", "--- Memory Pool Info ---");

    // 计算已用块
    int used_blocks = 0;
    for(size_t i = 0; i < _block_count; ++i) {
        if (_block_is_used[i]) {
            used_blocks++;
        }
    }
    ESP_LOGI("MemManager", "Framebuffer Pool: Used %d / %d blocks", used_blocks, _block_count);
    
    ESP_LOGI("MemManager", "--- System Heap Info ---");
    multi_heap_info_t info;
    
    // 打印默认内部SRAM堆信息
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI("MemManager", "SRAM Heap: Free=%d, MinFree=%d, LargestFree=%d", 
             info.total_free_bytes, info.minimum_free_bytes, info.largest_free_block);

    // 打印默认外部PSRAM堆信息
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI("MemManager", "PSRAM Heap (General): Free=%d, MinFree=%d, LargestFree=%d", 
             info.total_free_bytes, info.minimum_free_bytes, info.largest_free_block);
    
    ESP_LOGI("MemManager", "------------------------");
}
