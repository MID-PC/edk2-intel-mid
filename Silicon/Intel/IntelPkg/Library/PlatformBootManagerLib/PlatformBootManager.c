/** @file
  PlatformBootManagerLib for Intel MID platforms.

  Boot policy:
    - Connect everything, including the fixed-mode GOP, and register it as the
      console.
    - Register every console input device (a USB keyboard) in the ConIn
      variable and force ConSplitterDxe to adopt it: ConPlatformDxe only
      installs gEfiConsoleInDeviceGuid (the adoption gate) when the device is
      listed in ConIn, and USB keyboards behind a hub can enumerate
      asynchronously, so the registration loop keeps scanning until the set of
      console-input handles is stable and reconnects each one once ConIn is
      populated.
    - Real boot options (removable media, OS loaders) always win.
    - The built-in UEFI Shell is always registered as a boot option at the end
      of BootOrder (OVMF style): it shows up in the UiApp Boot Manager and
      remains the fallback when no real boot option boots.
    - UiApp is never registered as a boot option and is never used as a
      default or unable-to-boot fallback. The boot manager menu option BdsDxe
      auto-creates carries LOAD_OPTION_HIDDEN (BmRegisterBootManagerMenu), so
      BDS never auto-boots it and the UiApp Boot Manager never lists it; its
      only entry points are the ESC hotkey and EFI_OS_INDICATIONS_BOOT_TO_FW_UI.
    - ESC during the boot timeout jumps into the Boot Manager Menu (UiApp),
      in the same style as OVMF; the progress bar labels that hint.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/BootLogoLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>

//
// FILE_GUID of ShellPkg/Application/Shell/Shell.inf
//
STATIC CONST EFI_GUID  mShellFileGuid = {
  0x7C04A583, 0x9E3E, 0x4F1C, { 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
};

#pragma pack(1)
typedef struct {
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH    File;
  EFI_DEVICE_PATH_PROTOCOL             End;
} FV_FILE_DEVICE_PATH;
#pragma pack()

/**
  Register (or look up) a boot option pointing at an application stored in a
  firmware volume.

  @return The option number, or LoadOptionNumberUnassigned on failure.
**/
STATIC
UINTN
RegisterFvBootOption (
  IN CONST EFI_GUID  *FileGuid,
  IN CHAR16          *Description,
  IN UINTN           Position
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  INTN                          OptionIndex;
  UINTN                         OptionNumber;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;
  EFI_LOADED_IMAGE_PROTOCOL     *LoadedImage;
  FV_FILE_DEVICE_PATH           FileNode;

  OptionNumber = LoadOptionNumberUnassigned;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return OptionNumber;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode.File, FileGuid);
  SetDevicePathEndNode (&FileNode.End);

  DevicePath = AppendDevicePathNode (
                 DevicePathFromHandle (LoadedImage->DeviceHandle),
                 (EFI_DEVICE_PATH_PROTOCOL *)&FileNode
                 );
  if (DevicePath == NULL) {
    return OptionNumber;
  }

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             Description,
             DevicePath,
             NULL,
             0
             );
  if (!EFI_ERROR (Status)) {
    BootOptions = EfiBootManagerGetLoadOptions (
                    &BootOptionCount,
                    LoadOptionTypeBoot
                    );

    OptionIndex = EfiBootManagerFindLoadOption (
                    &NewOption,
                    BootOptions,
                    BootOptionCount
                    );

    if (OptionIndex == -1) {
      Status = EfiBootManagerAddLoadOptionVariable (&NewOption, Position);
      if (!EFI_ERROR (Status)) {
        OptionNumber = NewOption.OptionNumber;
      }

      DEBUG ((
        DEBUG_INFO,
        "PlatformBds: registered boot option \"%s\" as Boot%04x (%r)\n",
        Description,
        (UINT32)OptionNumber,
        Status
        ));
    } else {
      OptionNumber = BootOptions[OptionIndex].OptionNumber;
      DEBUG ((
        DEBUG_INFO,
        "PlatformBds: boot option \"%s\" already present as Boot%04x\n",
        Description,
        (UINT32)OptionNumber
        ));
    }

    EfiBootManagerFreeLoadOption (&NewOption);
    EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
  }

  FreePool (DevicePath);

  return OptionNumber;
}

