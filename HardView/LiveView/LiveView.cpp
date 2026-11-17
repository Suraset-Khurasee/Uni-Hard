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
// ===================================================================================
// LiveView.cpp: A cross-platform system monitoring library for Python.
//
// This library provides classes to monitor CPU, RAM, Disk, Network, GPU, and
// hardware sensors/temperatures on both Windows and Linux systems, exposed to
// Python via pybind11.
// ===================================================================================

#ifdef LIVEVIEW_CPP
    #define HV_GIL_RELEASE while(false){}
#else
    #define HV_GIL_RELEASE py::gil_scoped_release unlock
#endif

// --- Standard Library Includes ---
#include <algorithm>
#include <chrono>
#include <codecvt>
#include <locale>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include <map>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef LIVEVIEW_CPP
// --- pybind11 Includes ---
#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#endif

// --- HardView Includes ---
#include "../../cpuid/cpuid.hpp"
#include "../../cpuid/cpuidHelpers.hpp"

#ifdef _WIN32
#include "include/HardwareTemp.h" // For Hardware temperature
#include "../../C++/Headers/WMI/WMI_info.hpp"

// --- Platform-Specific Includes & Definitions ---
#include <pdh.h>
#include <pdhmsg.h>
#include <windows.h>
#include <winternl.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "ntdll.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#else // For Linux systems
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unistd.h>
#include <dlfcn.h>
#ifdef HAVE_LIBSENSORS
#include <sensors/sensors.h>
#endif
#include "../../C++/Headers/SMBIOS/SMBIOS.hpp"
typedef uint8_t BYTE;
#endif

#ifndef LIVEVIEW_CPP
namespace py = pybind11;
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// ===================================================================================
// HELPER FUNCTIONS
// ===================================================================================

#ifdef _WIN32
std::string wstring_to_utf8(const std::wstring& wstr) {
  if (wstr.empty())
    return std::string();
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(),
    NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0],
    size_needed, NULL, NULL);
  return strTo;
}

double
CalculateCpuUsagePerCore(const SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION& prev,
  const SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION& curr) {
  ULONGLONG prevIdle = prev.IdleTime.QuadPart;
  ULONGLONG currIdle = curr.IdleTime.QuadPart;
  ULONGLONG prevTotal = prev.KernelTime.QuadPart + prev.UserTime.QuadPart;
  ULONGLONG currTotal = curr.KernelTime.QuadPart + curr.UserTime.QuadPart;
  ULONGLONG totalDelta = currTotal - prevTotal;
  ULONGLONG idleDelta = currIdle - prevIdle;

  if (totalDelta == 0)
    return 0.0;
  return (1.0 - (static_cast<double>(idleDelta) / totalDelta)) * 100.0;
}

double MonitorCpuRealtime_Nt(int intervalMs) {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  DWORD numCores = sysInfo.dwNumberOfProcessors;

  std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> prevData(numCores);
  std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> currData(numCores);
  ULONG len = sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * numCores;

  if (!NT_SUCCESS(NtQuerySystemInformation(
    SystemProcessorPerformanceInformation, prevData.data(), len, NULL))) {
    throw std::runtime_error(
      "Failed to query system information (first call).");
  }
  HV_GIL_RELEASE;
  std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

  if (!NT_SUCCESS(NtQuerySystemInformation(
    SystemProcessorPerformanceInformation, currData.data(), len, NULL))) {
    throw std::runtime_error(
      "Failed to query system information (second call).");
  }

  double totalUsage = 0.0;
  for (DWORD i = 0; i < numCores; ++i) {
    totalUsage += CalculateCpuUsagePerCore(prevData[i], currData[i]);
  }

  return totalUsage / numCores;
}
#endif // _WIN32

#ifdef __linux__
// --- Linux-Specific Helper Structs and Functions ---

namespace fs = std::filesystem;

struct CpuTimes {
  long long user, nice, system, idle, iowait, irq, softirq, steal;
};

inline CpuTimes get_cpu_times() {
  std::ifstream proc_stat("/proc/stat");
  if (!proc_stat.is_open())
    throw std::runtime_error("Failed to open /proc/stat.");
  proc_stat.ignore(5, ' '); // Ignore "cpu "
  CpuTimes times{};
  proc_stat >> times.user >> times.nice >> times.system >> times.idle >>
    times.iowait >> times.irq >> times.softirq >> times.steal;
  return times;
}

inline std::vector<CpuTimes> get_per_core_cpu_times() {
  std::ifstream proc_stat("/proc/stat");
  if (!proc_stat.is_open())
    throw std::runtime_error("Failed to open /proc/stat.");
  std::vector<CpuTimes> cores;
  std::string line;
  while (std::getline(proc_stat, line)) {
    if (line.rfind("cpu", 0) == 0 && line.size() > 3 && std::isdigit(static_cast<unsigned char>(line[3]))) {
      std::stringstream ss(line);
      std::string label;
      CpuTimes t{};
      ss >> label >> t.user >> t.nice >> t.system >> t.idle >>
        t.iowait >> t.irq >> t.softirq >> t.steal;
      cores.push_back(t);
    }
  }
  return cores;
}

inline long long get_idle_time(const CpuTimes& t) { return t.idle + t.iowait; }
inline long long get_total_time(const CpuTimes& t) {
  return t.user + t.nice + t.system + t.idle + t.iowait + t.irq + t.softirq +
    t.steal;
}

inline bool is_whole_disk_device(const std::string& dev) {
  if (dev.empty()) return false;
  if (dev.rfind("loop", 0) == 0 || dev.rfind("ram", 0) == 0 ||
      dev.rfind("zram", 0) == 0 || dev.rfind("sr", 0) == 0 ||
      dev.rfind("dm-", 0) == 0 || dev.rfind("md", 0) == 0) {
    return false;
  }
  // Check /sys/block/<dev> first — whole block devices exist directly in /sys/block
  if (fs::exists("/sys/block/" + dev)) {
    return true;
  }
  // Fallback heuristic if /sys/block is not mounted:
  // nvme0n1 / mmcblk0 are whole disks; nvme0n1p1 / mmcblk0p1 are partitions
  if (dev.rfind("nvme", 0) == 0 || dev.rfind("mmcblk", 0) == 0) {
    auto p_pos = dev.rfind('p');
    if (p_pos != std::string::npos && p_pos > 4) {
      bool all_digits = true;
      for (size_t i = p_pos + 1; i < dev.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(dev[i]))) {
          all_digits = false;
          break;
        }
      }
      if (all_digits && p_pos + 1 < dev.size()) return false;
    }
    return true;
  }
  return !std::isdigit(static_cast<unsigned char>(dev.back()));
}

struct DiskStats {
  long long sectors_read;
  long long sectors_written;
  long long io_ticks;
};

inline std::map<std::string, DiskStats> get_disk_stats() {
  std::ifstream file("/proc/diskstats");
  if (!file.is_open())
    throw std::runtime_error("Failed to open /proc/diskstats.");
  std::map<std::string, DiskStats> stats;
  std::string line;
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    int major = 0, minor = 0;
    std::string device_name;
    long long reads = 0, reads_merged = 0, sectors_read = 0, time_reading = 0;
    long long writes = 0, writes_merged = 0, sectors_written = 0, time_writing = 0;
    long long ios_in_progress = 0, io_ticks = 0;
    ss >> major >> minor >> device_name >> reads >> reads_merged >>
      sectors_read >> time_reading >> writes >> writes_merged >>
      sectors_written >> time_writing >> ios_in_progress >> io_ticks;
    if (!is_whole_disk_device(device_name))
      continue;
    stats[device_name] = { sectors_read, sectors_written, io_ticks };
  }
  return stats;
}

struct NetworkStats {
  long long bytes_received;
  long long bytes_transmitted;
};

inline std::map<std::string, NetworkStats> get_network_stats() {
  std::ifstream file("/proc/net/dev");
  if (!file.is_open())
    throw std::runtime_error("Failed to open /proc/net/dev.");
  std::map<std::string, NetworkStats> stats;
  std::string line;
  std::getline(file, line); // Skip header line 1
  std::getline(file, line); // Skip header line 2
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string iface_name;
    long long recv_bytes = 0, trans_bytes = 0;
    ss >> iface_name >> recv_bytes;
    for (int i = 0; i < 7; ++i)
      ss.ignore(256, ' ');
    ss >> trans_bytes;

    if (!iface_name.empty() && iface_name.back() == ':')
      iface_name.pop_back();
    if (iface_name == "lo")
      continue; // Skip loopback
    stats[iface_name] = { recv_bytes, trans_bytes };
  }
  return stats;
}

// --- Dynamic NVML Loader for NVIDIA GPUs on Linux ---
struct NvmlUtilization {
  unsigned int gpu;
  unsigned int memory;
};

class DynamicNVML {
private:
  void* handle = nullptr;
  bool initialized = false;
  typedef int (*nvmlInit_t)();
  typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
  typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
  typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);
  typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, NvmlUtilization*);
  typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
  typedef int (*nvmlDeviceGetFanSpeed_t)(void*, unsigned int*);
  typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*);

  nvmlInit_t p_nvmlInit = nullptr;
  nvmlDeviceGetCount_t p_nvmlDeviceGetCount = nullptr;
  nvmlDeviceGetHandleByIndex_t p_nvmlDeviceGetHandleByIndex = nullptr;
  nvmlDeviceGetName_t p_nvmlDeviceGetName = nullptr;
  nvmlDeviceGetUtilizationRates_t p_nvmlDeviceGetUtilizationRates = nullptr;
  nvmlDeviceGetTemperature_t p_nvmlDeviceGetTemperature = nullptr;
  nvmlDeviceGetFanSpeed_t p_nvmlDeviceGetFanSpeed = nullptr;
  nvmlDeviceGetPowerUsage_t p_nvmlDeviceGetPowerUsage = nullptr;

  DynamicNVML() {
    handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!handle) handle = dlopen("libnvidia-ml.so", RTLD_LAZY);
    if (!handle) return;

    p_nvmlInit = (nvmlInit_t)dlsym(handle, "nvmlInit_v2");
    if (!p_nvmlInit) p_nvmlInit = (nvmlInit_t)dlsym(handle, "nvmlInit");
    p_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)dlsym(handle, "nvmlDeviceGetCount_v2");
    if (!p_nvmlDeviceGetCount) p_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)dlsym(handle, "nvmlDeviceGetCount");
    p_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)dlsym(handle, "nvmlDeviceGetHandleByIndex_v2");
    if (!p_nvmlDeviceGetHandleByIndex) p_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)dlsym(handle, "nvmlDeviceGetHandleByIndex");
    p_nvmlDeviceGetName = (nvmlDeviceGetName_t)dlsym(handle, "nvmlDeviceGetName");
    p_nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)dlsym(handle, "nvmlDeviceGetUtilizationRates");
    p_nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)dlsym(handle, "nvmlDeviceGetTemperature");
    p_nvmlDeviceGetFanSpeed = (nvmlDeviceGetFanSpeed_t)dlsym(handle, "nvmlDeviceGetFanSpeed");
    p_nvmlDeviceGetPowerUsage = (nvmlDeviceGetPowerUsage_t)dlsym(handle, "nvmlDeviceGetPowerUsage");

    if (p_nvmlInit && p_nvmlInit() == 0) {
      initialized = true;
    }
  }

