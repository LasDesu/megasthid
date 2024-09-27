/* fuck you, MCb, and fuck your GPL */
/* your code is absolutely awful and unusable */
/* and GPL is just pure evil virus */

#include "ps2async.h"
#include "ast_keys.h"

extern "C" char kbd_process( unsigned char scan );
#define kbd_process_reset() kbd_process( 0 )

static void ikbd_update_leds();

//#define DEBUG

#ifndef DEBUG
#define UART_BAUD  7812
#else
//#define UART_BAUD  115200
#define UART_BAUD  9600
#endif

#define LED			13

#define KBD_CLK		2
#define KBD_DATA	4
#define MOUSE_CLK	3
#define MOUSE_DATA	5

#define J0_UP		A0
#define J0_DOWN		A1
#define J0_LEFT		A2
#define J0_RIGHT	A3
#define J0_FIRE		A4

#define J1_UP		8
#define J1_DOWN		9
#define J1_LEFT		10
#define J1_RIGHT	11
#define J1_FIRE		A5

static inline void ikbd_send( uint8_t val )
{
	//digitalWrite( LED, LOW );
	digitalWrite( LED, HIGH );
#ifndef DEBUG
	Serial.write( val );
#else
	Serial.print( "-> " );
	Serial.println( val, HEX );
#endif
	//digitalWrite( LED, HIGH );
}

static inline uint8_t ikbd_read()
{
#ifndef DEBUG
	while ( Serial.available() == 0 )
		;
	return Serial.read();
#else
	static const uint8_t debug_data[] = { 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00 };
	static uint8_t cur;
	uint8_t ret = debug_data[cur ++];
	if ( cur >= sizeof(debug_data) )
		cur = 0;
	Serial.print( "<- " );
	Serial.println( ret, HEX );
	return ret;
#endif
}

static inline void ikbd_send_pat( uint8_t pat, unsigned char num )
{
	while ( num -- )
		ikbd_send( pat );
}

static inline void ikbd_key_click( uint8_t code )
{
	ikbd_send( code );
	ikbd_send( code | 0x80 );
}

struct ps2 ps2kbd;
struct ps2 ps2mouse;

void ps2interrupt_ch1()
{
	ps2_interrupt( &ps2kbd );
}

void ps2interrupt_ch2()
{
	ps2_interrupt( &ps2mouse );
}

#define MOUSE_ACT_PRESS		0x01
#define MOUSE_ACT_RELEASE	0x02
#define MOUSE_ACT_KEYS		0x04

#define MOUSE_Y_AT_BOTTOM	0x08

#define MOUSE_MODE_REL		0x00
#define MOUSE_MODE_ABS		0x10
#define MOUSE_MODE_KEY		0x20
#define MOUSE_DISABLE		0x80

#define MOUSE_MODE			0xB0

#define MEV_BUTL_PRESS		0x04
#define MEV_BUTL_RELEASE	0x08
#define MEV_BUTR_PRESS		0x01
#define MEV_BUTR_RELEASE	0x02

static char ikbd_pause;

static struct
{
	uint8_t mode;
	
	short accum_dx;
	short accum_dy;
	unsigned char but_state; 
	unsigned char but_events; 

	struct 
	{
		uint8_t thresh_x, thresh_y;
	} rel;
	struct 
	{
		uint16_t x, y;
		uint16_t max_x, max_y;
		uint8_t scale_x, scale_y;
	} abs;
	struct 
	{
		uint8_t delta_x, delta_y;
	} key;
} mouse;


#define KBD_LED_NUM		2
#define KBD_LED_CAPS	4
#define KBD_LED_SCROLL	1

static inline void kbd_set_leds( uint8_t val )
{
	if ( ps2_write( &ps2kbd, 0xED ) == 0 )
		ps2_write( &ps2kbd, val );
}

