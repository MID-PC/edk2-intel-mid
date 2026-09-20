# edk2 UEFI for Intel MID Android devices

## Description

This project focuses on making Intel Atom-based Android-only devices run Windows, Linux and other x86 UEFI operating systems using custom UEFI firmware.

## Resources

- [Device support status](Status.md)

## Requirements

Dependencies:

- Python 3.10 or newer
- git
- LLVM/Clang
  (CLANGPDB is the only supported toolchain)
- GNU make
- NASM
- The Python packages listed in pip-requirements.txt

Install the Python packages:

```
python3 -m pip install -r pip-requirements.txt
```

## Getting the source

```
git clone https://github.com/MID-PC/edk2-intel-mid.git
cd edk2-intel-mid
```

(Submodules are initialized automatically by build_uefi.py)

## Building

```
python3 build_uefi.py -d <device> [options]
```

Options:

- `-d, --device`: device codename
- `-r, --release`: build target, either DEBUG (default) or RELEASE
- `-c, --clean`: remove the existing build directory and any previous
  output image before building
- `-u, --update`: pull the latest repository and submodule changes, then run
  stuart setup and update.
- `--kdnet-usb`: build the KDNET-over-USB debug variant
Examples:

```
python3 build_uefi.py -d t00k
python3 build_uefi.py -d ducati -r RELEASE
python3 build_uefi.py -d t00g -c -u
```

## Output

Each successful build writes a flashable boot image to:

```
out/boot_<device>_<TARGET>.img
```

# Guides

Coming Soon

## Credits

[TianoCore EDK II](https://www.tianocore.org/) - the base for this project <br>
[Project Silicium](https://github.com/Project-Silicium/) - the inspiration and general idea <br>
[v1-727](https://github.com/v1-727/) - initial proof of concept on the Asus Zenfone 5 Lite (t00k) <br>
[NUC](https://github.com/iNUCi/) - repo refactor to support multiple devices and Intel Atom SoCs <br>
[Glitchy](https://github.com/Glitchythedev) - Project logo design <br>

## License

All code except drivers in `GPLDrivers` & `GPLApplications` & `GPLLibrary` directories are licensed under BSD-2-Clause. <br />
GPL Drivers are licensed under `GPLv2` or later license.