public:
  static DynamicNVML& instance() {
    static DynamicNVML inst;
    return inst;
  }

  bool is_available() const { return initialized; }

  unsigned int device_count() const {
    if (!initialized || !p_nvmlDeviceGetCount) return 0;
    unsigned int cnt = 0;
    if (p_nvmlDeviceGetCount(&cnt) == 0) return cnt;
    return 0;
  }

  bool get_device_metrics(unsigned int idx, std::string& name, double& util, double& temp, double& fan, double& power_w) const {
    if (!initialized || !p_nvmlDeviceGetHandleByIndex) return false;
    void* dev = nullptr;
    if (p_nvmlDeviceGetHandleByIndex(idx, &dev) != 0 || !dev) return false;
    char nbuf[96] = {0};
    if (p_nvmlDeviceGetName && p_nvmlDeviceGetName(dev, nbuf, sizeof(nbuf) - 1) == 0) {
      name = nbuf;
    } else {
      name = "NVIDIA GPU " + std::to_string(idx);
    }
    util = 0.0;
    if (p_nvmlDeviceGetUtilizationRates) {
      NvmlUtilization u{};
      if (p_nvmlDeviceGetUtilizationRates(dev, &u) == 0) {
        util = static_cast<double>(u.gpu);
      }
    }
    temp = -1.0;
    if (p_nvmlDeviceGetTemperature) {
      unsigned int t = 0;
      if (p_nvmlDeviceGetTemperature(dev, 0, &t) == 0) {
        temp = static_cast<double>(t);
      }
    }
    fan = 0.0;
    if (p_nvmlDeviceGetFanSpeed) {
      unsigned int f = 0;
      if (p_nvmlDeviceGetFanSpeed(dev, &f) == 0) {
        fan = static_cast<double>(f);
      }
    }
    power_w = -1.0;
    if (p_nvmlDeviceGetPowerUsage) {
      unsigned int mw = 0;
      if (p_nvmlDeviceGetPowerUsage(dev, &mw) == 0) {
        power_w = static_cast<double>(mw) / 1000.0;
      }
    }
    return true;
  }
};

inline std::string read_first_line_trimmed(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return "";
  std::string line;
  std::getline(f, line);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
    line.pop_back();
  return line;
}

inline std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Resolves a hardware name or hwmon chip name to COMPONENT_IDS:
// Motherboard=1, SuperIO=2, CPU=3, Memory=4, GPU=5, Storage=6, Network=7, Cooler=8, EmbeddedController=9, Psu=10, Battery=11
inline int ResolveLinuxHardwareIdByName(const std::string& name) {
  std::string low = to_lower_copy(name);
  if (low.empty()) return -1;

  if (low.find("cpu") != std::string::npos || low.find("k10temp") != std::string::npos ||
      low.find("coretemp") != std::string::npos || low.find("zenpower") != std::string::npos ||
      low.find("ryzen") != std::string::npos || low.find("intel") != std::string::npos ||
      low.find("x86_pkg") != std::string::npos || low.find("package") != std::string::npos) {
    return 3; // CPU
  }
  if (low.find("gpu") != std::string::npos || low.find("nvidia") != std::string::npos ||
      low.find("geforce") != std::string::npos || low.find("rtx") != std::string::npos ||
      low.find("gtx") != std::string::npos || low.find("radeon") != std::string::npos ||
      low.find("amdgpu") != std::string::npos || low.find("nouveau") != std::string::npos ||
      low.find("xe") != std::string::npos || low.find("i915") != std::string::npos) {
    return 5; // GPU
  }
  if (low.find("ssd") != std::string::npos || low.find("nvme") != std::string::npos ||
      low.find("hdd") != std::string::npos || low.find("storage") != std::string::npos ||
      low.find("drivetemp") != std::string::npos || low.find("disk") != std::string::npos ||
      low.find("wd") != std::string::npos || low.find("samsung") != std::string::npos ||
      low.find("kingston") != std::string::npos || low.find("crucial") != std::string::npos ||
      low.find("seagate") != std::string::npos || low.find("toshiba") != std::string::npos) {
    return 6; // Storage
  }
  if (low.find("mem") != std::string::npos || low.find("ram") != std::string::npos ||
      low.find("dimm") != std::string::npos || low.find("spd5118") != std::string::npos ||
      low.find("jc42") != std::string::npos) {
    return 4; // Memory
  }
  if (low.find("bat") != std::string::npos) {
    return 11; // Battery
  }
  if (low.find("acad") != std::string::npos || low.find("ucsi") != std::string::npos ||
      low.find("psu") != std::string::npos || low.find("power") != std::string::npos) {
    return 10; // Psu
  }
  if (low.find("fan") != std::string::npos || low.find("cooler") != std::string::npos) {
    return 8; // Cooler
  }
  if (low.find("ec") != std::string::npos || low.find("cros_ec") != std::string::npos) {
    return 9; // EmbeddedController
  }
  if (low.find("eth") != std::string::npos || low.find("wlan") != std::string::npos ||
      low.find("wifi") != std::string::npos || low.find("iwlwifi") != std::string::npos ||
      low.find("mt79") != std::string::npos || low.find("net") != std::string::npos) {
    return 7; // Network
  }
  if (low.find("nct") != std::string::npos || low.find("it87") != std::string::npos ||
      low.find("f718") != std::string::npos || low.find("w836") != std::string::npos ||
      low.find("superio") != std::string::npos) {
    return 2; // SuperIO
  }
  if (low.find("acpitz") != std::string::npos || low.find("asus") != std::string::npos ||
      low.find("motherboard") != std::string::npos || low.find("board") != std::string::npos ||
      low.find("system") != std::string::npos || low.find("pch") != std::string::npos ||
      low.find("thinkpad") != std::string::npos || low.find("dell") != std::string::npos ||
      low.find("hp") != std::string::npos || low.find("msi") != std::string::npos ||
      low.find("gigabyte") != std::string::npos) {
    return 1; // Motherboard
  }
  // Default to Motherboard (1) so parse_sensor("<Hardware> - <Type> - <Name>") always resolves cleanly
  return 1;
}

inline std::string MapChipToCanonicalHardwareName(const std::string& chip_name, const std::string& hwmon_dir) {
  int id = ResolveLinuxHardwareIdByName(chip_name);
  switch (id) {
    case 3: return "CPU (" + chip_name + ")";
    case 5: return "GPU (" + chip_name + ")";
    case 6: {
      // Check if an nvme model exists
      std::string model = read_first_line_trimmed(hwmon_dir + "/device/model");
      if (!model.empty()) return "NVMe SSD (" + model + ")";
      return "Storage (" + chip_name + ")";
    }
    case 4: return "DIMM Memory (" + chip_name + ")";
    case 11: return "Battery (" + chip_name + ")";
    case 10: return "PSU (" + chip_name + ")";
    case 8: return "Cooler (" + chip_name + ")";
    default: return "Motherboard (" + chip_name + ")";
  }
}

// Collects all sensors on Linux formatted as "<Hardware> - <SensorType> - <Name>"
inline std::map<std::string, double> CollectAllLinuxSensorsFormatted() {
  std::map<std::string, double> result;

  if (fs::exists("/sys/class/hwmon")) {
    for (const auto& entry : fs::directory_iterator("/sys/class/hwmon")) {
      std::string hdir = entry.path().string();
      std::string chip = read_first_line_trimmed(hdir + "/name");
      if (chip.empty()) chip = entry.path().filename().string();
      std::string hw_name = MapChipToCanonicalHardwareName(chip, hdir);

      for (const auto& fentry : fs::directory_iterator(hdir)) {
        std::string fname = fentry.path().filename().string();
        if (fname.size() <= 6 || fname.substr(fname.size() - 6) != "_input") continue;

        std::string prefix = fname.substr(0, fname.size() - 6); // e.g., temp1, fan1, in0, power1, freq1, curr1
        std::string raw_val_str = read_first_line_trimmed(fentry.path().string());
        if (raw_val_str.empty()) continue;

        double raw_val = 0.0;
        try {
          raw_val = std::stod(raw_val_str);
        } catch (...) {
          continue;
        }

        std::string label = read_first_line_trimmed(hdir + "/" + prefix + "_label");
        if (label.empty()) label = prefix;

        std::string sensor_type;
        double scaled_val = raw_val;

        if (prefix.rfind("temp", 0) == 0) {
          sensor_type = "Temperature";
          scaled_val = raw_val / 1000.0;
        } else if (prefix.rfind("fan", 0) == 0) {
          sensor_type = "Fan";
          scaled_val = raw_val;
        } else if (prefix.rfind("in", 0) == 0) {
          sensor_type = "Voltage";
          scaled_val = raw_val / 1000.0;
        } else if (prefix.rfind("power", 0) == 0) {
          sensor_type = "Power";
          scaled_val = raw_val / 1000000.0;
        } else if (prefix.rfind("curr", 0) == 0) {
          sensor_type = "Current";
          scaled_val = raw_val / 1000.0;
        } else if (prefix.rfind("freq", 0) == 0) {
          sensor_type = "Clock";
          scaled_val = raw_val / 1000000.0; // MHz
        } else {
          continue;
        }

        std::string full_key = hw_name + " - " + sensor_type + " - " + label;
        result[full_key] = scaled_val;
      }
    }
  }

  // Also include NVML GPU sensors if available
  auto& nvml = DynamicNVML::instance();
  unsigned int gcount = nvml.device_count();
  for (unsigned int i = 0; i < gcount; ++i) {
    std::string gname;
    double util = 0.0, temp = -1.0, fan = 0.0, power_w = -1.0;
    if (nvml.get_device_metrics(i, gname, util, temp, fan, power_w)) {
      std::string hw = "GPU (" + gname + ")";
      result[hw + " - Load - GPU Core"] = util;
      if (temp >= 0.0) result[hw + " - Temperature - GPU Core"] = temp;
      result[hw + " - Fan - GPU Fan"] = fan;
      if (power_w >= 0.0) result[hw + " - Power - GPU Package"] = power_w;
    }
  }

  // Fallback to thermal_zone if result is still empty
  if (result.empty() && fs::exists("/sys/class/thermal")) {
    for (const auto& entry : fs::directory_iterator("/sys/class/thermal")) {
      std::string fname = entry.path().filename().string();
      if (fname.rfind("thermal_zone", 0) != 0) continue;
      std::string ttype = read_first_line_trimmed(entry.path().string() + "/type");
      std::string tval = read_first_line_trimmed(entry.path().string() + "/temp");
      if (!tval.empty()) {
        try {
          double v = std::stod(tval) / 1000.0;
          result["Motherboard (" + (ttype.empty() ? fname : ttype) + ") - Temperature - " + fname] = v;
        } catch (...) {}
      }
    }
  }

  return result;
}

