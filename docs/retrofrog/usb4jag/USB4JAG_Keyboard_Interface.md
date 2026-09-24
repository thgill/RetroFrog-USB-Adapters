# USB4JAG Keyboard Interface

*Retro Frog · Atari Jaguar programmer's note*

How to read a USB keyboard through the USB4JAG adapter from your own Jaguar program. The adapter uses the same protocol as JagNote2, so software written for one works with the other.

- **You receive ASCII.** One byte per key press. The adapter handles Shift, Caps Lock and the US layout.
- **Keys are queued.** The adapter buffers up to 31 keys, so a slow poll loses nothing.
- **Repeat is built in.** A held key repeats after 500 ms, then every 80 ms.

## Setup

- Plug USB4JAG into **controller port 1**. The reference code below reads port 1, the same as JagNote2.
- Plug a USB keyboard into the adapter. The adapter switches to keyboard mode by itself and its LED turns cyan.
- In keyboard mode the normal joypad rows report nothing pressed, so the port looks like an idle standard pad to any code that isn't keyboard-aware.

## Reading one key

Every access is a write to `JOYSTICK` ($F14000) followed by a short wait and a read. The low nibble selects the row code on J3–J0 for port 1. Keep bit 15 (output enable) and bit 8 (audio enable) set, as you would for any pad read, and leave the port 2 nibble at `$F`.

| Step | Write | Code | Purpose | What to read |
|---|---|---|---|---|
| 1 | `$81F4` | 0100 | Status | `JOYBUTS` bit 0 (B0). Low = a key is waiting. High = no key: write `$81FF` and stop. |
| 2 | `$81F5` | 0101 | Data low | Key bits 0–5 from B0, B1, J11, J10, J9, J8. |
| 3 | `$81F6` | 0110 | Data high | Key bits 6–7 from B0, B1. |
| 4 | `$81F8` | 1000 | Acknowledge | Nothing. Selecting this code tells the adapter the key was taken, and it moves to the next one. |
| – | `$81FF` | 1111 | Idle | Write this when you are done. |

Always do steps 1 to 4 in order. The adapter shows the same key until it sees the acknowledge, so skipping step 4 returns that key again on the next poll.

### Bit mapping

All lines are active low: a line reading 0 means that key bit is 1. With `move.l JOYSTICK,d1` the high word holds JOYSTICK and the low word holds JOYBUTS, which puts the lines at the long-word bit positions shown.

| Key bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Read with code | 0101 | 0101 | 0101 | 0101 | 0101 | 0101 | 0110 | 0110 |
| Line | B0 | B1 | J11 | J10 | J9 | J8 | B0 | B1 |
| Register bit | JOYBUTS 0 | JOYBUTS 1 | JOYSTICK 11 | JOYSTICK 10 | JOYSTICK 9 | JOYSTICK 8 | JOYBUTS 0 | JOYBUTS 1 |
| Long-word bit | 0 | 1 | 27 | 26 | 25 | 24 | 0 | 1 |

## Key codes

| Byte | Key | Notes |
|---|---|---|
| `$20`–`$7E` | Printable ASCII | Letters, digits, punctuation, space. Keypad digits and `/ * - + .` send their characters. |
| `$0D` | Enter, keypad Enter | |
| `$08` | Backspace | |
| `$09` | Tab | JagNote2 ignores it. |
| `$1B` | Esc | JagNote2 ignores it. |
| `$7F` | Delete | JagNote2 ignores it. |
| `$0E` | Cursor up | JagNote2's cursor codes, not standard ASCII. |
| `$0F` | Cursor down | 〃 |
| `$10` | Cursor left | 〃 |
| `$11` | Cursor right | 〃 |

There are no key-up events, raw scancodes or modifier states. Caps Lock and Shift only change the character sent. A printable key pressed with Ctrl, Alt or the Windows key sends nothing. Function keys and other keys not listed send nothing.

## Reference code

Both versions return the key byte, or −1 when no key is waiting. Call them once per frame, or in a loop until they return −1 to take everything queued. They wait about 15 µs after each write. The adapter answers in well under a microsecond, so a shorter wait works too.

### 68000 assembly

