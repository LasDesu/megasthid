#include "ps2async.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

static inline void ps2_irq_read( struct ps2 *ch )
{
	unsigned char data = digitalRead( ch->pin_data );

	if ( ch->bits == 0 ) /* start bit */
	{
		ch->shiftreg = 0;
		ch->parity = 1;
		if ( data != 0 )
			ch->bits = 11;
	}
	else if ( ch->bits <= 8 )  /* data */
	{
		ch->shiftreg >>= 1;
		if ( data )
		{
			ch->shiftreg |= 0x80;
			ch->parity ++;
		}
	}
	else if ( ch->bits == 9 )
	{
		/* parity bit */
		/* there is ability to check, but no real reason */
	}
	
	ch->bits ++;
	if ( ch->bits == 11 )  /* stop bit */
	{
		if ( (data != 0) )
		{
			if ( ch->wrptr + 1 != ch->rdptr )
			{
				ch->buffer[ch->wrptr ++] = ch->shiftreg;
				if ( ch->wrptr >= PS2_BUFSIZE )
					ch->wrptr = 0;
			}
		}

		ch->bits = 0;
	}
}

static inline void ps2_irq_write( struct ps2 *ch )
{
	if ( ch->bits < 8 )  /* data */
	{
		digitalWrite( ch->pin_data, ch->shiftreg & 1 );
		if ( ch->shiftreg & 1 )
			ch->parity ++;
		ch->shiftreg >>= 1;
	}
	else if ( ch->bits == 8 )
	{
		/* parity bit */
		digitalWrite( ch->pin_data, ch->parity & 1 );
	}
	else if ( ch->bits == 9 )
	{
		/* stop bit */
		//digitalWrite( ch->pin_data, HIGH );
		/* actually it should be output high, but prepare to input ACK bit */
		/* swithing IRQ polarity for this is a bit too much */
		pinMode( ch->pin_data, INPUT_PULLUP );
	}
	
	ch->bits ++;
	if ( ch->bits == 11 )  /* ACK bit */
	{
		/*unsigned char data = digitalRead(KBD_DATA);
		save ACK here*/    
		/* switch back to read mode */
		ch->mode = 0;
		ch->bits = 0;
	}
}

void ps2_interrupt( struct ps2 *ch )
{
	unsigned long ts;
	
	ts = millis();  
	if ( ts - ch->last_ts >= 250 )
	{
		/* timedout */
		if ( ch->mode )
		{
			/* seems like no answer */
			pinMode( ch->pin_data, INPUT_PULLUP );
			ch->mode = 0;
		}

		/* probably new packet */
		ch->bits = 0;
	}
	
	if ( ch->mode )
		ps2_irq_write( ch );
	else
		ps2_irq_read( ch );
		
	ch->last_ts = ts;
}

inline unsigned char ps2_available( struct ps2 *ch )
{
	char diff = ch->wrptr - ch->rdptr;
	return diff >= 0 ? diff : PS2_BUFSIZE + diff;
}

inline unsigned char ps2_wait( struct ps2 *ch, unsigned char num )
{
	unsigned long ts = millis();

	while ( ps2_available( ch ) < num )
	{
		if ( millis() - ts >= 500 )
			return 1;
	}
	return 0;
}

inline void ps2_clear( struct ps2 *ch )
{
	noInterrupts();
	ch->rdptr = ch->wrptr;
	interrupts();
}

short ps2_read( struct ps2 *ch )
{
	uint8_t ret;

	if ( ps2_wait( ch, 1 ) )
		return -1;

	noInterrupts();
	ret = ch->buffer[ch->rdptr ++];
	if ( ch->rdptr >= PS2_BUFSIZE )
		ch->rdptr = 0;
	interrupts();

	return ret;
}

char ps2_write( struct ps2 *ch, uint8_t data )
{
	unsigned long ts;

	/* wait for packet to be processed */
	ts = millis();
	while ( ch->bits )
	{
		if ( millis() - ts >= 250 )
		{
			/*if ( ch->bits < 12 )
				return -1;*/  /* timed out */
			break;  /* if previous packet was broken, just ignore it */
		}
	}

	/* clock should be high */
	if ( digitalRead( ch->pin_clock ) == 0 )
		return -1;

	/* start transmission */
	pinMode( ch->pin_clock, OUTPUT );
	digitalWrite( ch->pin_clock, LOW );
	
	ch->mode = 1;
	ch->shiftreg = data;
	ch->bits = 0;
	ch->parity = 1;
	
	delayMicroseconds( 100 );
	
	pinMode( ch->pin_data, OUTPUT );
	digitalWrite( ch->pin_data, LOW );

	ch->last_ts = millis();
	pinMode( ch->pin_clock, INPUT_PULLUP );

	return 0;
}

short ps2_send( struct ps2 *ch, uint8_t data )
{
	if ( ps2_write( ch, data ) )
		return -2;
	ps2_clear( ch );
	return ps2_read( ch );
}

void ps2_setup( struct ps2 *ch, unsigned char clock, unsigned char data )
{
	pinMode( clock, INPUT_PULLUP );
	pinMode( data, INPUT_PULLUP );
	
	ch->pin_clock = clock;
	ch->pin_data = data;

	ch->mode = 0;
	ch->bits = 0;
	ch->wrptr = ch->rdptr = 0;
}