// Temperature helpers for Linux (work with or without libsensors)
inline double GetSensorTempByKeywords(const std::vector<std::string>& keywords, const std::vector<std::string>& MatchKeyWords = {}) {
#ifdef HAVE_LIBSENSORS
  if (sensors_init(nullptr) == 0) {
    const sensors_chip_name* chip;
    int chip_nr = 0;
    while ((chip = sensors_get_detected_chips(nullptr, &chip_nr)) != nullptr) {
      const sensors_feature* feature;
      int feature_nr = 0;
      while ((feature = sensors_get_features(chip, &feature_nr)) != nullptr) {
        const char* feature_label = sensors_get_label(chip, feature);
        if (!feature_label) continue;
        std::string label = feature_label;
        std::free((void*)feature_label);

        bool match = false;
        for (const auto& key : MatchKeyWords) {
          if (label == key) { match = true; break; }
        }
        for (const auto& key : keywords) {
          if (label == key || label.find(key) != std::string::npos) { match = true; break; }
        }

        if (match) {
          const sensors_subfeature* sub;
          int sub_nr = 0;
          while ((sub = sensors_get_all_subfeatures(chip, feature, &sub_nr)) != nullptr) {
            if (sub->flags & SENSORS_MODE_R) {
              double value = -1.0;
              if (sensors_get_value(chip, sub->number, &value) == 0 && value > 0.0) {
                sensors_cleanup();
                return value;
              }
              break;
            }
          }
        }
      }
    }
    sensors_cleanup();
  }
#endif
  // Fallback to sysfs hwmon scan
  auto all = CollectAllLinuxSensorsFormatted();
  for (const auto& [k, v] : all) {
    if (k.find(" - Temperature - ") == std::string::npos) continue;
    std::string k_low = to_lower_copy(k);
    for (const auto& kw : keywords) {
      if (k_low.find(to_lower_copy(kw)) != std::string::npos && v > 0.0) {
        return v;
      }
    }
    for (const auto& kw : MatchKeyWords) {
      if (k_low.find(to_lower_copy(kw)) != std::string::npos && v > 0.0) {
        return v;
      }
    }
  }
  return -1.0;
}

inline double GetCpuPackageTemp() {
  double t = GetSensorTempByKeywords({ "Package id", "Physical id", "Tdie", "Tctl", "k10temp", "coretemp", "CPU" });
  if (t > 0.0) return t;
  // Fallback to first thermal zone
  std::string tz = read_first_line_trimmed("/sys/class/thermal/thermal_zone0/temp");
  if (!tz.empty()) {
    try { return std::stod(tz) / 1000.0; } catch (...) {}
  }
  return -1.0;
}

inline double GetMotherboardTemp() {
  double t = GetSensorTempByKeywords({ "MB ", "SYSTIN", "System", "Board", "acpitz", "asus" }, { "temp1" });
  if (t > 0.0) return t;
  return GetCpuPackageTemp();
}

inline double GetVRMTemp() {
  return GetSensorTempByKeywords({ "VRM", "spd5118", "DIMM" });
}

inline double GetChipsetTemp() {
  return GetSensorTempByKeywords({ "PCH", "Chipset", "acpitz" });
}

inline double GetDIMMTemp() {
  return GetSensorTempByKeywords({ "DIMM", "Memory", "spd5118" });
}

inline double GetDriveTemp() {
  return GetSensorTempByKeywords({ "drive", "HDD", "SSD", "nvme", "Composite" });
}

inline double GetSensorByName(const std::string& sensorName, bool exactMatch) {
#ifdef HAVE_LIBSENSORS
  if (sensors_init(nullptr) == 0) {
    const sensors_chip_name* chip;
    int chip_nr = 0;
    while ((chip = sensors_get_detected_chips(nullptr, &chip_nr)) != nullptr) {
      const sensors_feature* feature;
      int feature_nr = 0;
      while ((feature = sensors_get_features(chip, &feature_nr)) != nullptr) {
        const char* feature_label = sensors_get_label(chip, feature);
        if (!feature_label) continue;
        std::string label = feature_label;
        std::free((void*)feature_label);
        bool match = exactMatch ? (label == sensorName) : (label.find(sensorName) != std::string::npos);
        if (match) {
          const sensors_subfeature* sub;
          int sub_nr = 0;
          while ((sub = sensors_get_all_subfeatures(chip, feature, &sub_nr)) != nullptr) {
            if (sub->flags & SENSORS_MODE_R) {
              double value = -1.0;
              if (sensors_get_value(chip, sub->number, &value) == 0) {
                sensors_cleanup();
                return value;
              }
              break;
            }
          }
        }
      }
    }
    sensors_cleanup();
  }
#endif
  auto all = CollectAllLinuxSensorsFormatted();
  for (const auto& [k, v] : all) {
    if (exactMatch) {
      if (k == sensorName) return v;
    } else {
      if (k.find(sensorName) != std::string::npos) return v;
    }
  }
  return -1.0;
}

inline std::vector<std::string> GetAllSensorNames() {
  std::vector<std::string> sensorNames;
#ifdef HAVE_LIBSENSORS
  if (sensors_init(nullptr) == 0) {
    const sensors_chip_name* chip;
    int chip_nr = 0;
    while ((chip = sensors_get_detected_chips(nullptr, &chip_nr)) != nullptr) {
      const sensors_feature* feature;
      int feature_nr = 0;
      while ((feature = sensors_get_features(chip, &feature_nr)) != nullptr) {
        const char* feature_label = sensors_get_label(chip, feature);
        if (feature_label) {
          sensorNames.emplace_back(feature_label);
          std::free((void*)feature_label);
        }
      }
    }
    sensors_cleanup();
  }
#endif
  if (sensorNames.empty()) {
    auto all = CollectAllLinuxSensorsFormatted();
    for (const auto& [k, _] : all) {
      sensorNames.push_back(k);
    }
  }
  return sensorNames;
}

#endif // __linux__

// ===================================================================================
// MONITORING CLASSES
// ===================================================================================

/**
 * @class PyLiveCPU
 * @brief Provides CPU monitoring functionalities.
 */
class PyLiveCPU {
public:
  double get_usage(int interval_ms) const {
#ifdef _WIN32
    return MonitorCpuRealtime_Nt(interval_ms);
#elif __linux__
    CpuTimes start = get_cpu_times();
    HV_GIL_RELEASE;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    CpuTimes end = get_cpu_times();

    long long idle_delta = get_idle_time(end) - get_idle_time(start);
    long long total_delta = get_total_time(end) - get_total_time(start);

    if (total_delta == 0)
      return 0.0;
    return (1.0 - static_cast<double>(idle_delta) / total_delta) * 100.0;
#else
    throw std::runtime_error("Unsupported platform for CPU usage.");
#endif
  }

  inline std::vector<std::pair<std::string, std::string>> cpuid() {
    auto res = cpuid::helpers::Smart_cpuid();
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& r : res) {
      result.push_back({ std::get<0>(r), std::get<1>(r) });
    }
    return result;
  }

#ifndef LIVEVIEW_CPP
  std::variant<int, py::dict> CpuSnapShot(int core, bool coreNumbers = false,
    bool Kernel = true, bool User = true,
    bool Idle = true,
    bool PureKernalTime = false) {
    if (core < 0) {
      throw std::invalid_argument("Core index must be non-negative.");
    }
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD numCores = sysInfo.dwNumberOfProcessors;

    if (coreNumbers) {
      return static_cast<int>(numCores);
    }

    if (static_cast<DWORD>(core) >= numCores) {
      throw std::out_of_range("Core index out of range.");
    }

    ULONG len = sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * numCores;
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> data(numCores);

    if (!NT_SUCCESS(NtQuerySystemInformation(
      SystemProcessorPerformanceInformation, data.data(), len, NULL))) {
      throw std::runtime_error(
        "Failed to query system information using NtQuerySystemInformation.");
    }

    py::dict result;
    if (Kernel)
      result["raw_kernel_time"] =
      static_cast<double>(data[core].KernelTime.QuadPart);
    if (PureKernalTime)
      result["pure_kernel_time"] = static_cast<double>(
        data[core].KernelTime.QuadPart - data[core].IdleTime.QuadPart);
    if (User)
      result["user_time"] = static_cast<double>(data[core].UserTime.QuadPart);
    if (Idle)
      result["idle_time"] = static_cast<double>(data[core].IdleTime.QuadPart);

    return result;
#elif __linux__
    auto cores = get_per_core_cpu_times();
    int numCores = static_cast<int>(cores.size());
    if (coreNumbers) {
      return numCores;
    }
    if (core >= numCores) {
      throw std::out_of_range("Core index out of range.");
    }
    const auto& c = cores[core];
    double pure_kernel = static_cast<double>(c.system + c.irq + c.softirq + c.steal);
    double idle_t = static_cast<double>(c.idle + c.iowait);
    double user_t = static_cast<double>(c.user + c.nice);

    py::dict result;
    if (Kernel)
      result["raw_kernel_time"] = pure_kernel + idle_t;
    if (PureKernalTime)
      result["pure_kernel_time"] = pure_kernel;
    if (User)
      result["user_time"] = user_t;
    if (Idle)
      result["idle_time"] = idle_t;
    return result;
#endif
  }
