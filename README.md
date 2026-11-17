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
| Linux x86_64 | Supported |
| Linux ARM64 | Supported when compiled locally |
| Arch Linux | Supported |
| Windows x86/x64 | Supported |
| macOS | Experimental; not covered by the main build |

Some low-level features require hardware, kernel interfaces, or elevated
permissions. Missing data is reported when the operating system does not
expose a capability.

## Install on Arch Linux

Arch's system Python is externally managed. Do not install this project with
system-wide `pip`. Use a virtual environment:

```bash
sudo pacman -S --needed base-devel python python-pip
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install .
```

For Linux temperature and fan sensors, install and configure `lm_sensors`:

```bash
sudo pacman -S --needed lm_sensors
sudo sensors-detect
sensors
```

The repository also includes an Arch package recipe:

```bash
sudo pacman -S --needed base-devel git python python-pybind11 \
  python-build python-installer python-setuptools python-wheel python-rich
makepkg -si
```

## Install on Windows or other Linux distributions

```bash
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python -m venv .venv

# Linux/macOS
.venv/bin/python -m pip install .

# Windows PowerShell
.venv\Scripts\python -m pip install .
```

If a compatible wheel is published on PyPI, the package can also be installed
with:

```bash
python -m pip install HardView
```

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
python -m venv .venv

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
