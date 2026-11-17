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


#include "Smart_disk.h"
#include "helpers.h"
#ifdef _WIN32
#include <windows.h>
#include <wbemidl.h>
#include "win_helpers.h"
#else
#include "linux_helpers.h"
#include <dirent.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <Python.h>

#ifdef _WIN32
// Helper function to escape backslashes for JSON
char* escape_for_json(const char* input) {
    if (!input) return strdup("");
    
    size_t len = strlen(input);
    size_t escaped_len = len;
    
    // Count backslashes to calculate new length
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\') escaped_len++;
    }
    
    char* escaped = (char*)malloc(escaped_len + 1);
    if (!escaped) return strdup("");
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = '\\';
        } else {
            escaped[j++] = input[i];
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}
#else
typedef struct {
    char model[256];
    char serial[256];
    char interface_type[64];
    char size[64];
    unsigned int partitions;
    char manufacturer[128];
    char media_type[128];
    char status[64];
    char device_id[256];
    char caption[256];
    char firmware[128];
    char pnp_id[256];
    unsigned int sectors_per_track;
    unsigned long long total_cylinders;
    unsigned int total_heads;
    unsigned long long total_sectors;
    unsigned long long total_tracks;
    unsigned int tracks_per_cylinder;
    unsigned int bytes_per_sector;
    unsigned int index;
    unsigned int signature;
} LinuxSmartDiskEntry;

static int _collect_linux_smart_disks(LinuxSmartDiskEntry* out_entries, int max_entries) {
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
        unsigned long long sectors = (size_str && strcmp(size_str, "N/A") != 0) ? strtoull(size_str, NULL, 10) : 0;
        if (size_str) free(size_str);
        if (sectors == 0) continue;

        unsigned long long bytes = sectors * 512ULL;

        char model_path[512];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", dev);
        char* model_str = _read_proc_sys_value(model_path, "");
        if (!model_str || strcmp(model_str, "N/A") == 0) {
            if (model_str) free(model_str);
            model_str = strdup(dev);
        }

        char serial_path[512];
        snprintf(serial_path, sizeof(serial_path), "/sys/block/%s/device/serial", dev);
        char* serial_str = _read_proc_sys_value(serial_path, "");
        if (!serial_str || strcmp(serial_str, "N/A") == 0) {
            if (serial_str) free(serial_str);
            serial_str = strdup("N/A");
        }

        char fw_path[512];
        snprintf(fw_path, sizeof(fw_path), "/sys/block/%s/device/firmware_rev", dev);
        char* fw_str = _read_proc_sys_value(fw_path, "");
        if (!fw_str || strcmp(fw_str, "N/A") == 0) {
            if (fw_str) free(fw_str);
            snprintf(fw_path, sizeof(fw_path), "/sys/block/%s/device/rev", dev);
            fw_str = _read_proc_sys_value(fw_path, "");
            if (!fw_str || strcmp(fw_str, "N/A") == 0) {
                if (fw_str) free(fw_str);
                fw_str = strdup("N/A");
            }
        }

        char rot_path[512];
        snprintf(rot_path, sizeof(rot_path), "/sys/block/%s/queue/rotational", dev);
        char* rot_str = _read_proc_sys_value(rot_path, "");
        int rotational = (rot_str && strcmp(rot_str, "1") == 0) ? 1 : 0;
        if (rot_str) free(rot_str);

        // Count partitions
        unsigned int part_count = 0;
        char disk_dir_path[512];
        snprintf(disk_dir_path, sizeof(disk_dir_path), "/sys/block/%s", dev);
        DIR* ddir = opendir(disk_dir_path);
        if (ddir) {
            struct dirent* pentry;
            size_t dev_len = strlen(dev);
            while ((pentry = readdir(ddir)) != NULL) {
                if (strncmp(pentry->d_name, dev, dev_len) == 0 && pentry->d_name[dev_len] != '\0') {
                    part_count++;
                }
            }
            closedir(ddir);
        }

        const char* iface = "SATA/SCSI";
        if (strncmp(dev, "nvme", 4) == 0) iface = "NVMe";
        else if (strncmp(dev, "mmcblk", 6) == 0) iface = "MMC/SD";
        else if (strncmp(dev, "vd", 2) == 0) iface = "VirtIO";

        memset(&out_entries[count], 0, sizeof(LinuxSmartDiskEntry));
        snprintf(out_entries[count].model, sizeof(out_entries[count].model), "%s", model_str);
        snprintf(out_entries[count].serial, sizeof(out_entries[count].serial), "%s", serial_str);
        snprintf(out_entries[count].interface_type, sizeof(out_entries[count].interface_type), "%s", iface);
        snprintf(out_entries[count].size, sizeof(out_entries[count].size), "%llu", bytes);
        out_entries[count].partitions = part_count;
        snprintf(out_entries[count].manufacturer, sizeof(out_entries[count].manufacturer), "(Standard disk drives)");
        snprintf(out_entries[count].media_type, sizeof(out_entries[count].media_type), "%s", rotational ? "Fixed hard disk media" : "SSD");
        snprintf(out_entries[count].status, sizeof(out_entries[count].status), "OK");
        snprintf(out_entries[count].device_id, sizeof(out_entries[count].device_id), "/dev/%s", dev);
        snprintf(out_entries[count].caption, sizeof(out_entries[count].caption), "%s", model_str);
        snprintf(out_entries[count].firmware, sizeof(out_entries[count].firmware), "%s", fw_str);
        snprintf(out_entries[count].pnp_id, sizeof(out_entries[count].pnp_id), "/sys/block/%s", dev);
        out_entries[count].sectors_per_track = 63;
        out_entries[count].total_heads = 255;
        out_entries[count].total_sectors = sectors;
        out_entries[count].total_tracks = sectors / 63;
        out_entries[count].total_cylinders = out_entries[count].total_tracks / 255;
        out_entries[count].tracks_per_cylinder = 255;
        out_entries[count].bytes_per_sector = 512;
        out_entries[count].index = (unsigned int)count;
        out_entries[count].signature = 0;

        free(model_str);
        free(serial_str);
        free(fw_str);
        count++;
    }
    closedir(dir);
    return count;
}
#endif