#endif
};

/**
 * @class PyLiveRam
 * @brief Provides RAM monitoring functionalities.
 */
class PyLiveRam {
public:
  std::variant<double, std::vector<double>> get_usage(bool Raw = false) const {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo = { sizeof(memInfo) };
    if (!GlobalMemoryStatusEx(&memInfo)) {
      throw std::runtime_error(
        "Failed to query memory status with GlobalMemoryStatusEx.");
    }
    DWORDLONG totalPhys = memInfo.ullTotalPhys;
    DWORDLONG usedPhys = totalPhys - memInfo.ullAvailPhys;
    if (Raw) {
      return std::vector<double>{static_cast<double>(usedPhys),
        static_cast<double>(totalPhys)};
    }
    return (static_cast<double>(usedPhys) / totalPhys) * 100.0;
#elif __linux__
    std::ifstream meminfo_file("/proc/meminfo");
    if (!meminfo_file.is_open())
      throw std::runtime_error("Failed to open /proc/meminfo.");
    std::string line;
    long long memTotal = -1, memAvailable = -1;
    while (std::getline(meminfo_file, line)) {
      if (line.rfind("MemTotal:", 0) == 0) {
        std::stringstream ss(line);
        std::string key;
        ss >> key >> memTotal;
      }
      else if (line.rfind("MemAvailable:", 0) == 0) {
        std::stringstream ss(line);
        std::string key;
        ss >> key >> memAvailable;
      }
    }
    if (memTotal == -1 || memAvailable == -1) {
      throw std::runtime_error(
        "Failed to parse MemTotal or MemAvailable from /proc/meminfo.");
    }
    long long memUsed = memTotal - memAvailable;
    if (Raw) {
      return std::vector<double>{static_cast<double>(memUsed * 1024),
        static_cast<double>(memTotal * 1024)};
    }
    return (static_cast<double>(memUsed) / memTotal) * 100.0;
#else
    throw std::runtime_error("Unsupported platform for RAM usage.");
#endif
  }
};

/**
 * @class PyLiveDisk
 * @brief Provides disk I/O monitoring functionalities.
 */
class PyLiveDisk {
private:
#ifdef _WIN32
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER counter = nullptr;
  PDH_HCOUNTER readCounter = nullptr;
  PDH_HCOUNTER writeCounter = nullptr;
#endif
  int mode = 0;

public:
  PyLiveDisk(int mode) : mode(mode) {
    if (mode != 0 && mode != 1) {
      throw std::invalid_argument("Invalid mode for PyLiveDisk. Use 0 for "
        "percentage or 1 for read/write speed.");
    }
#ifdef _WIN32
    if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS)
      throw std::runtime_error("Disk Monitor: Failed to open PDH query.");

    if (mode == 0) {
      if (PdhAddCounterW(query, L"\\PhysicalDisk(_Total)\\% Disk Time", 0,
        &counter) != ERROR_SUCCESS)
        throw std::runtime_error(
          "Disk Monitor: Failed to add '% Disk Time' counter.");
    }
    else if (mode == 1) {
      if (PdhAddCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
        0, &readCounter) != ERROR_SUCCESS)
        throw std::runtime_error(
          "Disk Monitor: Failed to add 'Disk Read Bytes/sec' counter.");
      if (PdhAddCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
        0, &writeCounter) != ERROR_SUCCESS)
        throw std::runtime_error(
          "Disk Monitor: Failed to add 'Disk Write Bytes/sec' counter.");
    }
    PdhCollectQueryData(query); // Initial sample
#endif
  }

  ~PyLiveDisk() {
#ifdef _WIN32
    if (query)
      PdhCloseQuery(query);
#endif
  }

  std::variant<double, std::vector<std::pair<std::string, double>>>
    get_usage(int interval = 1000) {
#ifdef _WIN32
    HV_GIL_RELEASE;
    Sleep(interval);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS)
      throw std::runtime_error("Disk Monitor: Failed to collect query data.");

    if (mode == 0) {
      PDH_FMT_COUNTERVALUE value;
      if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &value) !=
        ERROR_SUCCESS)
        throw std::runtime_error("Disk Monitor: Failed to read '% Disk Time'.");
      if (value.CStatus != ERROR_SUCCESS) {
        return -0.1;
      }
      return value.doubleValue;
    }
    else { // mode == 1
      PDH_FMT_COUNTERVALUE valueRead, valueWrite;
      if (PdhGetFormattedCounterValue(readCounter, PDH_FMT_DOUBLE, NULL,
        &valueRead) != ERROR_SUCCESS)
        throw std::runtime_error(
          "Disk Monitor: Failed to read 'Disk Read Bytes/sec'.");
      if (PdhGetFormattedCounterValue(writeCounter, PDH_FMT_DOUBLE, NULL,
        &valueWrite) != ERROR_SUCCESS)
        throw std::runtime_error(
          "Disk Monitor: Failed to read 'Disk Write Bytes/sec'.");
      if (valueRead.CStatus != ERROR_SUCCESS ||
        valueWrite.CStatus != ERROR_SUCCESS) {
        return std::vector<std::pair<std::string, double>>{
          {"Read MB/s", -0.1}, { "Write MB/s", -0.1 }};
      }
      double read_MBps = valueRead.doubleValue / (1024.0 * 1024.0);
      double write_MBps = valueWrite.doubleValue / (1024.0 * 1024.0);
      return std::vector<std::pair<std::string, double>>{
        {"Read MB/s", read_MBps}, { "Write MB/s", write_MBps }};
    }
#elif __linux__
    auto start_stats = get_disk_stats();
    HV_GIL_RELEASE;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    auto end_stats = get_disk_stats();

    if (mode == 0) {
      long long max_ticks_delta = 0;
      for (auto const& [dev, end_stat] : end_stats) {
        if (start_stats.count(dev)) {
          long long delta = end_stat.io_ticks - start_stats.at(dev).io_ticks;
          if (delta > max_ticks_delta) max_ticks_delta = delta;
        }
      }
      if (interval <= 0) return 0.0;
      double pct = (static_cast<double>(max_ticks_delta) / static_cast<double>(interval)) * 100.0;
      if (pct > 100.0) pct = 100.0;
      if (pct < 0.0) pct = 0.0;
      return pct;
    }

    long long total_sectors_read = 0;
    long long total_sectors_written = 0;
    for (auto const& [dev, end_stat] : end_stats) {
      if (start_stats.count(dev)) {
        total_sectors_read +=
          end_stat.sectors_read - start_stats.at(dev).sectors_read;
        total_sectors_written +=
          end_stat.sectors_written - start_stats.at(dev).sectors_written;
      }
    }
    double interval_sec = static_cast<double>(interval) / 1000.0;
    if (interval_sec <= 0.0) interval_sec = 1.0;
    double read_MBps =
      (total_sectors_read * 512.0) / (1024.0 * 1024.0) / interval_sec;
    double write_MBps =
      (total_sectors_written * 512.0) / (1024.0 * 1024.0) / interval_sec;
    return std::vector<std::pair<std::string, double>>{
      {"Read MB/s", read_MBps}, { "Write MB/s", write_MBps }};
#endif
  }

  bool HighDiskUsage(double threshold_mbps = 80.0) {
    if (mode != 1) {
      throw std::runtime_error("HighDiskUsage() can only be called in mode 1.");
    }
    auto usage =
      std::get<std::vector<std::pair<std::string, double>>>(get_usage(1000));
    return usage[0].second > threshold_mbps || usage[1].second > threshold_mbps;
  }
};

/**
 * @class PyLiveNetwork
 * @brief Provides network I/O monitoring functionalities.
 */
class PyLiveNetwork {
private:
#ifdef _WIN32
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER counter = nullptr;
#endif

public:
  PyLiveNetwork() {
#ifdef _WIN32
    if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS)
      throw std::runtime_error("Network Monitor: Failed to open PDH query.");
    if (PdhAddEnglishCounterW(query, L"\\Network Interface(*)\\Bytes Total/sec",
      0, &counter) != ERROR_SUCCESS)
      throw std::runtime_error(
        "Network Monitor: Failed to add 'Bytes Total/sec' counter.");
    PdhCollectQueryData(query);
#endif
  }

  ~PyLiveNetwork() {
#ifdef _WIN32
    if (query)
      PdhCloseQuery(query);
#endif
  }

  std::variant<double, std::vector<std::pair<std::string, double>>>
    get_usage(int interval = 1000, int mode = 0) {
#ifdef _WIN32
    HV_GIL_RELEASE;
    Sleep(interval);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS)
      throw std::runtime_error(
        "Network Monitor: Failed to collect query data.");

    DWORD bufferSize = 0, itemCount = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
    auto status = PdhGetFormattedCounterArrayW(
      counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA)
      throw std::runtime_error("Network Monitor: Failed to get buffer size for "
        "counter array. PDH Error: " +
        std::to_string(status));

    std::vector<BYTE> buffer(bufferSize);
    items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize,
      &itemCount, items) != ERROR_SUCCESS) {
      throw std::runtime_error(
        "Network Monitor: Failed to get formatted counter array.");
    }

    if (mode == 0) {
      double totalBytes = 0;
      for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
          continue;
        totalBytes += items[i].FmtValue.doubleValue;
      }
      return totalBytes / (1024.0 * 1024.0);
    }
    else {
      std::vector<std::pair<std::string, double>> result;
      for (DWORD i = 0; i < itemCount; ++i) {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS) {
          items[i].FmtValue.doubleValue = -0.1;
        }
        result.emplace_back(wstring_to_utf8(items[i].szName),
          items[i].FmtValue.doubleValue / (1024.0 * 1024.0));
      }
      return result;
    }
