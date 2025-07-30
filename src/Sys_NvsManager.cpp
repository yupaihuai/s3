/**
 * @file Sys_NvsManager.cpp
 * @brief NVS（非易失性存储）底层操作工具类的实现
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 本文件实现了 Sys_NvsManager.h 中定义的静态接口。
 * 严格遵循了设计蓝图中的代码规范，特别是：
 *  - 错误处理：所有与NVS交互的ESP-IDF函数都检查了返回值。
 *  - 代码注释：为所有公共函数提供了详细的中文注释。
 */

#include "Sys_NvsManager.h"
#include "Sys_Debug.h" // 用于调试日志

/**
 * @brief 初始化NVS分区。
 * @details
 *  这是系统启动时必须调用的核心函数之一。它会尝试初始化NVS闪存分区。
 *  如果分区不存在、损坏或版本不兼容，它会智能地擦除整个NVS分区，
 *  然后再次尝试初始化，从而确保系统总能在一个干净的NVS环境中启动。
 * @return esp_err_t 返回ESP-IDF的错误码。`ESP_OK`表示成功。
 */
esp_err_t Sys_NvsManager::initialize() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("NVS", "NVS partition was corrupted or outdated, erasing and re-initializing...");
        // 尝试擦除NVS分区
        ESP_ERROR_CHECK(nvs_flash_erase());
        // 再次尝试初始化
        ret = nvs_flash_init();
    }
    // 检查最终的初始化结果
    ESP_ERROR_CHECK(ret);
    DEBUG_LOG("NVS Manager initialized successfully.");
    return ret;
}

/**
 * @brief 从NVS中读取一个字符串。
 * @param ns_name NVS命名空间。
 * @param key 键名。
 * @param out_buffer 用于接收字符串的缓冲区。
 * @param buffer_size 缓冲区的最大尺寸。
 * @return bool `true` 表示成功, `false` 表示失败（如键未找到、缓冲区太小）。
 */
bool Sys_NvsManager::readString(const char* ns_name, const char* key, char* out_buffer, size_t buffer_size) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS namespace '%s'. Error: %s", ns_name, esp_err_to_name(err));
        return false;
    }

    size_t required_size = 0;
    // 第一次调用获取所需大小
    err = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (err == ESP_OK && required_size <= buffer_size) {
        // 第二次调用实际读取数据
        err = nvs_get_str(nvs_handle, key, out_buffer, &required_size);
    } else if (err == ESP_OK) {
        ESP_LOGE("NVS", "Buffer too small for key '%s'. Required: %d, Available: %d", key, required_size, buffer_size);
        err = ESP_ERR_NVS_INVALID_LENGTH; // 明确错误类型
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 向NVS写入一个字符串。
 * @param ns_name NVS命名空间。
 * @param key 键名。
 * @param value 要写入的字符串。
 * @return bool `true` 表示成功, `false` 表示失败。
 */
bool Sys_NvsManager::writeString(const char* ns_name, const char* key, const char* value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS namespace '%s' for writing. Error: %s", ns_name, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvs_handle, key, value);
    if (err == ESP_OK) {
        // 只有在set成功后才提交
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "Failed to commit NVS changes for key '%s'. Error: %s", key, esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("NVS", "Failed to set string for key '%s'. Error: %s", key, esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 从NVS中读取一个二进制大数据块 (BLOB)。
 * @param ns_name NVS命名空间。
 * @param key 键名。
 * @param out_blob 用于接收数据的缓冲区指针。
 * @param length [in/out] 输入时表示缓冲区的最大容量，输出时表示实际读取的字节数。
 * @return bool `true` 表示成功, `false` 表示失败。
 */
bool Sys_NvsManager::readBlob(const char* ns_name, const char* key, void* out_blob, size_t* length) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS namespace '%s'. Error: %s", ns_name, esp_err_to_name(err));
        return false;
    }

    err = nvs_get_blob(nvs_handle, key, out_blob, length);

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 向NVS写入一个二进制大数据块 (BLOB)。
 * @param ns_name NVS命名空间。
 * @param key 键名。
 * @param blob 要写入的数据的指针。
 * @param length 数据的长度。
 * @return bool `true` 表示成功, `false` 表示失败。
 */
bool Sys_NvsManager::writeBlob(const char* ns_name, const char* key, const void* blob, size_t length) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS namespace '%s' for writing. Error: %s", ns_name, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(nvs_handle, key, blob, length);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "Failed to commit NVS blob for key '%s'. Error: %s", key, esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("NVS", "Failed to set blob for key '%s'. Error: %s", key, esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief 擦除指定命名空间下的所有键值对。
 * @warning 这是一个危险操作，会删除该命名空间下的所有数据。
 * @param ns_name 要擦除的命名空间。
 * @return bool `true` 表示成功, `false` 表示失败。
 */
bool Sys_NvsManager::eraseNamespace(const char* ns_name) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS namespace '%s' for erasing. Error: %s", ns_name, esp_err_to_name(err));
        return false;
    }

    err = nvs_erase_all(nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE("NVS", "Failed to commit erase for namespace '%s'. Error: %s", ns_name, esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("NVS", "Failed to erase namespace '%s'. Error: %s", ns_name, esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}