static char kbd_reset()
{	
	char ret = -1;

	ps2_clear( &ps2kbd );
	if ( ps2_send( &ps2kbd, 0xFF ) == 0xFA )
	{
		if ( ps2_read( &ps2kbd ) == 0xAA )
		{
#ifdef DEBUG
			Serial.println( "keyboard OK!" );
#endif
			ikbd_update_leds();
			ret = 0;
		}
	}

	kbd_process_reset();

	return ret;
}

static char mouse_phase;
static char mouse_reset()
{
	ps2_clear( &ps2mouse );
	if ( ps2_send( &ps2mouse, 0xFF ) == 0xFA )
	{
		if ( ps2_read( &ps2mouse ) == 0xAA )
		{
			ps2_read( &ps2mouse ); /* mouse ID */
			
			ps2_send( &ps2mouse, 0xF0 );  /* remote mode */

			ps2_send( &ps2mouse, 0xE8 );	/* set resolution */
			ps2_send( &ps2mouse, 0x03 );	/* 8 counts/mm */
			
			//ps2_send( &ps2mouse, 0xF3 );	/* set sample rate */
			//ps2_send( &ps2mouse, 40 );		/* 40 samples/sec */
			
			//ps2_send( &ps2mouse, 0xF4 );	/* enable data reporting */

			mouse_phase = 0;

#ifdef DEBUG
			Serial.println( "mouse OK!" );			
#endif
			return 0;
		}
	}

	return -1;
}

static void mouse_report_abs()
{
	ikbd_send( 0xF7 );
	ikbd_send( mouse.but_events );
	ikbd_send( mouse.abs.x >> 8 );
	ikbd_send( mouse.abs.x );
	ikbd_send( mouse.abs.y >> 8 );
	ikbd_send( mouse.abs.y );
	mouse.but_events = 0;
}

#define MSEND_DELTA(d) do { \
	if ( (d) > 127 ) { ikbd_send( 127 ); (d) -= 127; } \
	else if ( (d) < -128 ) { ikbd_send( -128 ); (d) -= -128; } \
	else { ikbd_send( (d) ); (d) = 0; } \
} while ( 0 )

#define MSEND_DELTA_INV(d) do { \
	if ( (d) > 128 ) { ikbd_send( -128 ); (d) -= 128; } \
	else if ( (d) < -127 ) { ikbd_send( 127 ); (d) -= -127; } \
	else { ikbd_send( -(d) ); (d) = 0; } \
} while ( 0 )

