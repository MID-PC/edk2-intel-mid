/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

//
// A library instance is private to the module that links it, so two modules can
// be inside a command at once and nothing here can prevent it. That is safe
// because a command runs to completion without yielding and every caller's TPL is
// high enough that none can preempt another. An SMM driver, or a DXE entry that
// drops to a blocking TPL, breaks that argument.
//

#include <Library/ScuIpcLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MtrrLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

//
// IPC-1 register block, relative to PcdScuIpcBase.
//
#define SCU_IPC_COMMAND_OFFSET    0x00
#define SCU_IPC_STATUS_OFFSET     0x04
#define SCU_IPC_SPTR_OFFSET       0x08
#define SCU_IPC_DPTR_OFFSET       0x0C
#define SCU_IPC_WRITE_BUFFER      0x80
#define SCU_IPC_READ_BUFFER       0x90

#define SCU_IPC_STATUS_BUSY       BIT0
#define SCU_IPC_STATUS_ERROR      BIT1
#define SCU_IPC_STATUS_ERRCODE(S) (((S) >> 16) & 0xFF)

// The block is 0x100 bytes, but an MTRR cannot describe less than a page, so the
// page is what gets programmed. Both PcdScuIpcBase values are page-aligned.
#define SCU_IPC_APERTURE_SIZE     SIZE_4KB

// Poll budget, matching intel_scu_ipc_check_status(): three million reads with a
// microsecond between them, so about three seconds. The earlier copies in this
// tree spun on CpuPause, which retires in nanoseconds, and gave up thousands of
// times sooner.
#define SCU_IPC_POLL_LIMIT        3000000U

STATIC BOOLEAN  mScuIpcPrepared = FALSE;

/**
  Make the register block uncacheable, once.

  The block sits inside the one identity-mapped write-back MTRR window that
  VirtualMemory.c builds, so every access to it is cached. The SCU updates the status
  register by read-modify-write in ways a cached read can miss, and writes the read
  buffer we then read, so a stale line is a wrong PMIC value rather than a wrong
  status bit.

  Not a library constructor: we are linked from PEI, which has no boot services and
  no way to tell. Deferring to the first command also means the linking module has
  finished its own setup by then.

  The status is not checked against later use: a failure leaves the block cached,
  which is the bug this exists to fix, and the cause is not transient.
**/
STATIC
VOID
ScuIpcPrepare (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mScuIpcPrepared) {
    return;
  }

  mScuIpcPrepared = TRUE;

  Status = MtrrSetMemoryAttribute (
             (UINTN)FixedPcdGet32 (PcdScuIpcBase),
             SCU_IPC_APERTURE_SIZE,
             CacheUncacheable
             );
  DEBUG ((
    DEBUG_INFO,
    "ScuIpcLib: aperture 0x%lx %a\n",
    FixedPcdGet32 (PcdScuIpcBase),
    EFI_ERROR (Status) ? "left cached, reads may be stale" : "made uncacheable"
    ));
}

