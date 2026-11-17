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


#include "disk_info.h"
#include "helpers.h"
#ifdef _WIN32
#include "win_helpers.h"
#else
#include "linux_helpers.h"
#include <dirent.h>
#include <ctype.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_JSON_BUFFER_SIZE 2048
#define MAX_INFO_LEN 1024

#ifndef _WIN32
typedef struct {
    char model[256];
    char manufacturer[128];
    char interface_type[64];
    long long size;
    char serial_number[128];
    char media_type[64];
} LinuxDiskEntry;

static int _collect_linux_disks(LinuxDiskEntry* out_entries, int max_entries) {
    int count = 0;
    DIR* dir = opendir("/sys/block");
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && count < max_entries) {
        const char* dev = entry->d_name;
        if (dev[0] == '.') continue;
        if (strncmp(dev, "loop", 4) == 0 ||
            strncmp(dev, "ram", 3) == 0 ||
            strncmp(dev, "zram", 4) == 0 ||
            strncmp(dev, "dm-", 3) == 0 ||
            strncmp(dev, "sr", 2) == 0 ||
            strncmp(dev, "fd", 2) == 0) {
            continue;
        }

        char size_path[512];
        snprintf(size_path, sizeof(size_path), "/sys/block/%s/size", dev);
        char* size_str = _read_proc_sys_value(size_path, "");
        long long sectors = (size_str && strcmp(size_str, "N/A") != 0) ? atoll(size_str) : 0;
        if (size_str) free(size_str);
        if (sectors <= 0) continue;

        long long size_bytes = sectors * 512LL;

        char model_path[512];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", dev);
        char* model_str = _read_proc_sys_value(model_path, "");
        if (!model_str || strcmp(model_str, "N/A") == 0) {
            if (model_str) free(model_str);
            model_str = strdup(dev);
        }

        char vendor_path[512];
        snprintf(vendor_path, sizeof(vendor_path), "/sys/block/%s/device/vendor", dev);
        char* vendor_str = _read_proc_sys_value(vendor_path, "");
        if (!vendor_str || strcmp(vendor_str, "N/A") == 0) {
            if (vendor_str) free(vendor_str);
            // Infer manufacturer from first word of model
            char temp_mfg[128];
            snprintf(temp_mfg, sizeof(temp_mfg), "%s", model_str);
            char* sp = strchr(temp_mfg, ' ');
            if (sp) *sp = '\0';
            vendor_str = strdup(temp_mfg[0] ? temp_mfg : "Generic");
        }

        char serial_path[512];
        snprintf(serial_path, sizeof(serial_path), "/sys/block/%s/device/serial", dev);
        char* serial_str = _read_proc_sys_value(serial_path, "");
        if (!serial_str || strcmp(serial_str, "N/A") == 0) {
            if (serial_str) free(serial_str);
            serial_str = strdup("N/A");
        }

        char rot_path[512];
        snprintf(rot_path, sizeof(rot_path), "/sys/block/%s/queue/rotational", dev);
        char* rot_str = _read_proc_sys_value(rot_path, "");
        int rotational = (rot_str && strcmp(rot_str, "1") == 0) ? 1 : 0;
        if (rot_str) free(rot_str);

        const char* iface = "SATA/SCSI";
        if (strncmp(dev, "nvme", 4) == 0) iface = "NVMe";
        else if (strncmp(dev, "mmcblk", 6) == 0) iface = "MMC/SD";
        else if (strncmp(dev, "vd", 2) == 0) iface = "VirtIO";

        snprintf(out_entries[count].model, sizeof(out_entries[count].model), "%s", model_str);
        snprintf(out_entries[count].manufacturer, sizeof(out_entries[count].manufacturer), "%s", vendor_str);
        snprintf(out_entries[count].interface_type, sizeof(out_entries[count].interface_type), "%s", iface);
        out_entries[count].size = size_bytes;
        snprintf(out_entries[count].serial_number, sizeof(out_entries[count].serial_number), "%s", serial_str);
        snprintf(out_entries[count].media_type, sizeof(out_entries[count].media_type), "%s", rotational ? "HDD" : "SSD");

        free(model_str);
        free(vendor_str);
        free(serial_str);
        count++;
    }
    closedir(dir);
    return count;
}
#endif