#define MCALC_ABS(axis,v) do { \
	if ( ((v) < 0) && (mouse.abs.axis < ((uint16_t)-(v))) ) \
		mouse.abs.axis = 0; \
	else if ( ((v) > 0) && (mouse.abs.max_ ##axis - mouse.abs.axis < ((uint16_t)(v))) ) \
		mouse.abs.axis = mouse.abs.max_ ##axis; \
	else \
		mouse.abs.axis += (v); \
} while ( 0 )

static void mouse_process( uint8_t data )
{
	static uint8_t buf[3];
	uint8_t buts;

	if ( mouse_phase < 1 )
		return;	/* invalid phase */

	buf[mouse_phase - 1] = data;
	if ( (++ mouse_phase) < 4 )
		return; /* still collecting data */

	mouse_phase = 0;

	mouse.accum_dx += (buf[0] & 0x10) ? buf[1] - 0x100 : buf[1];
	mouse.accum_dy += (buf[0] & 0x20) ? buf[2] - 0x100 : buf[2];

	/* save button events for interrogation */
	buts = (mouse.but_state ^ buf[0]) & 3;
	if ( buts & 1 )	/* left button */
		mouse.but_events |= (buf[0] & 1) ? MEV_BUTL_PRESS : MEV_BUTL_RELEASE;
	if ( buts & 2 )	/* right button */
		mouse.but_events |= (buf[0] & 2) ? MEV_BUTR_PRESS : MEV_BUTR_RELEASE;

	if ( ikbd_pause )
		goto out;

	if ( mouse.mode & MOUSE_ACT_KEYS )
	{
		if ( buts & 1 )	/* left button */
			ikbd_send( STKEY_MOUSE_L | ((buf[0] & 1) ? 0 : 0x80) );
		if ( buts & 2 )	/* right button */
			ikbd_send( STKEY_MOUSE_R | ((buf[0] & 2) ? 0 : 0x80) );
	}

	/* process movement */
	if ( (mouse.mode & MOUSE_MODE) == MOUSE_MODE_REL )
	{
		/* generate report for button events  */
		char forcebuts = buts && !(mouse.mode & MOUSE_ACT_KEYS);
		
		/* get buttons state in proper order */
		buts = ((buf[0] & 1) << 1) | ((buf[0] & 2) >> 1);

		/* break into packets if needed */
		while ( (abs(mouse.accum_dx) >= mouse.rel.thresh_x) ||
				(abs(mouse.accum_dy) >= mouse.rel.thresh_y) || forcebuts )
		{
			ikbd_send( 0xF8 | buts );
			MSEND_DELTA( mouse.accum_dx );
			if ( mouse.mode & MOUSE_Y_AT_BOTTOM )
				MSEND_DELTA( mouse.accum_dy );
			else
				MSEND_DELTA_INV( mouse.accum_dy );
			forcebuts = 0;
		}
	}
	else if ( (mouse.mode & MOUSE_MODE) == MOUSE_MODE_ABS )
	{
		if ( abs(mouse.accum_dx) >= mouse.abs.scale_x )
		{
			int16_t scaled = mouse.accum_dx / mouse.abs.scale_x;
			mouse.accum_dx -= scaled * mouse.abs.scale_x;
			MCALC_ABS(x, scaled);
		}
		if ( abs(mouse.accum_dy) >= mouse.abs.scale_y )
		{
			int16_t scaled = mouse.accum_dy / mouse.abs.scale_y;
			mouse.accum_dy -= scaled * mouse.abs.scale_y;
			if ( !(mouse.mode & MOUSE_Y_AT_BOTTOM) )
				scaled = -scaled;
			MCALC_ABS(y, scaled);
		}

		/* report coordinates if needed */
		if ( !(mouse.mode & MOUSE_ACT_KEYS) )
		{
			if ( ((mouse.mode & MOUSE_ACT_PRESS) &&
				  (mouse.but_events & (MEV_BUTL_PRESS | MEV_BUTR_PRESS))) ||
				 ((mouse.mode & MOUSE_ACT_RELEASE) &&
				  (mouse.but_events & (MEV_BUTL_RELEASE | MEV_BUTR_RELEASE))) )
				mouse_report_abs();
		}
	}
	else if ( (mouse.mode & MOUSE_MODE) == MOUSE_MODE_KEY )
	{
		unsigned char op;	/* for keeping track of operations */
		do
		{
			op = 0;
			if ( abs(mouse.accum_dx) >= mouse.key.delta_x )
			{
				const uint8_t key = mouse.accum_dx < 0 ? STKEY_LEFT : STKEY_RIGHT;
				ikbd_send( key );
				ikbd_send( key | 0x80 );
				if ( mouse.accum_dx < 0 )
					mouse.accum_dx += mouse.key.delta_x;
				else
					mouse.accum_dx -= mouse.key.delta_x;
				op ++;
			}
			if ( abs(mouse.accum_dy) >= mouse.key.delta_y )
			{
				const uint8_t key = mouse.accum_dy < 0 ? STKEY_DOWN : STKEY_UP;
				ikbd_send( key );
				ikbd_send( key | 0x80 );
				if ( mouse.accum_dy < 0 )
					mouse.accum_dy += mouse.key.delta_y;
				else
					mouse.accum_dy -= mouse.key.delta_y;
				op ++;
			}
		} while ( op );
	}

out:
	/* update current button state */
	mouse.but_state = buf[0] & 3;
}

#define JOY_MODE_EVENT		0
#define JOY_MODE_INTER		1
#define JOY_MODE_MONITOR	2
#define JOY_MODE_FIREMON	3
#define JOY_MODE_KEYCODE	4
#define JOY_DISABLE			8

static struct
{
	unsigned char mode;

	uint8_t state0, state1;

	uint8_t rate;
	unsigned long report_ts;
	
	uint8_t fire_mon;
	unsigned char samples;

	struct
	{
		uint8_t rx, ry;
		uint8_t tx, ty;
		uint8_t vx, vy;
		char phase_x, phase_y;
		unsigned long ts_x, ts_y;
	} key;
} joy;

static void joystick_process()
{
	uint8_t joy0, joy1;

	if ( joy.mode == JOY_MODE_FIREMON )
	{
		if ( millis() >= joy.report_ts )
		{
			joy.fire_mon >>= 1;
			joy.fire_mon |= digitalRead( J1_FIRE ) ? 0 : 0x80;
			/* move to next report time */
			joy.report_ts ++;

			if ( ++ joy.samples >= 8 )
			{
				joy.samples = 0;
				ikbd_send( joy.fire_mon );
			}
		}
		return;
	}

	joy0 = (~PINC) & 0xF;
	joy0 |= digitalRead( J0_FIRE ) ? 0 : 0x80;
	joy1 = (~PINB) & 0xF;
	joy1 |= digitalRead( J1_FIRE ) ? 0 : 0x80;

	if ( joy.mode == JOY_MODE_EVENT )
	{
		if ( joy0 != joy.state0 )
		{
			ikbd_send( 0xFE );
			ikbd_send( joy0 );
		}
		if ( joy1 != joy.state1 )
		{
			ikbd_send( 0xFF );
			ikbd_send( joy1 );
		}
	}
	else if ( joy.mode == JOY_MODE_MONITOR )
	{
		if ( millis() >= joy.report_ts )
		{
			ikbd_send( ((joy1 & 0x80) >> 7) | ((joy0 & 0x80) >> 6) );
			ikbd_send( (joy1 & 0xF) | ((joy0 & 0xF) << 4) );
			/* move to next report time */
			joy.report_ts += joy.rate * 10;	
		}
	}
	else if ( joy.mode == JOY_MODE_KEYCODE )
	{
		/* TODO */
#if 0
		if ( joy0 & 0x0C )
		{
			if ( joy.key.phase_x == 0 )
			{
				if ( (joy0 & 0x0C) == 0x04 )	/* left */
					joy.key.phase_x --;
				else if ( (joy0 & 0x0C) == 0x08 )	/* right */
					joy.key.phase_x ++;
			}
		}
		else
			joy.key.phase_x = 0;
#endif
	}

	joy.state0 = joy0;
	joy.state1 = joy1;
}

#define LEDS_PAUSE		(KBD_LED_NUM|KBD_LED_CAPS|KBD_LED_SCROLL)
#define LEDS_JOYMON		(KBD_LED_NUM)
#define LEDS_JOYFIRE	(KBD_LED_NUM|KBD_LED_CAPS)
#define LEDS_MOUSEABS	(KBD_LED_CAPS)
#define LEDS_NORMAL		(0)

static void ikbd_update_leds()
{
	if ( ikbd_pause )
		kbd_set_leds( LEDS_PAUSE );
	else if ( joy.mode == JOY_MODE_MONITOR )
		kbd_set_leds( LEDS_JOYMON );
	else if ( joy.mode == JOY_MODE_FIREMON )
		kbd_set_leds( LEDS_JOYFIRE );
	else if ( mouse.mode & MOUSE_MODE_ABS )
		kbd_set_leds( LEDS_MOUSEABS );
	else
		kbd_set_leds( LEDS_NORMAL );
}

static void ikbd_reset()
{
	digitalWrite( LED, HIGH );

#ifdef DEBUG
	Serial.println("IKBD RESET!");
#endif

	pinMode( J0_FIRE, INPUT_PULLUP );
	pinMode( J0_UP, INPUT_PULLUP );
	pinMode( J0_DOWN, INPUT_PULLUP );
	pinMode( J0_LEFT, INPUT_PULLUP );
	pinMode( J0_RIGHT, INPUT_PULLUP );

	pinMode( J1_FIRE, INPUT_PULLUP );
	pinMode( J1_UP, INPUT_PULLUP );
	pinMode( J1_DOWN, INPUT_PULLUP );
	pinMode( J1_LEFT, INPUT_PULLUP );
	pinMode( J1_RIGHT, INPUT_PULLUP );

	mouse_phase = -1;
	ikbd_pause = 0;

	kbd_reset();
	mouse_reset();

	/* reset mouse state */
	mouse.mode = MOUSE_MODE_REL;
	mouse.rel.thresh_x = 1;
	mouse.rel.thresh_y = 1;
	mouse.abs.scale_x = 1;
	mouse.abs.scale_y = 1;
	mouse.abs.max_x = 65535;
	mouse.abs.max_y = 65535;
	mouse.key.delta_x = 1;
	mouse.key.delta_y = 1;

	/* reset joytick state */
	joy.mode = JOY_DISABLE;
	/* read current state, but do nothing */
	joystick_process();
	joy.mode = JOY_MODE_EVENT;
	
	ikbd_send( 0xF0 + 0 /* version */ );

	digitalWrite( LED, LOW );
}


static void ikbd_process_cmd( uint8_t cmd )
{
	uint8_t val;

	if ( ikbd_pause )	/* restore previous mode */
		ikbd_update_leds();
	ikbd_pause = 0;
	switch ( cmd )
	{
		case 0x80:	/* reset */
			if ( ikbd_read() == 0x01 )
				ikbd_reset();
			break;

		case 0x13: /* pause output */
			ikbd_pause = 1;
			ikbd_update_leds();
			break;

		case 0x11: /* resume */
			/* do nothing */
			/* any command unpauses output */
			break;

		/* mouse */
		case 0x0D:	/* interrogate mouse position */
			mouse_report_abs();	/* not sure if this should be generated in other modes */
			break;

		case 0x07:	/* set mouse button action */
			val = ikbd_read();
			mouse.mode = (mouse.mode & ~0x07) | (val & 7);
			break;

		case 0x12:	/* disable mouse */
			mouse.mode |= MOUSE_DISABLE;
			ikbd_update_leds();
			break;

		case 0x08:	/* set relative mouse position reporting */
			mouse.mode = (mouse.mode & ~MOUSE_MODE) | MOUSE_MODE_REL;
			ikbd_update_leds();
			break;

		case 0x09:	/* set absolute mouse position reporting */
			mouse.mode = (mouse.mode & ~MOUSE_MODE) | MOUSE_MODE_ABS;
			mouse.abs.max_x = ikbd_read() << 8;
			mouse.abs.max_x |= ikbd_read();
			mouse.abs.max_y = ikbd_read() << 8;
			mouse.abs.max_y |= ikbd_read();
			mouse.abs.x = mouse.abs.y = 0;
			ikbd_update_leds();
			break;

		case 0x0A:	/* set mouse keycode mode */
			mouse.mode = (mouse.mode & ~MOUSE_MODE) | MOUSE_MODE_KEY;
			mouse.key.delta_x = ikbd_read();
			mouse.key.delta_y = ikbd_read();
			ikbd_update_leds();
			break;

		case 0x0B:	/* set mouse threshold */
			mouse.rel.thresh_x = ikbd_read();
			mouse.rel.thresh_y = ikbd_read();			
			break;

		case 0x0C:	/* set mouse scale */
			mouse.abs.scale_x = ikbd_read();
			mouse.abs.scale_y = ikbd_read();
			break;

		case 0x0F:	/* set Y=0 at bottom */
			mouse.mode |= MOUSE_Y_AT_BOTTOM;
			break;
		case 0x10:	/* set Y=0 at top */
			mouse.mode &= ~MOUSE_Y_AT_BOTTOM;
			break;

		case 0x0E:	/* set load mouse position  */
			mouse.abs.x = ikbd_read() << 8;
			mouse.abs.x |= ikbd_read();
			mouse.abs.y = ikbd_read() << 8;
			mouse.abs.y |= ikbd_read();
			break;

		/* joystick */
		case 0x16: /* joystick interrogation */
			ikbd_send( 0xFD );
			ikbd_send( joy.state0 );
			ikbd_send( joy.state1 );
			break;

		case 0x14: /* set joystick event reporting */
			joy.mode = JOY_MODE_EVENT;
			ikbd_update_leds();
			break;

		case 0x15: /* set joystick interrogating mode */
			joy.mode = JOY_MODE_INTER;
			ikbd_update_leds();
			break;

		case 0x17: /* set joystick monitoring */
			joy.mode = JOY_MODE_MONITOR;
			joy.rate = ikbd_read();
			joy.report_ts = millis() + joy.rate * 10;
			ikbd_update_leds();
			break;

		case 0x18: /* set fire button monitoring */
			joy.mode = JOY_MODE_FIREMON;
			joy.samples = 0;
			joy.report_ts = millis() + 1;
			ikbd_update_leds();
			break;

		case 0x19: /* set joystick keycode mode */
			joy.mode = JOY_MODE_KEYCODE;
			joy.key.rx = ikbd_read();
			joy.key.ry = ikbd_read();
			joy.key.tx = ikbd_read();
			joy.key.ty = ikbd_read();
			joy.key.vx = ikbd_read();
			joy.key.vy = ikbd_read();
			joy.key.phase_x = 0;
			joy.key.phase_y = 0;
			break;

		case 0x1A: /* disable joysticks */
			joy.mode |= JOY_DISABLE;
			ikbd_update_leds();
			break;

		/* status inquiries */
		case 0x87:	/* mouse button action */
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( 0x07 );
			ikbd_send( mouse.mode & 7 );
			ikbd_send_pat( 0, 8 - 3 ); /* padding */
			break;

		case 0x88:	/* mouse button action */
		case 0x89:
		case 0x8A:
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( 0x08 | ((mouse.mode >> 4) & 3) );
			switch ( mouse.mode & 0x30 )
			{
				case MOUSE_MODE_ABS:
					ikbd_send( mouse.abs.max_x >> 8 );
					ikbd_send( mouse.abs.max_x );
					ikbd_send( mouse.abs.max_y >> 8 );
					ikbd_send( mouse.abs.max_y );
					ikbd_send_pat( 0, 8 - 6 ); /* padding */
					break;
				case MOUSE_MODE_KEY:
					ikbd_send( mouse.key.delta_x );
					ikbd_send( mouse.key.delta_y );
					ikbd_send_pat( 0, 8 - 4 ); /* padding */
					break;
				default:
					ikbd_send_pat( 0, 8 - 2 ); /* padding */
			}
			break;
			
		case 0x8B: /* mouse threshold */
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( 0x0B );
			ikbd_send( mouse.rel.thresh_x );
			ikbd_send( mouse.rel.thresh_y );
			ikbd_send_pat( 0, 8 - 4 ); /* padding */
			break;

		case 0x8C: /* mouse scale */
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( 0x0C );
			ikbd_send( mouse.abs.scale_x );
			ikbd_send( mouse.abs.scale_y );
			ikbd_send_pat( 0, 8 - 4 ); /* padding */
			break;

		case 0x8F: /* mouse vertical coordinates */
		case 0x90:
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( (mouse.mode & MOUSE_Y_AT_BOTTOM) ? 0x0F : 0x10 );
			ikbd_send_pat( 0, 8 - 2 ); /* padding */
			break;

		case 0x92: /* mouse enable/disable */
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( (mouse.mode & MOUSE_DISABLE) ? 0x12 : 0x00 );
			ikbd_send_pat( 0, 8 - 2 ); /* padding */
			break;

		case 0x94: /* joystick mode */
		case 0x95:
		case 0x99:
			ikbd_send( 0xF6 ); /* status response header */
			switch ( joy.mode & 7 )
			{
				case JOY_MODE_EVENT:
					ikbd_send( 0x14 );
					ikbd_send_pat( 0, 8 - 2 ); /* padding */
					break;
				case JOY_MODE_INTER:
					ikbd_send( 0x15 );
					ikbd_send_pat( 0, 8 - 2 ); /* padding */
					break;
				case JOY_MODE_KEYCODE:
					ikbd_send( 0x19 );
					ikbd_send( joy.key.rx );
					ikbd_send( joy.key.ry );
					ikbd_send( joy.key.tx );
					ikbd_send( joy.key.ty );
					ikbd_send( joy.key.vx );
					ikbd_send( joy.key.vy );
					break;
				/* in other modes output is fully occupied by monitoring */
			}
			break;

		case 0x9A: /* joystick enable/disable */
			ikbd_send( 0xF6 ); /* status response header */
			ikbd_send( (mouse.mode & JOY_DISABLE) ? 0x1A : 0x00 );
			ikbd_send_pat( 0, 8 - 2 ); /* padding */
			break;
	}
}

void setup()
{
	// put your setup code here, to run once:
	pinMode( LED, OUTPUT );
	digitalWrite( LED, HIGH );

	//interrupts();

	Serial.begin( UART_BAUD );
 
	ps2_setup( &ps2kbd, KBD_CLK, KBD_DATA );
	attachInterrupt( 0, ps2interrupt_ch1, FALLING );

	ps2_setup( &ps2mouse, MOUSE_CLK, MOUSE_DATA );
	attachInterrupt( 1, ps2interrupt_ch2, FALLING );

	delay( 500 );
	digitalWrite( LED, LOW );

#ifdef DEBUG
	Serial.print( "RESET!" );
#endif

	ikbd_reset();
}

void loop()
{
	unsigned long ts = millis();
	static unsigned long mouse_last_ts = millis();

	digitalWrite( LED, LOW );
	
	/* host */
#ifdef DEBUG
	static unsigned long last_ts = millis();
	
	if ( ts - last_ts >= 1000 )
	{Serial.print( mouse_phase, DEC );Serial.print( "f " );
		ikbd_process_cmd( ikbd_read() );
		last_ts = ts;
	}
#else
	if ( Serial.available() > 0 )
		ikbd_process_cmd( ikbd_read() );
#endif

	/* joystick */
	joystick_process();
	if ( (joy.mode == JOY_MODE_MONITOR) || (joy.mode == JOY_MODE_FIREMON) )
		return;

	/* mouse */
	if ( mouse_phase == 0 )
	{
		/* request data */
		if ( ps2_send( &ps2mouse, 0xEB ) == 0xFA )
		{
			mouse_phase ++;
			mouse_last_ts = millis();
		}
	}
	else if ( ps2_available( &ps2mouse ) )
	{
		uint8_t data = ps2_read( &ps2mouse );
#ifdef DEBUG
		Serial.print( data, HEX );
		Serial.print( "m " );
#endif
		if ( (data == 0xAA) && (ts - mouse_last_ts > 500))
		{
			/* seems like "BAT successful" */
			mouse_phase = -1;
			mouse_reset();
		}
		else
		{
			if ( mouse_phase == -2 )
				mouse_phase = 1; /* resent data */
			mouse_process( data );
			mouse_last_ts = millis();
		}
	}
	else if ( (mouse_phase > 0) && ((ts - mouse_last_ts) > 200) )
	{
		mouse_phase = -2;
		/* haven't got data in a while, try resending */
		if ( ps2_write( &ps2mouse, 0xFE ) == 0 )
		{
			ps2_clear( &ps2mouse );
			mouse_last_ts = millis();
#ifdef DEBUG
			Serial.println( "\nmouse resend!" );
#endif
		}
	}

	/* keyboard */
	if ( ps2_available( &ps2kbd ) )
	{
		uint8_t data = ps2_read( &ps2kbd );
#ifdef DEBUG
		Serial.print( data, HEX );
		Serial.print( "k " );
#endif
		if ( data == 0xAA )
		{
			/* seems like "BAT success" */
			kbd_reset();
		}
		else
		{
			data = kbd_process( data );
			if ( data && !ikbd_pause )
				ikbd_send( data );
		}
	}
}