/**
  Wait for the SCU to go idle, and report the outcome.

  @param[in] Command  Command being waited on, for the debug log only.

  @retval EFI_SUCCESS      Idle, no error.
  @retval EFI_NOT_FOUND    No SCU answers.
  @retval EFI_TIMEOUT      Still busy after the poll budget.
  @retval EFI_DEVICE_ERROR The SCU reported an error.
**/
STATIC
EFI_STATUS
ScuIpcWaitIdle (
  IN UINT32  Command
  )
{
  UINTN       Ipc;
  UINT32      Retry;
  UINT32      Status;

  Ipc    = (UINTN)FixedPcdGet32 (PcdScuIpcBase);
  Status = MmioRead32 (Ipc + SCU_IPC_STATUS_OFFSET);

  // An unmapped block reads back as all ones, which also sets every status bit
  // below. Distinguishing it here lets a caller tell "no SCU on this board" from
  // "the SCU said no".
  if (Status == MAX_UINT32) {
    return EFI_NOT_FOUND;
  }

  Retry = SCU_IPC_POLL_LIMIT;
  while ((Status & SCU_IPC_STATUS_BUSY) != 0) {
    MicroSecondDelay (1);
    if (--Retry == 0) {
      break;
    }

    Status = MmioRead32 (Ipc + SCU_IPC_STATUS_OFFSET);
  }

  if ((Status & SCU_IPC_STATUS_BUSY) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "ScuIpcLib: timed out, status 0x%08x, command 0x%04x\n",
      Status,
      Command
      ));
    return EFI_TIMEOUT;
  }

  if ((Status & SCU_IPC_STATUS_ERROR) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "ScuIpcLib: SCU error %u, status 0x%08x, command 0x%04x\n",
      SCU_IPC_STATUS_ERRCODE (Status),
      Status,
      Command
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ScuIpcSimpleCommand (
  IN UINT32  Cmd,
  IN UINT32  Sub
  )
{
  EFI_STATUS  Status;
  UINTN       Ipc;
  UINT32      Command;

  ScuIpcPrepare ();

  Ipc     = (UINTN)FixedPcdGet32 (PcdScuIpcBase);
  Command = (Sub << 12) | (Cmd & 0xFF);

  Status = ScuIpcWaitIdle (Command);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Deliberately no buffer writes: a simple command does not use them, and neither
  // does the kernel.
  MmioWrite32 (Ipc + SCU_IPC_COMMAND_OFFSET, Command);

  return ScuIpcWaitIdle (Command);
}

/**
  Run one power controller register access.

  @param[in]  Sub     SCU_IPC_PCNTRL_* sub-command.
  @param[in]  InLen   Valid bytes in InWord.
  @param[in]  InWord  Write buffer contents.
  @param[out] OutWord First dword of the read buffer. OPTIONAL.

  @retval EFI_SUCCESS      The SCU completed the command.
  @retval EFI_NOT_FOUND    No SCU answers; nothing was written.
  @retval EFI_TIMEOUT      The SCU did not go idle.
  @retval EFI_DEVICE_ERROR The SCU reported an error.
**/
STATIC
EFI_STATUS
ScuIpcCommand (
  IN  UINT32  Sub,
  IN  UINT32  InLen,
  IN  UINT32  InWord,
  OUT UINT32  *OutWord  OPTIONAL
  )
{
  EFI_STATUS  Status;
  UINTN       Ipc;
  UINT32      Command;

  ScuIpcPrepare ();

  Ipc     = (UINTN)FixedPcdGet32 (PcdScuIpcBase);
  Command = (InLen << 16) | (Sub << 12) | SCU_IPC_MSG_PCNTRL;

  Status = ScuIpcWaitIdle (Command);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // The SRAM controller has no byte-granular write, so the command goes out as one
  // dword and the SCU uses InLen to know how much of it is real. SPTR and DPTR are
  // offsets into the buffers; nothing here needs non-zero, and a stale value from
  // a previous command would point the SCU at the wrong place.
  MmioWrite32 (Ipc + SCU_IPC_DPTR_OFFSET, 0);
  MmioWrite32 (Ipc + SCU_IPC_SPTR_OFFSET, 0);
  MmioWrite32 (Ipc + SCU_IPC_WRITE_BUFFER, InWord);
  MmioWrite32 (Ipc + SCU_IPC_COMMAND_OFFSET, Command);

  Status = ScuIpcWaitIdle (Command);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (OutWord != NULL) {
    *OutWord = MmioRead32 (Ipc + SCU_IPC_READ_BUFFER);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ScuIpcPmicRead (
  IN  UINT16  Address,
  OUT UINT8   *Value
  )
{
  EFI_STATUS  Status;
  UINT32      Out;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = ScuIpcCommand (SCU_IPC_PCNTRL_READ, 2, (UINT32)Address, &Out);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Value = (UINT8)Out;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ScuIpcPmicWrite (
  IN UINT16  Address,
  IN UINT8   Value
  )
{
  return ScuIpcCommand (
           SCU_IPC_PCNTRL_WRITE,
           3,
           (UINT32)Address | ((UINT32)Value << 16),
           NULL
           );
}

EFI_STATUS
EFIAPI
ScuIpcPmicUpdate (
  IN UINT16  Address,
  IN UINT8   Bits,
  IN UINT8   Mask
  )
{
  return ScuIpcCommand (
           SCU_IPC_PCNTRL_UPDATE,
           4,
           (UINT32)Address | ((UINT32)Bits << 16) | ((UINT32)Mask << 24),
           NULL
           );
}