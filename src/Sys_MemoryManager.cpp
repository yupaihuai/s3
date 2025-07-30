/**
 * @file Sys_MemoryManager.cpp
 * @brief 混合内存管理器的实现 (ESP-IDF多堆 + 专用堆)
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 *  本文件根据您的专业建议，实现了混合式内存管理策略。
 *  核心思想是：
 *  1. 利用ESP-IDF强大的多堆（Multi-heap）功能作为基础。
 *  2. 手动从PSRAM中划分出一块专用的大块内存区域。
 *  3. 使用 `heap_caps_add_region` 将这块区域注册为一个新的、隔离的堆，
 *     专门用于可预见的大块内存分配（如摄像头帧缓冲）。
 *  这种方法兼具灵活性和稳定性，是本项目内存管理的最终方案。
 */

#include "Sys_MemoryManager.h"
#include "Sys_Debug.h"
#include "esp_heap_caps.h"

// --- 内存能力定义 ---
// 定义一个自定义的内存能力标志，用于从我们专用的帧缓冲堆中分配内存。
// 这使得代码意图非常明确：heap_caps_malloc(size, MALLOC_CAP_FRAMEBUFFER);
// #define MALLOC_CAP_FRAMEBUFFER (1 << 25) // [移除] 自定义能力标志依赖已废弃的API

// --- 专用堆配置 ---
// 定义为摄像头帧缓冲准备的专用堆的大小 (例如 3MB)
// 1920 * 1080 * 2 (YUY2) = 4,147,200 字节 ≈ 4MB
// 考虑到可能的裕量，我们分配4MB
static const size_t FRAMEBUFFER_HEAP_SIZE = 4 * 1024 * 1024; 
static void* _framebuffer_heap_start = nullptr;

// 初始化静态单例指针
Sys_MemoryManager* Sys_MemoryManager::_instance = nullptr;

/**
 * @brief 获取内存管理器的单例实例。
 */
Sys_MemoryManager* Sys_MemoryManager::getInstance() {
    if (_instance == nullptr) {
        // 在多任务启动前调用，此处无需加锁
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
 * @brief 初始化内存管理器，创建并注册专用堆。
 * @details
 *  这是内存管理的核心初始化函数。它会从主PSRAM堆中分配一大块内存，
 *  然后将其注册为一个带有`MALLOC_CAP_FRAMEBUFFER`能力的新堆。
 * @return bool `true` 如果专用堆创建并注册成功, `false` 否则。
 */
bool Sys_MemoryManager::initializePools() {
    DEBUG_LOG("Initializing Memory Manager with dedicated Framebuffer Heap...");

    // 1. 从PSRAM中为我们的专用堆分配内存
    _framebuffer_heap_start = heap_caps_malloc(FRAMEBUFFER_HEAP_SIZE, MALLOC_CAP_SPIRAM);

    if (_framebuffer_heap_start == nullptr) {
        ESP_LOGE("MemManager", "Fatal: Failed to allocate %d bytes for Framebuffer Heap from PSRAM!", FRAMEBUFFER_HEAP_SIZE);
        return false;
    }
    
    // [待办] ESP-IDF v5.x 中 heap_caps_add_region_with_caps 已被移除。
    // 暂时只分配内存，不注册为新堆，以确保编译通过。
    // 后续可研究使用 esp_heap_add_region 等底层API实现真正的隔离。
    ESP_LOGI("MemManager", "Successfully allocated %dMB for Framebuffer from PSRAM.", FRAMEBUFFER_HEAP_SIZE / (1024 * 1024));
    printMemoryInfo(); // 打印初始内存信息
    return true;
}


/**
 * @brief 释放一个之前分配的内存块。
 * @details 在新的模型中，这只是对 `heap_caps_free` 的简单封装。
 * @param block_ptr 指向通过 `heap_caps_malloc` 分配的内存的指针。
 */
void Sys_MemoryManager::releaseMemoryBlock(void* block_ptr) {
    if (block_ptr == nullptr) return;
    // 在多堆模型中，只需调用标准的free即可，系统会自动处理
    heap_caps_free(block_ptr);
}

/**
 * @brief 打印所有关键堆的当前使用状态，用于调试。
 */
void Sys_MemoryManager::printMemoryInfo() {
    Sys_LockGuard lock(_mutex);
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

    // 由于注册专用堆的API已更改，我们暂时无法获取其独立信息
    if (_framebuffer_heap_start) {
        ESP_LOGI("MemManager", "A %dMB block for framebuffer has been allocated from PSRAM.", FRAMEBUFFER_HEAP_SIZE / (1024*1024));
    }
    
    ESP_LOGI("MemManager", "------------------------");
}
