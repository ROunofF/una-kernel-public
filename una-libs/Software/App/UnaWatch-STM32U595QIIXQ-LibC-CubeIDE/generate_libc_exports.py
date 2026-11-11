#!/usr/bin/env python3
import subprocess
import re
import fnmatch
import os
import sys
from pathlib import Path

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <elf-file>")
    sys.exit(1)

script_dir = Path(__file__).parent
nm_path = script_dir / "arm-none-eabi-nm.exe"
ELF = Path(sys.argv[1])

# Outputs
EXPORT_LD_LIBC   = script_dir / "libc_exports.ld"
EXPORT_LD_KERNEL = script_dir / "libc_exports_kernel.ld"

# Address window filter (keep your original range)
BASE_ADDR_BEGIN = 0x08028000
BASE_ADDR_END   = 0x0805FFFF

# Existing blacklist + extended with your syscall wrappers
BLACKLIST = [
    '*free*',
    '*alloc*',
    '__assert',
    '__assert_func',
    '__env_lock',
    '__env_unlock',
    '__libc_fini_array',
    '__tz_lock',
    '__tz_unlock',
    '__tzcalc_limits',
    '_setenv_r',
    '_unsetenv_r',
    'fesetenv',
    '_write_r',
    'write',
    '_read_t',
    'read',

    # ---- Added syscall wrappers ----
    'initialise_monitor_handles',
    '_getpid',
    '_kill',
    '_exit',
    'exit',
    'abort',
    '_wait',
    '_fork',
    '_execve',
    '_times',
    '_read',
    '_write',
    '_close',
    '_fstat',
    '_isatty',
    '_lseek',
    '_open',
    '_unlink',
    '_stat',
    '_link',
    '_malloc_r',
    '_free_r',
    '_calloc_r',
    '_realloc_r',
    '_gettimeofday_r',

    'Reset_Handler',

    'y0', 'y1', 'yn', 'y0f', 'y1f', 'ynf',
    'j0', 'j1', 'jn', 'j0f', 'j1f', 'jnf',

    'localtime', 'gmtime', 'asctime', 'ctime',

    'fopen',
    'fputc',
    'fputs',
    'fread',
    'frexp',
    'frexpf',
    'frexpl',
    'fseek',
    'fseeko',
    'fstat',
    'ftell',
    'ftello',
    'fwrite',
    'g_pfnVectors',
    'getc',
    'getchar',
    'fflush',
    'fgetc',
    'fgets',
    'fileno',
    'fiprintf',
    'vfiprintf',

    'shared_lib_info',
]

BLACKLIST += [
    'open', 'close', 'read', 'write', 'lseek', 'stat', 'fstat', 'unlink',
    'remove', 'rename', 'access', 'link',
    
    'fopen', 'freopen', 'fclose', 'fflush', 'fread', 'fwrite',
    'fseek', 'ftell', 'rewind',
    'fgetc', 'fputc', 'fgets', 'fputs', 'getc', 'putc', 'getchar', 'putchar',
    'puts', 'ungetc',
    'fprintf', 'vfprintf',
    'scanf', 'sscanf', 'fscanf', 'vfscanf',
    'perror', 'setvbuf', 'setbuf', 'clearerr', 'feof', 'ferror', 'fileno',
    'tmpfile', 'tmpnam', 'tempnam',

    'raise',

    'LibcIM_syscallsInit'
]

# Kernel whitelist — anything matching these goes to libc_exports_kernel.ld
KERNEL_WHITELIST = [
    'setenv',
    'unsetenv',
    'getenv',
    'tzset',
    'putenv',
    'setlocale',
    'setbuf',
    'setvbuf',
    'LibcIM_init',
]

valid_types = {'T', 'D', 'B', 'R', 'W', 'V', 't', 'd', 'b', 'r'}

def is_blacklisted(name: str) -> bool:
    if name.startswith("_"):  # все внутрішнє / ABI / newlib glue
        return True
    for pattern in BLACKLIST:
        if fnmatch.fnmatch(name.lower(), pattern.lower()):
            return True
    return False

def is_kernel_whitelisted(name: str) -> bool:
    name_l = name.lower()
    for pattern in KERNEL_WHITELIST:
        if fnmatch.fnmatch(name_l, pattern.lower()):
            return True
    return False

# Check nm
if not nm_path.exists():
    print(f"[ERROR] arm-none-eabi-nm not found in {script_dir}")
    sys.exit(1)

# Check ELF
if not ELF.exists():
    print(f"[ERROR] ELF file not found: {ELF}")
    sys.exit(1)

# Run nm (global symbols)
try:
    nm_output = subprocess.check_output(
        [str(nm_path), '-g', str(ELF)], encoding='utf-8'
    )
except subprocess.CalledProcessError as e:
    print(f"[ERROR] Failed to execute nm: {e}")
    sys.exit(2)

libc_exports   = []  # (name, addr_hex)
kernel_exports = []  # (name, addr_hex)

for line in nm_output.splitlines():
    parts = line.strip().split()
    if len(parts) < 3:
        continue

    addr_str, sym_type, name = parts[0], parts[1], parts[2]
    if not re.match(r'^[0-9A-Fa-f]+$', addr_str):
        continue

    addr = int(addr_str, 16)

    if sym_type in valid_types and BASE_ADDR_BEGIN <= addr <= BASE_ADDR_END:
        if is_blacklisted(name):
            continue

        # Thumbify: set LSB=1 for *function* symbols (T/t). Data keeps LSB=0.
        if sym_type in {'T', 't'}:
            addr |= 1

        addr_hex = f"0x{addr:08X}"
        entry = (name, addr_hex)

        if is_kernel_whitelisted(name):
            kernel_exports.append(entry)
        else:
            libc_exports.append(entry)

# Write both outputs
EXPORT_LD_LIBC.parent.mkdir(parents=True, exist_ok=True)

with open(EXPORT_LD_KERNEL, 'w', encoding='utf-8') as fk:
    for name, addr in sorted(kernel_exports):
        fk.write(f"PROVIDE({name} = {addr});\n")

with open(EXPORT_LD_LIBC, 'w', encoding='utf-8') as fl:
    for name, addr in sorted(libc_exports):
        fl.write(f"PROVIDE({name} = {addr});\n")

print(f"[OK] Generated {EXPORT_LD_KERNEL} with {len(kernel_exports)} kernel symbols.")
print(f"[OK] Generated {EXPORT_LD_LIBC} with {len(libc_exports)} libc symbols (after filtering, Thumbified for functions).")