```asm
; ---------------------------------------------------------------------------
; USB4JAG keyboard reader (port 1) — 68000
; kb_read:  out d0.w = key byte (0-255), or -1 if no key is waiting
;           preserves all other registers
; ---------------------------------------------------------------------------
JOYSTICK    equ     $F14000         ; write: row code / read: J8-J15
JOYBUTS     equ     $F14002         ; read: B0-B3

kb_read:
        movem.l d1-d2/a0,-(sp)
        lea     JOYSTICK,a0

        move.w  #$81F4,(a0)         ; code 0100 = STATUS
        bsr     kb_wait
        move.w  2(a0),d1            ; JOYBUTS
        btst    #0,d1               ; B0 low = key waiting
        bne     .none

        move.w  #$81F5,(a0)         ; code 0101 = DATA LOW (key bits 0-5)
        bsr     kb_wait
        move.l  (a0),d1             ; high word = JOYSTICK, low word = JOYBUTS
        not.l   d1                  ; lines are active low
        move.w  d1,d0
        andi.w  #$0003,d0           ; B0,B1 -> bits 0,1
        btst    #27,d1              ; J11 -> bit 2
        beq.s   .b3
        bset    #2,d0
.b3:    btst    #26,d1              ; J10 -> bit 3
        beq.s   .b4
        bset    #3,d0
.b4:    btst    #25,d1              ; J9  -> bit 4
        beq.s   .b5
        bset    #4,d0
.b5:    btst    #24,d1              ; J8  -> bit 5
        beq.s   .hi
        bset    #5,d0

.hi:    move.w  #$81F6,(a0)         ; code 0110 = DATA HIGH (key bits 6-7)
        bsr     kb_wait
        move.w  2(a0),d1
        not.w   d1
        andi.w  #$0003,d1           ; B0,B1 -> bits 6,7
        lsl.w   #6,d1
        or.w    d1,d0

        move.w  #$81F8,(a0)         ; code 1000 = ACK, adapter drops this key
        bsr     kb_wait
        bra.s   .done

.none:  moveq   #-1,d0
.done:  move.w  #$81FF,(a0)         ; idle (no row selected)
        movem.l (sp)+,d1-d2/a0
        rts

kb_wait:                            ; ~15 us on a 13.3 MHz 68000
        move.w  #19,d2
.lp:    dbra    d2,.lp
        rts
```

### C

```c
#define JOYSTICK (*(volatile unsigned short *)0xF14000)  /* write: row code, read: J8-J15 */
#define JOYBUTS  (*(volatile unsigned short *)0xF14002)  /* read: B0-B3 */

static void kb_wait(void)            /* a few microseconds is plenty */
{
    volatile int i;
    for (i = 0; i < 20; i++) ;
}

/* Poll the USB4JAG keyboard on port 1.
   Returns the key byte (0-255), or -1 if no key is waiting. */
int kb_read(void)
{
    unsigned short j, b;
    int key;

    JOYSTICK = 0x81F4;                   /* 0100 STATUS */
    kb_wait();
    if (JOYBUTS & 1) {                   /* B0 high = no key */
        JOYSTICK = 0x81FF;
        return -1;
    }

    JOYSTICK = 0x81F5;                   /* 0101 DATA LOW: key bits 0-5 */
    kb_wait();
    j = ~JOYSTICK;                       /* lines are active low */
    b = ~JOYBUTS;
    key  =  b & 3;                       /* B0, B1 -> bits 0, 1 */
    key |= ((j >> 11) & 1) << 2;         /* J11    -> bit 2 */
    key |= ((j >> 10) & 1) << 3;         /* J10    -> bit 3 */
    key |= ((j >>  9) & 1) << 4;         /* J9     -> bit 4 */
    key |= ((j >>  8) & 1) << 5;         /* J8     -> bit 5 */

    JOYSTICK = 0x81F6;                   /* 0110 DATA HIGH: key bits 6-7 */
    kb_wait();
    key |= (~JOYBUTS & 3) << 6;          /* B0, B1 -> bits 6, 7 */

    JOYSTICK = 0x81F8;                   /* 1000 ACK: adapter drops this key */
    kb_wait();
    JOYSTICK = 0x81FF;                   /* idle */
    return key;
}
```

## Things to watch for

- **Don't interleave port reads.** If a VBL or other interrupt also writes `JOYSTICK` to read pads, it can land between steps 1 and 4. Read the keyboard from the same place you read your pads, or mask interrupts around `kb_read`.
- **There is no presence check.** "No key waiting" and "no adapter" look the same. The adapter does not report Atari's keyboard/mouse controller ID.
- **A standard joypad in port 1 can look like a key.** Code 0100 selects several joypad rows at once, and holding Pause pulls B0 low, so status reports a key and the data reads are garbage. Only read the keyboard when the user has chosen keyboard input, or when you expect USB4JAG in port 1.
- **Avoid the Team Tap codes on the keyboard port.** Codes 0100, 0101, 0110 and 1000 are also 4-player adapter socket addresses. Scanning for a Team Tap on port 1 selects code 1000, which acknowledges and throws away a waiting key.
- **Port 2 is untested.** The adapter doesn't care which port it is in, but port 2 uses the mirrored bit order described in the Jaguar Technical Reference, and this has not been tried.

---

Both reference routines were checked against the USB4JAG keyboard firmware in simulation: the 68000 version assembled and run on an emulated 68000, the C version on the host. JagNote2 was confirmed on real hardware. The protocol was worked out from JagNote2 and is not an official Atari specification.
