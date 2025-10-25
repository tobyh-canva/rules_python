#!/usr/bin/env python3
import sys
import zipfile


def main():
    zipfile_path = sys.argv[1]
    output_path = sys.argv[2]
    with zipfile.ZipFile(zipfile_path, "r") as zf, open(output_path, "w") as f:
        write_manifest(zf, f)


def write_manifest(zf, output_stream):
    print(f"{'FILE_SIZE':<15} {'CRC32':<12} NAME", file=output_stream)
    print(f"{'-' * 15} {'-' * 12} --------", file=output_stream)
    infolist = sorted(zf.infolist(), key=lambda x: x.filename)
    for file in infolist:
        crc_hex = f"0x{file.CRC:08X}"
        print(f"{file.file_size:<15} {crc_hex:<12} {file.filename}", file=output_stream)



if __name__ == "__main__":
    main()
