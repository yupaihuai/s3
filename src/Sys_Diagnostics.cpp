/**
 * @file Sys_Diagnostics.cpp
 * @brief 系统诊断服务模块的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 实现了所有具体的诊断检查逻辑。
 * 通过`#if CORE_DEBUG_MODE`宏，确保本文件所有代码只在调试模式下被编译。
 */
#include "Sys_Diagnostics.h"

// 整个文件内容都受此宏保护
#if CORE_DEBUG_MODE

// --- 必要的头文件 ---
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_spi_flash.h"
#include "esp32/spiram.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "Sys_Filesystem.h" // 需要获取文件系统状态

// --- 日志标签 ---
// 所有此模块的输出都会带上 `[Diagnostics]` 前缀
static const char* TAG = "Diagnostics";

// --- 主入口函数 ---

/**
 * @brief 运行所有诊断检查。
 */
void Sys_Diagnostics::run() {
    // 使用INFO级别，确保输出总是可见
    ESP_LOGI(TAG, "\n\n=============================================");
    ESP_LOGI(TAG, "      Running System Diagnostics Report");
    ESP_LOGI(TAG, "      运行系统诊断报告");
    ESP_LOGI(TAG, "=============================================");

    checkSystemInfo();
    checkMemory();
    checkPartitions();
    checkFileSystems();

    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "      Diagnostics Complete");
    ESP_LOGI(TAG, "      诊断完成");
    ESP_LOGI(TAG, "=============================================\n");
}

// --- 私有辅助函数 ---

/**
 * @brief 检查并打印核心系统信息。
 */
void Sys_Diagnostics::checkSystemInfo() {
    ESP_LOGI(TAG, "--- 1. System Information / 系统信息 ---");
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "  Chip / 芯片          : %s (rev %d)", ESP.getChipModel(), chip_info.revision);
    ESP_LOGI(TAG, "  CPU Cores / 内核数   : %d", chip_info.cores);
    ESP_LOGI(TAG, "  CPU Frequency / 频率 : %d MHz", ESP.getCpuFreqMHz());
    ESP_LOGI(TAG, "  ESP-IDF Version / 版本 : %s", esp_get_idf_version());
}

/**
 * @brief 检查并打印内存使用情况。
 */
void Sys_Diagnostics::checkMemory() {
    ESP_LOGI(TAG, "--- 2. Memory Verification / 内存验证 ---");
    
    // Flash 芯片物理大小
    ESP_LOGI(TAG, "  Flash Size (HW)    : %u MB", spi_flash_get_chip_size() / (1024 * 1024));

    // PSRAM 物理大小及编译模式
    if (esp_spiram_is_initialized()) {
        ESP_LOGI(TAG, "  PSRAM Size (HW)    : %u MB", esp_spiram_get_size() / (1024 * 1024));
        #if CONFIG_SPIRAM_MODE_OCT
            ESP_LOGI(TAG, "  PSRAM Mode (Compile) : OPI (Octal)");
        #elif CONFIG_SPIRAM_MODE_QUAD
            ESP_LOGI(TAG, "  PSRAM Mode (Compile) : QPI (Quad)");
        #endif
    } else {
        ESP_LOGW(TAG, "  PSRAM              : Not detected or not enabled!");
    }
    
    // 堆内存 (Heap) 分布情况
    ESP_LOGI(TAG, "  Heap (Internal)    : %u KB Free / %u KB Total", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024);
    ESP_LOGI(TAG, "  Heap (PSRAM)       : %u KB Free / %u KB Total", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024, heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024);
    
    // 确认PSRAM是否被成功集成
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
        ESP_LOGI(TAG, "  [OK] PSRAM is successfully integrated into the heap.");
    } else {
        ESP_LOGE(TAG, "  [FAIL] PSRAM not integrated! Check build flags & board config.");
    }
}

/**
 * @brief 检查并打印分区表。
 */
void Sys_Diagnostics::checkPartitions() {
    ESP_LOGI(TAG, "--- 3. Partition Table Verification / 分区表校验 ---");
    ESP_LOGI(TAG, "  %-10s | %-9s | %-10s | %-12s | %-s", "Type", "Subtype", "Address", "Size (bytes)", "Label");
    ESP_LOGI(TAG, "  -------------------------------------------------------------------");

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (!it) {
        ESP_LOGE(TAG, "  Error: Could not find any partitions!");
        return;
    }
    
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        char addr_str[11], size_str[13];
        snprintf(addr_str, sizeof(addr_str), "0x%08X", part->address);
        snprintf(size_str, sizeof(size_str), "%u", part->size);

        ESP_LOGI(TAG, "  %-10s | %-9d | %-10s | %-12s | %-s",
                 (part->type == ESP_PARTITION_TYPE_APP) ? "app" : "data",
                 part->subtype,
                 addr_str,
                 size_str,
                 part->label);
        
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    ESP_LOGI(TAG, "  [INFO] Compare this table with your 'my_8MB.csv' file.");
}

/**
 * @brief 检查并打印文件系统状态。
 */
void Sys_Diagnostics::checkFileSystems() {
    ESP_LOGI(TAG, "--- 4. File System Status / 文件系统状态 ---");
    Sys_Filesystem* fs = Sys_Filesystem::getInstance();
    
    if (fs->isLittleFSMounted()) {
         ESP_LOGI(TAG, "  LittleFS (UI)      : Mounted. Total: %llu KB, Used: %llu KB",
                 fs->getLittleFSTotalBytes() / 1024, fs->getLittleFSUsedBytes() / 1024);
         listDir(LittleFS, "/", 1); // 列出根目录内容，递归1层
    } else {
        ESP_LOGE(TAG, "  LittleFS (UI)      : [FAIL] Not mounted!");
    }
    
    if (fs->isFFatMounted()) {
        ESP_LOGI(TAG, "  FFat (Media)       : Mounted. Total: %.2f MB, Used: %.2f MB",
                 (float)fs->getFFatTotalBytes() / (1024*1024), (float)fs->getFFatUsedBytes() / (1024*1024));
        listDir(FFat, "/"); // 列出根目录内容
    } else {
        ESP_LOGE(TAG, "  FFat (Media)       : [FAIL] Not mounted!");
    }
}

/**
 * @brief 递归列出目录内容。
 * @details 这是一个辅助函数，用于checkFileSystems。
 */
void Sys_Diagnostics::listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
    ESP_LOGI(TAG, "    Listing directory: %s", dirname);

    File root = fs.open(dirname);
    if(!root){
        ESP_LOGE(TAG, "    - Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        ESP_LOGE(TAG, "    - Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            ESP_LOGI(TAG, "    DIR : %s", file.name());
            if(levels){
                listDir(fs, file.name(), levels - 1);
            }
        } else {
            ESP_LOGI(TAG, "    FILE: %s  SIZE: %u", file.name(), file.size());
        }
        file = file.openNextFile();
    }
}

#endif // CORE_DEBUG_MODE