#ifdef BUILD_PYTHON_MODULE
// Helper function to create Python disk object
PyObject* create_disk_python_object(const char* model, const char* manufacturer, 
                                   const char* interface_type, long long size,
                                   const char* serial_number, const char* media_type) {
    PyObject* disk_dict = PyDict_New();
    if (!disk_dict) return NULL;
    
    // Add all fields to the dictionary with proper error checking
    PyObject* temp_obj;
    
    temp_obj = PyUnicode_FromString(model ? model : "");
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "model", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(manufacturer ? manufacturer : "");
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "manufacturer", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(interface_type ? interface_type : "");
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "interface_type", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLongLong(size);
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "size", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(serial_number ? serial_number : "");
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "serial_number", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(media_type ? media_type : "");
    if (temp_obj) {
        PyDict_SetItemString(disk_dict, "media_type", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    return disk_dict;
}

// Function that returns Python objects directly
PyObject* get_disk_info_objects(bool Json) {
    if (Json) {
        // JSON mode - return string representation
        char* json_str = get_disk_info(true);
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
        BSTR query = SysAllocString(L"SELECT Model, Manufacturer, InterfaceType, Size, SerialNumber, MediaType FROM Win32_DiskDrive");
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
        
        PyObject* disks_list = PyList_New(0);
        if (!disks_list) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return Py_None;
        }
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* model = _get_wmi_string_property(pclsObj, L"Model");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* interface_type = _get_wmi_string_property(pclsObj, L"InterfaceType");
            LONGLONG size = _get_wmi_longlong_property(pclsObj, L"Size");
            char* serial_number = _get_wmi_string_property(pclsObj, L"SerialNumber");
            char* media_type = _get_wmi_string_property(pclsObj, L"MediaType");
            
            PyObject* disk_obj = create_disk_python_object(model, manufacturer, interface_type, 
                                                         size, serial_number, media_type);
            
            if (disk_obj) {
                int append_result = PyList_Append(disks_list, disk_obj);
                if (append_result != 0) {
                    Py_DECREF(disk_obj);
                    Py_DECREF(disks_list);
                    free(model); free(manufacturer); free(interface_type); 
                    free(serial_number); free(media_type);
                    if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); }
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return Py_None;
                }
                Py_DECREF(disk_obj);
            }
            
            free(model); free(manufacturer); free(interface_type); 
            free(serial_number); free(media_type);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return disks_list;
#else
        // Linux implementation
        PyObject* disks_list = PyList_New(0);
        if (!disks_list) {
            return Py_None;
        }

        LinuxDiskEntry entries[32];
        int count = _collect_linux_disks(entries, 32);
        for (int i = 0; i < count; i++) {
            PyObject* disk_obj = create_disk_python_object(
                entries[i].model,
                entries[i].manufacturer,
                entries[i].interface_type,
                entries[i].size,
                entries[i].serial_number,
                entries[i].media_type
            );
            if (disk_obj) {
                PyList_Append(disks_list, disk_obj);
                Py_DECREF(disk_obj);
            }
        }
        return disks_list;
#endif
    }
}
#endif

