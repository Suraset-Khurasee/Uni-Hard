/*
================================================================================
 MIT License

 Copyright (c) 2025 gafoo

 This file is part of the HardView project:
 https://github.com/gafoo173/HardView

 Licensed under the MIT License.
 See the LICENSE file in the project root for more details.
================================================================================
*/


#include "advanced_storage_info.h"
#include "helpers.h"
#ifdef _WIN32
#include "win_helpers.h"
#else
#include "linux_helpers.h"
#include <dirent.h>
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

// Function to escape backslashes - moved to top to avoid implicit declaration
char* escape_backslashes(const char* input) {
    if (!input) return NULL;
    
    size_t len = strlen(input);
    // Worst case: every character is a backslash => double + 1 for null terminator
    char* output = malloc(len * 2 + 1);
    if (!output) return NULL;
    
    char* out = output;
    for (const char* in = input; *in; ++in) {
        if (*in == '\\') *out++ = '\\';
        *out++ = *in;
    }
    *out = 0;
    return output;
}

#ifndef _WIN32
typedef struct {
    char disk_model[256];
    char disk_serial[128];
    char disk_interface[64];
    long long disk_size;
    char disk_media[64];
    char partition_device_id[272];
    char partition_type[384];
    long long partition_size;
    unsigned long partition_index;
} LinuxPartitionEntry;

static void _lookup_linux_partition_fstype(const char* dev_name, char* fstype_out, size_t out_sz) {
    snprintf(fstype_out, out_sz, "Linux Partition");
    char dev_path[272];
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", dev_name);

    FILE* fp = fopen("/proc/mounts", "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char m_dev[128], m_mnt[256], m_fstype[64];
        if (sscanf(line, "%127s %255s %63s", m_dev, m_mnt, m_fstype) == 3) {
            if (strcmp(m_dev, dev_path) == 0) {
                snprintf(fstype_out, out_sz, "%s (%s)", m_fstype, m_mnt);
                break;
            }
        }
    }
    fclose(fp);
}

