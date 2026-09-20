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
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Guid/ConsoleInDevice.h>
#include <Guid/EventGroup.h>

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
  // Signal EndOfDxe before connecting anything
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  //
  // Connect every driver so the fixed GOP and any storage show up
  //
  EfiBootManagerConnectAll ();

  //
  // Make the fixed-mode GOP the console output device
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
  // sure ConSplitterDxe adopts it
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
        // path; skip it
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
  Check whether a device path contains a USB messaging node.

  A USB device (fixed or removable) is identified by a USB, USB Class or
  USB WWID messaging device path node anywhere in its device path.

  @param  DevicePath  The device path to inspect.

  @retval TRUE   The device path contains a USB node.
  @retval FALSE  Otherwise (including a NULL device path).
**/
STATIC
BOOLEAN
IsUsbDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  if (DevicePath == NULL) {
    return FALSE;
  }

  while (!IsDevicePathEnd (DevicePath)) {
    if ((DevicePathType (DevicePath) == MESSAGING_DEVICE_PATH) &&
        ((DevicePathSubType (DevicePath) == MSG_USB_DP) ||
         (DevicePathSubType (DevicePath) == MSG_USB_CLASS_DP) ||
         (DevicePathSubType (DevicePath) == MSG_USB_WWID_DP))) {
      return TRUE;
    }

    DevicePath = NextDevicePathNode (DevicePath);
  }

  return FALSE;
}

/**
  SORT_COMPARE callback that puts every USB boot option ahead of all others.

  @param  Buffer1  Pointer to the first EFI_BOOT_MANAGER_LOAD_OPTION.
  @param  Buffer2  Pointer to the second EFI_BOOT_MANAGER_LOAD_OPTION.

  @retval <0  Buffer1 is on USB and Buffer2 is not.
  @retval >0  Buffer2 is on USB and Buffer1 is not.
  @retval 0   Both options are on the same side of the USB boundary.
**/
STATIC
INTN
EFIAPI
CompareUsbBootOption (
  IN CONST VOID  *Buffer1,
  IN CONST VOID  *Buffer2
  )
{
  CONST EFI_BOOT_MANAGER_LOAD_OPTION  *Option1;
  CONST EFI_BOOT_MANAGER_LOAD_OPTION  *Option2;

  Option1 = (CONST EFI_BOOT_MANAGER_LOAD_OPTION *)Buffer1;
  Option2 = (CONST EFI_BOOT_MANAGER_LOAD_OPTION *)Buffer2;

  if (IsUsbDevicePath (Option1->FilePath)) {
    return IsUsbDevicePath (Option2->FilePath) ? 0 : -1;
  }

  return IsUsbDevicePath (Option2->FilePath) ? 1 : 0;
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
  // Clear the panel in RELEASE builds
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
  // LogoDxe: draw logo
  //
  BootLogoEnableLogo ();

  //
  // Pick up whatever real boot options exist on the platform first
  //
  EfiBootManagerRefreshAllBootOption ();

  //
  // Force USB boot options above every other one
  //
  EfiBootManagerSortLoadOptionVariable (LoadOptionTypeBoot, (SORT_COMPARE)CompareUsbBootOption);

  //
  // Register the built-in UEFI Shell as a persistent fallback boot option
  //
  RegisterFvBootOption (&gUefiShellFileGuid, L"UEFI Shell", (UINTN)-1);

  //
  // Register ESC as a hotkey that jumps into the UEFI Setup (UiApp)
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
  Progress callback during the boot timeout
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
  if (TimeoutInitial == 0) {
    return;
  }

  //
  // LogoDxe: draw logo
  //
  BootLogoEnableLogo ();

  Black.Raw = 0x00000000;
  White.Raw = 0x00FFFFFF;

  BootLogoUpdateProgress (
    White.Pixel,
    Black.Pixel,
    L"[ESC] - Boot Menu",
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

  ShellOption = RegisterFvBootOption (&gUefiShellFileGuid, L"UEFI Shell", 0);
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
