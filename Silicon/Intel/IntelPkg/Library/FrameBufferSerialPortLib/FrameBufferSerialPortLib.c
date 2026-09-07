/** @file
  SerialPortLib implementation that renders debug output into the fixed
  framebuffer of an Atom Z25xx (Clover Trail+) phone platform.

  The primary bootloader leaves the panel initialized and scanning out of
  PcdFrameBufferBase (0x3F000000, 544x960, 4 bytes per pixel). We therefore
  only need to draw glyphs; no display hardware programming is performed.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/SerialPortLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>

extern CONST UINT8 gFont8x16[95][16];

#define GLYPH_WIDTH   8
#define GLYPH_HEIGHT  16

#define FB_BASE    ((UINTN)FixedPcdGet64 (PcdFrameBufferBase))
#define FB_WIDTH   ((UINTN)FixedPcdGet32 (PcdFrameBufferWidth))
#define FB_HEIGHT  ((UINTN)FixedPcdGet32 (PcdFrameBufferHeight))
#define FB_BPP     ((UINTN)FixedPcdGet32 (PcdFrameBufferBpp))
//
// Scanout stride in pixels: the panel shows 540 columns but the display pipe
// advances 544 pixels (2176 bytes) per scanline, so row addressing must use
// the stride while clipping still uses FB_WIDTH.
//
#define FB_STRIDE  ((UINTN)FixedPcdGet32 (PcdFrameBufferStride))
#define FB_PITCH   (FB_STRIDE * FB_BPP)

#define TEXT_COLS  (FB_WIDTH / GLYPH_WIDTH)
#define TEXT_ROWS  (FB_HEIGHT / GLYPH_HEIGHT)

//
// Number of CpuPause() iterations inserted after every completed log line so
// the output is readable on the panel (there is no scrollback and no host
// serial port here). On a ~1.6 GHz Z2560 this is roughly a tenth of a second
// per line. Set to 0 for full-speed logging.
//
#define FB_LOG_LINE_DELAY_LOOPS  1000000

//
// Cursor state. This library is used from SEC/PEI (where globals live in the
// read-only firmware image) as well as from DXE. To stay functional in both
// phases the cursor is kept in a small writable scratch area at the very end
// of the framebuffer region, which is RAM and always writable.
//
typedef struct {
  UINT32    Signature;
  UINT32    Col;
  UINT32    Row;
} FB_CONSOLE_STATE;

#define FB_CONSOLE_SIGNATURE  SIGNATURE_32 ('C', 'T', 'F', 'B')

//
// The memory immediately after the visible framebuffer is NOT guaranteed to be
// writable RAM on this platform. When those writes are dropped, the signature
// never matches, so the cursor is reset to (0,0) for every single character:
// the screen gets cleared and no text is ever visible. Because this firmware is
// loaded into DRAM at PcdFdBaseAddress (SEC/PEI execute from RAM) and DXE
// modules are relocated into RAM, a plain writable global is valid in every
// phase.
//
//
// A module-local global is NOT usable here: every phase and every module gets
// its own private copy of it. SEC, PEI Core, each PEIM, the DXE Core and each
// DXE driver would therefore each start with Signature == 0, so their first
// DEBUG() call runs SerialPortInitialize()'s "first use" path again, resets the
// cursor to row 6 and re-clears the whole log area. That is exactly the
// "nothing appears after HandOffToDxeCore()" symptom: DXE Core and every driver
// after it wipe the log region and overwrite each other at row 6.
//
// Keep the cursor in one fixed scratch location shared by all phases. 0x020F0000
// sits inside the 0x01000000-0x02100000 bootloader-owned window that PlatformPei
// already covers with a reserved allocation HOB, above the SEC/PEI temporary RAM
// (0x02000000 + 0x40000) and below permanent PEI memory (0x02100000), so nothing
// else ever writes there and it is real, writable DRAM in every phase.
//
//
// RELOCATED from 0x020F0000: that low-DRAM page is scratch for the primary
// bootloader and is also allocated over by the Windows boot loader, which
// corrupted the shared cursor state on most boots. The new base is the top of
// the high DRAM island (0x379FE000..0x3EEFF000); PlatformPei reserves the
// PcdConsoleStateBase page + 4 KiB as EfiReservedMemoryType (console state),
// keeping this consumer in sync with the reservation.
//

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

  //
  // Do NOT scroll by copying the framebuffer onto itself. 0x3F000000 is an
  // uncached MMIO aperture of the display controller: CopyMem() READS it back
  // (rep movsd) and reads of this aperture do not return the written pixel
  // data on Clover Trail+. The result is that the moment enough output is
  // produced to reach the bottom text row - which happens immediately once the
  // DXE Core starts dumping HOBs/allocations - the whole panel gets filled with
  // noise. That is exactly the "screen full of garbage right after
  // HandOffToDxeCore()" symptom: DxeCore IS running fine, only the console
  // scroll is destroying the screen.
  //
  // Wrap instead of scrolling: clear the log area (write-only) and restart at
  // its first row. No framebuffer reads are ever performed.
  //
  FillRect (
    0,
    0,
    FB_WIDTH,
    FB_HEIGHT,
    FixedPcdGet32 (PcdFrameBufferLogBgColor)
    );

  //
  // PutChar() decrements Row once after each ScrollUp() call, so set 1 here
  // to land on row 0 - the log now owns the whole panel, there is no beacon
  // band to skip.
  //
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

  //
  // Clear the whole panel once, on first use. SEC no longer paints anything
  // into the framebuffer before calling in here, so there is no beacon band
  // to preserve - the log owns every row starting at 0.
  //
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

    //
    // Throttle the log so it can actually be read/photographed on the panel.
    // There is no dependable time source in every phase here (SEC runs before
    // the APIC timer is programmed and TimerLib must not be pulled into this
    // library), so use a bounded busy-wait after each completed line. Set
    // FB_LOG_LINE_DELAY_LOOPS to 0 to restore full-speed logging.
    //
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
