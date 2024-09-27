#include "kbd_layout.h"

#include <avr/pgmspace.h>

static const unsigned char scan_set_main[] PROGMEM =
{
//		0				1				2				3				4				5				6				7
//		8				9				A				B				C				D				E				F
	PKEY_NONE,		PKEY_F9,		PKEY_NONE,		PKEY_F5,		PKEY_F3,		PKEY_F1,		PKEY_F2,		PKEY_F12,		// 00
	PKEY_NONE,		PKEY_F10,		PKEY_F8,		PKEY_F6,		PKEY_F4,		PKEY_TAB,		PKEY_TILDE,		PKEY_NONE,		// 08

	PKEY_NONE,		PKEY_LALT,		PKEY_LSHIFT,	PKEY_NONE,		PKEY_LCTRL,		PKEY_Q,			PKEY_1,			PKEY_NONE,		// 10
	PKEY_NONE,		PKEY_NONE,		PKEY_Z,			PKEY_S,			PKEY_A,			PKEY_W,			PKEY_2,			PKEY_NONE,		// 18

	PKEY_NONE,		PKEY_C,			PKEY_X,			PKEY_D,			PKEY_E,			PKEY_4,			PKEY_3,			PKEY_NONE,		// 20
	PKEY_NONE,		PKEY_SPACE,		PKEY_V,			PKEY_F,			PKEY_T,			PKEY_R,			PKEY_5,			PKEY_NONE,		// 28

	PKEY_NONE,		PKEY_N,			PKEY_B,			PKEY_H,			PKEY_G,			PKEY_Y,			PKEY_6,			PKEY_NONE,		// 30
	PKEY_NONE,		PKEY_NONE,		PKEY_M,			PKEY_J,			PKEY_U,			PKEY_7,			PKEY_8,			PKEY_NONE,		// 38

	PKEY_NONE,		PKEY_COMMA,		PKEY_K,			PKEY_I,			PKEY_O,			PKEY_0,			PKEY_9,			PKEY_NONE,		// 40
	PKEY_NONE,		PKEY_PERIOD,	PKEY_SLASH,		PKEY_L,			PKEY_SEMICOLON,	PKEY_P,			PKEY_MINUS,		PKEY_NONE,		// 48

	PKEY_NONE,		PKEY_NONE,		PKEY_QUOTE,		PKEY_NONE,		PKEY_LBRACE,	PKEY_EQUAL,		PKEY_NONE,		PKEY_NONE,		// 50
	PKEY_CAPSLOCK,	PKEY_RSHIFT,	PKEY_RETURN,	PKEY_RBRACE,	PKEY_NONE,		PKEY_BACKSLASH,	PKEY_NONE,		PKEY_NONE,		// 58

	PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_BACKSPACE,	PKEY_NONE,		// 60
	PKEY_NONE,		PKEY_KP_1,		PKEY_NONE,		PKEY_KP_4,		PKEY_KP_7,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		// 68

	PKEY_KP_0,		PKEY_KP_PERIOD,	PKEY_KP_2,		PKEY_KP_5,		PKEY_KP_6,		PKEY_KP_8,		PKEY_ESC,		PKEY_NUMLOCK,	// 70
	PKEY_F11,		PKEY_KP_PLUS,	PKEY_KP_3,		PKEY_KP_MINUS,	PKEY_KP_MUL,	PKEY_KP_9,		PKEY_SCRLOCK,	PKEY_NONE,		// 78

	PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_F7,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		// 80
};

static const unsigned char scan_set_extra_68[] PROGMEM =
{
	PKEY_NONE,		PKEY_END,		PKEY_NONE,		PKEY_LEFT,		PKEY_HOME,		PKEY_NONE,		PKEY_NONE,		PKEY_NONE,		// 68
	PKEY_INSERT,	PKEY_DELETE,	PKEY_DOWN,		PKEY_NONE,		PKEY_RIGHT,		PKEY_UP,		PKEY_NONE,		PKEY_NONE,		// 70
	PKEY_NONE,		PKEY_NONE,		PKEY_PAGEDOWN,	PKEY_NONE,		PKEY_PRTSRC,	PKEY_PAGEUP,	PKEY_NONE,		PKEY_NONE,		// 78
};

struct kbd_bind
{
	unsigned scan;
	unsigned char key;
};

static const struct kbd_bind scan_set_extra[] =
{
	{ 0xE014, PKEY_RCTRL },		// right control
	{ 0xE011, PKEY_RALT },		// right alt
	{ 0xE05A, PKEY_KP_ENTER },
	{ 0xE04A, PKEY_KP_DIV },
	//{ 0xE01F, PKEY_LWIN },		// left winkey
	//{ 0xE027, PKEY_RWIN },		// right winkey
	//{ 0xE02F, PKEY_MENU }
	{ 0, PKEY_NONE }
};

static char kbd_break = 0;
static unsigned scan_prefix = 0;

#ifdef ARDUINO
#include <Arduino.h>
#endif

char kbd_process( unsigned char scan )
{
	unsigned char key = STKEY_NONE;

	if ( scan == 0xF0 )
	{
		kbd_break = 1;
		return STKEY_NONE;
	}
	else if ( (scan & 0xFE) == 0xE0 )
	{
		//scan_prefix <<= 8;
		scan_prefix = scan;
		return STKEY_NONE;
	}

	if ( (scan_prefix == 0) && (scan < sizeof(scan_set_main)) )
	{
		key = pgm_read_byte( &scan_set_main[scan] );
	}
	else
	{
		if ( (scan_prefix == 0xE0) && (scan >= 0x68) && (scan < 0x80) )
		{
			key = pgm_read_byte( &scan_set_extra_68[scan - 0x68] );
		}
		else
		{
			const struct kbd_bind *b = scan_set_extra;
			unsigned pscan = scan | (scan_prefix << 8);

			while ( b->key != STKEY_NONE )
			{
				if ( b->scan == pscan )
				{
					key = b->key;
					break;
				}

				b ++;
			}
		}
	}

	if ( key != STKEY_NONE )
	{
		if ( kbd_break )
			key |= 0x80;
	}

	kbd_break = 0;
	scan_prefix = 0;

	return key;
}