static int _collect_linux_partitions(LinuxPartitionEntry* out_entries, int max_entries) {
    int count = 0;
    DIR* dir = opendir("/sys/block");
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && count < max_entries) {
        const char* disk = entry->d_name;
        if (disk[0] == '.') continue;
        if (strncmp(disk, "loop", 4) == 0 ||
            strncmp(disk, "ram", 3) == 0 ||
            strncmp(disk, "zram", 4) == 0 ||
            strncmp(disk, "dm-", 3) == 0 ||
            strncmp(disk, "sr", 2) == 0 ||
            strncmp(disk, "fd", 2) == 0) {
            continue;
        }

        char size_path[512];
        snprintf(size_path, sizeof(size_path), "/sys/block/%s/size", disk);
        char* size_str = _read_proc_sys_value(size_path, "");
        long long disk_sectors = (size_str && strcmp(size_str, "N/A") != 0) ? atoll(size_str) : 0;
        if (size_str) free(size_str);
        if (disk_sectors <= 0) continue;

        long long disk_bytes = disk_sectors * 512LL;

        char model_path[512];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", disk);
        char* model_str = _read_proc_sys_value(model_path, "");
        if (!model_str || strcmp(model_str, "N/A") == 0) {
            if (model_str) free(model_str);
            model_str = strdup(disk);
        }

        char serial_path[512];
        snprintf(serial_path, sizeof(serial_path), "/sys/block/%s/device/serial", disk);
        char* serial_str = _read_proc_sys_value(serial_path, "");
        if (!serial_str || strcmp(serial_str, "N/A") == 0) {
            if (serial_str) free(serial_str);
            serial_str = strdup("N/A");
        }

        char rot_path[512];
        snprintf(rot_path, sizeof(rot_path), "/sys/block/%s/queue/rotational", disk);
        char* rot_str = _read_proc_sys_value(rot_path, "");
        int rotational = (rot_str && strcmp(rot_str, "1") == 0) ? 1 : 0;
        if (rot_str) free(rot_str);

        const char* iface = "SATA/SCSI";
        if (strncmp(disk, "nvme", 4) == 0) iface = "NVMe";
        else if (strncmp(disk, "mmcblk", 6) == 0) iface = "MMC/SD";
        else if (strncmp(disk, "vd", 2) == 0) iface = "VirtIO";

        // Iterate partition subdirectories inside /sys/block/<disk>
        char disk_dir_path[512];
        snprintf(disk_dir_path, sizeof(disk_dir_path), "/sys/block/%s", disk);
        DIR* ddir = opendir(disk_dir_path);
        if (ddir) {
            struct dirent* pentry;
            size_t disk_len = strlen(disk);
            while ((pentry = readdir(ddir)) != NULL && count < max_entries) {
                if (strncmp(pentry->d_name, disk, disk_len) != 0) continue;
                if (pentry->d_name[disk_len] == '\0') continue;

                char part_idx_path[512];
                snprintf(part_idx_path, sizeof(part_idx_path), "/sys/block/%s/%s/partition", disk, pentry->d_name);
                char* part_idx_str = _read_proc_sys_value(part_idx_path, "");
                if (!part_idx_str || strcmp(part_idx_str, "N/A") == 0) {
                    if (part_idx_str) free(part_idx_str);
                    continue;
                }
                unsigned long part_index = (unsigned long)atol(part_idx_str);
                free(part_idx_str);

                char part_size_path[512];
                snprintf(part_size_path, sizeof(part_size_path), "/sys/block/%s/%s/size", disk, pentry->d_name);
                char* part_size_str = _read_proc_sys_value(part_size_path, "");
                long long part_sectors = (part_size_str && strcmp(part_size_str, "N/A") != 0) ? atoll(part_size_str) : 0;
                if (part_size_str) free(part_size_str);

                snprintf(out_entries[count].disk_model, sizeof(out_entries[count].disk_model), "%s", model_str);
                snprintf(out_entries[count].disk_serial, sizeof(out_entries[count].disk_serial), "%s", serial_str);
                snprintf(out_entries[count].disk_interface, sizeof(out_entries[count].disk_interface), "%s", iface);
                out_entries[count].disk_size = disk_bytes;
                snprintf(out_entries[count].disk_media, sizeof(out_entries[count].disk_media), "%s", rotational ? "HDD" : "SSD");
                snprintf(out_entries[count].partition_device_id, sizeof(out_entries[count].partition_device_id), "/dev/%s", pentry->d_name);
                _lookup_linux_partition_fstype(pentry->d_name, out_entries[count].partition_type, sizeof(out_entries[count].partition_type));
                out_entries[count].partition_size = part_sectors * 512LL;
                out_entries[count].partition_index = part_index;
                count++;
            }
            closedir(ddir);
        }

        free(model_str);
        free(serial_str);
    }
    closedir(dir);
    return count;
}
#endif

