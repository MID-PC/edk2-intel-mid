# z00xs (Asus Zenfone Zoom / ZX551ML) boot image resources

The build packs the UEFI FD into a standard Google `mkbootimg` boot image
(header v0), placing the FD in the **second bootloader** slot (loaded at
`0x10F00000`), then appends the ASUS `sig` blob (`cat sig >> boot.img`). The
signature is not verified - the bootloader only checks that it is present.

## Files

| File      | Committed | Purpose                                                        |
|-----------|-----------|----------------------------------------------------------------|
| `sig`     | yes       | ASUS `zf2_6_sig` appended after the boot image.                |
| `kernel`  | no        | Stock kernel, extracted from the device's stock `boot.img`.    |
| `ramdisk` | no        | Stock ramdisk, extracted from the device's stock `boot.img`.   |

## Populating `kernel` and `ramdisk`

Extract them once from the stock boot image:

```
python3 Resources/Scripts/unpack_bootimg.py boot.img Platforms/z00xsPkg/ImageResources
```

This writes `kernel` and `ramdisk` here. The build then reassembles the image
with `Resources/Scripts/mkbootimg.py` using the exact stock load addresses,
page size and kernel command line.
