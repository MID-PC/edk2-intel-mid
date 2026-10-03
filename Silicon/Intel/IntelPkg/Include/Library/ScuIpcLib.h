/** @file
  Drive the SCU IPC-1 register block, the path to the PMIC and the south-complex
  power gates. Wire format unchanged from the kernel's intel_scu_ipc.c: the write
  buffer is a byte stream, and read-modify-write leaves the mask to the SCU.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SCU_IPC_LIB_H_
#define SCU_IPC_LIB_H_

#include <Uefi/UefiBaseType.h>

//
// Message IDs: bits 7:0 of the command register.
//
#define SCU_IPC_MSG_WATCHDOG_TIMER  0xF8  // set kernel watchdog threshold
#define SCU_IPC_MSG_WARM_RESET      0xF0
#define SCU_IPC_MSG_COLD_RESET      0xF1
#define SCU_IPC_MSG_PCNTRL          0xFF  // power controller register access

//
// Sub-commands: bits 15:12. Only the power controller defines any.
//
#define SCU_IPC_PCNTRL_WRITE        0     // register write
#define SCU_IPC_PCNTRL_READ         1     // register read
#define SCU_IPC_PCNTRL_UPDATE       2     // read-modify-write
#define SCU_IPC_WDT_SUB_STOP        1     // IPCMSG_WATCHDOG_TIMER: stop it

/**
  Issue a command that carries no data. The read buffer belongs to whatever ran
  last, so a caller wanting a value back needs ScuIpcPmicRead.

  @param[in] Cmd  Message ID, one of the SCU_IPC_MSG_* values.
  @param[in] Sub  Sub-command, bits 15:12 of the command word.
  @retval EFI_SUCCESS      The SCU completed the command.
  @retval EFI_NOT_FOUND    No SCU answers; nothing was written.
  @retval EFI_TIMEOUT      The SCU did not go idle.
  @retval EFI_DEVICE_ERROR The SCU reported an error.
**/
EFI_STATUS
EFIAPI
ScuIpcSimpleCommand (
  IN UINT32  Cmd,
  IN UINT32  Sub
  );

/**
  Read one 8-bit power controller register.

  @param[in]  Address  Register address.
  @param[out] Value    Register contents. Untouched on failure.
  @retval EFI_SUCCESS           The SCU completed the command.
  @retval EFI_NOT_FOUND         No SCU answers; nothing was written.
  @retval EFI_TIMEOUT           The SCU did not go idle.
  @retval EFI_DEVICE_ERROR      The SCU reported an error.
  @retval EFI_INVALID_PARAMETER Value is NULL.
**/
EFI_STATUS
EFIAPI
ScuIpcPmicRead (
  IN  UINT16  Address,
  OUT UINT8   *Value
  );

/**
  Write one 8-bit power controller register.

  @param[in] Address  Register address.
  @param[in] Value    Value to write.
  @retval EFI_SUCCESS      The SCU completed the command.
  @retval EFI_NOT_FOUND    No SCU answers; nothing was written.
  @retval EFI_TIMEOUT      The SCU did not go idle.
  @retval EFI_DEVICE_ERROR The SCU reported an error.
**/
EFI_STATUS
EFIAPI
ScuIpcPmicWrite (
  IN UINT16  Address,
  IN UINT8   Value
  );

/**
  Change the masked bits of one 8-bit power controller register. The SCU applies
  the mask itself, so two callers cannot lose an update to each other.

  @param[in] Address  Register address.
  @param[in] Bits     Values for the masked bits.
  @param[in] Mask     Bits to change; one to write, zero to leave alone.
  @retval EFI_SUCCESS      The SCU completed the command.
  @retval EFI_NOT_FOUND    No SCU answers; nothing was written.
  @retval EFI_TIMEOUT      The SCU did not go idle.
  @retval EFI_DEVICE_ERROR The SCU reported an error.
**/
EFI_STATUS
EFIAPI
ScuIpcPmicUpdate (
  IN UINT16  Address,
  IN UINT8   Bits,
  IN UINT8   Mask
  );

#endif // SCU_IPC_LIB_H_