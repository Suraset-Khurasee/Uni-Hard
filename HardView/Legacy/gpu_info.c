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


#include "gpu_info.h"
#include "helpers.h"
#ifdef _WIN32
#include "win_helpers.h"
#else
#include "linux_helpers.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <dirent.h>
#endif
#include <ctype.h>

#ifndef _WIN32
typedef struct {
    char name[256];
    char manufacturer[128];
    char driver_version[128];
    long long memory_size;
    char video_processor[128];
    char video_mode_description[128];
} LinuxGpuEntry;

static int _collect_linux_gpus(LinuxGpuEntry* out_entries, int max_entries) {
    int count = 0;

    // 1. Check /sys/class/drm/card[0-9]*
    DIR* dir = opendir("/sys/class/drm");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && count < max_entries) {
            if (strncmp(entry->d_name, "card", 4) != 0) continue;
            if (!isdigit((unsigned char)entry->d_name[4])) continue;
            if (strchr(entry->d_name, '-') != NULL) continue; // skip connector subdirs

            char uevent_path[512];
            snprintf(uevent_path, sizeof(uevent_path), "/sys/class/drm/%s/device/uevent", entry->d_name);
            FILE* fuev = fopen(uevent_path, "r");
            if (!fuev) continue;

            char driver[128] = "Unknown";
            char pci_id[128] = "Unknown";
            char pci_slot[128] = "";
            char line[256];
            while (fgets(line, sizeof(line), fuev)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strncmp(line, "DRIVER=", 7) == 0) {
                    snprintf(driver, sizeof(driver), "%s", line + 7);
                } else if (strncmp(line, "PCI_ID=", 7) == 0) {
                    snprintf(pci_id, sizeof(pci_id), "%s", line + 7);
                } else if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
                    snprintf(pci_slot, sizeof(pci_slot), "%s", line + 14);
                }
            }
            fclose(fuev);

            char vendor_path[512];
            snprintf(vendor_path, sizeof(vendor_path), "/sys/class/drm/%s/device/vendor", entry->d_name);
            char* vendor_hex = _read_proc_sys_value(vendor_path, "");
            const char* mfg = "Unknown";
            if (vendor_hex) {
                if (strstr(vendor_hex, "10de") || strstr(vendor_hex, "10DE")) mfg = "NVIDIA";
                else if (strstr(vendor_hex, "1002")) mfg = "AMD";
                else if (strstr(vendor_hex, "8086")) mfg = "Intel";
                else mfg = vendor_hex;
            }

            char vram_path[512];
            snprintf(vram_path, sizeof(vram_path), "/sys/class/drm/%s/device/mem_info_vram_total", entry->d_name);
            char* vram_str = _read_proc_sys_value(vram_path, "");
            long long mem_bytes = 0;
            if (vram_str && strcmp(vram_str, "N/A") != 0) {
                mem_bytes = atoll(vram_str);
            }
            if (vram_str) free(vram_str);

            char gpu_name[256];
            snprintf(gpu_name, sizeof(gpu_name), "%s GPU (%s)", mfg, pci_id);

            // Try lspci if pci_slot is known
            if (pci_slot[0] != '\0') {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "lspci -s %s 2>/dev/null", pci_slot);
                FILE* flspci = popen(cmd, "r");
                if (flspci) {
                    char lspci_line[512];
                    if (fgets(lspci_line, sizeof(lspci_line), flspci)) {
                        lspci_line[strcspn(lspci_line, "\r\n")] = 0;
                        char* first_colon = strchr(lspci_line, ':');
                        if (first_colon) {
                            char* second_colon = strchr(first_colon + 1, ':');
                            if (second_colon) {
                                second_colon++;
                                while (*second_colon == ' ') second_colon++;
                                snprintf(gpu_name, sizeof(gpu_name), "%s", second_colon);
                            }
                        }
                    }
                    pclose(flspci);
                }
            }

            // If NVIDIA GPU, query nvidia-smi for exact name, driver_version, and VRAM
            if (strcmp(mfg, "NVIDIA") == 0) {
                FILE* fnv = popen("nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
                if (fnv) {
                    char nv_line[512];
                    if (fgets(nv_line, sizeof(nv_line), fnv)) {
                        nv_line[strcspn(nv_line, "\r\n")] = 0;
                        char* tok1 = strtok(nv_line, ",");
                        char* tok2 = strtok(NULL, ",");
                        char* tok3 = strtok(NULL, ",");
                        if (tok1) {
                            while (*tok1 == ' ') tok1++;
                            snprintf(gpu_name, sizeof(gpu_name), "%s", tok1);
                        }
                        if (tok2) {
                            while (*tok2 == ' ') tok2++;
                            snprintf(driver, sizeof(driver), "%s", tok2);
                        }
                        if (tok3 && mem_bytes == 0) {
                            while (*tok3 == ' ') tok3++;
                            long long mb = atoll(tok3);
                            if (mb > 0) mem_bytes = mb * 1024LL * 1024LL;
                        }
                    }
                    pclose(fnv);
                }
            }

            snprintf(out_entries[count].name, sizeof(out_entries[count].name), "%s", gpu_name);
            snprintf(out_entries[count].manufacturer, sizeof(out_entries[count].manufacturer), "%s", mfg);
            snprintf(out_entries[count].driver_version, sizeof(out_entries[count].driver_version), "%s", driver);
            out_entries[count].memory_size = mem_bytes;
            snprintf(out_entries[count].video_processor, sizeof(out_entries[count].video_processor), "%s", pci_id);
            snprintf(out_entries[count].video_mode_description, sizeof(out_entries[count].video_mode_description), "%s", entry->d_name);
            if (vendor_hex) free(vendor_hex);
            count++;
        }
        closedir(dir);
    }

    return count;
}
#endif

