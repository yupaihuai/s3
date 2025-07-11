/**
 * @file Sys_MemoryManager.cpp
 * @brief PSRAM多池内存管理器的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 实现了内存池的创建、分配和释放的全部逻辑。
 * 内存池的配置集中在此文件的顶部，便于统一管理和调整。
 * 通过FreeRTOS互斥锁和`Sys_LockGuard`确保了所有内存操作的线程安全。
 */
#include "Sys_MemoryManager.h"
#include "Sys_Debug.h"
#include "esp_heap_caps.h"

// 初始化静态单例指针
Sys_MemoryManager* Sys_MemoryManager::_instance = nullptr;

/**
 * @struct PoolConfig
 * @brief 用于清晰地定义每个内存池的配置。
 */
struct PoolConfig {
    const char* name;       // 池的名称
    size_t block_size;      // 每个块的大小（字节）
    int block_count;        // 块的数量
};

/**
 * @brief 在此集中定义您的多池配置。
 * @details
 *  - 根据应用场景（摄像头、文件上传等）定义不同尺寸和数量的内存池。
 *  - 这是调整系统PSRAM内存策略的核心位置。
 */
static const PoolConfig pool_configs[] = {
    {"FrameBuffer_Pool", 1024 * 1024, 4}, // 池0: 用于摄像头帧缓冲，4个1MB的块
    {"FileUpload_Pool",  256 * 1024,  8}, // 池1: 用于文件上传，8个256KB的块
    {"GeneralData_Pool",  64 * 1024, 16}  // 池2: 用于通用大块数据，16个64KB的块
};

/**
 * @brief 私有构造函数
 */
Sys_MemoryManager::Sys_MemoryManager() {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        ESP_LOGE("MemManager", "FATAL: Failed to create mutex!");
    }
}

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
 * @brief 初始化所有内存池。
 */
