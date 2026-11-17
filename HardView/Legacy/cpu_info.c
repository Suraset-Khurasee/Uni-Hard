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


#include "cpu_info.h"
#include "helpers.h"
#ifdef _WIN32
#include "win_helpers.h"
#else
#include "linux_helpers.h"
#include <sys/utsname.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef _WIN32
static void _get_linux_cpu_details(
    char* name, size_t name_sz,
    char* manufacturer, size_t mfg_sz,
    char* architecture, size_t arch_sz,
    int* cores, int* threads,
    double* max_clock_speed,
    char* socket_designation, size_t sock_sz
) {
    snprintf(name, name_sz, "Unknown Processor");
    snprintf(manufacturer, mfg_sz, "Unknown");
    snprintf(architecture, arch_sz, "x86_64");
    snprintf(socket_designation, sock_sz, "Socket 0");
    *cores = 0;
    *threads = 0;
    *max_clock_speed = 0.0;

    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(architecture, arch_sz, "%s", uts.machine);
    }

    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[512];
        int logical_count = 0;
        int cpu_cores_field = 0;
        double cpu_mhz_field = 0.0;

        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "processor", 9) == 0) {
                logical_count++;
            } else if (strncmp(line, "model name", 10) == 0 && strcmp(name, "Unknown Processor") == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ' || *colon == '\t') colon++;
                    colon[strcspn(colon, "\r\n")] = 0;
                    snprintf(name, name_sz, "%s", colon);
                }
            } else if (strncmp(line, "vendor_id", 9) == 0 && strcmp(manufacturer, "Unknown") == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ' || *colon == '\t') colon++;
                    colon[strcspn(colon, "\r\n")] = 0;
                    snprintf(manufacturer, mfg_sz, "%s", colon);
                }
            } else if (strncmp(line, "cpu cores", 9) == 0 && cpu_cores_field == 0) {
                char* colon = strchr(line, ':');
                if (colon) cpu_cores_field = atoi(colon + 1);
            } else if (strncmp(line, "cpu MHz", 7) == 0 && cpu_mhz_field == 0.0) {
                char* colon = strchr(line, ':');
                if (colon) cpu_mhz_field = atof(colon + 1);
            } else if (strncmp(line, "physical id", 11) == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    int phys_id = atoi(colon + 1);
                    snprintf(socket_designation, sock_sz, "Socket %d", phys_id);
                }
            }
        }
        fclose(fp);

        if (logical_count <= 0) {
            long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
            logical_count = (nprocs > 0) ? (int)nprocs : 1;
        }
        *threads = logical_count;
        *cores = (cpu_cores_field > 0) ? cpu_cores_field : logical_count;
        *max_clock_speed = cpu_mhz_field;
    }

    FILE* fmax = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fmax) {
        long khz = 0;
        if (fscanf(fmax, "%ld", &khz) == 1 && khz > 0) {
            *max_clock_speed = (double)khz / 1000.0;
        }
        fclose(fmax);
    }
}
#endif

#ifdef BUILD_PYTHON_MODULE
// Helper function to create Python CPU object
PyObject* create_cpu_python_object(const char* name, const char* manufacturer, 
                                  const char* architecture, int cores, int threads,
                                  double max_clock_speed, const char* socket_designation) {
    PyObject* cpu_dict = PyDict_New();
    if (!cpu_dict) return NULL;
    
    // Add all fields to the dictionary with proper error checking
    PyObject* temp_obj;
    
    temp_obj = PyUnicode_FromString(name ? name : "");
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "name", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(manufacturer ? manufacturer : "");
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "manufacturer", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(architecture ? architecture : "");
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "architecture", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLong(cores);
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "cores", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyLong_FromLong(threads);
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "threads", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyFloat_FromDouble(max_clock_speed);
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "max_clock_speed", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    temp_obj = PyUnicode_FromString(socket_designation ? socket_designation : "");
    if (temp_obj) {
        PyDict_SetItemString(cpu_dict, "socket_designation", temp_obj);
        Py_DECREF(temp_obj);
    }
    
    return cpu_dict;
}

// Function that returns Python objects directly
PyObject* get_cpu_info_objects(bool Json) {
    if (Json) {
        // JSON mode - return string representation
        char* json_str = get_cpu_info(true);
        if (!json_str) return Py_None;
        
        PyObject* py_str = PyUnicode_FromString(json_str);
        free(json_str);
        return py_str ? py_str : Py_None;
    } else {
        // Python objects mode - return object directly
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
        BSTR query = SysAllocString(L"SELECT Name, Manufacturer, Architecture, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, SocketDesignation FROM Win32_Processor");
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

        if (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* architecture = _get_wmi_string_property(pclsObj, L"Architecture");
            ULONG cores = _get_wmi_ulong_property(pclsObj, L"NumberOfCores");
            ULONG threads = _get_wmi_ulong_property(pclsObj, L"NumberOfLogicalProcessors");
            ULONG max_clock = _get_wmi_ulong_property(pclsObj, L"MaxClockSpeed");
            char* socket_designation = _get_wmi_string_property(pclsObj, L"SocketDesignation");
            
            PyObject* cpu_obj = create_cpu_python_object(name, manufacturer, architecture, 
                                                        cores, threads, max_clock, socket_designation);
            
            free(name); free(manufacturer); free(architecture); free(socket_designation);
            
            _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
            return cpu_obj ? cpu_obj : Py_None;
        }
        
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return Py_None;
#else
        // Linux implementation
        char name[256], manufacturer[128], architecture[64], socket_designation[64];
        int cores = 0, threads = 0;
        double max_speed = 0.0;
        _get_linux_cpu_details(
            name, sizeof(name),
            manufacturer, sizeof(manufacturer),
            architecture, sizeof(architecture),
            &cores, &threads, &max_speed,
            socket_designation, sizeof(socket_designation)
        );

        PyObject* cpu_obj = create_cpu_python_object(
            name, manufacturer, architecture,
            cores, threads, max_speed, socket_designation
        );
        return cpu_obj ? cpu_obj : Py_None;
#endif
    }
}
#endif

