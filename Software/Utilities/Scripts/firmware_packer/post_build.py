import os
import sys
import re
import subprocess
from pathlib import Path
import argparse
import struct
import logging

# ANSI color codes for logging
class ColoredFormatter(logging.Formatter):
    COLORS = {
        'DEBUG': '\033[94m',
        'INFO': '\033[92m',
        'WARNING': '\033[93m',
        'ERROR': '\033[91m',
        'CRITICAL': '\033[95m'
    }
    RESET = '\033[0m'

    def format(self, record):
        color = self.COLORS.get(record.levelname, self.RESET)
        record.levelname = f"{color}{record.levelname}{self.RESET}"
        record.msg = f"{color}{record.msg}{self.RESET}"
        return super().format(record)

logger = logging.getLogger(__name__)
handler = logging.StreamHandler()
handler.setFormatter(ColoredFormatter('%(levelname)s: %(message)s'))
logger.addHandler(handler)
logger.setLevel(logging.INFO)

# Installing dependencies
module_name = "bincopy"
try:
    import bincopy
except ImportError:
    logger.info(f"Module {module_name} not found, installing it...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", module_name])
    import bincopy

# Firmware OTA image encryptor
from encrypt_firmware import pack

# The bootloader and shared library are folders in this repository
bootloader_repo_path = "una-bootloader"
# Path to key file in the bootloader folder
key_path_in_bootloader = "Software/Keys/SecurityKeys.pem"
# Path to hex dir in the bootloader folder
bootloader_img_path_in_bootloader = "Exe/"
shared_lib_repo_path = "una-libs"
# Path to hex dir in the shared library folder
shared_lib_img_path_in_repo = "Exe/"