#elif __linux__
    auto start_stats = get_network_stats();
    HV_GIL_RELEASE;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    auto end_stats = get_network_stats();
    double interval_sec = static_cast<double>(interval) / 1000.0;
    if (interval_sec <= 0.0) interval_sec = 1.0;

    if (mode == 0) {
      long long total_bytes_delta = 0;
      for (auto const& [iface, end_stat] : end_stats) {
        if (start_stats.count(iface)) {
          total_bytes_delta +=
            (end_stat.bytes_received - start_stats.at(iface).bytes_received);
          total_bytes_delta += (end_stat.bytes_transmitted -
            start_stats.at(iface).bytes_transmitted);
        }
      }
      return (total_bytes_delta) / (1024.0 * 1024.0) / interval_sec;
    }
    else { // mode == 1
      std::vector<std::pair<std::string, double>> result;
      for (auto const& [iface, end_stat] : end_stats) {
        if (start_stats.count(iface)) {
          long long bytes_delta =
            (end_stat.bytes_received - start_stats.at(iface).bytes_received) +
            (end_stat.bytes_transmitted -
              start_stats.at(iface).bytes_transmitted);
          result.emplace_back(iface,
            (bytes_delta) / (1024.0 * 1024.0) / interval_sec);
        }
      }
      return result;
    }
#endif
  }

  std::string getHighCard() {
    auto cards = std::get<std::vector<std::pair<std::string, double>>>(
      get_usage(1000, 1));
    if (cards.empty())
      return "N/A";

    auto max_it = std::max_element(
      cards.begin(), cards.end(),
      [](const auto& a, const auto& b) { return a.second < b.second; });
    return max_it->first;
  }
};

#ifdef _WIN32
/**
 * @class PyLiveGpu (Windows implementation)
 */
class PyLiveGpu {
private:
  HQUERY query = nullptr;
  bool TotalAv = false;
  HCOUNTER TotalCounter = nullptr;
  std::vector<HCOUNTER> counters;
  std::vector<std::wstring> counter_names;

public:
  PyLiveGpu() {
    if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS) {
      throw std::runtime_error("GPU Monitor: Failed to open PDH query.");
    }

    DWORD counter_list_size = 0;
    DWORD instance_list_size = 0;

    PdhEnumObjectItemsW(NULL, NULL, L"GPU Engine", NULL, &counter_list_size,
      NULL, &instance_list_size, PERF_DETAIL_WIZARD, 0);

    std::vector<wchar_t> instance_list(instance_list_size);
    std::vector<wchar_t> counter_list(counter_list_size);

    if (PdhEnumObjectItemsW(NULL, NULL, L"GPU Engine", counter_list.data(),
      &counter_list_size, instance_list.data(),
      &instance_list_size, PERF_DETAIL_WIZARD,
      0) == ERROR_SUCCESS) {

      for (const wchar_t* p_instance = instance_list.data(); *p_instance;
        p_instance += wcslen(p_instance) + 1) {
        std::wstring current_instance(p_instance);

        if (current_instance.find(L"_Total") != std::wstring::npos) {
          TotalAv = true;
          std::wstring counter_path = L"\\GPU Engine(" + current_instance +
            L")\\Utilization Percentage";
          HCOUNTER counter;
          if (PdhAddCounterW(query, counter_path.c_str(), 0, &counter) ==
            ERROR_SUCCESS) {
            counters.insert(counters.begin(), counter);
            counter_names.insert(counter_names.begin(), current_instance);
          }
          continue;
        }

        std::wstring counter_path =
          L"\\GPU Engine(" + current_instance + L")\\Utilization Percentage";

        HCOUNTER counter;
        if (PdhAddCounterW(query, counter_path.c_str(), 0, &counter) ==
          ERROR_SUCCESS) {
          counters.push_back(counter);
          counter_names.push_back(current_instance);
        }
      }
    }

    if (counters.empty()) {
      PdhCloseQuery(query);
      throw std::runtime_error(
        "GPU Monitor: Could not find any suitable GPU performance counters.");
    }

    PdhCollectQueryData(query);
  }

  ~PyLiveGpu() {
    if (query)
      PdhCloseQuery(query);
  }

  double get_usage(int interval_ms = 1000) const {
    if (!query || counters.empty()) {
      throw std::runtime_error("GPU Monitor was not initialized correctly.");
    }
    HV_GIL_RELEASE;
    Sleep(interval_ms);

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
      throw std::runtime_error("GPU Monitor: Failed to collect query data.");
    }

    if (TotalAv) {
      PDH_FMT_COUNTERVALUE value;
      if (PdhGetFormattedCounterValue(counters[0], PDH_FMT_DOUBLE, NULL,
        &value) == ERROR_SUCCESS) {
        if (value.CStatus == ERROR_SUCCESS && value.doubleValue >= 0) {
          return value.doubleValue;
        }
      }
    }
    double total_usage = 0.0;
    int valid_counters = 0;
    for (size_t i = 0; i < counters.size(); ++i) {
      PDH_FMT_COUNTERVALUE value;
      if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, NULL,
        &value) == ERROR_SUCCESS) {
        if (value.CStatus == ERROR_SUCCESS && value.doubleValue >= 0) {
          total_usage += value.doubleValue;
          valid_counters++;
        }
      }
    }

    if (valid_counters == 0) {
      throw std::runtime_error(
        "GPU Monitor: No valid counter values available.");
    }

    return total_usage;
  }

  double get_average_usage(int interval_ms = 1000) const {
    if (!query || counters.empty()) {
      throw std::runtime_error("GPU Monitor was not initialized correctly.");
    }
    HV_GIL_RELEASE;
    Sleep(interval_ms);

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
      throw std::runtime_error("GPU Monitor: Failed to collect query data.");
    }

    double total_usage = 0.0;
    int valid_counters = 0;

    for (size_t i = 0; i < counters.size(); ++i) {
      if (TotalAv && i == 0)
        continue;
      PDH_FMT_COUNTERVALUE value;
      if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, NULL,
        &value) == ERROR_SUCCESS) {
        if (value.CStatus == ERROR_SUCCESS && value.doubleValue >= 0) {
          total_usage += value.doubleValue;
          valid_counters++;
        }
      }
    }

    if (valid_counters == 0) {
      throw std::runtime_error(
        "GPU Monitor: No valid counter values available.");
    }

    return total_usage / valid_counters;
  }

  double get_max_usage(int interval_ms = 1000) const {
    if (!query || counters.empty()) {
      throw std::runtime_error("GPU Monitor was not initialized correctly.");
    }
    HV_GIL_RELEASE;
    Sleep(interval_ms);

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
      throw std::runtime_error("GPU Monitor: Failed to collect query data.");
    }

    double max_usage = 0.0;
    int valid_counters = 0;

    for (size_t i = 0; i < counters.size(); ++i) {
      if (TotalAv && i == 0)
        continue;
      PDH_FMT_COUNTERVALUE value;
      if (PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, NULL,
        &value) == ERROR_SUCCESS) {
        if (value.CStatus == ERROR_SUCCESS && value.doubleValue >= 0) {
          max_usage = max(max_usage, value.doubleValue);
          valid_counters++;
        }
      }
    }

    if (valid_counters == 0) {
      throw std::runtime_error(
        "GPU Monitor: No valid counter values available.");
    }

    return max_usage;
  }

  size_t get_counter_count() const { return counters.size(); }
};

class PyTempCpu {
  double temp;
  double max_temp;
  double avg_temp;
  double fan_rpm;

public:
  PyTempCpu(bool init = true) {
    if (init) {
      if (InitHardwareTempMonitor() != 0) {
        throw std::runtime_error(
          "LiveView: Failed to initialize hardware temperature monitor Check "
          "if HardwareWrapper.dll is present.");
      }
    }
    temp = GetCpuTemperatureTemp();
    max_temp = GetMaxCpuCoreTemperatureTemp();
    avg_temp = GetAverageCpuCoreTemperatureTemp();
    fan_rpm = GetCpuFanRpmTemp();
  }
  double get_temp() const { return temp; }
  double get_max_temp() const { return max_temp; }
  double get_avg_temp() const { return avg_temp; }
  double get_fan_rpm() const { return fan_rpm; }
  void update() {
    UpdateHardwareMonitorTemp();
    ReGet();
  }
  void ReGet() {
    temp = GetCpuTemperatureTemp();
    max_temp = GetMaxCpuCoreTemperatureTemp();
    avg_temp = GetAverageCpuCoreTemperatureTemp();
    fan_rpm = GetCpuFanRpmTemp();
  }
};

class PyTempGpu {
  double temp;
  double fan_rpm;

public:
  PyTempGpu(bool init = true) {
    if (init) {
      if (InitHardwareTempMonitor() != 0) {
        throw std::runtime_error(
          "LiveView: Failed to initialize hardware temperature monitor Check "
          "if HardwareWrapper.dll is present.");
      }
    }
    temp = GetGpuTemperatureTemp();
    fan_rpm = GetGpuFanRpmTemp();
  }
  double get_temp() const { return temp; }
  double get_fan_rpm() const { return fan_rpm; }
  void update() {
    UpdateHardwareMonitorTemp();
    ReGet();
  }
  void ReGet() {
    temp = GetGpuTemperatureTemp();
    fan_rpm = GetGpuFanRpmTemp();
  }
};

class PyTempOther {
  double mb_temp;
  double storage_temp;
public:
  PyTempOther(bool init = true) {
    if (init) {
      if (InitHardwareTempMonitor() != 0) {
        throw std::runtime_error(
          "LiveView: Failed to initialize hardware temperature monitor Check "
          "if HardwareWrapper.dll is present.");
      }
    }
    mb_temp = GetMotherboardTemperatureTemp();
    storage_temp = GetStorageTemperatureTemp();
  }
  double get_mb_temp() const { return mb_temp; }
  double get_Storage_temp() const { return storage_temp; }
  void update() {
    UpdateHardwareMonitorTemp();
    ReGet();
  }
  void ReGet() {
    mb_temp = GetMotherboardTemperatureTemp();
    storage_temp = GetStorageTemperatureTemp();
  }
};

