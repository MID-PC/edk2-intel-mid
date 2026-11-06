/** @file
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/SerialPortLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>

#include "Font8x16.h"

extern CONST UINT8 gFont8x16[95][16];

#define GLYPH_WIDTH   8
#define GLYPH_HEIGHT  16

#define FB_BASE    ((UINTN)FixedPcdGet64 (PcdFrameBufferBase))
#define FB_WIDTH   ((UINTN)FixedPcdGet32 (PcdFrameBufferWidth))
#define FB_HEIGHT  ((UINTN)FixedPcdGet32 (PcdFrameBufferHeight))
#define FB_BPP     ((UINTN)FixedPcdGet8 (PcdFrameBufferColorDepth) / 8)
#define FB_STRIDE  ((UINTN)FixedPcdGet32 (PcdFrameBufferStride))
#define FB_PITCH   (FB_STRIDE * FB_BPP)
#define FB_STATE_OFFSET  ALIGN_VALUE (FB_STRIDE * FB_HEIGHT * FB_BPP, EFI_PAGE_SIZE)

#define TEXT_COLS  (FB_WIDTH / GLYPH_WIDTH)
#define TEXT_ROWS  (FB_HEIGHT / GLYPH_HEIGHT)

// Cursor state
// Kept past the visible framebuffer so every UEFI phase shares it
typedef struct {
  UINT32    Signature;
  UINT32    Col;
  UINT32    Row;
} FB_CONSOLE_STATE;

#define FB_CONSOLE_SIGNATURE  SIGNATURE_32 ('C', 'T', 'F', 'B')

STATIC FB_CONSOLE_STATE  *mConsoleState;

STATIC
FB_CONSOLE_STATE *
GetConsoleState (
  VOID
  )
{
  if (mConsoleState == NULL) {
    mConsoleState = (FB_CONSOLE_STATE *)(FB_BASE + FB_STATE_OFFSET);

    if (mConsoleState->Signature != FB_CONSOLE_SIGNATURE) {
      mConsoleState->Signature = FB_CONSOLE_SIGNATURE;
      mConsoleState->Col       = 0;
      mConsoleState->Row       = 0;
    }
  }

  return mConsoleState;
}

STATIC
VOID
ClearScreen (
  VOID
  )
{
  UINT64  Fill;
  UINTN   Size;

  Fill  = (UINT64)FixedPcdGet32 (PcdFrameBufferLogBgColor);
  Fill |= Fill << 32;
  Size  = FB_PITCH * FB_HEIGHT;

  SetMem64 ((VOID *)FB_BASE, Size & ~(UINTN)7, Fill);
  if ((Size & 7) != 0) {
    SetMem32 (
      (VOID *)(FB_BASE + (Size & ~(UINTN)7)),
      Size & 7,
      (UINT32)Fill
      );
  }
}

STATIC
VOID
ScrollUp (
  VOID
  )
{
  FB_CONSOLE_STATE  *State;

  // Clear the log area (write-only) and restart at its first row
  ClearScreen ();

  // PutChar() decrements Row once after each ScrollUp() call, so set 1 here
  // to land on row 0 - the log owns the entire framebuffer now
  State            = GetConsoleState ();
  State->Signature = FB_CONSOLE_SIGNATURE;
  State->Col       = 0;
  State->Row       = 1;
}

STATIC
VOID
DrawGlyph (
  IN UINTN  Col,
  IN UINTN  Row,
  IN CHAR8  Char
  )
{
  CONST UINT8  *Glyph;
  UINTN        Line;
  UINT32       *Pixels;
  UINT8        Bits;
  UINT32       Fg;
  UINT32       Bg;

  if ((Col >= TEXT_COLS) || (Row >= TEXT_ROWS)) {
    return;
  }

  if ((Char < 0x20) || ((UINT8)Char > 0x7E)) {
    Char = '?';
  }

  Glyph  = gFont8x16[(UINT8)Char - 0x20];
  Fg     = FixedPcdGet32 (PcdFrameBufferLogFgColor);
  Bg     = FixedPcdGet32 (PcdFrameBufferLogBgColor);
  Pixels = (UINT32 *)(FB_BASE + Row * GLYPH_HEIGHT * FB_PITCH) + Col * GLYPH_WIDTH;

  for (Line = 0; Line < GLYPH_HEIGHT; Line++) {
    Bits = Glyph[Line];

    Pixels[0] = (Bits & 0x80) ? Fg : Bg;
    Pixels[1] = (Bits & 0x40) ? Fg : Bg;
    Pixels[2] = (Bits & 0x20) ? Fg : Bg;
    Pixels[3] = (Bits & 0x10) ? Fg : Bg;
    Pixels[4] = (Bits & 0x08) ? Fg : Bg;
    Pixels[5] = (Bits & 0x04) ? Fg : Bg;
    Pixels[6] = (Bits & 0x02) ? Fg : Bg;
    Pixels[7] = (Bits & 0x01) ? Fg : Bg;

    Pixels = (UINT32 *)((UINT8 *)Pixels + FB_PITCH);
  }
}

STATIC
VOID
PutChar (
  IN CHAR8  Char
  )
{
  FB_CONSOLE_STATE  *State;

  State = GetConsoleState ();

  switch (Char) {
    case '\r':
      State->Col = 0;
      return;

    case '\n':
      State->Col = 0;
      State->Row++;
      break;

    case '\t':
      State->Col = (State->Col + 8) & ~(UINT32)7;
      break;

    case '\b':
      if (State->Col > 0) {
        State->Col--;
        DrawGlyph (State->Col, State->Row, ' ');
      }

      return;

    default:
      DrawGlyph (State->Col, State->Row, Char);
      State->Col++;
      break;
  }

  if (State->Col >= TEXT_COLS) {
    State->Col = 0;
    State->Row++;
  }

  while (State->Row >= TEXT_ROWS) {
    ScrollUp ();
    State->Row--;
  }
}

/**
  Initialize the framebuffer console.
**/
RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  FB_CONSOLE_STATE  *State;

  State = GetConsoleState ();

  // Clear the framebuffer before logging anything
  if ((State->Col == 0) && (State->Row == 0)) {
    ClearScreen ();
  }

  return RETURN_SUCCESS;
}

/**
  Write data to the framebuffer console.
**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINTN  Index;

  if (Buffer == NULL) {
    return 0;
  }

  for (Index = 0; Index < NumberOfBytes; Index++) {
    PutChar ((CHAR8)Buffer[Index]);

    // Arificial print delay between each log line, controlled by PcdFrameBufferDelay
    if ((CHAR8)Buffer[Index] == '\n') {
      volatile UINT32  Spin;

      for (Spin = 0; Spin < FixedPcdGet32 (PcdFrameBufferDelay); Spin++) {
        CpuPause ();
      }
    }
  }

  return NumberOfBytes;
}

/**
  Read is not supported by a write-only framebuffer console.
**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  if (Control == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  *Control = EFI_SERIAL_OUTPUT_BUFFER_EMPTY | EFI_SERIAL_INPUT_BUFFER_EMPTY;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_SUCCESS;
}
