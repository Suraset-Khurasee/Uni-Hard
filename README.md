# Uni-Hard

Uni-Hard is a cross-platform hardware information and monitoring library.
The Python package and import namespace are still named `HardView`.

It combines native C and C++ extensions with a Python API for:

- CPU, memory, disk, network, GPU, BIOS, and motherboard information
- SMBIOS data
- SMART/NVMe drive information
- CPU, memory, disk, and network usage monitoring
- Linux hardware sensors through `lm_sensors`
- Windows hardware sensors through LibreHardwareMonitor
- Native C++ headers and C APIs for application developers

## Status

| Platform | Status |
| --- | --- |
| Ubuntu 20.04 / 22.04 / 24.04 / 26.04 | Supported (primary target) |
| Linux x86_64 | Supported |
| Linux ARM64 | Supported when compiled locally |
| Arch Linux | Supported |
| Windows x86/x64 | Supported |
| macOS | Experimental; not covered by the main build |

Some low-level features require hardware, kernel interfaces, or elevated
permissions. Missing data is reported when the operating system does not
expose a capability.

## Install on Ubuntu / Debian (recommended)

Ubuntu does not ship a compiler, Python headers, or `venv` support by default,
and has no `python` command (only `python3`). The included script installs
those packages with `apt` and builds HardView into `./.venv`:

```bash
sudo apt-get install -y git
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
./install.sh
```

Or do the same steps by hand:

```bash
sudo apt-get update
sudo apt-get install -y git python3 python3-venv python3-pip python3-dev \
  build-essential libsensors-dev lm-sensors
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install .
```

Tested on Ubuntu 20.04, 22.04, 24.04, and 26.04 (x86_64 and ARM64).
Ubuntu 23.04+ marks the system Python as externally managed, so always
install into a virtual environment rather than with system-wide `pip`.

## Install on Arch Linux

`./install.sh` also works on Arch (it uses `pacman`). To do it by hand:

```bash
sudo pacman -S --needed base-devel git python python-pip lm_sensors
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install .
```

The repository also includes an Arch package recipe:

```bash
sudo pacman -S --needed base-devel git python python-pybind11 \
  python-build python-installer python-setuptools python-wheel python-rich
makepkg -si
```

## Linux sensors

Temperature and fan readings come from `lm_sensors` (with a sysfs fallback).
Detect your sensor chips once after installing:

```bash
sudo sensors-detect
sensors
```

## Root access on Linux

CPU, RAM, sensor, and usage APIs work as a normal user. Raw SMBIOS tables
(`/sys/firmware/dmi/tables`) and SMART/NVMe queries are root-only on Linux,
so run those through the venv's Python with `sudo`:

```bash
sudo .venv/bin/python tests/quick_start.py
```

## Install on Windows or other Linux distributions

`./install.sh` supports `apt`, `pacman`, `dnf`, and `zypper`. Elsewhere,
install a C/C++ compiler and Python headers, then:

```bash
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python3 -m venv .venv

# Linux/macOS
.venv/bin/python -m pip install .

# Windows PowerShell
.venv\Scripts\python -m pip install .
```

Note: `pip install HardView` from PyPI installs the upstream HardView
project, not this fork. Install from this repository instead.

## Quick start

```python
from HardView.LiveView import PyLiveCPU, PyLiveRam

cpu = PyLiveCPU()
ram = PyLiveRam()

print(f"CPU: {cpu.get_usage(1000):.1f}%")
print(f"RAM: {ram.get_usage():.1f}%")
```

Run the included interactive example:

```bash
.venv/bin/python tests/quick_start.py
```

The example uses `rich` for formatted output when available and falls back to
plain text when it is not installed.

## Verify an installation

After cloning, check that everything builds and runs:

```bash
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
./install.sh
.venv/bin/python tests/quick_start.py
sudo .venv/bin/python tests/quick_start.py   # SMBIOS + SMART need root
```

`install.sh` finishes by importing every module and printing CPU and RAM
usage. `quick_start.py` prints SMBIOS tables, live sensors, and SMART data.

The install path is checked in clean Docker containers as a normal sudo user
on Ubuntu 20.04, 22.04, 24.04 and 26.04 (x86_64), plus Ubuntu 24.04 ARM64,
and in CI by `.github/workflows/TestLive.yml`. Containers cannot see every
device (for example NVIDIA GPUs through NVML), so test on real hardware too.

### Troubleshooting on Ubuntu

| Symptom | Fix |
| --- | --- |
| `python: command not found` | Use `python3`; Ubuntu has no `python` command |
| `ensurepip is not available` | `sudo apt-get install python3-venv` |
| `needs a compiler toolchain` | `sudo apt-get install build-essential python3-dev` |
| `externally-managed-environment` | Install into a venv, not with system `pip` |
| No temperatures or fans | `sudo apt-get install lm-sensors && sudo sensors-detect` |
| Empty SMBIOS or SMART data | Run with `sudo .venv/bin/python ...` |

## Python modules

```python
import HardView
from HardView import LiveView, SMART, smbios, process
```

The main native modules are:

| Module | Purpose |
| --- | --- |
| `HardView.HardView` | Legacy hardware and system information API |
| `HardView.LiveView` | Real-time usage and sensor monitoring |
| `HardView.SMART` | SMART and storage information |
| `HardView.smbios` | SMBIOS parsing |
| `HardView.process` | Process inspection and control |

## Development

Install the package in editable mode with development dependencies:

```bash
python3 -m venv .venv

# Linux/macOS
.venv/bin/python -m pip install -e ".[dev]"

# Windows PowerShell
.venv\Scripts\python -m pip install -e ".[dev]"
```

Build and test the native extensions through the package install:

```bash
.venv/bin/python -m pip install .
.venv/bin/python -m py_compile tests/quick_start.py
```

The source tree contains:

- `HardView/` — Python bindings and native extension sources
- `C++/` — reusable C++ headers
- `Drivers/` — optional Windows driver sources
- `Tools/` — optional command-line and GUI tools
- `tests/` — examples and test scripts
- `docs/` — API and installation documentation
- `PKGBUILD` — Arch Linux package recipe

## Documentation

- [Installation guide](docs/INSTALL.md)
- [LiveView API](docs/LiveViewAPI.md)
- [SMART API](docs/SMART.md)
- [SMBIOS API](docs/SMBIOS.md)
- [Legacy API and examples](docs/What.md)
- [Hardware temperature API](docs/HardwareTempAPI.md)

## License

The core Uni-Hard/HardView library is licensed under the MIT License.
Some tools and bundled components have separate licenses; see
[LICENSE](LICENSE), [LICENSE.GPL3](LICENSE.GPL3), and the `licenses/` directory.
