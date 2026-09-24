/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PcdLib.h>
#include <Library/FrameBufferBltLib.h>

#define FB_BASE    ((EFI_PHYSICAL_ADDRESS)FixedPcdGet64 (PcdFrameBufferBase))
#define FB_WIDTH   ((UINT32)FixedPcdGet32 (PcdFrameBufferWidth))
#define FB_STRIDE  ((UINT32)FixedPcdGet32 (PcdFrameBufferStride))
#define FB_HEIGHT  ((UINT32)FixedPcdGet32 (PcdFrameBufferHeight))
#define FB_BPP     ((UINT32)FixedPcdGet32 (PcdFrameBufferBpp))

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} PLATFORM_GOP_DEVICE_PATH;
#pragma pack()

STATIC PLATFORM_GOP_DEVICE_PATH  mGopDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x6b9d1e04, 0x4c1a, 0x4f2b, { 0xa5, 0x33, 0x0c, 0x71, 0x9e, 0x22, 0x84, 0x10 } }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mModeInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mMode;
STATIC FRAME_BUFFER_CONFIGURE                *mFrameBufferConfigure;

STATIC
EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;

  if ((Info == NULL) || (SizeOfInfo == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  ModeInfo = AllocateCopyPool (sizeof (mModeInfo), &mModeInfo);
  if (ModeInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *Info       = ModeInfo;
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // Mode is fixed and already active; nothing to program.
  //
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta         OPTIONAL
  )
{
  //
  // Delegate all Blt operations to FrameBufferBltLib
  //
  if ((Width == 0) || (Height == 0) || (mFrameBufferConfigure == NULL)) {
    return EFI_SUCCESS;
  }

  return FrameBufferBlt (
           mFrameBufferConfigure,
           BltBuffer,
           BltOperation,
           SourceX,
           SourceY,
           DestinationX,
           DestinationY,
           Width,
           Height,
           Delta
           );
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  mGop = {
  GopQueryMode,
  GopSetMode,
  GopBlt,
  &mMode
};

/**
  Publish the fixed-mode GOP.
**/
EFI_STATUS
EFIAPI
PlatformGopEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINTN       ConfigureSize;

  ASSERT (FB_BPP == 4);

  ZeroMem (&mModeInfo, sizeof (mModeInfo));
  mModeInfo.Version              = 0;
  mModeInfo.HorizontalResolution = FB_WIDTH;
  mModeInfo.VerticalResolution   = FB_HEIGHT;
  mModeInfo.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mModeInfo.PixelsPerScanLine    = FB_STRIDE;

  mMode.MaxMode         = 1;
  mMode.Mode            = 0;
  mMode.Info            = &mModeInfo;
  mMode.SizeOfInfo      = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mMode.FrameBufferBase = FB_BASE;
  mMode.FrameBufferSize = (UINTN)FB_STRIDE * FB_HEIGHT * FB_BPP;

  //
  // Size-discover the FrameBufferBltLib configuration, then create it
  //
  ConfigureSize = 0;
  Status = FrameBufferBltConfigure (
             (VOID *)(UINTN)FB_BASE,
             &mModeInfo,
             NULL,
             &ConfigureSize
             );
  ASSERT (Status == RETURN_BUFFER_TOO_SMALL);

  mFrameBufferConfigure = AllocatePool (ConfigureSize);
  ASSERT (mFrameBufferConfigure != NULL);

  Status = FrameBufferBltConfigure (
             (VOID *)(UINTN)FB_BASE,
             &mModeInfo,
             mFrameBufferConfigure,
             &ConfigureSize
             );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((
    DEBUG_INFO,
    "PlatformGop: %ux%u @ 0x%lx (%u bpp)\n",
    FB_WIDTH,
    FB_HEIGHT,
    (UINT64)FB_BASE,
    FB_BPP * 8
    ));

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mGopDevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
