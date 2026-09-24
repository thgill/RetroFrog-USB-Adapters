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