class PyManageTemp {
public:
  void Init() {
    if (InitHardwareTempMonitor() != 0) {
      throw std::runtime_error(
        "LiveView: Failed to initialize hardware temperature monitor Check "
        "if HardwareWrapper.dll is present.");
    }
  }
  void Close() { ShutdownHardwareTempMonitor(); }
  void Update() { UpdateHardwareMonitorTemp(); }
  void SpecificUpdate(int id) {
    if (id >= 1 && id <= 11)
      SpecificUpdateHardwareTempMonitor(id);
  }
  void MultiSpecificUpdate(std::vector<int> ids) {
    for (const int& id : ids) {
      if (id >= 1 && id <= 11)
        SpecificUpdateHardwareTempMonitor(id);
    }
  }
  int GetHardwareIdByName(std::string name) {
    return GetHardwareIdByNameTemp(name.c_str());
  }
};

class PySensor {
  std::map<std::string, double> ParsePackedSensors(const char* data, size_t size) {
    std::map<std::string, double> sensors;
    if (data == nullptr || size == 0)
      return sensors;
    const char* p = data;
    const char* end = data + size;
    while (p < end) {
      const char* nameStart = p;
      while (p < end && *p != '\0')
        ++p;
      if (p >= end)
        break;
      std::string name(nameStart);
      ++p;
      if (p + sizeof(double) > end)
        break;
      double value;
      memcpy(&value, p, sizeof(double));
      p += sizeof(double);
      sensors.emplace(std::move(name), value);
    }
    return sensors;
  }
  std::map<std::string, double> sensors;

public:
  void GetData(bool init = false) {
    if (init) {
      if (InitHardwareTempMonitor() != 0) {
        throw std::runtime_error(
          "LiveView: Failed to initialize hardware temperature monitor. "
          "Check if HardwareWrapper.dll is present.");
      }
    }
    char* raw = nullptr;
    int size = 0;
    GetAllSensorsPackedTemp(&raw, &size);
    if (raw == nullptr || size == 0) {
      throw std::runtime_error("HardwareWrapper.dll returned no sensor data.");
    }
    std::unique_ptr<char, decltype(&FreePackedSensorsTemp)>
      data(raw, FreePackedSensorsTemp);
    sensors = ParsePackedSensors(data.get(), static_cast<size_t>(size));
    if (sensors.empty()) {
      throw std::runtime_error(
        "Failed to parse sensor data returned by HardwareWrapper.dll.");
    }
  }

  PySensor(bool init = true) { GetData(init); }

  double GetValueByName(const std::string& name) {
    auto it = sensors.find(name);
    if (it == sensors.end()) {
      throw std::runtime_error("Sensor Not Found");
    }
    return it->second;
  }
  auto getAllSensors() {
    std::vector<std::string> names;
    for (const auto& item : sensors) {
      names.push_back(item.first);
    }
    return names;
  }
  std::map<std::string, double> GetSensors() { return sensors; }
  std::vector<std::pair<std::string, double>> getAllFanRPMs() { return {}; }
  void Update() {
    UpdateHardwareMonitorTemp();
    GetData();
  }
  void ReGet() { GetData(); }
};

class PyRawInfo {
public:
  typedef UINT(WINAPI* pGetSystemFirmwareTable)(DWORD, DWORD, PVOID, DWORD);

  static std::vector<BYTE> RSMB() {
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    auto GetSystemFirmwareTable = (pGetSystemFirmwareTable)GetProcAddress(
      hKernel32, "GetSystemFirmwareTable");

    if (!GetSystemFirmwareTable) {
      throw std::runtime_error(
        "Failed to get address of GetSystemFirmwareTable.");
    }

    DWORD size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (size == 0) {
      throw std::runtime_error("Failed to get RSMB table size. Error: " +
        std::to_string(GetLastError()));
    }

    std::vector<BYTE> buffer(size);
    DWORD result = GetSystemFirmwareTable('RSMB', 0, buffer.data(), size);

    if (result == 0) {
      throw std::runtime_error("Failed to retrieve RSMB table. Error: " +
        std::to_string(GetLastError()));
    }
    return buffer;
  }
};

class PyHPCMISensor {
  enum HP_UNITS {
    HP_UNIT_OTHER = 1,
    HP_UNIT_CELSIUS = 2,
    HP_UNIT_VOLTS = 5,
    HP_UNIT_AMPS = 6,
    HP_UNIT_RPM = 19
  };
public:
  struct CMISensor {
    std::string Name;
    std::string Description;
    double Value;
    std::string Unit;
  };
private:
  IWbemLocator* m_pLoc = nullptr;
  IWbemServices* m_pSvc = nullptr;

  static double ApplyScaling(uint32_t rawValue, int32_t modifier) {
    return rawValue * std::pow(10, modifier);
  }
  static std::vector<CMISensor> GetHPCMISensors(IWbemLocator* pLoc, IWbemServices* pSvc) {
    std::vector<CMISensor> sensors;
    if (!pLoc || !pSvc) return sensors;

    IEnumWbemClassObject* pEnumerator = nullptr;
    BSTR bstrLanguage = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(L"SELECT * FROM HPBIOS_BIOSNumericSensor");

    if (!bstrLanguage || !bstrQuery) {
      if (bstrLanguage) SysFreeString(bstrLanguage);
      if (bstrQuery) SysFreeString(bstrQuery);
      return sensors;
    }

    HRESULT hr = pSvc->ExecQuery(
      bstrLanguage, bstrQuery,
      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
      NULL, &pEnumerator);

    SysFreeString(bstrLanguage);
    SysFreeString(bstrQuery);

    if (FAILED(hr) || !pEnumerator) return sensors;

    IWbemClassObject* pclsObj = nullptr;
    ULONG uReturn = 0;
    try {
      while (true) {
        if (FAILED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) || 0 == uReturn) break;
        CMISensor sensor;
        sensor.Name = win::get_string_property(pclsObj, L"Name");
        sensor.Description = win::get_string_property(pclsObj, L"Description");
        uint32_t rawVal = win::get_uint32_property(pclsObj, L"CurrentReading");
        uint32_t baseUnits = win::get_uint32_property(pclsObj, L"BaseUnits");
        int32_t modifier = static_cast<int32_t>(win::get_uint32_property(pclsObj, L"UnitModifier"));
        sensor.Value = ApplyScaling(rawVal, modifier);
        switch (baseUnits) {
          case HP_UNIT_CELSIUS: sensor.Unit = "C"; break;
          case HP_UNIT_RPM: sensor.Unit = "RPM"; break;
          case HP_UNIT_VOLTS: sensor.Unit = "V"; break;
          case HP_UNIT_AMPS: sensor.Unit = "A"; break;
          default: sensor.Unit = ""; break;
        }
        if (sensor.Value != 0) sensors.push_back(sensor);
        pclsObj->Release();
        pclsObj = nullptr;
      }
    } catch (...) {
      if (pclsObj) pclsObj->Release();
      if (pEnumerator) pEnumerator->Release();
      throw;
    }
    pEnumerator->Release();
    return sensors;
  }
public:
  std::vector<CMISensor> sensors;
  PyHPCMISensor() {
    bool init = false;
    try {
      win::initialize_wmi(&m_pLoc, &m_pSvc, L"ROOT\\HP\\InstrumentedBIOS", &init);
    } catch (...) {
      if (m_pLoc) m_pLoc->Release();
      if (m_pSvc) m_pSvc->Release();
      if (init) CoUninitialize();
      throw;
    }
    sensors = GetHPCMISensors(m_pLoc, m_pSvc);
  }
  ~PyHPCMISensor() {
    if (m_pLoc) m_pLoc->Release();
    if (m_pSvc) m_pSvc->Release();
    CoUninitialize();
  }
  static void UninitializeWMI() { CoUninitialize(); }
  bool Update() {
    sensors = GetHPCMISensors(m_pLoc, m_pSvc);
    return !(sensors.empty());
  }
};

#elif defined(__linux__)

/**
 * @class PyLiveGpu (Linux implementation using dynamic NVML + DRM sysfs)
 */
class PyLiveGpu {
private:
  std::vector<std::string> drm_busy_paths;
  unsigned int nvml_count = 0;

  std::vector<double> sample_all_engines() const {
    std::vector<double> vals;
    auto& nvml = DynamicNVML::instance();
    for (unsigned int i = 0; i < nvml_count; ++i) {
      std::string gname;
      double util = 0.0, temp = -1.0, fan = 0.0, pwr = -1.0;
      if (nvml.get_device_metrics(i, gname, util, temp, fan, pwr)) {
        vals.push_back(util);
      }
    }
    for (const auto& p : drm_busy_paths) {
      std::string s = read_first_line_trimmed(p);
      if (!s.empty()) {
        try {
          vals.push_back(std::stod(s));
        } catch (...) {}
      }
    }
    if (vals.empty()) {
      vals.push_back(0.0);
    }
    return vals;
  }

public:
  PyLiveGpu() {
    nvml_count = DynamicNVML::instance().device_count();
    if (fs::exists("/sys/class/drm")) {
      for (const auto& entry : fs::directory_iterator("/sys/class/drm")) {
        std::string bpath = entry.path().string() + "/device/gpu_busy_percent";
        if (fs::exists(bpath)) {
          drm_busy_paths.push_back(bpath);
        }
      }
    }
  }