char* get_cpu_info(bool Json) {
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
            return _create_json_string("{\"error\": \"Failed to initialize WMI for CPU info: 0x%lx\"}", hr);
        } else {
            return NULL; // Return NULL for Python objects on error
        }
    }

    BSTR query_lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Name, Manufacturer, Architecture, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, SocketDesignation FROM Win32_Processor");
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
            return _create_json_string("{\"error\": \"Failed to execute WMI query for CPU: 0x%lx\"}", hr);
        } else {
            return NULL;
        }
    }

    if (Json) {
        // JSON mode - original implementation
        if (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* architecture = _get_wmi_string_property(pclsObj, L"Architecture");
            ULONG cores = _get_wmi_ulong_property(pclsObj, L"NumberOfCores");
            ULONG threads = _get_wmi_ulong_property(pclsObj, L"NumberOfLogicalProcessors");
            ULONG max_clock = _get_wmi_ulong_property(pclsObj, L"MaxClockSpeed");
            char* socket_designation = _get_wmi_string_property(pclsObj, L"SocketDesignation");

            json_result = _create_json_string(
                "{\"name\": \"%s\", \"manufacturer\": \"%s\", \"architecture\": \"%s\", \"cores\": %lu, \"threads\": %lu, \"max_clock_speed\": %lu, \"socket_designation\": \"%s\"}",
                name, manufacturer, architecture, cores, threads, max_clock, socket_designation
            );
            free(name); free(manufacturer); free(architecture); free(socket_designation);
        } else {
            json_result = strdup("{\"error\": \"CPU information not found in WMI.\"}");
        }

        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return json_result;
    } else {
        // Python objects mode - return string representation of Python object
#ifdef BUILD_PYTHON_MODULE
        if (SUCCEEDED(pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn == 1) {
            char* name = _get_wmi_string_property(pclsObj, L"Name");
            char* manufacturer = _get_wmi_string_property(pclsObj, L"Manufacturer");
            char* architecture = _get_wmi_string_property(pclsObj, L"Architecture");
            ULONG cores = _get_wmi_ulong_property(pclsObj, L"NumberOfCores");
            ULONG threads = _get_wmi_ulong_property(pclsObj, L"NumberOfLogicalProcessors");
            ULONG max_clock = _get_wmi_ulong_property(pclsObj, L"MaxClockSpeed");
            char* socket_designation = _get_wmi_string_property(pclsObj, L"SocketDesignation");
            
            PyObject* cpu_obj = create_cpu_python_object(name, manufacturer, architecture, 
                                                        cores, threads, max_clock, socket_designation);
            
            free(name); free(manufacturer); free(architecture); free(socket_designation);
            
            if (cpu_obj) {
                PyObject* repr = PyObject_Repr(cpu_obj);
                if (!repr) {
                    Py_DECREF(cpu_obj);
                    _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                    return strdup("{}");
                }
                
                const char* repr_str = PyUnicode_AsUTF8(repr);
                char* result = strdup(repr_str ? repr_str : "{}");
                Py_DECREF(repr);
                Py_DECREF(cpu_obj);
                
                _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
                return result;
            }
        }
        
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("{}");
#else
        _cleanup_wmi(pLoc, pSvc, pEnumerator, pclsObj);
        return strdup("{}");
#endif
    }

#else
    // Linux implementation
    char name[256], manufacturer[128], architecture[64], socket_designation[64];
    int cores = 0, threads = 0;
    double max_speed = 0.0;
    _get_linux_cpu_details(
        name, sizeof(name),
        manufacturer, sizeof(manufacturer),
        architecture, sizeof(architecture),
        &cores, &threads, &max_speed,
        socket_designation, sizeof(socket_designation)
    );

    if (Json) {
        return _create_json_string(
            "{\"name\": \"%s\", \"manufacturer\": \"%s\", \"architecture\": \"%s\", \"cores\": %d, \"threads\": %d, \"max_clock_speed\": %.2f, \"socket_designation\": \"%s\"}",
            name, manufacturer, architecture, cores, threads, max_speed, socket_designation
        );
    } else {
#ifdef BUILD_PYTHON_MODULE
        PyObject* cpu_obj = create_cpu_python_object(
            name, manufacturer, architecture,
            cores, threads, max_speed, socket_designation
        );
        if (cpu_obj) {
            PyObject* repr = PyObject_Repr(cpu_obj);
            if (!repr) {
                Py_DECREF(cpu_obj);
                return strdup("{}");
            }
            const char* repr_str = PyUnicode_AsUTF8(repr);
            char* result = strdup(repr_str ? repr_str : "{}");
            Py_DECREF(repr);
            Py_DECREF(cpu_obj);
            return result;
        }
        return strdup("{}");
#else
        return strdup("{}");
#endif
    }
#endif
}
