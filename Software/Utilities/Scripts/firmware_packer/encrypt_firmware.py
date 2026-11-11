import os
import struct
import sys
import time
import base64
import zlib
import argparse

from aespython import key_expander, aes_cipher, cbc_mode

LibVersion = 1



# Adds non-significant zeros to the end of a file to make it a multiple of 16
def padd(input):
    size = len(input)
    if size % 16 != 0:
        padding = 16 - (size % 16)
        input += b'\x00' * padding

    # Workaround for a bug in bootloader versions <= 0.1.1:
    # If (size - 16) % 512 == 0, the CRC check is triggered prematurely
    # with an incomplete CRC, causing firmware update failure.
    # Adding 16 bytes of padding shifts the size to avoid this condition.
    size = len(input)
    if (size - 16) % 512 == 0:
        input += b'\x00' * 16

    return input
    
    
# Calculate CRC32
def calcCRC(input):
    return zlib.crc32(input)



# Add 16 bytes of the subheader to the binary data
def addSubHeader(input):
    data_len = len(input)
    data_crc = calcCRC(input)
    output = struct.pack('=LLLL', data_len, data_crc, 0, 0)
    output += input

    return output
    


# Get key and IV from a pem file
def getKeys(pem_file_path):
    with open(pem_file_path, 'r') as content:
        strings = content.readlines()
        mkey = ''
        mivkey = ''
        for i in range(len(strings)):
            strings[i] = strings[i].replace('\n','')
        for i in range(len(strings)):
            if strings[i] == '-----BEGIN KEY-----':
                mkey = strings[i+1]
            if strings[i] == '-----BEGIN IV KEY-----':
                mivkey = strings[i+1]
        key = base64.b64decode(mkey)
        iv = base64.b64decode(mivkey)
        # print('KEY: ' + key.hex(' '))
        # print('IV: ' + iv.hex(' '))

    return key, iv



# Encrypt data
def encryptPayload(input, key, iv):
    #Initialize encryption using key and iv
    key_expander_256 = key_expander.KeyExpander(256)
    expanded_key = key_expander_256.expand(key)
    aes_cipher_256 = aes_cipher.AESCipher(expanded_key)
    aes_cbc_256 = cbc_mode.CBCMode(aes_cipher_256, 16)
    aes_cbc_256.set_iv(iv)

    out = bytearray()
    chunk_size = 16
    for i in range(0, len(input), chunk_size):
        chunk = input[i:i + chunk_size]
        out_chunk = aes_cbc_256.encrypt_block(bytearray(chunk))
        out += bytearray(out_chunk)

    return out



# Add 64 bytes headr to the encrypted payload
def addHeader(ver, bin_data_crc, user_data, encrypted_payload):
    enc_crc = calcCRC(encrypted_payload)
    enc_len = len(encrypted_payload)
    output = struct.pack('=LLLL44s', ver, enc_crc, enc_len, bin_data_crc, user_data)
    header_crc = calcCRC(output)
    # repack structure with own CRC
    output = struct.pack('=60sL', output, header_crc)
    output += encrypted_payload

    return output, header_crc



# Encrypt and pack raw binary data
def pack(bin_file_path, pem_file_path, user_data):

    # Validate and open binary file
    try:
        with open(bin_file_path, 'rb') as bin:
            bin_file = bin.read()
    except FileNotFoundError:
        print(f"Binary file ({bin_file_path}) does not exist!")
        return None
    
    if user_data:
        if len(user_data) > 44:
            print(f"User data len exeds 44 bytes!")
            return None
    else:
         # empty array
         user_data = bytearray(44)   

    # Make sure that file size is a multiple of 16
    bin_data = padd(bin_file)
    bin_data_crc = calcCRC(bin_data)

    # Add 16 byte header to binary data
    bin_payload = addSubHeader(bin_data)

    try:
        key, iv = getKeys(pem_file_path)
        encrypted_payload = encryptPayload(bin_payload, key, iv)
    except FileNotFoundError:
        print(f"Key file ({pem_file_path}) does not exist!")
        return None



    # Add 64 bytes main header
    out, header_crc = addHeader(LibVersion, bin_data_crc, user_data, encrypted_payload)


    # print(f'version:               {LibVersion}')
    # print(f'bin_data len:          {len(bin_data)}')
    # print(f'bin_data crc:          0x{bin_data_crc:08X}')
    # print(f'encrypted_payload len: {len(encrypted_payload)}')
    # print(f'encrypted_payload crc: 0x{calcCRC(encrypted_payload):08X}')
    # print(f'header crc:            0x{header_crc:08X}')
    # print('user data:            ', ' '.join(f"{byte:02x}" for byte in user_data[:44]))

    return out





def main():
    parser = argparse.ArgumentParser(description='Firmware image generator script')
    parser.add_argument('-i', '--in', dest='in_file', required=True, help='Specify input file.')
    parser.add_argument('-p', '--pem', dest='pem_file', required=True, help='Specify input key file.')
    parser.add_argument('-o', '--out', dest='out_file', required=True, help='Specify output file.')
    parser.add_argument('-u', '--user', dest='user_hex', required=False, help='User data in HEX string (44 bytes max))')

    args = parser.parse_args()

    print(f'in_file:  {args.in_file}')
    print(f'pem_file: {args.pem_file}')
    print(f'out_file: {args.out_file}')
    print(f'user_hex: {args.user_hex}')

    if sys.version_info < (2, 6):
        print('Requires Python 2.6 or greater')
        sys.exit(1)

    if not os.path.exists(args.in_file):
        print(f"Input file ({args.in_file}) does not exist!")
        sys.exit(2)

    if not os.path.exists(args.pem_file):
        print(f"PEM key file ({args.pem_file}) does not exist!")
        sys.exit(2)

    if args.user_hex:
        try:
            user_data = bytes.fromhex(args.user_hex)
            if len(user_data) > 44:
                print("User data length exceeds 44!")
                sys.exit(2)
        except ValueError:
            print("Invalid user data HEX string. Please provide a valid HEX string!")
            sys.exit(2)
    else:
         # empty array
         user_data = bytearray(44) 


    start = time.time()

    out = pack(args.in_file, args.pem_file, user_data)

    if out:
        with open(args.out_file, "wb") as fout:
            fout.write(out)


    end = time.time()
    print(f'Time {round((end - start), 2)} s')

if __name__ == "__main__":
    main()