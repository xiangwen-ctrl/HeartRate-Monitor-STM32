"""Compile and execute firmware C regressions; failures return nonzero.

Requires GCC/Clang, or Visual Studio C++ Build Tools on Windows.
No Python packages, simulated Python algorithm, or hardware are required.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
API = ROOT / 'software/stm32/user/api'
SOURCES = [ROOT / 'verify/test_pulse.c', API / 'pulse_detect.c', API / 'pulse_buffer.c']


def main():
    with tempfile.TemporaryDirectory(prefix='heart-rate-tests-') as directory:
        build = Path(directory)
        exe = build / ('test_pulse.exe' if os.name == 'nt' else 'test_pulse')
        cc = shutil.which('gcc') or shutil.which('clang')
        if cc:
            subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(API), *map(str, SOURCES), '-lm', '-o', str(exe)],
                           cwd=build, check=True)
        elif os.name == 'nt':
            arguments = ['/nologo', '/W4', '/WX', '/O2', '/std:c11', '/utf-8',
                         '/I' + str(API), *map(str, SOURCES), '/Fe:' + str(exe)]
            if shutil.which('cl'):
                subprocess.run(['cl', *arguments], cwd=build, check=True)
            else:
                vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
                if not vswhere.exists():
                    raise SystemExit('Install GCC/Clang or Visual Studio C++ Build Tools.')
                install = subprocess.check_output([
                    str(vswhere), '-latest', '-products', '*', '-requires',
                    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                    '-property', 'installationPath'], text=True).strip()
                if not install:
                    raise SystemExit('Visual Studio C++ compiler not found.')
                setup = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
                batch = build / 'build.cmd'
                batch.write_text('@echo off\ncall "' + str(setup) + '" >nul\n'
                                 'if errorlevel 1 exit /b 1\ncl ' + subprocess.list2cmdline(arguments)
                                 + '\nexit /b %errorlevel%\n', encoding='utf-8')
                subprocess.run(['cmd', '/d', '/c', 'build.cmd'], cwd=build, check=True)
        else:
            raise SystemExit('Install GCC or Clang to run the C regression tests.')
        subprocess.run([str(exe)], cwd=build, check=True)


if __name__ == '__main__':
    main()
