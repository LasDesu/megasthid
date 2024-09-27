#ifndef PS2ASYNC_H
#define PS2ASYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_BUFSIZE 64

struct ps2 
{
	unsigned char pin_data;
	unsigned char pin_clock;

	uint8_t shiftreg;
	uint8_t parity;
	unsigned char bits;
	unsigned long last_ts;
	unsigned char mode;

	uint8_t buffer[PS2_BUFSIZE];
	volatile unsigned char wrptr, rdptr;
};

void ps2_interrupt( struct ps2 *ch );
unsigned char ps2_available( struct ps2 *ch );
unsigned char ps2_wait( struct ps2 *ch, unsigned char num );
void ps2_clear( struct ps2 *ch );
short ps2_read( struct ps2 *ch );
char ps2_write( struct ps2 *ch, uint8_t data );
short ps2_send( struct ps2 *ch, uint8_t data );
void ps2_setup( struct ps2 *ch, unsigned char clk_pin, unsigned char data_pin );

#ifdef __cplusplus
}
#endif

#endif
