/**
 * @file Sys_NvsManager.cpp
 * @brief NVS底层操作工具类的实现文件
 * @author [ANEAK]
 * @date [2025/7]
 * 
 * @details
 * 实现了`Sys_NvsManager`中所有非模板静态方法的具体逻辑。
 */
#include "Sys_NvsManager.h"
#include "Sys_Debug.h" // 使用我们自己的调试宏

/**
 * @brief 初始化NVS分区。
 */
esp_err_t Sys_NvsManager::initialize() {
    esp_err_t err = nvs_flash_init();
    // 当NVS分区已满或发现一个不兼容的旧版本时，最好的做法是擦除它。
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("NVS", "NVS partition contains no free pages or is a new version, erasing...");
        // 警告: NVS分区无空闲页或版本不兼容，正在擦除...
        ESP_ERROR_CHECK(nvs_flash_erase()); // 擦除NVS分区
        err = nvs_flash_init(); // 再次尝试初始化
    }
    ESP_ERROR_CHECK(err); // 检查最终的初始化结果，如果失败则会panic
    
    if (err == ESP_OK) {
        DEBUG_LOG("NVS storage initialized successfully."); // 调试日志: NVS存储初始化成功。
    }
    return err;
}

/**
 * @brief 读取一个字符串。
 */
bool Sys_NvsManager::readString(const char* ns_name, const char* key, char* out_buffer, size_t buffer_size) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;

    // nvs_get_str会处理字符串长度和缓冲区的匹配
    err = nvs_get_str(nvs_handle, key, out_buffer, &buffer_size);
    
    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 写入一个字符串。
 */
bool Sys_NvsManager::writeString(const char* ns_name, const char* key, const char* value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(nvs_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 读取一个二进制数据块 (BLOB)。
 */
bool Sys_NvsManager::readBlob(const char* ns_name, const char* key, void* out_blob, size_t* length) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;

    // nvs_get_blob会处理长度检查
    err = nvs_get_blob(nvs_handle, key, out_blob, length);

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 写入一个二进制数据块 (BLOB)。
 */
bool Sys_NvsManager::writeBlob(const char* ns_name, const char* key, const void* blob, size_t length) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;
    
    err = nvs_set_blob(nvs_handle, key, blob, length);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 擦除整个命名空间。
 */
bool Sys_NvsManager::eraseNamespace(const char* ns_name) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open namespace '%s' for erasing.", ns_name);
        return false;
    }

    err = nvs_erase_all(nvs_handle);
    if(err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to erase or commit namespace '%s'.", ns_name);
    } else {
        ESP_LOGI("NVS", "Namespace '%s' erased successfully.", ns_name);
    }
    
    return err == ESP_OK;
}
