/**
 * @file Sys_NvsManager.h
 * @brief NVS（非易失性存储）底层操作工具类的接口定义
 * @author [ANEAK]
 * @date [2025/7]
 *
 * @details
 * 该模块提供了一组静态方法，用于对NVS进行类型安全的读写操作。
 * 它被设计为上层管理器（如 `Sys_SettingsManager`）的底层服务，
 * 封装了NVS句柄的打开、关闭以及详细的错误处理，使得上层逻辑更清晰。
 * 这个类不应该被实例化。
 */
#pragma once

#include <Arduino.h>
#include "nvs_flash.h"
#include "nvs.h"
#include <type_traits> // 用于 std::is_same

/**
 * @class Sys_NvsManager
 * @brief 一个提供类型安全的NVS读写操作的静态工具类。
 */
class Sys_NvsManager {
public:
    // 删除默认构造函数，明确表示这是一个纯静态工具类，禁止实例化。
    Sys_NvsManager() = delete;

    /**
     * @brief 初始化NVS分区。系统启动时必须调用一次。
     * @details 会处理NVS分区损坏或版本不兼容的情况，并在必要时擦除并重新初始化分区。
     * @return esp_err_t `ESP_OK` 表示成功。
     */
    static esp_err_t initialize();

    /**
     * @brief 从NVS中读取一个基本数据类型的值。
     * @tparam T 要读取的值的类型 (如 `uint8_t`, `int32_t`, `bool` 等)。
     * @param ns_name NVS命名空间 (Namespace)。
     * @param key 键名。
     * @param out_value 用于接收读取值的变量的引用。
     * @return bool `true` 表示成功, `false` 表示失败 (如键未找到或类型不匹配)。
     */
    template<typename T>
    static bool readValue(const char* ns_name, const char* key, T& out_value);

    /**
     * @brief 向NVS写入一个基本数据类型的值。
     * @details 操作完成后会自动提交 (commit)。
     * @tparam T 要写入的值的类型。
     * @param ns_name NVS命名空间。
     * @param key 键名。
     * @param value 要写入的值。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    template<typename T>
    static bool writeValue(const char* ns_name, const char* key, T value);

    /**
     * @brief 从NVS中读取一个字符串。
     * @param ns_name NVS命名空间。
     * @param key 键名。
     * @param out_buffer 用于接收字符串的缓冲区。
     * @param buffer_size 缓冲区的最大尺寸。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    static bool readString(const char* ns_name, const char* key, char* out_buffer, size_t buffer_size);

    /**
     * @brief 向NVS写入一个字符串。
     * @param ns_name NVS命名空间。
     * @param key 键名。
     * @param value 要写入的字符串。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    static bool writeString(const char* ns_name, const char* key, const char* value);
    
    /**
     * @brief 从NVS中读取一个二进制大数据块 (BLOB)。
     * @param ns_name NVS命名空间。
     * @param key 键名。
     * @param out_blob 用于接收数据的缓冲区指针。
     * @param length [in/out] 输入时表示缓冲区的最大容量，输出时表示实际读取的字节数。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    static bool readBlob(const char* ns_name, const char* key, void* out_blob, size_t* length);
    
    /**
     * @brief 向NVS写入一个二进制大数据块 (BLOB)。
     * @param ns_name NVS命名空间。
     * @param key 键名。
     * @param blob 要写入的数据的指针。
     * @param length 数据的长度。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    static bool writeBlob(const char* ns_name, const char* key, const void* blob, size_t length);
    
    /**
     * @brief 擦除指定命名空间下的所有键值对。
     * @warning 这是一个危险操作，会删除该命名空间下的所有数据。
     * @param ns_name 要擦除的命名空间。
     * @return bool `true` 表示成功, `false` 表示失败。
     */
    static bool eraseNamespace(const char* ns_name);
};

// --- 模板函数的实现必须放在头文件中 ---