bool Sys_MemoryManager::initializePools() {
    if (_mutex == NULL) return false;

    DEBUG_LOG("Initializing PSRAM memory pools...");
    
    Sys_LockGuard lock(_mutex);

    for (const auto& config : pool_configs) {
        size_t total_pool_size = config.block_size * config.block_count;

        // 从PSRAM中分配一大块连续内存作为池的存储空间。
        // 使用 MALLOC_CAP_8BIT 确保按字节访问。
        void* pool_start = heap_caps_malloc(total_pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        
        if (pool_start) {
            // 分配成功，创建并配置MemoryPool结构体
            MemoryPool pool;
            pool.name = config.name;
            pool.start_ptr = pool_start;
            pool.total_size = total_pool_size;
            pool.block_size = config.block_size;
            pool.block_count = config.block_count;
            pool.used_flags.assign(config.block_count, false); // 将所有块的标志位初始化为“未使用”
            _pools.push_back(pool);
            DEBUG_LOG("Created pool '%s': %d blocks of %u KB, total %u KB", config.name, config.block_count, config.block_size / 1024, total_pool_size / 1024);
        } else {
            ESP_LOGE("MemManager", "FATAL: Failed to allocate %u KB for pool '%s'!", total_pool_size / 1024, config.name);
            // 注意：即使部分池失败，也继续尝试创建其他池，但最终应返回失败
        }
    }

    // 验证并打印总的PSRAM分配量
    size_t total_allocated = 0;
    for(const auto& pool : _pools) total_allocated += pool.total_size;
    ESP_LOGI("MemManager", "Total PSRAM allocated for pools: %.2f MB", (float)total_allocated / (1024*1024));

    return !_pools.empty();
}

/**
 * @brief 根据请求大小获取一个内存块。
 */
void* Sys_MemoryManager::getMemoryBlock(size_t required_size) {
    // 策略：从后向前遍历池（即从最小的池开始），找到第一个尺寸足够且有空闲块的池。
    // 这样可以尽可能地用“刚刚好”的块来满足请求，避免用大块满足小请求造成的浪费。
    for (size_t i = _pools.size(); i-- > 0; ) {
         if (required_size <= _pools[i].block_size) {
             void* block = getMemoryBlockFromPool(i);
             if (block) return block; // 如果在这个池里找到了，就直接返回
         }
    }
    
    // 降级策略：如果所有“刚刚好”的池都满了，再从头遍历，尝试用更大的池来满足请求。
    for (size_t i = 0; i < _pools.size(); ++i) {
        if (required_size <= _pools[i].block_size) {
            void* block = getMemoryBlockFromPool(i);
            if (block) {
                ESP_LOGW("MemManager", "Allocated a larger block (%u KB) from pool '%s' for a smaller request (%u KB)", 
		_pools[i].block_size / 1024, _pools[i].name, required_size / 1024);
                return block;
            }
        }
    }

    ESP_LOGE("MemManager", "No suitable memory block found for size %u bytes!", required_size);
    return nullptr;
}

/**
 * @brief 从指定的池中获取一个内存块。
 */
void* Sys_MemoryManager::getMemoryBlockFromPool(size_t pool_index) {
    if (pool_index >= _pools.size()) {
        ESP_LOGE("MemManager", "Invalid pool index requested: %d", pool_index);
        return nullptr;
    }

    Sys_LockGuard lock(_mutex);
    void* block_ptr_to_return = nullptr;
    auto& pool = _pools[pool_index];

    // 遍历指定池的标志位向量，寻找一个空闲块。
    for (int i = 0; i < pool.block_count; ++i) {
        if (!pool.used_flags[i]) {
            pool.used_flags[i] = true; // 标记为已使用
            block_ptr_to_return = static_cast<uint8_t*>(pool.start_ptr) + (i * pool.block_size);
            DEBUG_LOG("Allocated block from pool '%s' at index %d", pool.name, i);
            break; // 找到后立即退出循环
        }
    }

    // --- 临界区结束：必须释放锁 ---
    xSemaphoreGive(_mutex);
    
    if (!block_ptr_to_return) {
        ESP_LOGW("MemManager", "Pool '%s' is full! Cannot allocate block.", pool.name);
    }
    
    return block_ptr_to_return;
}

/**
 * @brief 释放一个内存块。
 */
void Sys_MemoryManager::releaseMemoryBlock(void* block_ptr) {
    if (!block_ptr) return; // 安全检查：不处理空指针

    Sys_LockGuard lock(_mutex);
    bool block_found = false;

    // [优化] 直接通过指针地址范围判断所属的池，而不是遍历
    for (auto& pool : _pools) {
        uintptr_t start = (uintptr_t)pool.start_ptr;
        uintptr_t end = start + pool.total_size;
        uintptr_t ptr = (uintptr_t)block_ptr;

        // 判断传入的指针地址是否落在当前池的内存范围内。
        if (ptr >= start && ptr < end) {
            // 计算指针相对于池起始地址的偏移，从而推算出它属于第几个块。
            size_t offset = ptr - start;
            int index = offset / pool.block_size;

            // 健壮性检查
            if (index >= 0 && index < pool.block_count) {
                if (pool.used_flags[index]) {
                    pool.used_flags[index] = false; // 标记为未使用，即“归还”
                    DEBUG_LOG("Released block to pool '%s' at index %d", pool.name, index);
                } else {
                    // 警告：尝试释放一个已经是空闲状态的块（重复释放）。
                    ESP_LOGW("MemManager", "Attempt to double-free a block in pool '%s' at index %d", pool.name, index);
                }
            }
            block_found = true;
            break; // 找到池后即可退出循环
        }
    }

    if (!block_found) {
        ESP_LOGW("MemManager", "Attempt to free a memory block (at 0x%p) not managed by any pool.", block_ptr);
    }
}

/**
 * @brief 打印所有内存池的使用信息。
 */
void Sys_MemoryManager::printMemoryInfo() {
    Sys_LockGuard lock(_mutex);
    ESP_LOGI("MemManager", "--- PSRAM Memory Pool Status ---");
    for (size_t i = 0; i < _pools.size(); ++i) {
        const auto& pool = _pools[i];
        int used_count = 0;
        // 统计已使用的块数量
        for(bool used : pool.used_flags) {
            if(used) used_count++;
        }
        ESP_LOGI("MemManager", "Pool %d ('%s'): %d/%d blocks used (Block Size: %u KB)", i, pool.name, used_count, pool.block_count, pool.block_size / 1024);
    }
}