def get_git_root():
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            stdout=subprocess.PIPE,
            text=True,
            check=True
        )
        return Path(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        logger.error(f"Could not determine git root: {e}")
        return None


def get_absolute_path(repo_relative_path):
    git_root = get_git_root()
    if git_root is None:
        return None
    return (git_root / repo_relative_path).resolve()


def get_bootloader_key():
    path_to_key = Path(bootloader_repo_path, key_path_in_bootloader).as_posix()
    abs_path = get_absolute_path(path_to_key)
    logger.info(f"Reading bootloader key from: {abs_path}")
    if abs_path is None:
        logger.error("Cannot resolve absolute path to the key file.")
        return None
    try:
        with open(abs_path, "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        logger.error(f"Failed to read key file: {e}")
        return None


def get_bootloader_image():
    abs_dir = get_absolute_path(Path(bootloader_repo_path, bootloader_img_path_in_bootloader).as_posix())

    if abs_dir is None or not abs_dir.is_dir():
        logger.error(f"Bootloader image directory not found: {abs_dir}")
        return None

    # Search .hex file in the Directory
    hex_files = list(abs_dir.glob("*.hex"))
    if not hex_files:
        logger.error(f"No .hex file found in bootloader image directory ({abs_dir})")
        return None

    logger.info(f"Found bootloader hex file: {hex_files[0]}")
    try:
        with open(hex_files[0], "r", encoding="utf-8") as f:
            hex_data = f.read()
    except Exception as e:
        logger.error(f"Failed to read .hex file: {e}")
        return None

    return hex_data


def get_shared_lib_image():
    abs_dir = get_absolute_path(Path(shared_lib_repo_path, shared_lib_img_path_in_repo).as_posix())

    if abs_dir is None or not abs_dir.is_dir():
        logger.error(f"Shared library image directory not found: {abs_dir}")
        return None

    # Search .hex file in the Directory
    hex_files = list(abs_dir.glob("*.hex"))
    if not hex_files:
        logger.error(f"No .hex file found in shared library image directory ({abs_dir})")
        return None

    logger.info(f"Found shared library hex file: {hex_files[0]}")
    try:
        with open(hex_files[0], "r", encoding="utf-8") as f:
            hex_data = f.read()
    except Exception as e:
        logger.error(f"Failed to read .hex file: {e}")
        return None

    return hex_data


def extract_shared_lib_version_string(hex_text: str, struct_size: int = 128, field_size: int = 32) -> str:
    # Extracts the string version[32] from the last 128 bytes (SharedLibInfo_t) of Intel HEX.
    # Checks for null terminator. Returns as Python str.
    # typedef struct {
    #     char version[32];
    #     char date[16];
    #     char time[16];
    #     char vendor[32];
    #     char reserved[32];
    # } SharedLibInfo_t;


    binfile = bincopy.BinFile()
    binfile.add_ihex(hex_text)

    data = binfile.as_binary()

    if len(data) < struct_size:
        raise ValueError("The image is too short.")

    struct_bytes = data[-struct_size:]
    version_field = struct_bytes[:field_size]

    try:
        end = version_field.index(0)
    except ValueError:
        raise ValueError("The version[32] field does not contain a null terminator.")

    # Version format: "sl.123.123.123-suffix" 
    # we need to read bytes starting from the 3 (skip sl.)
    version_str = version_field[3:end].decode("ascii", errors="strict")

    return version_str


def make_full_fw_image_bincopy(boot, lib, app):
    binfile = bincopy.BinFile()
    binfile.add_ihex(boot)
    binfile.add_ihex(lib)
    binfile.add_ihex(app)
    return binfile.as_ihex(16)

def make_libc_app_bin_image_bincopy(lib, app):
    binfile = bincopy.BinFile()
    binfile.add_ihex(lib)
    binfile.add_ihex(app)
    return binfile.as_binary()

# Format: <major>.<minor>.<patch>-<any_text>
def parse_firmware_version(version_string):
    match = re.match(r'^(\d+)\.(\d+)\.(\d+)(?:-(rc\d+))*(?:-(.+))*?', version_string)
    if match:
        major, minor, patch, suffix, commit = match.groups()
        return int(major), int(minor), int(patch), suffix, commit
    else:
        logger.error(f"Incorrect version format: {version_string}")
        return 0,0,0,"dirty",""

def main():
    boot_hex = None
    shared_lib_hex = None
    app_hex = None

    # Get firmware name and version from arguments
    parser = argparse.ArgumentParser(description="Post-build script")
    parser.add_argument("-d", "--dir", help="path to the work directory", required=True)
    parser.add_argument("-o", "--out", help="path to the output directory", required=True)
    parser.add_argument("-n", "--name", help="name of the project", required=True)
    parser.add_argument("-v", "--version", help="version of the project", required=True)
    parser.add_argument("-m", "--mode", help="Mode: 0 - do nothing, 1 - only HEX (bootloader + app), 2 - HEX and OTA ", required=True)
    parser.add_argument("--nolibc", help="firmware dosn't use libc", action="store_true")
    parser.add_argument("--inclibc", help="include libc into ota image", action="store_true")

    args = parser.parse_args()
    fw_name = args.name
    fw_version = args.version
    work_dir = Path(args.dir).resolve()
    out_dir = Path(args.out).resolve()
    mode = int(args.mode)
    no_libc = args.nolibc

    if no_libc is False:
        include_libc = args.inclibc
    else:
        logger.info("LIBC not used")
        include_libc = False

    # Do nothing
    if mode == 0:
        sys.exit(0)

    logger.info(f"FW Version: {fw_version}")
    fw_ver_major, fw_ver_minor, fw_ver_patch, fw_suffix, fw_commit = parse_firmware_version(fw_version)
    if fw_suffix is None:
        fw_suffix = ""
    if fw_commit is not None:
        fw_suffix += fw_commit

    boot_hex = get_bootloader_image()

    if boot_hex is None:
        sys.exit(2)

    if no_libc is False:
        shared_lib_hex = get_shared_lib_image()

        if shared_lib_hex is None:
            sys.exit(2)

        # Extracts 4 bytes of the shared library version
        shared_lib_version_str = extract_shared_lib_version_string(shared_lib_hex)
        logger.info(f"Libs Version: {fw_version}")
        sl_ver_major, sl_ver_minor, sl_ver_patch, sl_suffix, sl_commit = parse_firmware_version(shared_lib_version_str)
        if sl_suffix is None:
            sl_suffix = ""
        if sl_commit is not None:
            sl_suffix += sl_commit


    else:
        sl_ver_major = 0
        sl_ver_minor = 0
        sl_ver_patch = 0
        sl_suffix = ""

    app_hex_name = fw_name + ".hex"
    app_hex_path = os.path.normpath(os.path.join(work_dir, app_hex_name))

    full_fw_hex_name = f"{fw_name}_{fw_version}.hex"
    full_fw_hex_path = os.path.normpath(os.path.join(out_dir, full_fw_hex_name))

    if os.path.exists(app_hex_path):
        with open(app_hex_path) as fapp:
            app_hex = fapp.read()
    else:
        logger.error(f"Application firmware file ({app_hex_path}) does not exist!")
        sys.exit(2)

    # Create out dir
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    # remove all hex files
    old_hex_files = os.listdir(out_dir)
    for old_hex in old_hex_files:
        if old_hex.endswith(".hex"):
            os.remove(os.path.join(out_dir, old_hex))

    if no_libc is False:
        out_hex = make_full_fw_image_bincopy(boot_hex, shared_lib_hex, app_hex)
    else:
        out_hex = make_full_fw_image_bincopy(boot_hex, None, app_hex)

    with open(full_fw_hex_path, "w") as fout:
        fout.write(out_hex)
        logger.info(f"Saved: {full_fw_hex_path}")


    # Done, if only HEX required
    if mode == 1:
        sys.exit(0)

    # Encrypt OTA image

    key = get_bootloader_key()
    if key is None:
        sys.exit(2)

    app_bin_name = fw_name + ".bin"
    app_bin_path = os.path.normpath(os.path.join(work_dir, app_bin_name))

    key_name = "key.pem"
    key_name_path = os.path.normpath(os.path.join(work_dir, key_name))

    full_fw_ota_name = f"{fw_name}_{fw_version}.ota"
    full_fw_ota_path = os.path.normpath(os.path.join(out_dir, full_fw_ota_name))

    full_fw_lib_name = f"{fw_name}_{fw_version}.lib"
    full_fw_lib_path = os.path.normpath(os.path.join(out_dir, full_fw_lib_name))

    full_fw_ota_factory_name = f"{fw_name}_{fw_version}.gld"
    full_fw_ota_factory_path = os.path.normpath(os.path.join(out_dir, full_fw_ota_factory_name))


    # save bootloader key
    with open(key_name_path, "w") as f:
        f.write(key)

    # remove all ota files
    old_ota_files = os.listdir(out_dir)
    for old_ota in old_ota_files:
        if old_ota.endswith(".ota"):
            os.remove(os.path.join(out_dir, old_ota))
        if old_ota.endswith(".gld"):
            os.remove(os.path.join(out_dir, old_ota))


    user_data_version = 1    # Version of the User Date fields
    fw_update_type = 1
    fw_factory_type = 2
    fw_lib_type = 4

    # Create combinate image libc + app
    if include_libc:
        fw_bin_img_name =  fw_name + "_libc.bin"
        combinate_bin_ipath = os.path.normpath(os.path.join(work_dir, fw_bin_img_name))
        # print(f"Combined image: {combinate_bin_ipath}")

        w_bin_img = make_libc_app_bin_image_bincopy(shared_lib_hex, app_hex)
        with open(combinate_bin_ipath, "wb") as f:
            f.write(w_bin_img)

        # Create Firmware OLIB image
        user_data = struct.pack('=BBBBBBBB23sBBBBB8s',
                                user_data_version, int(fw_lib_type), 0,0,
                                fw_ver_patch,fw_ver_minor,fw_ver_major,0,
                                fw_suffix.encode(),0, # with terminator
                                sl_ver_patch,sl_ver_minor,sl_ver_major,0,
                                bytearray(8))

        out_ota = pack(combinate_bin_ipath, key_name_path, user_data)

        if out_ota:
            with open(full_fw_lib_path, "wb") as fout:
                fout.write(out_ota)
                logger.info(f"Saved: {full_fw_lib_path}")


    #Create Firmware OTA image
    user_data = struct.pack('=BBBBBBBB23sBBBBB8s',
                            user_data_version, int(fw_update_type), 0,0,
                            fw_ver_patch,fw_ver_minor,fw_ver_major,0,
                            fw_suffix.encode(),0, # with terminator
                            sl_ver_patch,sl_ver_minor,sl_ver_major,0,
                            bytearray(8))

    out_ota = pack(app_bin_path, key_name_path, user_data)

    if out_ota:
        with open(full_fw_ota_path, "wb") as fout:
            fout.write(out_ota)
            logger.info(f"Saved: {full_fw_ota_path}")


    # Create FACTORY image
    user_data_factory = struct.pack('=BBBBBBBB23sBBBBB8s',
                            user_data_version, int(fw_factory_type), 0,0,
                            fw_ver_patch,fw_ver_minor,fw_ver_major,0,
                            fw_suffix.encode(),0, # with terminator
                            sl_ver_patch,sl_ver_minor,sl_ver_major,0,
                            bytearray(8))

    out_ota_factory = pack(app_bin_path, key_name_path, user_data_factory)

    if out_ota_factory:
        with open(full_fw_ota_factory_path, "wb") as fout:
            fout.write(out_ota_factory)
            logger.info(f"Saved: {full_fw_ota_factory_path}")



    # remove key
    os.remove(key_name_path)


if __name__ == "__main__":
    main()