/**
  Called before the console is connected.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;
  UINTN       Iteration;
  UINTN       PreviousCount;

  DEBUG ((DEBUG_INFO, "PlatformBds: BeforeConsole\n"));

  //
  // Signal EndOfDxe before connecting anything. SecurityStubDxe's
  // Defer3rdPartyImageLoad handler defers every image loaded from a
  // non-firmware-volume device path until EndOfDxe has been signalled; if the
  // platform never signals it, the deferred images are never loadable and BDS
  // ends up in PlatformRecovery0000 -> ASSERT in Defer3rdPartyImageLoad.c.
  // Nothing on this platform needs to run between DXE dispatch and EndOfDxe.
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  //
  // Connect every driver so the fixed GOP and any storage show up.
  //
  EfiBootManagerConnectAll ();

  //
  // Make the fixed-mode GOP the console output device.
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < HandleCount; Index++) {
      EfiBootManagerUpdateConsoleVariable (
        ConOut,
        DevicePathFromHandle (Handles[Index]),
        NULL
        );
      EfiBootManagerUpdateConsoleVariable (
        ErrOut,
        DevicePathFromHandle (Handles[Index]),
        NULL
        );
    }

    FreePool (Handles);
  } else {
    DEBUG ((DEBUG_WARN, "PlatformBds: no GOP found!\n"));
  }

  //
  // Make every console input device (a USB keyboard) a ConIn device and make
  // sure ConSplitterDxe actually adopts it.
  //
  // ConPlatformDxe only installs gEfiConsoleInDeviceGuid - the gate for
  // ConSplitterDxe to adopt the device into the virtual gST->ConIn - when the
  // device path is already listed in the ConIn variable. USB keyboards,
  // especially behind a hub, can be enumerated asynchronously by UsbBusDxe
  // after EfiBootManagerConnectAll() above has returned, so registering
  // whatever is present exactly once is not sufficient.
  //
  // Loop until the set of SimpleTextIn-capable handles is stable: register
  // each device path in ConIn, and reconnect each handle that has not been
  // adopted yet. A second ConnectController() is harmless here - ConPlatformDxe
  // closed the SimpleTextInput open again during the first (ConIn-less)
  // connect, so it re-evaluates and now installs gEfiConsoleInDeviceGuid, and
  // ConSplitterDxe picks the device up in the same connect pass.
  //
  PreviousCount = MAX_UINTN;
  for (Iteration = 0; Iteration < 20; Iteration++) {
    Status = gBS->LocateHandleBuffer (
                    ByProtocol,
                    &gEfiSimpleTextInProtocolGuid,
                    NULL,
                    &HandleCount,
                    &Handles
                    );
    if (EFI_ERROR (Status)) {
      HandleCount = 0;
    }

    for (Index = 0; Index < HandleCount; Index++) {
      EFI_DEVICE_PATH_PROTOCOL  *InputDevicePath;
      VOID                      *ConsoleInDevice;

      InputDevicePath = DevicePathFromHandle (Handles[Index]);
      if (InputDevicePath == NULL) {
        //
        // ConSplitterDxe's own virtual gST->ConIn handle carries no device
        // path; skip it (and anything else like it).
        //
        continue;
      }

      EfiBootManagerUpdateConsoleVariable (ConIn, InputDevicePath, NULL);

      Status = gBS->HandleProtocol (
                      Handles[Index],
                      &gEfiConsoleInDeviceGuid,
                      &ConsoleInDevice
                      );
      if (!EFI_ERROR (Status)) {
        //
        // Already adopted into the splitter console.
        //
        continue;
      }

      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);

      DEBUG ((
        DEBUG_INFO,
        "PlatformBds: reconnecting console-input handle to adopt it\n"
        ));
    }

    if (Handles != NULL) {
      FreePool (Handles);
    }

    DEBUG ((
      DEBUG_INFO,
      "PlatformBds: console-input scan %u: %u handle(s)\n",
      Iteration,
      (UINT32)HandleCount
      ));

    if (HandleCount == PreviousCount) {
      break;
    }

    PreviousCount = HandleCount;

    //
    // Give UsbBusDxe a moment to bring up hub-attached devices before
    // re-scanning.
    //
    gBS->Stall (100 * 1000);
  }
}

/**
  Called after the console is connected.
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "PlatformBds: AfterConsole\n"));

  DEBUG ((
    DEBUG_INFO,
    "PlatformBds: framebuffer %ux%u @ 0x%lx\n",
    FixedPcdGet32 (PcdFrameBufferWidth),
    FixedPcdGet32 (PcdFrameBufferHeight),
    FixedPcdGet64 (PcdFrameBufferBase)
    ));

  //
  // Clear the panel in RELEASE builds: the vendor IFWI logo is still in the
  // framebuffer at this point (the fixed-mode GOP reuses the panel without a
  // SetMode, so no automatic clear happens) and would otherwise stay visible
  // behind the TianoCore logo and progress bar. The DEBUG framebuffer log
  // already clears the panel on its own and must not be wiped here, so the
  // clear is compiled out under MDEPKG_NDEBUG, which RELEASE defines and
  // DEBUG un-defines (IntelPkg.dsc.inc).
  //
#ifdef MDEPKG_NDEBUG
  ZeroMem (
    (VOID *)(UINTN)FixedPcdGet64 (PcdFrameBufferBase),
    (UINTN)FixedPcdGet32 (PcdFrameBufferStride) *
        FixedPcdGet32 (PcdFrameBufferHeight) *
        FixedPcdGet32 (PcdFrameBufferBpp)
    );
#endif

  //
  // Show the TianoCore logo, which stays up through the boot-timeout
  // countdown (see PlatformBootManagerWaitCallback) until the boot option
  // takes over the display.
  //
  BootLogoEnableLogo ();

  //
  // Pick up whatever real boot options exist on the platform first.
  //
  EfiBootManagerRefreshAllBootOption ();

  //
  // Register the built-in UEFI Shell as a persistent boot option, in the same
  // style as OVMF, so it always shows up in the UiApp Boot Manager. It is
  // appended at the end of BootOrder: BootOrder therefore stays
  // [real boot options..., UEFI Shell], real options are attempted first and
  // the Shell remains the fallback when none of them boots. The Boot Manager
  // Menu that BdsDxe auto-created in BootOrder carries LOAD_OPTION_HIDDEN
  // (BmRegisterBootManagerMenu), so neither the BDS boot loop nor the UiApp
  // Boot Manager sees it.
  //
  RegisterFvBootOption (&mShellFileGuid, L"UEFI Shell", (UINTN)-1);

  //
  // Register ESC as a hotkey that jumps into the Boot Manager Menu (UiApp)
  // during the boot timeout, in the same style as OVMF. The menu popup that
  // BdsDxe auto-creates is reachable this way; UiApp itself is never listed
  // in boot options.
  //
  // Note: BdsDxe only arms the hotkey service when PcdConInConnectOnDemand is
  // FALSE, which this platform guarantees. EfiBootManagerAddKeyOptionVariable
  // re-processes the key right away (BmHotkey.c), so the ESC key is already
  // live when the countdown shown in PlatformBootManagerWaitCallback starts.
  //
  {
    EFI_BOOT_MANAGER_LOAD_OPTION  BootManagerMenu;
    EFI_INPUT_KEY                 Esc;
    EFI_STATUS                    Status;

    Esc.ScanCode    = SCAN_ESC;
    Esc.UnicodeChar = CHAR_NULL;

    Status = EfiBootManagerGetBootManagerMenu (&BootManagerMenu);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "PlatformBds: cannot find Boot Manager Menu, ESC hotkey skipped (%r)\n",
        Status
        ));
    } else {
      Status = EfiBootManagerAddKeyOptionVariable (
                 NULL,
                 (UINT16)BootManagerMenu.OptionNumber,
                 0,
                 &Esc,
                 NULL
                 );
      ASSERT (Status == EFI_SUCCESS || Status == EFI_ALREADY_STARTED);
      DEBUG ((
        DEBUG_INFO,
        "PlatformBds: ESC -> Boot Manager Menu Boot%04x (%r)\n",
        BootManagerMenu.OptionNumber,
        Status
        ));
      EfiBootManagerFreeLoadOption (&BootManagerMenu);
    }
  }
}

/**
  Progress callback during the boot timeout. Renders the TianoCore logo
  progress bar at the bottom of the screen, in the same style as OVMF.

  This platform has no separate serial console: DEBUG output from BdsDxe and
  other drivers is rendered into the same framebuffer as text, and the log
  clears the whole panel whenever it wraps (see FrameBufferSerialPortLib's
  ScrollUp). The one-shot logo drawn in PlatformBootManagerAfterConsole is
  therefore overwritten before the timeout loop starts. The logo is redrawn
  here on every tick, so it stays visible for as long as the countdown does;
  the progress bar needs the same refresh for the same reason.
**/
VOID
EFIAPI
PlatformBootManagerWaitCallback (
  IN UINT16  TimeoutRemain
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION  Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION  White;
  UINT16                               TimeoutInitial;

  TimeoutInitial = PcdGet16 (PcdPlatformBootTimeOut);

  //
  // If PcdPlatformBootTimeOut is set to zero, then we consider
  // that no progress update should be enacted (since we'd only
  // ever display a one-shot progress of either 0% or 100%).
  //
  if (TimeoutInitial == 0) {
    return;
  }

  BootLogoEnableLogo ();

  Black.Raw = 0x00000000;
  White.Raw = 0x00FFFFFF;

  BootLogoUpdateProgress (
    White.Pixel,
    Black.Pixel,
    L"Press ESC for boot menu",
    White.Pixel,
    (TimeoutInitial - TimeoutRemain) * 100 / TimeoutInitial,
    0
    );
}

/**
  Called when no boot option could be started. Boot the Shell, never UiApp.
**/
VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  UINTN                         Index;
  UINTN                         ShellOption;

  DEBUG ((DEBUG_ERROR, "PlatformBds: unable to boot, entering the Shell\n"));

  ShellOption = RegisterFvBootOption (&mShellFileGuid, L"UEFI Shell", 0);
  if (ShellOption != LoadOptionNumberUnassigned) {
    BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);

    for (Index = 0; Index < BootOptionCount; Index++) {
      if (BootOptions[Index].OptionNumber == ShellOption) {
        for ( ; ;) {
          EfiBootManagerBoot (&BootOptions[Index]);
        }
      }
    }

    EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
  }

  DEBUG ((DEBUG_ERROR, "PlatformBds: Shell is not available, halting\n"));
  CpuDeadLoop ();
}