/**
 * @brief `readValue` 模板函数的具体实现。
 * @details
 *  - 使用 `std::is_same` 来进行类型判断，并在编译时确定要调用哪个nvs_get_...函数。
 *  - 对 `bool` 类型进行了特殊处理，通过 `uint8_t` 进行存储和读取。
 */
template<typename T>
bool Sys_NvsManager::readValue(const char* ns_name, const char* key, T& out_value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // 如果打开命名空间失败，直接返回false。
        return false;
    }

    // 使用if-constexpr (C++17) 或 std::is_same (C++11) 进行类型分发
    if (std::is_same<T, uint8_t>::value)       err = nvs_get_u8(nvs_handle, key, reinterpret_cast<uint8_t*>(&out_value));
    else if (std::is_same<T, int8_t>::value)   err = nvs_get_i8(nvs_handle, key, reinterpret_cast<int8_t*>(&out_value));
    else if (std::is_same<T, uint16_t>::value) err = nvs_get_u16(nvs_handle, key, reinterpret_cast<uint16_t*>(&out_value));
    else if (std::is_same<T, int16_t>::value)  err = nvs_get_i16(nvs_handle, key, reinterpret_cast<int16_t*>(&out_value));
    else if (std::is_same<T, uint32_t>::value) err = nvs_get_u32(nvs_handle, key, reinterpret_cast<uint32_t*>(&out_value));
    else if (std::is_same<T, int32_t>::value)  err = nvs_get_i32(nvs_handle, key, reinterpret_cast<int32_t*>(&out_value));
    else if (std::is_same<T, uint64_t>::value) err = nvs_get_u64(nvs_handle, key, reinterpret_cast<uint64_t*>(&out_value));
    else if (std::is_same<T, int64_t>::value)  err = nvs_get_i64(nvs_handle, key, reinterpret_cast<int64_t*>(&out_value));
    else if (std::is_same<T, bool>::value) {
        uint8_t val = 0;
        err = nvs_get_u8(nvs_handle, key, &val);
        if (err == ESP_OK) {
            *reinterpret_cast<bool*>(&out_value) = (val != 0);
        }
    } else {
        // 如果传入了不支持的类型，返回类型不匹配错误。
        err = ESP_ERR_NVS_TYPE_MISMATCH;
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}

/**
 * @brief `writeValue` 模板函数的具体实现。
 */
template<typename T>
bool Sys_NvsManager::writeValue(const char* ns_name, const char* key, T value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;

    if (std::is_same<T, uint8_t>::value)       err = nvs_set_u8(nvs_handle, key, *reinterpret_cast<const uint8_t*>(&value));
    else if (std::is_same<T, int8_t>::value)   err = nvs_set_i8(nvs_handle, key, *reinterpret_cast<const int8_t*>(&value));
    else if (std::is_same<T, uint16_t>::value) err = nvs_set_u16(nvs_handle, key, *reinterpret_cast<const uint16_t*>(&value));
    else if (std::is_same<T, int16_t>::value)  err = nvs_set_i16(nvs_handle, key, *reinterpret_cast<const int16_t*>(&value));
    else if (std::is_same<T, uint32_t>::value) err = nvs_set_u32(nvs_handle, key, *reinterpret_cast<const uint32_t*>(&value));
    else if (std::is_same<T, int32_t>::value)  err = nvs_set_i32(nvs_handle, key, *reinterpret_cast<const int32_t*>(&value));
    else if (std::is_same<T, uint64_t>::value) err = nvs_set_u64(nvs_handle, key, *reinterpret_cast<const uint64_t*>(&value));
    else if (std::is_same<T, int64_t>::value)  err = nvs_set_i64(nvs_handle, key, *reinterpret_cast<const int64_t*>(&value));
    else if (std::is_same<T, bool>::value)     err = nvs_set_u8(nvs_handle, key, static_cast<uint8_t>(*reinterpret_cast<const bool*>(&value)));
    else {
        err = ESP_ERR_NVS_TYPE_MISMATCH;
    }

    // 只有在set操作成功后，才执行commit操作。
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err == ESP_OK;
}