#ifdef BUILD_PYTHON_MODULE
// Helper function to create Python GPU object
PyObject* create_gpu_python_object(const char* name, const char* manufacturer, 
                                  const char* driver_version, long long memory_size,
                                  const char* video_processor, const char* video_mode_description) {
    PyObject* gpu_dict = PyDict_New();
    if (!gpu_dict) return NULL;
    
    // Add all fields to the dictionary with proper error checking
    PyObject* temp_obj;
    
    temp_obj = PyUnicode_FromString(name ? name : "");
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "name", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(manufacturer ? manufacturer : "");
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "manufacturer", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(driver_version ? driver_version : "");
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "driver_version", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLongLong(memory_size);
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "memory_size", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(video_processor ? video_processor : "");
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "video_processor", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(video_mode_description ? video_mode_description : "");
    if (temp_obj) {
        PyDict_SetItemString(gpu_dict, "video_mode_description", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    return gpu_dict;
}

// Function that returns Python objects directly
PyObject* get_gpu_info_objects(bool Json) {
    if (Json) {
        // JSON mode - return string representation
        char* json_str = get_gpu_info(true);
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
        BSTR query = SysAllocString(L"SELECT Name, AdapterCompatibility, DriverVersion, AdapterRAM, VideoProcessor, VideoModeDescription FROM Win32_VideoController");
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
        
        PyObject* gpus_list = PyList_New(0);
        if (!gpus_list) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return Py_None;
        }
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"AdapterCompatibility");
            char* driver_version = _get_wmi_string_property(pclsObj, L"DriverVersion");
            LONGLONG memory_size = _get_wmi_longlong_property(pclsObj, L"AdapterRAM");
            char* video_processor = _get_wmi_string_property(pclsObj, L"VideoProcessor");
            char* video_mode = _get_wmi_string_property(pclsObj, L"VideoModeDescription");
            
            PyObject* gpu_obj = create_gpu_python_object(name, manufacturer, driver_version, 
                                                        memory_size, video_processor, video_mode);
            
            if (gpu_obj) {
                int append_result = PyList_Append(gpus_list, gpu_obj);
                if (append_result != 0) {
                    Py_DECREF(gpu_obj);
                    Py_DECREF(gpus_list);
                    free(name); free(manufacturer); free(driver_version); 
                    free(video_processor); free(video_mode);
                    if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); }
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return Py_None;
                }
                Py_DECREF(gpu_obj);
            }
            
            free(name); free(manufacturer); free(driver_version); 
            free(video_processor); free(video_mode);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return gpus_list;
#else
        // Linux implementation
        PyObject* gpus_list = PyList_New(0);
        if (!gpus_list) {
            return Py_None;
        }

        LinuxGpuEntry entries[16];
        int count = _collect_linux_gpus(entries, 16);
        for (int i = 0; i < count; i++) {
            PyObject* gpu_obj = create_gpu_python_object(
                entries[i].name,
                entries[i].manufacturer,
                entries[i].driver_version,
                entries[i].memory_size,
                entries[i].video_processor,
                entries[i].video_mode_description
            );
            if (gpu_obj) {
                PyList_Append(gpus_list, gpu_obj);
                Py_DECREF(gpu_obj);
            }
        }
        return gpus_list;
#endif
    }
}
#endif