#ifdef BUILD_PYTHON_MODULE
// Helper function to create Python partition object
PyObject* create_partition_python_object(const char* disk_model, const char* disk_serial, 
                                       const char* disk_interface, long long disk_size,
                                       const char* disk_media, const char* partition_device_id,
                                       const char* partition_type, long long partition_size,
                                       unsigned long partition_index) {
    PyObject* partition_dict = PyDict_New();
    if (!partition_dict) return NULL;
    
    // Add all fields to the dictionary with proper error checking
    PyObject* temp_obj;
    
    temp_obj = PyUnicode_FromString(disk_model ? disk_model : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "disk_model", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(disk_serial ? disk_serial : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "disk_serial", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(disk_interface ? disk_interface : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "disk_interface", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLongLong(disk_size);
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "disk_size", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(disk_media ? disk_media : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "disk_media", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(partition_device_id ? partition_device_id : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "partition_device_id", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(partition_type ? partition_type : "");
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "partition_type", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLongLong(partition_size);
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "partition_size", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromUnsignedLong(partition_index);
    if (temp_obj) {
        PyDict_SetItemString(partition_dict, "partition_index", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    return partition_dict;
}

// Function that returns Python objects directly
PyObject* get_partitions_info_objects(bool Json) {
    if (Json) {
        // JSON mode - return string representation
        char* json_str = get_partitions_info(true);
        if (!json_str) return Py_None;
        
        PyObject* py_str = PyUnicode_FromString(json_str);
        free(json_str);
        return py_str ? py_str : Py_None;
    } else {
        // Python objects mode - return objects directly
#ifdef _WIN32
        IWbemLocator *pLoc = NULL;
        IWbemServices *pSvc = NULL;
        IEnumWbemClassObject *pEnumerator = NULL;
        IWbemClassObject *pclsObj = NULL;
        HRESULT hr;
        ULONG uReturn = 0;
        
        hr = _initialize_wmi(&pLoc, &pSvc);
        if (FAILED(hr)) {
            return Py_None;
        }
        
        BSTR query_lang = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT DeviceID, Model, SerialNumber, InterfaceType, Size, MediaType FROM Win32_DiskDrive");
        if (!query_lang || !query) {
            SysFreeString(query_lang); SysFreeString(query);
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return Py_None;
        }
        
        hr = pSvc->lpVtbl->ExecQuery(
            pSvc, query_lang, query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &pEnumerator
        );
        SysFreeString(query_lang);
        SysFreeString(query);
        
        if (FAILED(hr)) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return Py_None;
        }
        
        PyObject* partitions_list = PyList_New(0);
        if (!partitions_list) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return Py_None;
        }
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* device_id = _get_wmi_string_property(pclsObj, L"DeviceID");
            char* model = _get_wmi_string_property(pclsObj, L"Model");
            char* serial = _get_wmi_string_property(pclsObj, L"SerialNumber");
            char* interface_type = _get_wmi_string_property(pclsObj, L"InterfaceType");
            LONGLONG size = _get_wmi_longlong_property(pclsObj, L"Size");
            char* media_type = _get_wmi_string_property(pclsObj, L"MediaType");
            
            // Query partitions associated with the disk
            IEnumWbemClassObject* pPartEnum = NULL;
            IWbemClassObject* pPartObj = NULL;
            ULONG uPartRet = 0;
            char assoc_query[512];
            char* device_id_escaped = escape_backslashes(device_id ? device_id : "");
            snprintf(assoc_query, sizeof(assoc_query),
                "ASSOCIATORS OF {Win32_DiskDrive.DeviceID=\"%s\"} WHERE AssocClass=Win32_DiskDriveToDiskPartition",
                device_id_escaped
            );
            free(device_id_escaped);
            BSTR wql_bstr = SysAllocString(L"WQL");
            BSTR assoc_query_bstr = SysAllocStringLen(NULL, strlen(assoc_query));
            mbstowcs(assoc_query_bstr, assoc_query, strlen(assoc_query));
            assoc_query_bstr[strlen(assoc_query)] = 0;
            hr = pSvc->lpVtbl->ExecQuery(
                pSvc, wql_bstr, assoc_query_bstr,
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL, &pPartEnum
            );
            SysFreeString(wql_bstr);
            SysFreeString(assoc_query_bstr);
            
            if (SUCCEEDED(hr)) {
                while (SUCCEEDED(pPartEnum->lpVtbl->Next(pPartEnum, WBEM_INFINITE, 1, &pPartObj, &uPartRet)) && uPartRet == 1) {
                    char* part_device_id = _get_wmi_string_property(pPartObj, L"DeviceID");
                    char* part_type = _get_wmi_string_property(pPartObj, L"Type");
                    LONGLONG part_size = _get_wmi_longlong_property(pPartObj, L"Size");
                    ULONG part_index = _get_wmi_ulong_property(pPartObj, L"Index");
                    
                    PyObject* partition_obj = create_partition_python_object(
                        model, serial, interface_type, size, media_type,
                        part_device_id, part_type, part_size, part_index
                    );
                    
                    if (partition_obj) {
                        int append_result = PyList_Append(partitions_list, partition_obj);
                        if (append_result != 0) {
                            Py_DECREF(partition_obj);
                            Py_DECREF(partitions_list);
                            free(part_device_id); free(part_type);
                            if (pPartObj) { pPartObj->lpVtbl->Release(pPartObj); }
                            if (pPartEnum) pPartEnum->lpVtbl->Release(pPartEnum);
                            free(device_id); free(model); free(serial); free(interface_type); free(media_type);
                            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); }
                            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                            return Py_None;
                        }
                        Py_DECREF(partition_obj);
                    }
                    
                    free(part_device_id); free(part_type);
                    if (pPartObj) { pPartObj->lpVtbl->Release(pPartObj); pPartObj = NULL; }
                }
                if (pPartEnum) pPartEnum->lpVtbl->Release(pPartEnum);
            }
            free(device_id); free(model); free(serial); free(interface_type); free(media_type);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return partitions_list;
#else
        // Linux implementation
        PyObject* partitions_list = PyList_New(0);
        if (!partitions_list) {
            return Py_None;
        }

        LinuxPartitionEntry entries[64];
        int count = _collect_linux_partitions(entries, 64);
        for (int i = 0; i < count; i++) {
            PyObject* partition_obj = create_partition_python_object(
                entries[i].disk_model,
                entries[i].disk_serial,
                entries[i].disk_interface,
                entries[i].disk_size,
                entries[i].disk_media,
                entries[i].partition_device_id,
                entries[i].partition_type,
                entries[i].partition_size,
                entries[i].partition_index
            );
            if (partition_obj) {
                PyList_Append(partitions_list, partition_obj);
                Py_DECREF(partition_obj);
            }
        }
        return partitions_list;
#endif
    }
}
#endif

char* get_partitions_info(bool Json) {
#ifdef _WIN32
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    IEnumWbemClassObject *pEnumerator = NULL;
    IWbemClassObject *pclsObj = NULL;
    HRESULT hr;
    ULONG uReturn = 0;
    
    hr = _initialize_wmi(&pLoc, &pSvc);
    if (FAILED(hr)) {
        if (Json) {
            return _create_json_string("{\"error\": \"Failed to initialize WMI for partitions info: 0x%lx\"}", hr);
        } else {
            return NULL; // Return NULL for Python objects on error
        }
    }
    
    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT DeviceID, Model, SerialNumber, InterfaceType, Size, MediaType FROM Win32_DiskDrive");
    if (!query_lang || !query) {
        SysFreeString(query_lang); SysFreeString(query);
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        if (Json) {
            return _create_json_string("{\"error\": \"Memory allocation failed for WMI query strings.\"}");
        } else {
            return NULL;
        }
    }
    
    hr = pSvc->lpVtbl->ExecQuery(
        pSvc, query_lang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator
    );
    SysFreeString(query_lang);
    SysFreeString(query);
    
    if (FAILED(hr)) {
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        if (Json) {
            return _create_json_string("{\"error\": \"Failed to execute WMI query for partitions: 0x%lx\"}", hr);
        } else {
            return NULL;
        }
    }
    
    if (Json) {
        // JSON mode - original implementation
        size_t bufsize = 4096;
        size_t used = 0;
        char* json = (char*)malloc(bufsize);
        if (!json) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return strdup("{\"error\": \"Memory allocation failed for partitions JSON buffer.\"}");
        }
        strcpy(json, "{\"partitions\": [");
        used = strlen(json);
        int first = 1;
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* device_id = _get_wmi_string_property(pclsObj, L"DeviceID");
            char* model = _get_wmi_string_property(pclsObj, L"Model");
            char* serial = _get_wmi_string_property(pclsObj, L"SerialNumber");
            char* interface_type = _get_wmi_string_property(pclsObj, L"InterfaceType");
            LONGLONG size = _get_wmi_longlong_property(pclsObj, L"Size");
            char* media_type = _get_wmi_string_property(pclsObj, L"MediaType");
            
            IEnumWbemClassObject* pPartEnum = NULL;
            IWbemClassObject* pPartObj = NULL;
            ULONG uPartRet = 0;
            char assoc_query[512];
            char* device_id_escaped = escape_backslashes(device_id ? device_id : "");
            snprintf(assoc_query, sizeof(assoc_query),
                "ASSOCIATORS OF {Win32_DiskDrive.DeviceID=\"%s\"} WHERE AssocClass=Win32_DiskDriveToDiskPartition",
                device_id_escaped
            );
            free(device_id_escaped);
            BSTR wql_bstr = SysAllocString(L"WQL");
            BSTR assoc_query_bstr = SysAllocStringLen(NULL, strlen(assoc_query));
            mbstowcs(assoc_query_bstr, assoc_query, strlen(assoc_query));
            assoc_query_bstr[strlen(assoc_query)] = 0;
            hr = pSvc->lpVtbl->ExecQuery(
                pSvc, wql_bstr, assoc_query_bstr,
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL, &pPartEnum
            );
            SysFreeString(wql_bstr);
            SysFreeString(assoc_query_bstr);
            
            if (SUCCEEDED(hr)) {
                while (SUCCEEDED(pPartEnum->lpVtbl->Next(pPartEnum, WBEM_INFINITE, 1, &pPartObj, &uPartRet)) && uPartRet == 1) {
                    char* part_device_id = _get_wmi_string_property(pPartObj, L"DeviceID");
                    char* part_type = _get_wmi_string_property(pPartObj, L"Type");
                    LONGLONG part_size = _get_wmi_longlong_property(pPartObj, L"Size");
                    ULONG part_index = _get_wmi_ulong_property(pPartObj, L"Index");
                    
                    char* json_part = _create_json_string(
                        "%s{\"disk_model\":\"%s\",\"disk_serial\":\"%s\",\"disk_interface\":\"%s\",\"disk_size\":%lld,\"disk_media\":\"%s\",\"partition_device_id\":\"%s\",\"partition_type\":\"%s\",\"partition_size\":%lld,\"partition_index\":%lu}",
                        first ? "" : ",",
                        model ? model : "",
                        serial ? serial : "",
                        interface_type ? interface_type : "",
                        size,
                        media_type ? media_type : "",
                        part_device_id ? part_device_id : "",
                        part_type ? part_type : "",
                        part_size,
                        part_index
                    );
                    first = 0;
                    size_t part_len = strlen(json_part);
                    if (used + part_len + 3 >= bufsize) {
                        bufsize += part_len + 4096;
                        char* temp = (char*)realloc(json, bufsize);
                        if (!temp) {
                            free(json); free(json_part);
                            if (pPartEnum) pPartEnum->lpVtbl->Release(pPartEnum);
                            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                            return strdup("{\"error\": \"Memory re-allocation failed for partitions JSON buffer.\"}");
                        }
                        json = temp;
                    }
                    strcat(json, json_part);
                    used += part_len;
                    free(json_part);
                    free(part_device_id); free(part_type);
                    if (pPartObj) { pPartObj->lpVtbl->Release(pPartObj); pPartObj = NULL; }
                }
                if (pPartEnum) pPartEnum->lpVtbl->Release(pPartEnum);
            }
            free(device_id); free(model); free(serial); free(interface_type); free(media_type);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        if (first) {
            strcpy(json, "{\"partitions\": []}");
        } else {
            strcat(json, "]}");
        }
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return json;
    } else {
#ifdef BUILD_PYTHON_MODULE
        PyObject* partitions_list = get_partitions_info_objects(false);
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        if (partitions_list && partitions_list != Py_None) {
            PyObject* repr = PyObject_Repr(partitions_list);
            if (!repr) {
                Py_DECREF(partitions_list);
                return strdup("[]");
            }
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(partitions_list);
            return result;
        }
        return strdup("[]");
#else
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("[]");
#endif
    }
#else
    // Linux implementation
    LinuxPartitionEntry entries[64];
    int count = _collect_linux_partitions(entries, 64);

    if (Json) {
        size_t bufsize = 4096;
        size_t used = 0;
        char* json = (char*)malloc(bufsize);
        if (!json) {
            return strdup("{\"error\": \"Memory allocation failed for partitions JSON buffer.\"}");
        }
        strcpy(json, "{\"partitions\": [");
        used = strlen(json);

        for (int i = 0; i < count; i++) {
            char* json_part = _create_json_string(
                "%s{\"disk_model\":\"%s\",\"disk_serial\":\"%s\",\"disk_interface\":\"%s\",\"disk_size\":%lld,\"disk_media\":\"%s\",\"partition_device_id\":\"%s\",\"partition_type\":\"%s\",\"partition_size\":%lld,\"partition_index\":%lu}",
                (i == 0) ? "" : ",",
                entries[i].disk_model,
                entries[i].disk_serial,
                entries[i].disk_interface,
                entries[i].disk_size,
                entries[i].disk_media,
                entries[i].partition_device_id,
                entries[i].partition_type,
                entries[i].partition_size,
                entries[i].partition_index
            );
            if (json_part) {
                size_t part_len = strlen(json_part);
                if (used + part_len + 4 >= bufsize) {
                    bufsize += part_len + 4096;
                    char* temp = (char*)realloc(json, bufsize);
                    if (!temp) {
                        free(json);
                        free(json_part);
                        return strdup("{\"error\": \"Memory re-allocation failed for partitions JSON buffer.\"}");
                    }
                    json = temp;
                }
                strcat(json, json_part);
                used += part_len;
                free(json_part);
            }
        }
        strcat(json, "]}");
        return json;
    } else {
#ifdef BUILD_PYTHON_MODULE
        PyObject* partitions_list = get_partitions_info_objects(false);
        if (partitions_list && partitions_list != Py_None) {
            PyObject* repr = PyObject_Repr(partitions_list);
            if (!repr) {
                Py_DECREF(partitions_list);
                return strdup("[]");
            }
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(partitions_list);
            return result;
        }
        return strdup("[]");
#else
        return strdup("[]");
#endif
    }
#endif
}
