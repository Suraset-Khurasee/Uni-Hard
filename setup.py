import sys
import os
import glob
import shutil
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext as _build_ext
import pybind11

# =================================================================
# ==      Helper: Check for libsensors on Linux                  ==
# =================================================================

def _has_libsensors():
    if not (sys.platform.startswith('linux') or sys.platform.startswith('darwin')):
        return False
    candidate_paths = [
        '/usr/include/sensors/sensors.h',
        '/usr/local/include/sensors/sensors.h',
        '/usr/include/x86_64-linux-gnu/sensors/sensors.h',
        '/usr/include/aarch64-linux-gnu/sensors/sensors.h',
    ]
    return any(os.path.exists(p) for p in candidate_paths)

HAS_LIBSENSORS = _has_libsensors()

# =================================================================
# ==      HardView Main Extension (C)                            ==
# =================================================================

LEGACY_DIR = 'HardView/Legacy'

hardview_source_files = [
    f'{LEGACY_DIR}/HardView.c',
    f'{LEGACY_DIR}/helpers.c',
    f'{LEGACY_DIR}/bios_info.c',
    f'{LEGACY_DIR}/system_info.c',
    f'{LEGACY_DIR}/baseboard_info.c',
    f'{LEGACY_DIR}/chassis_info.c',
    f'{LEGACY_DIR}/cpu_info.c',
    f'{LEGACY_DIR}/ram_info.c',
    f'{LEGACY_DIR}/disk_info.c',
    f'{LEGACY_DIR}/gpu_info.c',
    f'{LEGACY_DIR}/network_info.c',
    f'{LEGACY_DIR}/performance_monitor.c',
    f'{LEGACY_DIR}/advanced_storage_info.c',
    f'{LEGACY_DIR}/Smart_disk.c'
]

hardview_libraries = []
hardview_extra_compile_args = []

if sys.platform.startswith('win'):
    hardview_source_files.append(f'{LEGACY_DIR}/win_helpers.c')
    hardview_libraries.extend(['wbemuuid', 'ole32', 'oleaut32'])
elif sys.platform.startswith('linux') or sys.platform.startswith('darwin'):
    hardview_source_files.append(f'{LEGACY_DIR}/linux_helpers.c')

hardview_module = Extension(
    'HardView.HardView',
    sources=hardview_source_files,
    depends=glob.glob(f'{LEGACY_DIR}/*.h'),
    define_macros=[('BUILD_PYTHON_MODULE', '1')],
    extra_compile_args=hardview_extra_compile_args,
    libraries=hardview_libraries,
    library_dirs=[],
    include_dirs=[LEGACY_DIR]
)

# =================================================================
# ==      Common C++ Extension Flags                             ==
# =================================================================

cpp_compile_args = []
cpp_link_args = []
cpp_include_dirs = ['C++', 'C++/Headers', pybind11.get_include()]

if sys.platform.startswith('win'):
    cpp_compile_args.extend(['/std:c++17', '/MT'])
    cpp_link_args.extend(['-static-libgcc', '-static-libstdc++'])
elif sys.platform.startswith('linux') or sys.platform.startswith('darwin'):
    cpp_compile_args.append('-std=c++17')

# =================================================================
# ==      LiveView Extension (C++)                               ==
# =================================================================

liveview_libraries = []
liveview_macros = []

if sys.platform.startswith('win'):
    liveview_libraries.extend(['pdh', 'ntdll'])
elif sys.platform.startswith('linux') or sys.platform.startswith('darwin'):
    liveview_libraries.append('dl')
    if HAS_LIBSENSORS:
        liveview_libraries.append('sensors')
        liveview_macros.append(('HAVE_LIBSENSORS', '1'))

liveview_module = Extension(
    'HardView.LiveView',
    sources=['HardView/LiveView/LiveView.cpp'],
    depends=glob.glob('C++/Headers/**/*.hpp', recursive=True) + glob.glob('C++/Headers/**/*.h', recursive=True),
    include_dirs=['HardView/LiveView'] + cpp_include_dirs,
    libraries=liveview_libraries,
    define_macros=liveview_macros,
    extra_compile_args=list(cpp_compile_args),
    extra_link_args=list(cpp_link_args),
    language='c++'
)

# =================================================================
# ==      SMBIOS Extension (C++) - Cross-Platform                ==
# =================================================================

smbios_module = Extension(
    'HardView.smbios',
    sources=['HardView/SMBIOS/PySMBIOS.cpp'],
    depends=['C++/Headers/SMBIOS/SMBIOS.hpp'],
    include_dirs=['HardView/SMBIOS'] + cpp_include_dirs,
    libraries=[],
    extra_compile_args=list(cpp_compile_args),
    extra_link_args=list(cpp_link_args),
    language='c++'
)

# =================================================================
# ==      SMART Extension (C++) - Cross-Platform                 ==
# =================================================================

