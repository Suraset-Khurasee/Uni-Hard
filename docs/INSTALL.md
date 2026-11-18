# HardView Installation Guide

This document explains how to install the HardView library on supported platforms.

---

## Supported Platforms

- **Windows:**
  - 32-bit and 64-bit (x86, x86_64)
- **Linux:**
  - x86_64 and ARM64, built from source
  - Ubuntu 20.04, 22.04, 24.04, 26.04 (primary target)
  - Arch Linux, Fedora/RHEL, openSUSE

> **Note:**
> - `pip install hardview` from PyPI installs the upstream HardView project,
>   not this fork. Install from this repository as shown below.
> - On Linux, pip builds the native extensions locally, so a C/C++ compiler
>   and the Python development headers are required.

---

## Installation Methods

### 1. Install script (Recommended on Linux)

`install.sh` installs the system build dependencies with your package manager
(`apt`, `pacman`, `dnf`, or `zypper`), creates `./.venv`, builds HardView, and
runs a smoke test:

```bash
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
./install.sh
```

Options: `--no-system` (skip the package manager step), `--venv DIR`,
`--editable`.

### 2. Manual install on Ubuntu / Debian

Ubuntu does not ship a compiler, Python headers, or `venv` support by default,
and the interpreter is `python3` (there is no `python` command):

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

Ubuntu 23.04+ marks the system Python as externally managed, so install into
a virtual environment rather than with system-wide `pip`.

### 3. Manual install on Arch Linux

```bash
sudo pacman -S --needed base-devel git python python-pip lm_sensors
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install .
```

For an Arch package installation, use the repository `PKGBUILD`:

```bash
sudo pacman -S --needed base-devel git python python-pybind11 python-build \
  python-installer python-setuptools python-wheel python-rich
makepkg -si
```

### 4. Windows

```powershell
git clone https://github.com/Suraset-Khurasee/Uni-Hard.git
cd Uni-Hard
python -m venv .venv
.venv\Scripts\python -m pip install .
```

---

## Root access on Linux

CPU, RAM, sensor, and usage APIs work as a normal user. Raw SMBIOS tables and
SMART/NVMe queries are root-only on Linux:

```bash
sudo .venv/bin/python tests/quick_start.py
```
