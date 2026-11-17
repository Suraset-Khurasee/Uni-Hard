#pragma once

// Cross-platform C++ headers (Linux & Windows)
#include "Headers/Process/Process.hpp"
#include "Headers/SMART/SMART.hpp"
#include "Headers/SMART/SMARTHelprs.hpp"
#include "Headers/SMBIOS/SMBIOS.hpp"

#if defined(__linux__)
// Linux-specific headers
#include "Headers/Linux/info.hpp"

#elif defined(_WIN32)
// Windows-specific headers
// For monitoring, use LiveView.cpp with the #LIVEVIEW_CPP macro.
// Do not use monitoring/Live.hpp because it is outdated.

// PCI
#include "Headers/PCI/PCI.hpp"

// SPD
#include "Headers/SPD/SPD.hpp"
#include "Headers/SPD/Timing.hpp"

// WMI
#include "Headers/WMI/WMI_Info.hpp"
#else
#warning "HardViewC++ is not supported on this platform"
#endif