char* get_disk_info(bool Json) {
#ifdef _WIN32
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    IEnumWbemClassObject *pEnumerator = NULL;
    IWbemClassObject *pclsObj = NULL;
    HRESULT hr;
    char* json_result = NULL;
    ULONG uReturn = 0;
    
    hr = _initialize_wmi(&pLoc, &pSvc);
    if (FAILED(hr)) {
        if (Json) {
            return _create_json_string("{\"error\": \"Failed to initialize WMI for disk info: 0x%lx\"}", hr);
        } else {
            return NULL; // Return NULL for Python objects on error
        }
    }
    
    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Model, Manufacturer, InterfaceType, Size, SerialNumber, MediaType FROM Win32_DiskDrive");
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
            return _create_json_string("{\"error\": \"Failed to execute WMI query for disk: 0x%lx\"}", hr);
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
            return strdup("{\"error\": \"Memory allocation failed for disk JSON buffer.\"}");
        }
        strcpy(json, "{\"disks\": [");
        used = strlen(json);
        int first = 1;
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* model = _get_wmi_string_property(pclsObj, L"Model");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* interface_type = _get_wmi_string_property(pclsObj, L"InterfaceType");
            LONGLONG size = _get_wmi_longlong_property(pclsObj, L"Size");
            char* serial_number = _get_wmi_string_property(pclsObj, L"SerialNumber");
            char* media_type = _get_wmi_string_property(pclsObj, L"MediaType");
            
            char* json_part = _create_json_string(
                "%s{\"model\":\"%s\",\"manufacturer\":\"%s\",\"interface_type\":\"%s\",\"size\":%lld,\"serial_number\":\"%s\",\"media_type\":\"%s\"}",
                first ? "" : ",",
                model ? model : "",
                manufacturer ? manufacturer : "",
                interface_type ? interface_type : "",
                size,
                serial_number ? serial_number : "",
                media_type ? media_type : ""
            );
            first = 0;
            size_t part_len = strlen(json_part);
            if (used + part_len + 3 >= bufsize) {
                bufsize += part_len + 4096;
                char* temp = (char*)realloc(json, bufsize);
                if (!temp) {
                    free(json); free(json_part);
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return strdup("{\"error\": \"Memory re-allocation failed for disk JSON buffer.\"}");
                }
                json = temp;
            }
            strcat(json, json_part);
            used += part_len;
            free(json_part);
            free(model); free(manufacturer); free(interface_type); 
            free(serial_number); free(media_type);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        if (first) {
            strcpy(json, "{\"disks\": []}");
        } else {
            strcat(json, "]}");
        }
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return json;
    } else {
        // Python objects mode - return string representation of Python object
#ifdef BUILD_PYTHON_MODULE
        PyObject* disks_list = PyList_New(0);
        if (!disks_list) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return NULL;
        }
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* model = _get_wmi_string_property(pclsObj, L"Model");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* interface_type = _get_wmi_string_property(pclsObj, L"InterfaceType");
            LONGLONG size = _get_wmi_longlong_property(pclsObj, L"Size");
            char* serial_number = _get_wmi_string_property(pclsObj, L"SerialNumber");
            char* media_type = _get_wmi_string_property(pclsObj, L"MediaType");
            
            PyObject* disk_obj = create_disk_python_object(model, manufacturer, interface_type, 
                                                         size, serial_number, media_type);
            
            if (disk_obj) {
                int append_result = PyList_Append(disks_list, disk_obj);
                if (append_result != 0) {
                    Py_DECREF(disk_obj);
                    Py_DECREF(disks_list);
                    free(model); free(manufacturer); free(interface_type); 
                    free(serial_number); free(media_type);
                    if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); }
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return strdup("[]");
                }
                Py_DECREF(disk_obj);
            }
            
            free(model); free(manufacturer); free(interface_type); 
            free(serial_number); free(media_type);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        if (PyList_Size(disks_list) > 0) {
            PyObject* repr = PyObject_Repr(disks_list);
            if (!repr) {
                Py_DECREF(disks_list);
                _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                return strdup("[]");
            }
            
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(disks_list);
            
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return result;
        } else {
            Py_DECREF(disks_list);
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return strdup("[]");
        }
#else
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("[]");
#endif
    }

#else
    // Linux implementation
    LinuxDiskEntry entries[32];
    int count = _collect_linux_disks(entries, 32);

    if (Json) {
        size_t bufsize = 4096;
        size_t used = 0;
        char* json = (char*)malloc(bufsize);
        if (!json) {
            return strdup("{\"error\": \"Memory allocation failed for disk JSON buffer.\"}");
        }
        strcpy(json, "{\"disks\": [");
        used = strlen(json);

        for (int i = 0; i < count; i++) {
            char* json_part = _create_json_string(
                "%s{\"model\": \"%s\", \"manufacturer\": \"%s\", \"interface_type\": \"%s\", \"size\": %lld, \"serial_number\": \"%s\", \"media_type\": \"%s\"}",
                (i == 0) ? "" : ",",
                entries[i].model,
                entries[i].manufacturer,
                entries[i].interface_type,
                entries[i].size,
                entries[i].serial_number,
                entries[i].media_type
            );
            if (json_part) {
                size_t part_len = strlen(json_part);
                if (used + part_len + 4 >= bufsize) {
                    bufsize += part_len + 4096;
                    char* temp = (char*)realloc(json, bufsize);
                    if (!temp) {
                        free(json);
                        free(json_part);
                        return strdup("{\"error\": \"Memory re-allocation failed for disk JSON buffer.\"}");
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
        PyObject* disks_list = get_disk_info_objects(false);
        if (disks_list && disks_list != Py_None) {
            PyObject* repr = PyObject_Repr(disks_list);
            if (!repr) {
                Py_DECREF(disks_list);
                return strdup("[]");
            }
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(disks_list);
            return result;
        }
        return strdup("[]");
#else
        return strdup("[]");
#endif
    }
#endif
}