  double get_usage(int interval_ms = 1000) const {
    if (interval_ms > 0) {
      HV_GIL_RELEASE;
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    auto vals = sample_all_engines();
    double total = 0.0;
    for (double v : vals) total += v;
    return total;
  }

  double get_average_usage(int interval_ms = 1000) const {
    if (interval_ms > 0) {
      HV_GIL_RELEASE;
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    auto vals = sample_all_engines();
    if (vals.empty()) return 0.0;
    double total = 0.0;
    for (double v : vals) total += v;
    return total / static_cast<double>(vals.size());
  }

  double get_max_usage(int interval_ms = 1000) const {
    if (interval_ms > 0) {
      HV_GIL_RELEASE;
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    auto vals = sample_all_engines();
    double m = 0.0;
    for (double v : vals) {
      if (v > m) m = v;
    }
    return m;
  }

  size_t get_counter_count() const {
    size_t c = static_cast<size_t>(nvml_count) + drm_busy_paths.size();
    return c > 0 ? c : 1;
  }
};

class PyTempCpu {
  double temp = -1.0;
  double max_temp = -1.0;
  double avg_temp = -1.0;
  double fan_rpm = 0.0;

  void refresh() {
    auto all = CollectAllLinuxSensorsFormatted();
    std::vector<double> cpu_temps;
    double found_fan = 0.0;

    for (const auto& [k, v] : all) {
      std::string kl = to_lower_copy(k);
      if (k.find(" - Temperature - ") != std::string::npos) {
        if (kl.find("cpu") != std::string::npos || kl.find("k10temp") != std::string::npos ||
            kl.find("coretemp") != std::string::npos || kl.find("tctl") != std::string::npos ||
            kl.find("tdie") != std::string::npos || kl.find("package") != std::string::npos) {
          if (v > 0.0) cpu_temps.push_back(v);
        }
      } else if (k.find(" - Fan - ") != std::string::npos) {
        if (kl.find("cpu") != std::string::npos || found_fan == 0.0) {
          found_fan = v;
        }
      }
    }

    if (cpu_temps.empty()) {
      double fallback = GetCpuPackageTemp();
      if (fallback > 0.0) cpu_temps.push_back(fallback);
    }

    if (!cpu_temps.empty()) {
      temp = cpu_temps.front();
      max_temp = *std::max_element(cpu_temps.begin(), cpu_temps.end());
      double sum = std::accumulate(cpu_temps.begin(), cpu_temps.end(), 0.0);
      avg_temp = sum / static_cast<double>(cpu_temps.size());
    } else {
      temp = -1.0;
      max_temp = -1.0;
      avg_temp = -1.0;
    }
    fan_rpm = found_fan;
  }

public:
  PyTempCpu(bool init = true) {
    (void)init;
    refresh();
  }
  double get_temp() const { return temp; }
  double get_max_temp() const { return max_temp; }
  double get_avg_temp() const { return avg_temp; }
  double get_fan_rpm() const { return fan_rpm; }
  void update() { refresh(); }
  void ReGet() { refresh(); }
};

class PyTempGpu {
  double temp = -1.0;
  double fan_rpm = 0.0;

  void refresh() {
    auto& nvml = DynamicNVML::instance();
    if (nvml.device_count() > 0) {
      std::string gname;
      double util = 0.0, t = -1.0, f = 0.0, pwr = -1.0;
      if (nvml.get_device_metrics(0, gname, util, t, f, pwr) && t > 0.0) {
        temp = t;
        fan_rpm = f;
        return;
      }
    }
    auto all = CollectAllLinuxSensorsFormatted();
    for (const auto& [k, v] : all) {
      std::string kl = to_lower_copy(k);
      if (k.find(" - Temperature - ") != std::string::npos &&
          (kl.find("gpu") != std::string::npos || kl.find("edge") != std::string::npos ||
           kl.find("nouveau") != std::string::npos)) {
        if (v > 0.0) temp = v;
      } else if (k.find(" - Fan - ") != std::string::npos && kl.find("gpu") != std::string::npos) {
        fan_rpm = v;
      }
    }
  }

public:
  PyTempGpu(bool init = true) {
    (void)init;
    refresh();
  }
  double get_temp() const { return temp; }
  double get_fan_rpm() const { return fan_rpm; }
  void update() { refresh(); }
  void ReGet() { refresh(); }
};

class PyTempOther {
  double mb_temp = -1.0;
  double storage_temp = -1.0;

  void refresh() {
    mb_temp = GetMotherboardTemp();
    storage_temp = GetDriveTemp();
  }

public:
  PyTempOther(bool init = true) {
    (void)init;
    refresh();
  }
  double get_mb_temp() const { return mb_temp; }
  double get_Storage_temp() const { return storage_temp; }
  void update() { refresh(); }
  void ReGet() { refresh(); }
};

class PyManageTemp {
public:
  void Init() {}
  void Close() {}
  void Update() {}
  void SpecificUpdate(int id) { (void)id; }
  void MultiSpecificUpdate(std::vector<int> ids) { (void)ids; }
  int GetHardwareIdByName(std::string name) {
    return ResolveLinuxHardwareIdByName(name);
  }
};

class PySensor {
  std::map<std::string, double> sensors;

public:
  void GetData(bool init = false) {
    (void)init;
    sensors = CollectAllLinuxSensorsFormatted();
  }

  PySensor(bool init = true) { GetData(init); }

  double GetValueByName(const std::string& name) {
    auto it = sensors.find(name);
    if (it != sensors.end()) return it->second;
    for (const auto& [k, v] : sensors) {
      if (k.find(name) != std::string::npos) return v;
    }
    throw std::runtime_error("Sensor Not Found");
  }

  auto getAllSensors() {
    std::vector<std::string> names;
    for (const auto& item : sensors) {
      names.push_back(item.first);
    }
    return names;
  }

  std::map<std::string, double> GetSensors() { return sensors; }
  std::vector<std::pair<std::string, double>> getAllFanRPMs() { return {}; }
  void Update() { GetData(); }
  void ReGet() { GetData(); }
};

class PyRawInfo {
public:
  static std::vector<BYTE> RSMB() {
    HV::SMBIOS::SMBIOSParser parser;
    if (!parser.LoadSMBIOSData() || !parser.raw_data || parser.data_size == 0) {
      throw std::runtime_error("Failed to load SMBIOS/RSMB table on Linux.");
    }
    return std::vector<BYTE>(parser.raw_data.get(), parser.raw_data.get() + parser.data_size);
  }
};

class PyHPCMISensor {
public:
  struct CMISensor {
    std::string Name;
    std::string Description;
    double Value;
    std::string Unit;
  };
  std::vector<CMISensor> sensors;
  PyHPCMISensor() {}
  static void UninitializeWMI() {}
  bool Update() { return false; }
};

class PyLinuxSensor {
  std::vector<std::string> sensors_names;
  double cputemp = 0;
  double mbtemp = 0;
  double chipstemp = 0;
  double memorytemp = 0;
  double storagetemp = 0;

public:
  PyLinuxSensor() {
    sensors_names = GetAllSensorNames();
    cputemp = GetCpuPackageTemp();
    chipstemp = GetChipsetTemp();
    mbtemp = GetMotherboardTemp();
    memorytemp = GetVRMTemp();
    storagetemp = GetDriveTemp();
  }
  double getCpuTemp() const { return cputemp; }
  double getChipsetTemp() const { return chipstemp; }
  double getMotherboardTemp() const { return mbtemp; }
  double getVRMTemp() const { return memorytemp; }
  double getDriveTemp() const { return storagetemp; }
  std::vector<std::string> getAllSensorNames() const { return sensors_names; }
  std::vector<std::pair<std::string, size_t>>
    findSensorName(std::string name) const {
    std::vector<std::pair<std::string, size_t>> res;
    for (size_t i = 0; i < sensors_names.size(); i++) {
      if (sensors_names[i] == name || sensors_names[i].find(name) != std::string::npos) {
        res.emplace_back(sensors_names[i], i);
      }
    }
    return res;
  }
  double GetSensorTemp(std::string name, bool Match) const {
    if (name.empty()) {
      return -1;
    }
    return GetSensorByName(name, Match);
  }
  std::vector<std::pair<std::string, double>> GetSensorsWithTemp() const {
    std::vector<std::pair<std::string, double>> sensors;
    for (const auto& sensor : sensors_names) {
      double val = GetSensorByName(sensor, true);
      sensors.emplace_back(sensor, val);
    }
    return sensors;
  }
  void update(bool names = false) {
    if (names)
      sensors_names = GetAllSensorNames();
    cputemp = GetCpuPackageTemp();
    chipstemp = GetChipsetTemp();
    mbtemp = GetMotherboardTemp();
    memorytemp = GetVRMTemp();
    storagetemp = GetDriveTemp();
  }
};
#endif // __linux__

#ifndef LIVEVIEW_CPP
// ===================================================================================
// PYBIND11 MODULE DEFINITION
// ===================================================================================
PYBIND11_MODULE(LiveView, m) {
  m.doc() = "Cross-platform system monitoring module (CPU, RAM, Disk, Network, "
    "GPU, and Hardware Sensors/Temperatures for Windows and Linux)";

  // --- PyLiveCPU Class Binding ---
  auto cpu_class =
    py::class_<PyLiveCPU>(m, "PyLiveCPU")
    .def(py::init<>())
    .def("get_usage", &PyLiveCPU::get_usage,
      "Returns total CPU usage percentage.", py::arg("interval_ms") = 1000)
    .def("cpuid", &PyLiveCPU::cpuid,
      "Returns CPU information using CPUID instruction.")
    .def("cpu_id", &PyLiveCPU::cpuid,
      "Returns CPU information using CPUID instruction.")
    .def("CpuSnapShot", &PyLiveCPU::CpuSnapShot,
      "CPU snapshot for a given core.", py::arg("core"),
      py::arg("coreNumbers") = false, py::arg("Kernel") = true,
      py::arg("User") = true, py::arg("Idle") = true,
      py::arg("PureKernalTime") = false)
    .def("cpu_snapshot", &PyLiveCPU::CpuSnapShot,
      "CPU snapshot for a given core.", py::arg("core"),
      py::arg("coreNumbers") = false, py::arg("Kernel") = true,
      py::arg("User") = true, py::arg("Idle") = true,
      py::arg("PureKernalTime") = false);

  // --- PyLiveRam Class Binding ---
  py::class_<PyLiveRam>(m, "PyLiveRam")
    .def(py::init<>())
    .def("get_usage", &PyLiveRam::get_usage,
      "Returns total RAM usage percentage.", py::arg("Raw") = false);

  // --- PyLiveDisk Class Binding ---
  py::class_<PyLiveDisk>(m, "PyLiveDisk")
    .def(py::init<int>(),
      "mode=0 for % disk active time, mode=1 for R/W speed (MB/s).",
      py::arg("mode") = 1)
    .def("get_usage", &PyLiveDisk::get_usage, "Returns disk usage info.",
      py::arg("interval") = 1000)
    .def("HighDiskUsage", &PyLiveDisk::HighDiskUsage,
      "Checks if R/W speed exceeds a threshold (MB/s).",
      py::arg("threshold_mbps") = 80.0)
    .def("high_disk_usage", &PyLiveDisk::HighDiskUsage,
      "Checks if R/W speed exceeds a threshold (MB/s).",
      py::arg("threshold_mbps") = 80.0);

  // --- PyLiveNetwork Class Binding ---
  py::class_<PyLiveNetwork>(m, "PyLiveNetwork")
    .def(py::init<>())
    .def("get_usage", &PyLiveNetwork::get_usage,
      "mode=0 for total MB/s, mode=1 for per-interface MB/s.",
      py::arg("interval") = 1000, py::arg("mode") = 0)
    .def("getHighCard", &PyLiveNetwork::getHighCard,
      "Returns the network interface with the highest usage.")
    .def("get_high_card", &PyLiveNetwork::getHighCard,
      "Returns the network interface with the highest usage.");

  // --- PyLiveGpu Binding (Cross-Platform) ---
  py::class_<PyLiveGpu>(m, "PyLiveGpu")
    .def(py::init<>())
    .def("get_usage", &PyLiveGpu::get_usage, py::arg("interval_ms") = 1000,
      "Get total GPU usage percentage.")
    .def("get_average_usage", &PyLiveGpu::get_average_usage,
      py::arg("interval_ms") = 1000, "Get average GPU usage percentage.")
    .def("get_max_usage", &PyLiveGpu::get_max_usage,
      py::arg("interval_ms") = 1000, "Get max GPU usage percentage.")
    .def("get_counter_count", &PyLiveGpu::get_counter_count,
      "Get number of GPU counters monitored.");

  // --- PyTempCpu Binding (Cross-Platform) ---
  py::class_<PyTempCpu>(m, "PyTempCpu")
    .def(py::init<>())
    .def(py::init<bool>(), py::arg("init") = true)
    .def("get_temp", &PyTempCpu::get_temp, "Get current CPU temperature.")
    .def("get_max_temp", &PyTempCpu::get_max_temp,
      "Get max CPU core temperature.")
    .def("get_avg_temp", &PyTempCpu::get_avg_temp,
      "Get average CPU core temperature.")
    .def("get_fan_rpm", &PyTempCpu::get_fan_rpm, "Get CPU fan RPM.")
    .def("update", &PyTempCpu::update,
      "Update CPU temperature and fan RPM data.")
    .def("reget", &PyTempCpu::ReGet,
      "ReGet CPU temperature and fan RPM data.")
    .def("re_get", &PyTempCpu::ReGet,
      "ReGet CPU temperature and fan RPM data.");

  // --- PyTempGpu Binding (Cross-Platform) ---
  py::class_<PyTempGpu>(m, "PyTempGpu")
    .def(py::init<>())
    .def(py::init<bool>(), py::arg("init") = true)
    .def("get_temp", &PyTempGpu::get_temp, "Get current GPU temperature.")
    .def("get_fan_rpm", &PyTempGpu::get_fan_rpm, "Get GPU fan RPM.")
    .def("update", &PyTempGpu::update,
      "Update GPU temperature and fan RPM data.")
    .def("reget", &PyTempGpu::ReGet,
      "ReGet GPU temperature and fan RPM data.")
    .def("re_get", &PyTempGpu::ReGet,
      "ReGet GPU temperature and fan RPM data.");

  // --- PyTempOther Binding (Cross-Platform) ---
  py::class_<PyTempOther>(m, "PyTempOther")
    .def(py::init<>())
    .def(py::init<bool>(), py::arg("init") = true)
    .def("get_mb_temp", &PyTempOther::get_mb_temp,
      "Get motherboard temperature.")
    .def("get_Storage_temp", &PyTempOther::get_Storage_temp,
      "Get storage device temperature.")
    .def("get_storage_temp", &PyTempOther::get_Storage_temp,
      "Get storage device temperature.")
    .def("update", &PyTempOther::update, "Update temperature data.")
    .def("reget", &PyTempOther::ReGet, "ReGet temperature data.")
    .def("re_get", &PyTempOther::ReGet, "ReGet temperature data.");

  // --- PySensor Binding (Cross-Platform) ---
  py::class_<PySensor>(m, "PySensor")
    .def(py::init<>())
    .def(py::init<bool>(), py::arg("init") = true)
    .def("GetData", &PySensor::GetData, py::arg("init") = false,
      "Fetch sensors and fans data from hardware.")
    .def("get_data", &PySensor::GetData, py::arg("init") = false,
      "Fetch sensors and fans data from hardware.")
    .def("GetValueByName", &PySensor::GetValueByName, py::arg("name"),
      "Get sensor value by name.")
    .def("get_value_by_name", &PySensor::GetValueByName, py::arg("name"),
      "Get sensor value by name.")
    .def("getAllSensors", &PySensor::getAllSensors,
      "Get list of all sensor names.")
    .def("get_all_sensors", &PySensor::getAllSensors,
      "Get list of all sensor names.")
    .def("getAllFanRPMs", &PySensor::getAllFanRPMs,
      "return Empty list")
    .def("get_all_fan_rpms", &PySensor::getAllFanRPMs,
      "return Empty list")
    .def("get_sensors", &PySensor::GetSensors,
      "Return a dict[str, float] mapping each sensor name to its current value.")
    .def("GetSensors", &PySensor::GetSensors,
      "Return a dict[str, float] mapping each sensor name to its current value.")
    .def("update", &PySensor::Update, "Update sensor and fan data.")
    .def("reget", &PySensor::ReGet, "ReGet sensor and fan data.")
    .def("re_get", &PySensor::ReGet, "ReGet sensor and fan data.");

  // --- PyManageTemp Binding (Cross-Platform) ---
  py::class_<PyManageTemp>(m, "PyManageTemp")
    .def(py::init<>())
    .def("Init", &PyManageTemp::Init)
    .def("init", &PyManageTemp::Init)
    .def("Close", &PyManageTemp::Close)
    .def("close", &PyManageTemp::Close)
    .def("Update", &PyManageTemp::Update)
    .def("update", &PyManageTemp::Update)
    .def("SpecificUpdate", &PyManageTemp::SpecificUpdate)
    .def("specific_update", &PyManageTemp::SpecificUpdate)
    .def("MultiSpecificUpdate", &PyManageTemp::MultiSpecificUpdate)
    .def("GetHardwareIdByName", &PyManageTemp::GetHardwareIdByName)
    .def("get_hardware_id_by_name", &PyManageTemp::GetHardwareIdByName)
    .def("multi_specific_update", &PyManageTemp::MultiSpecificUpdate);

  // --- PyRawInfo Binding (Cross-Platform) ---
  py::class_<PyRawInfo>(m, "PyRawInfo")
    .def_static("RSMB", &PyRawInfo::RSMB,
      "Get raw SMBIOS firmware table bytes.")
    .def_static("rsmb", &PyRawInfo::RSMB,
      "Get raw SMBIOS firmware table bytes.");

  // --- HPCMISensor::CMISensor Binding ---
  py::class_<PyHPCMISensor::CMISensor>(m, "HPCMISensorData")
    .def(py::init<>())
    .def_readonly("name", &PyHPCMISensor::CMISensor::Name)
    .def_readonly("description", &PyHPCMISensor::CMISensor::Description)
    .def_readonly("value", &PyHPCMISensor::CMISensor::Value)
    .def_readonly("unit", &PyHPCMISensor::CMISensor::Unit);

  // --- PyHPCMISensor Binding ---
  py::class_<PyHPCMISensor>(m, "PyHPCMISensor")
    .def(py::init<>())
    .def("update", &PyHPCMISensor::Update, "Update HP CMI sensor values.")
    .def("uninitialize_wmi", &PyHPCMISensor::UninitializeWMI, "Call CoUninitialize()")
    .def_readonly("sensors", &PyHPCMISensor::sensors, "List of HP CMI sensors.");

#ifdef __linux__
  // --- PyLinuxSensor Binding ---
  py::class_<PyLinuxSensor>(m, "PyLinuxSensor")
    .def(py::init<>())
    .def("getCpuTemp", &PyLinuxSensor::getCpuTemp, "Get CPU temperature")
    .def("get_cpu_temp", &PyLinuxSensor::getCpuTemp, "Get CPU temperature")
    .def("getChipsetTemp", &PyLinuxSensor::getChipsetTemp,
      "Get chipset temperature")
    .def("get_chipset_temp", &PyLinuxSensor::getChipsetTemp,
      "Get chipset temperature")
    .def("getMotherboardTemp", &PyLinuxSensor::getMotherboardTemp,
      "Get motherboard temperature")
    .def("get_motherboard_temp", &PyLinuxSensor::getMotherboardTemp,
      "Get motherboard temperature")
    .def("getVRMTemp", &PyLinuxSensor::getVRMTemp,
      "Get VRM (memory) temperature")
    .def("get_vrm_temp", &PyLinuxSensor::getVRMTemp,
      "Get VRM (memory) temperature")
    .def("getDriveTemp", &PyLinuxSensor::getDriveTemp,
      "Get storage device temperature")
    .def("get_drive_temp", &PyLinuxSensor::getDriveTemp,
      "Get storage device temperature")
    .def("getAllSensorNames", &PyLinuxSensor::getAllSensorNames,
      "Get all sensor names")
    .def("get_all_sensor_names", &PyLinuxSensor::getAllSensorNames,
      "Get all sensor names")
    .def("findSensorName", &PyLinuxSensor::findSensorName, py::arg("name"),
      "Find sensor name occurrences")
    .def("find_sensor_name", &PyLinuxSensor::findSensorName, py::arg("name"),
      "Find sensor name occurrences")
    .def("GetSensorTemp", &PyLinuxSensor::GetSensorTemp, py::arg("name"),
      py::arg("Match") = false,
      "Get sensor temperature by name with optional match")
    .def("get_sensor_temp", &PyLinuxSensor::GetSensorTemp, py::arg("name"),
      py::arg("Match") = false,
      "Get sensor temperature by name with optional match")
    .def("GetSensorsWithTemp", &PyLinuxSensor::GetSensorsWithTemp,
      "Get all sensors with their temperature values")
    .def("get_sensors_with_temp", &PyLinuxSensor::GetSensorsWithTemp,
      "Get all sensors with their temperature values")
    .def("update", &PyLinuxSensor::update, py::arg("names") = false,
      "Update sensor data, optionally update names");
#endif
}
#endif