char* get_gpu_info(bool Json) {
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
            return _create_json_string("{\"error\": \"Failed to initialize WMI for GPU info: 0x%lx\"}", hr);
        } else {
            return NULL; // Return NULL for Python objects on error
        }
    }
    
    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Name, AdapterCompatibility, DriverVersion, AdapterRAM, VideoProcessor, VideoModeDescription FROM Win32_VideoController");
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
            return _create_json_string("{\"error\": \"Failed to execute WMI query for GPU: 0x%lx\"}", hr);
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
            return strdup("{\"error\": \"Memory allocation failed for GPU JSON buffer.\"}");
        }
        strcpy(json, "{\"gpus\": [");
        used = strlen(json);
        int first = 1;
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"AdapterCompatibility");
            char* driver_version = _get_wmi_string_property(pclsObj, L"DriverVersion");
            LONGLONG memory_size = _get_wmi_longlong_property(pclsObj, L"AdapterRAM");
            char* video_processor = _get_wmi_string_property(pclsObj, L"VideoProcessor");
            char* video_mode = _get_wmi_string_property(pclsObj, L"VideoModeDescription");
            
            char* json_part = _create_json_string(
                "%s{\"name\":\"%s\",\"manufacturer\":\"%s\",\"driver_version\":\"%s\",\"memory_size\":%lld,\"video_processor\":\"%s\",\"video_mode_description\":\"%s\"}",
                first ? "" : ",",
                name ? name : "",
                manufacturer ? manufacturer : "",
                driver_version ? driver_version : "",
                memory_size,
                video_processor ? video_processor : "",
                video_mode ? video_mode : ""
            );
            first = 0;
            size_t part_len = strlen(json_part);
            if (used + part_len + 3 >= bufsize) {
                bufsize += part_len + 4096;
                char* temp = (char*)realloc(json, bufsize);
                if (!temp) {
                    free(json); free(json_part);
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return strdup("{\"error\": \"Memory re-allocation failed for GPU JSON buffer.\"}");
                }
                json = temp;
            }
            strcat(json, json_part);
            used += part_len;
            free(json_part);
            free(name); free(manufacturer); free(driver_version); 
            free(video_processor); free(video_mode);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        if (first) {
            strcpy(json, "{\"gpus\": []}");
        } else {
            strcat(json, "]}");
        }
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return json;
    } else {
        // Python objects mode - return string representation of Python object
#ifdef BUILD_PYTHON_MODULE
        PyObject* gpus_list = PyList_New(0);
        if (!gpus_list) {
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return NULL;
        }
        
        while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"AdapterCompatibility");
            char* driver_version = _get_wmi_string_property(pclsObj, L"DriverVersion");
            LONGLONG memory_size = _get_wmi_longlong_property(pclsObj, L"AdapterRAM");
            char* video_processor = _get_wmi_string_property(pclsObj, L"VideoProcessor");
            char* video_mode = _get_wmi_string_property(pclsObj, L"VideoModeDescription");
            
            PyObject* gpu_obj = create_gpu_python_object(name, manufacturer, driver_version, 
                                                        memory_size, video_processor, video_mode);
            
            if (gpu_obj) {
                int append_result = PyList_Append(gpus_list, gpu_obj);
                if (append_result != 0) {
                    Py_DECREF(gpu_obj);
                    Py_DECREF(gpus_list);
                    free(name); free(manufacturer); free(driver_version); 
                    free(video_processor); free(video_mode);
                    if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); }
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return strdup("[]");
                }
                Py_DECREF(gpu_obj);
            }
            
            free(name); free(manufacturer); free(driver_version); 
            free(video_processor); free(video_mode);
            if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
        }
        
        if (PyList_Size(gpus_list) > 0) {
            PyObject* repr = PyObject_Repr(gpus_list);
            if (!repr) {
                Py_DECREF(gpus_list);
                _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                return strdup("[]");
            }
            
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(gpus_list);
            
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return result;
        } else {
            Py_DECREF(gpus_list);
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
    LinuxGpuEntry entries[16];
    int count = _collect_linux_gpus(entries, 16);

    if (Json) {
        size_t bufsize = 4096;
        size_t used = 0;
        char* json = (char*)malloc(bufsize);
        if (!json) {
            return strdup("{\"error\": \"Memory allocation failed for GPU JSON buffer.\"}");
        }
        strcpy(json, "{\"gpus\": [");
        used = strlen(json);

        for (int i = 0; i < count; i++) {
            char* json_part = _create_json_string(
                "%s{\"name\": \"%s\", \"manufacturer\": \"%s\", \"driver_version\": \"%s\", \"memory_size\": %lld, \"video_processor\": \"%s\", \"video_mode_description\": \"%s\"}",
                (i == 0) ? "" : ",",
                entries[i].name,
                entries[i].manufacturer,
                entries[i].driver_version,
                entries[i].memory_size,
                entries[i].video_processor,
                entries[i].video_mode_description
            );
            if (json_part) {
                size_t part_len = strlen(json_part);
                if (used + part_len + 4 >= bufsize) {
                    bufsize += part_len + 4096;
                    char* temp = (char*)realloc(json, bufsize);
                    if (!temp) {
                        free(json);
                        free(json_part);
                        return strdup("{\"error\": \"Memory re-allocation failed for GPU JSON buffer.\"}");
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
        PyObject* gpus_list = get_gpu_info_objects(false);
        if (gpus_list && gpus_list != Py_None) {
            PyObject* repr = PyObject_Repr(gpus_list);
            if (!repr) {
                Py_DECREF(gpus_list);
                return strdup("[]");
            }
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "[]");
            Py_DECREF(repr);
            Py_DECREF(gpus_list);
            return result;
        }
        return strdup("[]");
#else
        return strdup("[]");
#endif
    }
#endif
}
