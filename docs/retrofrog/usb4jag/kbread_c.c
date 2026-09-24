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