char* get_smart_info(bool Json) {
#ifdef _WIN32
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    IEnumWbemClassObject *pEnumerator = NULL;
    IWbemClassObject *pclsObj = NULL;
    HRESULT hr;
    ULONG uReturn = 0;

    hr = _initialize_wmi(&pLoc, &pSvc);
    if (FAILED(hr)) {
        return strdup("{\"error\": \"Failed to initialize WMI for disk info\"}");
    }

    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT * FROM Win32_DiskDrive");
    hr = pSvc->lpVtbl->ExecQuery(
        pSvc, query_lang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator
    );
    SysFreeString(query_lang);
    SysFreeString(query);
    if (FAILED(hr)) {
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("{\"error\": \"Failed to execute WMI query for disk info\"}");
    }

    size_t bufsize = 8192;
    size_t used = 0;
    char* json = (char*)malloc(bufsize);
    if (!json) {
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("{\"error\": \"Memory allocation failed for disk JSON buffer\"}");
    }
    strcpy(json, "{\"disks\": [");
    used = strlen(json);
    int first = 1;
    while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {

        VARIANT vtProp;
        char model[256] = "", serial[256] = "", interface_type[64] = "", size[64] = "", manufacturer[128] = "", media_type[128] = "", status[64] = "", device_id[256] = "", caption[256] = "", firmware[128] = "", pnp_id[256] = "";
        unsigned long long total_cylinders = 0, total_sectors = 0, total_tracks = 0;
        unsigned int partitions = 0, sectors_per_track = 0, total_heads = 0, tracks_per_cylinder = 0, bytes_per_sector = 0, index = 0, signature = 0;
        // Model
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Model", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(model, vtProp.bstrVal, sizeof(model));
        VariantClear(&vtProp);
        // SerialNumber
        hr = pclsObj->lpVtbl->Get(pclsObj, L"SerialNumber", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(serial, vtProp.bstrVal, sizeof(serial));
        VariantClear(&vtProp);
        // InterfaceType
        hr = pclsObj->lpVtbl->Get(pclsObj, L"InterfaceType", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(interface_type, vtProp.bstrVal, sizeof(interface_type));
        VariantClear(&vtProp);
        // Size
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Size", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_BSTR || vtProp.vt == VT_I8 || vtProp.vt == VT_UI8)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(size, vtProp.bstrVal, sizeof(size));
            else snprintf(size, sizeof(size), "%llu", vtProp.ullVal);
        }
        VariantClear(&vtProp);
        // Partitions
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Partitions", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) partitions = vtProp.uintVal;
        VariantClear(&vtProp);
        // Manufacturer
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Manufacturer", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(manufacturer, vtProp.bstrVal, sizeof(manufacturer));
        VariantClear(&vtProp);
        // MediaType
        hr = pclsObj->lpVtbl->Get(pclsObj, L"MediaType", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(media_type, vtProp.bstrVal, sizeof(media_type));
        VariantClear(&vtProp);
        // Status
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Status", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(status, vtProp.bstrVal, sizeof(status));
        VariantClear(&vtProp);
        // DeviceID
        hr = pclsObj->lpVtbl->Get(pclsObj, L"DeviceID", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(device_id, vtProp.bstrVal, sizeof(device_id));
        VariantClear(&vtProp);
        // Caption
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Caption", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(caption, vtProp.bstrVal, sizeof(caption));
        VariantClear(&vtProp);
        // FirmwareRevision
        hr = pclsObj->lpVtbl->Get(pclsObj, L"FirmwareRevision", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(firmware, vtProp.bstrVal, sizeof(firmware));
        VariantClear(&vtProp);
        // PNPDeviceID
        hr = pclsObj->lpVtbl->Get(pclsObj, L"PNPDeviceID", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(pnp_id, vtProp.bstrVal, sizeof(pnp_id));
        VariantClear(&vtProp);
        
        // Escape fields that may contain backslashes for JSON
        char* escaped_device_id = escape_for_json(device_id);
        char* escaped_pnp_id = escape_for_json(pnp_id);
        // SectorsPerTrack
        hr = pclsObj->lpVtbl->Get(pclsObj, L"SectorsPerTrack", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) sectors_per_track = vtProp.uintVal;
        VariantClear(&vtProp);
        // TotalCylinders
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalCylinders", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_cylinders = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_cylinders = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);
        // TotalHeads
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalHeads", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) total_heads = vtProp.uintVal;
        VariantClear(&vtProp);
        // TotalSectors
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalSectors", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_sectors = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_sectors = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);
        // TotalTracks
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalTracks", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_tracks = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_tracks = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);
        // TracksPerCylinder
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TracksPerCylinder", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) tracks_per_cylinder = vtProp.uintVal;
        VariantClear(&vtProp);
        // BytesPerSector
        hr = pclsObj->lpVtbl->Get(pclsObj, L"BytesPerSector", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) bytes_per_sector = vtProp.uintVal;
        VariantClear(&vtProp);
        // Index
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Index", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) index = vtProp.uintVal;
        VariantClear(&vtProp);
        // Signature
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Signature", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) signature = vtProp.uintVal;
        VariantClear(&vtProp);
        // Build JSON for this disk
        char* disk_json = _create_json_string(
            "%s{\"model\":\"%s\",\"serial_number\":\"%s\",\"interface_type\":\"%s\",\"size\":\"%s\",\"partitions\":%u,\"manufacturer\":\"%s\",\"media_type\":\"%s\",\"status\":\"%s\",\"device_id\":\"%s\",\"caption\":\"%s\",\"firmware_revision\":\"%s\",\"pnp_device_id\":\"%s\",\"sectors_per_track\":%u,\"total_cylinders\":%llu,\"total_heads\":%u,\"total_sectors\":%llu,\"total_tracks\":%llu,\"tracks_per_cylinder\":%u,\"bytes_per_sector\":%u,\"index\":%u,\"signature\":%u}",
            first ? "" : ",",
            model, serial, interface_type, size, partitions, manufacturer, media_type, status, escaped_device_id, caption, firmware, escaped_pnp_id, sectors_per_track, total_cylinders, total_heads, total_sectors, total_tracks, tracks_per_cylinder, bytes_per_sector, index, signature
        );
        first = 0;
        strcat(json, disk_json);
        used += strlen(disk_json);
        free(disk_json);
        free(escaped_device_id);
        free(escaped_pnp_id);
        if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
    }
    strcat(json, "]}");
    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
    return json;
#else
    LinuxSmartDiskEntry entries[32];
    int count = _collect_linux_smart_disks(entries, 32);

    size_t bufsize = 8192;
    size_t used = 0;
    char* json = (char*)malloc(bufsize);
    if (!json) {
        return strdup("{\"error\": \"Memory allocation failed for disk JSON buffer\"}");
    }
    strcpy(json, "{\"disks\": [");
    used = strlen(json);

    for (int i = 0; i < count; i++) {
        char* disk_json = _create_json_string(
            "%s{\"model\":\"%s\",\"serial_number\":\"%s\",\"interface_type\":\"%s\",\"size\":\"%s\",\"partitions\":%u,\"manufacturer\":\"%s\",\"media_type\":\"%s\",\"status\":\"%s\",\"device_id\":\"%s\",\"caption\":\"%s\",\"firmware_revision\":\"%s\",\"pnp_device_id\":\"%s\",\"sectors_per_track\":%u,\"total_cylinders\":%llu,\"total_heads\":%u,\"total_sectors\":%llu,\"total_tracks\":%llu,\"tracks_per_cylinder\":%u,\"bytes_per_sector\":%u,\"index\":%u,\"signature\":%u}",
            (i == 0) ? "" : ",",
            entries[i].model, entries[i].serial, entries[i].interface_type, entries[i].size,
            entries[i].partitions, entries[i].manufacturer, entries[i].media_type, entries[i].status,
            entries[i].device_id, entries[i].caption, entries[i].firmware, entries[i].pnp_id,
            entries[i].sectors_per_track, entries[i].total_cylinders, entries[i].total_heads,
            entries[i].total_sectors, entries[i].total_tracks, entries[i].tracks_per_cylinder,
            entries[i].bytes_per_sector, entries[i].index, entries[i].signature
        );
        if (disk_json) {
            size_t part_len = strlen(disk_json);
            if (used + part_len + 4 >= bufsize) {
                bufsize += part_len + 4096;
                char* temp = (char*)realloc(json, bufsize);
                if (!temp) {
                    free(json);
                    free(disk_json);
                    return strdup("{\"error\": \"Memory re-allocation failed for disk JSON buffer\"}");
                }
                json = temp;
            }
            strcat(json, disk_json);
            used += part_len;
            free(disk_json);
        }
    }
    strcat(json, "]}");
    return json;
#endif
}

PyObject* get_smart_info_objects(bool Json) {
    if (Json) {
        char* json_result = get_smart_info(true);
        PyObject* py_result = PyUnicode_FromString(json_result);
        free(json_result);
        return py_result;
    }

#ifdef _WIN32
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    IEnumWbemClassObject *pEnumerator = NULL;
    IWbemClassObject *pclsObj = NULL;
    HRESULT hr;
    ULONG uReturn = 0;

    hr = _initialize_wmi(&pLoc, &pSvc);
    if (FAILED(hr)) {
        PyObject* error_dict = PyDict_New();
        PyDict_SetItemString(error_dict, "error", PyUnicode_FromString("Failed to initialize WMI for disk info"));
        return error_dict;
    }

    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT * FROM Win32_DiskDrive");
    hr = pSvc->lpVtbl->ExecQuery(
        pSvc, query_lang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator
    );
    SysFreeString(query_lang);
    SysFreeString(query);

    if (FAILED(hr)) {
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        PyObject* error_dict = PyDict_New();
        PyDict_SetItemString(error_dict, "error", PyUnicode_FromString("Failed to execute WMI query for disk info"));
        return error_dict;
    }

    PyObject* result_dict = PyDict_New();
    PyObject* disks_list = PyList_New(0);

    while (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
        VARIANT vtProp;
        char model[256] = "", serial[256] = "", interface_type[64] = "", size[64] = "", manufacturer[128] = "", media_type[128] = "", status[64] = "", device_id[256] = "", caption[256] = "", firmware[128] = "", pnp_id[256] = "";
        unsigned long long total_cylinders = 0, total_sectors = 0, total_tracks = 0;
        unsigned int partitions = 0, sectors_per_track = 0, total_heads = 0, tracks_per_cylinder = 0, bytes_per_sector = 0, index = 0, signature = 0;

        // Model
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Model", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(model, vtProp.bstrVal, sizeof(model));
        VariantClear(&vtProp);

        // SerialNumber
        hr = pclsObj->lpVtbl->Get(pclsObj, L"SerialNumber", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(serial, vtProp.bstrVal, sizeof(serial));
        VariantClear(&vtProp);

        // InterfaceType
        hr = pclsObj->lpVtbl->Get(pclsObj, L"InterfaceType", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(interface_type, vtProp.bstrVal, sizeof(interface_type));
        VariantClear(&vtProp);

        // Size
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Size", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_BSTR || vtProp.vt == VT_I8 || vtProp.vt == VT_UI8)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(size, vtProp.bstrVal, sizeof(size));
            else snprintf(size, sizeof(size), "%llu", vtProp.ullVal);
        }
        VariantClear(&vtProp);

        // Partitions
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Partitions", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) partitions = vtProp.uintVal;
        VariantClear(&vtProp);

        // Manufacturer
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Manufacturer", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(manufacturer, vtProp.bstrVal, sizeof(manufacturer));
        VariantClear(&vtProp);

        // MediaType
        hr = pclsObj->lpVtbl->Get(pclsObj, L"MediaType", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(media_type, vtProp.bstrVal, sizeof(media_type));
        VariantClear(&vtProp);

        // Status
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Status", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(status, vtProp.bstrVal, sizeof(status));
        VariantClear(&vtProp);

        // DeviceID
        hr = pclsObj->lpVtbl->Get(pclsObj, L"DeviceID", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(device_id, vtProp.bstrVal, sizeof(device_id));
        VariantClear(&vtProp);

        // Caption
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Caption", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(caption, vtProp.bstrVal, sizeof(caption));
        VariantClear(&vtProp);

        // FirmwareRevision
        hr = pclsObj->lpVtbl->Get(pclsObj, L"FirmwareRevision", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(firmware, vtProp.bstrVal, sizeof(firmware));
        VariantClear(&vtProp);

        // PNPDeviceID
        hr = pclsObj->lpVtbl->Get(pclsObj, L"PNPDeviceID", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) wcstombs(pnp_id, vtProp.bstrVal, sizeof(pnp_id));
        VariantClear(&vtProp);

        // SectorsPerTrack
        hr = pclsObj->lpVtbl->Get(pclsObj, L"SectorsPerTrack", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) sectors_per_track = vtProp.uintVal;
        VariantClear(&vtProp);

        // TotalCylinders
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalCylinders", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_cylinders = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_cylinders = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);

        // TotalHeads
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalHeads", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) total_heads = vtProp.uintVal;
        VariantClear(&vtProp);

        // TotalSectors
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalSectors", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_sectors = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_sectors = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);

        // TotalTracks
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TotalTracks", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr)) {
            if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                char temp[64];
                wcstombs(temp, vtProp.bstrVal, sizeof(temp));
                total_tracks = _strtoui64(temp, NULL, 10);
            } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                total_tracks = vtProp.ullVal;
            }
        }
        VariantClear(&vtProp);

        // TracksPerCylinder
        hr = pclsObj->lpVtbl->Get(pclsObj, L"TracksPerCylinder", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) tracks_per_cylinder = vtProp.uintVal;
        VariantClear(&vtProp);

        // BytesPerSector
        hr = pclsObj->lpVtbl->Get(pclsObj, L"BytesPerSector", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) bytes_per_sector = vtProp.uintVal;
        VariantClear(&vtProp);

        // Index
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Index", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) index = vtProp.uintVal;
        VariantClear(&vtProp);

        // Signature
        hr = pclsObj->lpVtbl->Get(pclsObj, L"Signature", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) signature = vtProp.uintVal;
        VariantClear(&vtProp);

        // Create Python dictionary for this disk
        PyObject* disk_dict = PyDict_New();
        PyDict_SetItemString(disk_dict, "model", PyUnicode_FromString(model));
        PyDict_SetItemString(disk_dict, "serial_number", PyUnicode_FromString(serial));
        PyDict_SetItemString(disk_dict, "interface_type", PyUnicode_FromString(interface_type));
        PyDict_SetItemString(disk_dict, "size", PyUnicode_FromString(size));
        PyDict_SetItemString(disk_dict, "partitions", PyLong_FromUnsignedLong(partitions));
        PyDict_SetItemString(disk_dict, "manufacturer", PyUnicode_FromString(manufacturer));
        PyDict_SetItemString(disk_dict, "media_type", PyUnicode_FromString(media_type));
        PyDict_SetItemString(disk_dict, "status", PyUnicode_FromString(status));
        PyDict_SetItemString(disk_dict, "device_id", PyUnicode_FromString(device_id));
        PyDict_SetItemString(disk_dict, "caption", PyUnicode_FromString(caption));
        PyDict_SetItemString(disk_dict, "firmware_revision", PyUnicode_FromString(firmware));
        PyDict_SetItemString(disk_dict, "pnp_device_id", PyUnicode_FromString(pnp_id));
        PyDict_SetItemString(disk_dict, "sectors_per_track", PyLong_FromUnsignedLong(sectors_per_track));
        PyDict_SetItemString(disk_dict, "total_cylinders", PyLong_FromUnsignedLongLong(total_cylinders));
        PyDict_SetItemString(disk_dict, "total_heads", PyLong_FromUnsignedLong(total_heads));
        PyDict_SetItemString(disk_dict, "total_sectors", PyLong_FromUnsignedLongLong(total_sectors));
        PyDict_SetItemString(disk_dict, "total_tracks", PyLong_FromUnsignedLongLong(total_tracks));
        PyDict_SetItemString(disk_dict, "tracks_per_cylinder", PyLong_FromUnsignedLong(tracks_per_cylinder));
        PyDict_SetItemString(disk_dict, "bytes_per_sector", PyLong_FromUnsignedLong(bytes_per_sector));
        PyDict_SetItemString(disk_dict, "index", PyLong_FromUnsignedLong(index));
        PyDict_SetItemString(disk_dict, "signature", PyLong_FromUnsignedLong(signature));

        PyList_Append(disks_list, disk_dict);
        Py_DECREF(disk_dict);

        if (pclsObj) { pclsObj->lpVtbl->Release(pclsObj); pclsObj = NULL; }
    }

    PyDict_SetItemString(result_dict, "disks", disks_list);
    Py_DECREF(disks_list);
    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
    return result_dict;

#else
    LinuxSmartDiskEntry entries[32];
    int count = _collect_linux_smart_disks(entries, 32);

    PyObject* result_dict = PyDict_New();
    PyObject* disks_list = PyList_New(0);

    for (int i = 0; i < count; i++) {
        PyObject* disk_dict = PyDict_New();
        PyDict_SetItemString(disk_dict, "model", PyUnicode_FromString(entries[i].model));
        PyDict_SetItemString(disk_dict, "serial_number", PyUnicode_FromString(entries[i].serial));
        PyDict_SetItemString(disk_dict, "interface_type", PyUnicode_FromString(entries[i].interface_type));
        PyDict_SetItemString(disk_dict, "size", PyUnicode_FromString(entries[i].size));
        PyDict_SetItemString(disk_dict, "partitions", PyLong_FromUnsignedLong(entries[i].partitions));
        PyDict_SetItemString(disk_dict, "manufacturer", PyUnicode_FromString(entries[i].manufacturer));
        PyDict_SetItemString(disk_dict, "media_type", PyUnicode_FromString(entries[i].media_type));
        PyDict_SetItemString(disk_dict, "status", PyUnicode_FromString(entries[i].status));
        PyDict_SetItemString(disk_dict, "device_id", PyUnicode_FromString(entries[i].device_id));
        PyDict_SetItemString(disk_dict, "caption", PyUnicode_FromString(entries[i].caption));
        PyDict_SetItemString(disk_dict, "firmware_revision", PyUnicode_FromString(entries[i].firmware));
        PyDict_SetItemString(disk_dict, "pnp_device_id", PyUnicode_FromString(entries[i].pnp_id));
        PyDict_SetItemString(disk_dict, "sectors_per_track", PyLong_FromUnsignedLong(entries[i].sectors_per_track));
        PyDict_SetItemString(disk_dict, "total_cylinders", PyLong_FromUnsignedLongLong(entries[i].total_cylinders));
        PyDict_SetItemString(disk_dict, "total_heads", PyLong_FromUnsignedLong(entries[i].total_heads));
        PyDict_SetItemString(disk_dict, "total_sectors", PyLong_FromUnsignedLongLong(entries[i].total_sectors));
        PyDict_SetItemString(disk_dict, "total_tracks", PyLong_FromUnsignedLongLong(entries[i].total_tracks));
        PyDict_SetItemString(disk_dict, "tracks_per_cylinder", PyLong_FromUnsignedLong(entries[i].tracks_per_cylinder));
        PyDict_SetItemString(disk_dict, "bytes_per_sector", PyLong_FromUnsignedLong(entries[i].bytes_per_sector));
        PyDict_SetItemString(disk_dict, "index", PyLong_FromUnsignedLong(entries[i].index));
        PyDict_SetItemString(disk_dict, "signature", PyLong_FromUnsignedLong(entries[i].signature));

        PyList_Append(disks_list, disk_dict);
        Py_DECREF(disk_dict);
    }

    PyDict_SetItemString(result_dict, "disks", disks_list);
    Py_DECREF(disks_list);
    return result_dict;
#endif
}
