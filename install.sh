#!/usr/bin/env bash
# Install HardView (Uni-Hard) from a source checkout on Linux.
#
#   ./install.sh                 # install system deps + build into ./.venv
#   ./install.sh --no-system     # skip the package manager step
#   ./install.sh --venv DIR      # use a different virtual environment path
#   ./install.sh --editable      # pip install -e (development)
#
# Supports Ubuntu/Debian (apt), Arch (pacman), Fedora/RHEL (dnf) and openSUSE (zypper).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$REPO_DIR/.venv"
INSTALL_SYSTEM_DEPS=1
PIP_ARGS=("$REPO_DIR")

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-system) INSTALL_SYSTEM_DEPS=0 ;;
    --venv) VENV_DIR="$(realpath -m "$2")"; shift ;;
    --editable) PIP_ARGS=(-e "$REPO_DIR") ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "install.sh is for Linux. On Windows run: python -m pip install ." >&2
  exit 1
fi

SUDO=""
if [[ $EUID -ne 0 ]]; then
  SUDO="sudo"
fi

install_system_deps() {
  if command -v apt-get >/dev/null 2>&1; then
    echo "==> Installing build dependencies with apt (Ubuntu/Debian)"
    $SUDO apt-get update
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y \
      python3 python3-venv python3-pip python3-dev build-essential \
      libsensors-dev lm-sensors
  elif command -v pacman >/dev/null 2>&1; then
    echo "==> Installing build dependencies with pacman (Arch)"
    $SUDO pacman -S --needed --noconfirm base-devel python python-pip lm_sensors
  elif command -v dnf >/dev/null 2>&1; then
    echo "==> Installing build dependencies with dnf (Fedora/RHEL)"
    $SUDO dnf install -y python3 python3-pip python3-devel gcc gcc-c++ make \
      lm_sensors lm_sensors-devel
  elif command -v zypper >/dev/null 2>&1; then
    echo "==> Installing build dependencies with zypper (openSUSE)"
    $SUDO zypper install -y python3 python3-pip python3-devel gcc gcc-c++ make \
      sensors libsensors4-devel
  else
    echo "Unknown package manager. Install a C/C++ compiler, Python 3 headers," >&2
    echo "python3-venv and (optionally) lm_sensors headers, then rerun with --no-system." >&2
    exit 1
  fi
}

if [[ $INSTALL_SYSTEM_DEPS -eq 1 ]]; then
  install_system_deps
fi

PYTHON="$(command -v python3 || command -v python || true)"
if [[ -z "$PYTHON" ]]; then
  echo "Python 3 was not found." >&2
  exit 1
fi

# A venv left behind by a failed run (no ensurepip) has no working pip.
if [[ -d "$VENV_DIR" && ! -x "$VENV_DIR/bin/python" ]]; then
  echo "==> Removing incomplete virtual environment at $VENV_DIR"
  rm -rf "$VENV_DIR"
fi

if [[ ! -x "$VENV_DIR/bin/python" ]]; then
  echo "==> Creating virtual environment at $VENV_DIR"
  if ! "$PYTHON" -m venv "$VENV_DIR"; then
    rm -rf "$VENV_DIR"
    echo "Could not create a virtual environment. On Ubuntu/Debian: sudo apt-get install python3-venv" >&2
    exit 1
  fi
fi

echo "==> Installing HardView into $VENV_DIR"
"$VENV_DIR/bin/python" -m pip install --upgrade pip
"$VENV_DIR/bin/python" -m pip install "${PIP_ARGS[@]}"

echo "==> Verifying the installation"
(cd / && "$VENV_DIR/bin/python" - <<'PY'
import HardView
from HardView import LiveView, SMART, smbios, process
from HardView.LiveView import PyLiveCPU, PyLiveRam
print(f"CPU usage: {PyLiveCPU().get_usage(500):.1f}%")
print(f"RAM usage: {PyLiveRam().get_usage():.1f}%")
print("HardView modules imported successfully.")
PY
)

cat <<EOF

Done. Use it with:
  $VENV_DIR/bin/python $REPO_DIR/tests/quick_start.py

SMBIOS/SMART raw access needs root on Linux, e.g.:
  sudo $VENV_DIR/bin/python $REPO_DIR/tests/quick_start.py
EOF
