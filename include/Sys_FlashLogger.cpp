/**
 * @file Sys_FlashLogger.cpp
 * @brief Flash友好型日志记录器的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 实现了日志的缓冲、后台任务的创建和文件写入逻辑。
 * 它利用了FreeRTOS的Ringbuf和两种同步原语，实现了高效、线程安全的日志处理：
 * 1. **`_flush_semaphore` (二元信号量):** 用于从任何任务触发一次刷写操作。
 * 2. **`_file_mutex` (互斥信号量):** 用于保护对日志文件的实际I/O，防止并发访问导致的文件系统损坏。
 */
#include "Sys_FlashLogger.h"
#include "Sys_Debug.h"
#include "Sys_Filesystem.h" // 需要文件系统来操作文件
#include <cstdarg>

// 初始化静态单例指针
Sys_FlashLogger* Sys_FlashLogger::_instance = nullptr;

Sys_FlashLogger::Sys_FlashLogger() {}

/**
 * @brief 获取日志记录器的单例实例。
 */
Sys_FlashLogger* Sys_FlashLogger::getInstance() {
    if (_instance == nullptr) {
        _instance = new Sys_FlashLogger();
    }
    return _instance;
}

/**
 * @brief 初始化日志记录器。
 */
bool Sys_FlashLogger::begin(const char* log_filepath, size_t buffer_size, uint32_t flush_interval_ms) {
    if (_ring_buffer_handle) {
        ESP_LOGW("FlashLogger", "Flash Logger already initialized.");
        return true;
    }
    
    DEBUG_LOG("Initializing Flash Logger...");
    _log_filepath = log_filepath;
    _flush_interval_ms = flush_interval_ms;
    
    // 步骤1：创建环形缓冲区。
    // 类型设为 RINGBUF_TYPE_NOSPLIT 确保日志条目在缓冲区中是连续的，不会被分割。
    _ring_buffer_handle = xRingbufferCreate(buffer_size, RINGBUF_TYPE_NOSPLIT);
    if (!_ring_buffer_handle) {
        ESP_LOGE("FlashLogger", "FATAL: Failed to create ring buffer!");
        return false;
    }

    // 步骤2：创建用于手动触发刷写的二进制信号量。
    _flush_semaphore = xSemaphoreCreateBinary();
    if (!_flush_semaphore) {
        vRingbufferDelete(_ring_buffer_handle); // 清理已创建的资源
        ESP_LOGE("FlashLogger", "FATAL: Failed to create flush semaphore!");
        return false;
    }

    // 步骤3：创建用于保护文件操作的互斥锁
    _file_mutex = xSemaphoreCreateMutex();
    if (!_file_mutex) {
        vRingbufferDelete(_ring_buffer_handle);
        vSemaphoreDelete(_flush_semaphore);
        ESP_LOGE("FlashLogger", "FATAL: Failed to create file mutex!");
        return false;
    }

    // 步骤4：创建后台刷写任务
    xTaskCreatePinnedToCore(flushTask, "FlashLog_FlushTask", 4096, this, 1, &_flush_task_handle, 1);

    ESP_LOGI("FlashLogger", "Initialized. Logging to '%s', buffer: %u B, flush interval: %u ms", 
             _log_filepath.c_str(), buffer_size, flush_interval_ms);
    return true;
}

/**
 * @brief 记录一条格式化的日志。
 */
void Sys_FlashLogger::log(const char* format, ...) {
    if (!_ring_buffer_handle) return; // 安全检查：如果未初始化则不执行任何操作
    
    char buffer[256]; // 限制单条日志的最大长度，防止栈溢出
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len > 0) {
        // xRingbufferSend 是线程安全的，无需额外加锁。
        if (xRingbufferSend(_ring_buffer_handle, buffer, len, pdMS_TO_TICKS(10)) != pdTRUE) {
            // 缓冲区满，日志被丢弃
            ESP_LOGW("FlashLogger", "Ring buffer full, log message dropped.");
        }
    }
}

/**
 * @brief 强制刷写缓冲区。
 */
void Sys_FlashLogger::flush() {
    if (_flush_semaphore) {
        xSemaphoreGive(_flush_semaphore); // 释放信号量，以唤醒后台任务
    }
}

/**
 * @brief 线程安全地清理日志文件。
 */
void Sys_FlashLogger::clearLogFile() {
    // [优化] 关键：在接触文件系统前，获取文件互斥锁。
    Sys_LockGuard lock(_file_mutex);

    if (FFat.exists(_log_filepath)) {
        if (FFat.remove(_log_filepath)) {
            ESP_LOGI("FlashLogger", "Log file '%s' cleared.", _log_filepath.c_str());
        } else {
            ESP_LOGE("FlashLogger", "Failed to clear log file '%s'.", _log_filepath.c_str());
        }
    }
}

// --- 私有辅助方法 (Private Methods) ---

/**
 * @brief 将缓冲区数据写入文件的核心逻辑。
 */
void Sys_FlashLogger::writeBufferToFile() {
    // 步骤1：检查缓冲区是否有数据，避免不必要的加锁和文件打开。这是一个无锁的快速检查。
    UBaseType_t items_waiting = 0;
    if(_ring_buffer_handle) {
        vRingbufferGetInfo(_ring_buffer_handle, NULL, NULL, NULL, &items_waiting);
    }
    if (items_waiting == 0) {
        return;
    }

    // 步骤2：获取文件锁，准备执行I/O操作
    // [优化] 使用RAII锁来保证即使发生错误也能释放锁
    Sys_LockGuard lock(_file_mutex);
    
    // 打开文件准备写入（以追加模式）
    File logFile = FFat.open(_log_filepath, "a");
    if (!logFile) {
        ESP_LOGE("FlashLogger", "Failed to open log file for appending: %s", _log_filepath.c_str());
        return;
    }
    
    DEBUG_LOG("Flushing log buffer to flash...");
    size_t total_written = 0;
    size_t item_size;
    
    // 循环处理，直到缓冲区中的所有数据项都被取出并写入
    while (true) {
        char* item = (char*)xRingbufferReceive(_ring_buffer_handle, &item_size, 0);
        if (item == NULL) {
            // 缓冲区已空，退出循环
            break;
        }

        logFile.write((uint8_t*)item, item_size);
        logFile.write('\n'); // 每条日志后添加换行符
        total_written += item_size;

        // 必须返还item的内存给ringbuffer，以便它可以被重用
        vRingbufferReturnItem(_ring_buffer_handle, (void*)item);
    }

    logFile.close();
    
    DEBUG_LOG("Flush complete. %u bytes written to '%s'.", total_written, _log_filepath.c_str());
}

/**
 * @brief 后台任务的静态循环函数。
 */
void Sys_FlashLogger::flushTask(void* parameter) {
    Sys_FlashLogger* self = (Sys_FlashLogger*)parameter;

    for (;;) {
        // 阻塞等待，直到被手动flush()或超时唤醒
        xSemaphoreTake(self->_flush_semaphore, pdMS_TO_TICKS(self->_flush_interval_ms));
        
        // 无论是哪种方式唤醒，都执行一次刷写操作。
        self->writeBufferToFile();
    }
}