smart_module = Extension(
    'HardView.SMART',
    sources=['HardView/SMART/PySMART.cpp'],
    depends=['C++/Headers/SMART/SMART.hpp', 'C++/Headers/SMART/SMARTHelprs.hpp'],
    include_dirs=['HardView/SMART'] + cpp_include_dirs,
    libraries=[],
    extra_compile_args=list(cpp_compile_args),
    extra_link_args=list(cpp_link_args),
    language='c++'
)

# =================================================================
# ==      Process Extension (C++) - Cross-Platform               ==
# =================================================================

process_libraries = ['Psapi', 'Advapi32'] if sys.platform.startswith('win') else []

process_module = Extension(
    'HardView.process',
    sources=['HardView/Process/PyProcess.cpp'],
    depends=['C++/Headers/Process/Process.hpp'],
    include_dirs=['HardView/Process'] + cpp_include_dirs,
    libraries=process_libraries,
    extra_compile_args=list(cpp_compile_args),
    extra_link_args=list(cpp_link_args),
    language='c++'
)

# =================================================================
# ==      List of Extensions to be built                         ==
# =================================================================

extensions = [
    hardview_module,
    liveview_module,
    smbios_module,
    smart_module,
    process_module,
]

# =================================================================
# ==      Package Configuration                                  ==
# =================================================================

packages = ['HardView']
package_dir = {'HardView': 'HardView/py'}
package_data = {}

# =================================================================
# ==      Custom Build for Extensions                            ==
# =================================================================

class build_ext(_build_ext):
    """
    Custom build_ext command to:
    1. Adapt compiler/linker flags to the active compiler (MSVC vs GCC/Clang).
    2. Remove platform-specific tags from the compiled extension filename.
    3. Copy necessary DLLs for the LiveView extension on Windows (32-bit and 64-bit).
    """
    def build_extensions(self):
        compiler_type = getattr(self.compiler, 'compiler_type', '')
        for ext in self.extensions:
            if ext.language == 'c++':
                if compiler_type == 'msvc':
                    ext.extra_compile_args = [
                        a for a in ext.extra_compile_args
                        if not a.startswith('-std=')
                    ]
                    ext.extra_link_args = [
                        a for a in ext.extra_link_args
                        if not a.startswith('-static-')
                    ]
                else:
                    ext.extra_compile_args = [
                        a for a in ext.extra_compile_args
                        if not a.startswith('/')
                    ]
                    if '-std=c++17' not in ext.extra_compile_args:
                        ext.extra_compile_args.append('-std=c++17')
        super().build_extensions()

    def run(self):
        super().run()

        if sys.platform.startswith('win'):
            if sys.maxsize > 2**32:
                dll_source_dir = 'HardView/LiveView/64'
            else:
                dll_source_dir = 'HardView/LiveView/32'

            files_to_copy = []
            files_to_copy.extend(glob.glob(os.path.join(dll_source_dir, '*.dll')))
            files_to_copy.extend(glob.glob(os.path.join(dll_source_dir, '*.json')))

            build_lib = self.build_lib
            liveview_ext_path = self.get_ext_filename('HardView.LiveView')
            dest_dir = os.path.join(build_lib, os.path.dirname(liveview_ext_path))

            os.makedirs(dest_dir, exist_ok=True)

            for file in files_to_copy:
                print(f"Copying: {file} -> {dest_dir}")
                shutil.copy2(file, dest_dir)

    def get_ext_filename(self, ext_name):
        original_filename = super().get_ext_filename(ext_name)
        _, ext_suffix = os.path.splitext(original_filename)
        ext_path = ext_name.split('.')
        filename = ext_path[-1] + ext_suffix
        path_parts = ext_path[:-1]
        return os.path.join(*path_parts, filename)

# =================================================================
# ==      Setup Function                                         ==
# =================================================================

setup(
    name='HardView',
    version='4.0.0',
    description='Universal cross-platform (Linux & Windows) Python library for hardware information and real-time performance monitoring.',
    long_description='''
# HardView Universal 4.0.0

A comprehensive cross-platform Python & C/C++ library for querying low-level hardware information (SMBIOS, SMART, Process, CPU, GPU, RAM, Disk, Network) and monitoring system performance and sensors in real-time on both Linux and Windows systems.
    ''',
    long_description_content_type='text/markdown',
    author='gafoo',
    author_email='omarwaled3374@gmail.com',
    url='https://github.com/gafoo173/HardView',
    license="MIT",
    packages=packages,
    package_dir=package_dir,
    package_data=package_data,
    include_package_data=True,
    ext_modules=extensions,
    cmdclass={'build_ext': build_ext},
    classifiers=[
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        'Operating System :: Microsoft :: Windows',
        'Operating System :: POSIX :: Linux',
        'Topic :: System :: Hardware',
        'Topic :: System :: Monitoring',
    ],
    keywords='hardware system monitoring performance cpu ram disk network gpu bios smbios smart process windows linux wmi real-time live',
    python_requires='>=3.7',
    install_requires=[
        'pybind11>=2.6'
    ],
    extras_require={
        'dev': [
            'pytest>=6.0',
        ],
    },
    project_urls={
        'Bug Reports': 'https://github.com/gafoo173/HardView/issues',
        'Source': 'https://github.com/gafoo173/HardView',
    },
)
