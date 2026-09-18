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
#define FB_BPP     ((UINTN)FixedPcdGet32 (PcdFrameBufferBpp))
#define FB_STRIDE  ((UINTN)FixedPcdGet32 (PcdFrameBufferStride))
#define FB_PITCH   (FB_STRIDE * FB_BPP)

#define TEXT_COLS  (FB_WIDTH / GLYPH_WIDTH)
#define TEXT_ROWS  (FB_HEIGHT / GLYPH_HEIGHT)

#define FB_LOG_LINE_DELAY_LOOPS  1000000

// Cursor state
// Reserved as PcdConsoleStateBase, because it runs in every UEFI phase
typedef struct {
  UINT32    Signature;
  UINT32    Col;
  UINT32    Row;
} FB_CONSOLE_STATE;

#define FB_CONSOLE_SIGNATURE  SIGNATURE_32 ('C', 'T', 'F', 'B')

STATIC
FB_CONSOLE_STATE *
GetConsoleState (
  VOID
  )
{
  FB_CONSOLE_STATE  *State;

  State = (FB_CONSOLE_STATE *)(UINTN)FixedPcdGet32 (PcdConsoleStateBase);

  if (State->Signature != FB_CONSOLE_SIGNATURE) {
    State->Signature = FB_CONSOLE_SIGNATURE;
    State->Col       = 0;
    State->Row       = 0;
  }

  return State;
}

STATIC
VOID
FillRect (
  IN UINTN   X,
  IN UINTN   Y,
  IN UINTN   Width,
  IN UINTN   Height,
  IN UINT32  Color
  )
{
  UINTN   Row;
  UINTN   Col;
  UINT32  *Line;

  for (Row = 0; Row < Height; Row++) {
    if ((Y + Row) >= FB_HEIGHT) {
      break;
    }

    Line = (UINT32 *)(FB_BASE + (Y + Row) * FB_PITCH);
    for (Col = 0; Col < Width; Col++) {
      if ((X + Col) >= FB_WIDTH) {
        break;
      }

      Line[X + Col] = Color;
    }
  }
}

STATIC
VOID
ScrollUp (
  VOID
  )
{
  FB_CONSOLE_STATE  *State;

  // Fill the log area (write-only) and restart at its first row
  FillRect (
    0,
    0,
    FB_WIDTH,
    FB_HEIGHT,
    FixedPcdGet32 (PcdFrameBufferLogBgColor)
    );

  // PutChar() decrements Row once after each ScrollUp() call, so set 1 here
  // to land on row 0 - the log owns the entire framebuffer now
  State            = (FB_CONSOLE_STATE *)(UINTN)FixedPcdGet32 (PcdConsoleStateBase);
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
  UINTN        X;
  UINTN        Y;
  UINTN        Line;
  UINTN        Bit;
  UINT32       *Pixels;
  UINT32       Fg;
  UINT32       Bg;

  if ((Char < 0x20) || ((UINT8)Char > 0x7E)) {
    Char = '?';
  }

  Glyph = gFont8x16[(UINT8)Char - 0x20];
  Fg    = FixedPcdGet32 (PcdFrameBufferLogFgColor);
  Bg    = FixedPcdGet32 (PcdFrameBufferLogBgColor);

  X = Col * GLYPH_WIDTH;
  Y = Row * GLYPH_HEIGHT;

  for (Line = 0; Line < GLYPH_HEIGHT; Line++) {
    if ((Y + Line) >= FB_HEIGHT) {
      return;
    }

    Pixels = (UINT32 *)(FB_BASE + (Y + Line) * FB_PITCH);
    for (Bit = 0; Bit < GLYPH_WIDTH; Bit++) {
      if ((X + Bit) >= FB_WIDTH) {
        break;
      }

      Pixels[X + Bit] = ((Glyph[Line] & (0x80 >> Bit)) != 0) ? Fg : Bg;
    }
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
    FillRect (
      0,
      0,
      FB_WIDTH,
      FB_HEIGHT,
      FixedPcdGet32 (PcdFrameBufferLogBgColor)
      );
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

    // Busy-wait throttle so the log can actually be read on the panel; there
    // is no dependable time source in every phase (SEC runs before the APIC
    // timer is programmed, and TimerLib must not be pulled into this
    // library). Set FB_LOG_LINE_DELAY_LOOPS to 0 for full speed.
    if ((CHAR8)Buffer[Index] == '\n') {
      volatile UINT32  Spin;

      for (Spin = 0; Spin < FB_LOG_LINE_DELAY_LOOPS; Spin++) {
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
