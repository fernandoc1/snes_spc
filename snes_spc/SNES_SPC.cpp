// Core SPC emulation: CPU, timers, SMP registers, memory

// snes_spc 0.9.0. http://www.slack.net/~ant/

#include "SNES_SPC.h"

#include <string.h>

/* Copyright (C) 2004-2007 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

static inline void dprintf( const char*, ... ) { }

#define RAM         (m.ram.ram)
#define REGS        (m.smp_regs [0])
#define REGS_IN     (m.smp_regs [1])

// (n ? n : 256)
#define IF_0_THEN_256( n ) ((uint8_t) ((n) - 1) + 1)

// Note: SPC_MORE_ACCURACY exists mainly so I can run my validation tests, which
// do crazy echo buffer accesses.
#ifndef SPC_MORE_ACCURACY
	#define SPC_MORE_ACCURACY 0
#endif

#ifdef BLARGG_ENABLE_OPTIMIZER
	#include BLARGG_ENABLE_OPTIMIZER
#endif


//// Timers

#if SPC_DISABLE_TEMPO
	#define TIMER_DIV( t, n ) ((n) >> t->prescaler)
	#define TIMER_MUL( t, n ) ((n) << t->prescaler)
#else
	#define TIMER_DIV( t, n ) ((n) / t->prescaler)
	#define TIMER_MUL( t, n ) ((n) * t->prescaler)
#endif

SNES_SPC::Timer* SNES_SPC::run_timer_( Timer* t, rel_time_t time )
{
	int elapsed = TIMER_DIV( t, time - t->next_time ) + 1;
	t->next_time += TIMER_MUL( t, elapsed );

	if ( t->enabled )
	{
		int remain = IF_0_THEN_256( t->period - t->divider );
		int divider = t->divider + elapsed;
		int over = elapsed - remain;
		if ( over >= 0 )
		{
			int n = over / t->period;
			t->counter = (t->counter + 1 + n) & 0x0F;
			divider = over - n * t->period;
		}
		t->divider = (uint8_t) divider;
	}
	return t;
}

inline SNES_SPC::Timer* SNES_SPC::run_timer( Timer* t, rel_time_t time )
{
	if ( time >= t->next_time )
		t = run_timer_( t, time );
	return t;
}


//// ROM

void SNES_SPC::enable_rom( int enable )
{
	if ( m.rom_enabled != enable )
	{
		m.rom_enabled = enable;
		if ( enable )
			memcpy( m.hi_ram, &RAM [rom_addr], sizeof m.hi_ram );
		memcpy( &RAM [rom_addr], (enable ? m.rom : m.hi_ram), rom_size );
		// TODO: ROM can still get overwritten when DSP writes to echo buffer
	}
}


//// DSP

#if SPC_LESS_ACCURATE
	int const max_reg_time = 29;

	signed char const SNES_SPC::reg_times_ [256] =
	{
		 -1,  0,-11,-10,-15,-11, -2, -2,  4,  3, 14, 14, 26, 26, 14, 22,
		  2,  3,  0,  1,-12,  0,  1,  1,  7,  6, 14, 14, 27, 14, 14, 23,
		  5,  6,  3,  4, -1,  3,  4,  4, 10,  9, 14, 14, 26, -5, 14, 23,
		  8,  9,  6,  7,  2,  6,  7,  7, 13, 12, 14, 14, 27, -4, 14, 24,
		 11, 12,  9, 10,  5,  9, 10, 10, 16, 15, 14, 14, -2, -4, 14, 24,
		 14, 15, 12, 13,  8, 12, 13, 13, 19, 18, 14, 14, -2,-36, 14, 24,
		 17, 18, 15, 16, 11, 15, 16, 16, 22, 21, 14, 14, 28, -3, 14, 25,
		 20, 21, 18, 19, 14, 18, 19, 19, 25, 24, 14, 14, 14, 29, 14, 25,

		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
		 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
	};

	#define RUN_DSP( time, offset ) \
		int count = (time) - (offset) - m.dsp_time;\
		if ( count >= 0 )\
		{\
			int clock_count = (count & ~(clocks_per_sample - 1)) + clocks_per_sample;\
			m.dsp_time += clock_count;\
			dsp.run( clock_count );\
		}
#else
	#define RUN_DSP( time, offset ) \
		{\
			int count = (time) - m.dsp_time;\
			if ( !SPC_MORE_ACCURACY || count )\
			{\
				assert( count > 0 );\
				m.dsp_time = (time);\
				dsp.run( count );\
			}\
		}
#endif

int SNES_SPC::dsp_read( rel_time_t time )
{
	RUN_DSP( time, reg_times [REGS [r_dspaddr] & 0x7F] );

	int result = dsp.read( REGS [r_dspaddr] & 0x7F );

	#ifdef SPC_DSP_READ_HOOK
		SPC_DSP_READ_HOOK( spc_time + time, (REGS [r_dspaddr] & 0x7F), result );
	#endif

	return result;
}

inline void SNES_SPC::dsp_write( int data, rel_time_t time )
{
	RUN_DSP( time, reg_times [REGS [r_dspaddr]] )
	#if SPC_LESS_ACCURATE
		else if ( m.dsp_time == skipping_time )
		{
			int r = REGS [r_dspaddr];
			if ( r == SPC_DSP::r_kon )
				m.skipped_kon |= data & ~dsp.read( SPC_DSP::r_koff );

			if ( r == SPC_DSP::r_koff )
			{
				m.skipped_koff |= data;
				m.skipped_kon &= ~data;
			}
		}
	#endif

	#ifdef SPC_DSP_WRITE_HOOK
		SPC_DSP_WRITE_HOOK( m.spc_time + time, REGS [r_dspaddr], (uint8_t) data );
	#endif

	if ( REGS [r_dspaddr] <= 0x7F )
		dsp.write( REGS [r_dspaddr], data );
	else if ( !SPC_MORE_ACCURACY )
		dprintf( "SPC wrote to DSP register > $7F\n" );
}


//// Memory access extras

#if SPC_MORE_ACCURACY
	#define MEM_ACCESS( time, addr ) \
	{\
		if ( time >= m.dsp_time )\
		{\
			RUN_DSP( time, max_reg_time );\
		}\
	}
#elif !defined (NDEBUG)
	// Debug-only check for read/write within echo buffer, since this might result in
	// inaccurate emulation due to the DSP not being caught up to the present.

	bool SNES_SPC::check_echo_access( int addr )
	{
		if ( !(dsp.read( SPC_DSP::r_flg ) & 0x20) )
		{
			int start = 0x100 * dsp.read( SPC_DSP::r_esa );
			int size  = 0x800 * (dsp.read( SPC_DSP::r_edl ) & 0x0F);
			int end   = start + (size ? size : 4);
			if ( start <= addr && addr < end )
			{
				if ( !m.echo_accessed )
				{
					m.echo_accessed = 1;
					return true;
				}
			}
		}
		return false;
	}

	//#define MEM_ACCESS( time, addr ) check( !check_echo_access( (uint16_t) addr ) );
#else
	#define MEM_ACCESS( time, addr )
#endif


//// CPU write

#if SPC_MORE_ACCURACY
static unsigned char const glitch_probs [3] [256] =
{
	0xC3,0x92,0x5B,0x1C,0xD1,0x92,0x5B,0x1C,0xDB,0x9C,0x72,0x18,0xCD,0x5C,0x38,0x0B,
	0xE1,0x9C,0x74,0x17,0xCF,0x75,0x45,0x0C,0xCF,0x6E,0x4A,0x0D,0xA3,0x3A,0x1D,0x08,
	0xDB,0xA0,0x82,0x19,0xD9,0x73,0x3C,0x0E,0xCB,0x76,0x52,0x0B,0xA5,0x46,0x1D,0x09,
	0xDA,0x74,0x55,0x0F,0xA2,0x3F,0x21,0x05,0x9A,0x40,0x20,0x07,0x63,0x1E,0x10,0x01,
	0xDF,0xA9,0x85,0x1D,0xD3,0x84,0x4B,0x0E,0xCF,0x6F,0x49,0x0F,0xB3,0x48,0x1E,0x05,
	0xD8,0x77,0x52,0x12,0xB7,0x49,0x23,0x06,0xAA,0x45,0x28,0x07,0x7D,0x28,0x0F,0x07,
	0xCC,0x7B,0x4A,0x0E,0xB2,0x4F,0x24,0x07,0xAD,0x43,0x2C,0x06,0x86,0x29,0x11,0x07,
	0xAE,0x48,0x1F,0x0A,0x76,0x21,0x19,0x05,0x76,0x21,0x14,0x05,0x44,0x11,0x0B,0x01,
	0xE7,0xAD,0x96,0x23,0xDC,0x86,0x59,0x0E,0xDC,0x7C,0x5F,0x15,0xBB,0x53,0x2E,0x09,
	0xD6,0x7C,0x4A,0x16,0xBB,0x4A,0x25,0x08,0xB3,0x4F,0x28,0x0B,0x8E,0x23,0x15,0x08,
	0xCF,0x7F,0x57,0x11,0xB5,0x4A,0x23,0x0A,0xAA,0x42,0x28,0x05,0x7D,0x22,0x12,0x03,
	0xA6,0x49,0x28,0x09,0x82,0x2B,0x0D,0x04,0x7A,0x20,0x0F,0x04,0x3D,0x0F,0x09,0x03,
	0xD1,0x7C,0x4C,0x0F,0xAF,0x4E,0x21,0x09,0xA8,0x46,0x2A,0x07,0x85,0x1F,0x0E,0x07,
	0xA6,0x3F,0x26,0x07,0x7C,0x24,0x14,0x07,0x78,0x22,0x16,0x04,0x46,0x12,0x0A,0x02,
	0xA6,0x41,0x2C,0x0A,0x7E,0x28,0x11,0x05,0x73,0x1B,0x14,0x05,0x3D,0x11,0x0A,0x02,
	0x70,0x22,0x17,0x05,0x48,0x13,0x08,0x03,0x3C,0x07,0x0D,0x07,0x26,0x07,0x06,0x01,

	0xE0,0x9F,0xDA,0x7C,0x4F,0x18,0x28,0x0D,0xE9,0x9F,0xDA,0x7C,0x4F,0x18,0x1F,0x07,
	0xE6,0x97,0xD8,0x72,0x64,0x13,0x26,0x09,0xDC,0x67,0xA9,0x38,0x21,0x07,0x15,0x06,
	0xE9,0x91,0xD2,0x6B,0x63,0x14,0x2B,0x0E,0xD6,0x61,0xB7,0x41,0x2B,0x0E,0x10,0x09,
	0xCF,0x59,0xB0,0x2F,0x35,0x08,0x0F,0x07,0xB6,0x30,0x7A,0x21,0x17,0x07,0x09,0x03,
	0xE7,0xA3,0xE5,0x6B,0x65,0x1F,0x34,0x09,0xD8,0x6B,0xBE,0x45,0x27,0x07,0x10,0x07,
	0xDA,0x54,0xB1,0x39,0x2E,0x0E,0x17,0x08,0xA9,0x3C,0x86,0x22,0x16,0x06,0x07,0x03,
	0xD4,0x51,0xBC,0x3D,0x38,0x0A,0x13,0x06,0xB2,0x37,0x79,0x1C,0x17,0x05,0x0E,0x06,
	0xA7,0x31,0x74,0x1C,0x11,0x06,0x0C,0x02,0x6D,0x1A,0x38,0x10,0x0B,0x05,0x06,0x03,
	0xEB,0x9A,0xE1,0x7A,0x6F,0x13,0x34,0x0E,0xE6,0x75,0xC5,0x45,0x3E,0x0B,0x1A,0x05,
	0xD8,0x63,0xC1,0x40,0x3C,0x1B,0x19,0x06,0xB3,0x42,0x83,0x29,0x18,0x0A,0x08,0x04,
	0xD4,0x58,0xBA,0x43,0x3F,0x0A,0x1F,0x09,0xB1,0x33,0x8A,0x1F,0x1F,0x06,0x0D,0x05,
	0xAF,0x3C,0x7A,0x1F,0x16,0x08,0x0A,0x01,0x72,0x1B,0x52,0x0D,0x0B,0x09,0x06,0x01,
	0xCF,0x63,0xB7,0x47,0x40,0x10,0x14,0x06,0xC0,0x41,0x96,0x20,0x1C,0x09,0x10,0x05,
	0xA6,0x35,0x82,0x1A,0x20,0x0C,0x0E,0x04,0x80,0x1F,0x53,0x0F,0x0B,0x02,0x06,0x01,
	0xA6,0x31,0x81,0x1B,0x1D,0x01,0x08,0x08,0x7B,0x20,0x4D,0x19,0x0E,0x05,0x07,0x03,
	0x6B,0x17,0x49,0x07,0x0E,0x03,0x0A,0x05,0x37,0x0B,0x1F,0x06,0x04,0x02,0x07,0x01,

	0xF0,0xD6,0xED,0xAD,0xEC,0xB1,0xEB,0x79,0xAC,0x22,0x47,0x1E,0x6E,0x1B,0x32,0x0A,
	0xF0,0xD6,0xEA,0xA4,0xED,0xC4,0xDE,0x82,0x98,0x1F,0x50,0x13,0x52,0x15,0x2A,0x0A,
	0xF1,0xD1,0xEB,0xA2,0xEB,0xB7,0xD8,0x69,0xA2,0x1F,0x5B,0x18,0x55,0x18,0x2C,0x0A,
	0xED,0xB5,0xDE,0x7E,0xE6,0x85,0xD3,0x59,0x59,0x0F,0x2C,0x09,0x24,0x07,0x15,0x09,
	0xF1,0xD6,0xEA,0xA0,0xEC,0xBB,0xDA,0x77,0xA9,0x23,0x58,0x14,0x5D,0x12,0x2F,0x09,
	0xF1,0xC1,0xE3,0x86,0xE4,0x87,0xD2,0x4E,0x68,0x15,0x26,0x0B,0x27,0x09,0x15,0x02,
	0xEE,0xA6,0xE0,0x5C,0xE0,0x77,0xC3,0x41,0x67,0x1B,0x3C,0x07,0x2A,0x06,0x19,0x07,
	0xE4,0x75,0xC6,0x43,0xCC,0x50,0x95,0x23,0x35,0x09,0x14,0x04,0x15,0x05,0x0B,0x04,
	0xEE,0xD6,0xED,0xAD,0xEC,0xB1,0xEB,0x79,0xAC,0x22,0x56,0x14,0x5A,0x12,0x26,0x0A,
	0xEE,0xBB,0xE7,0x7E,0xE9,0x8D,0xCB,0x49,0x67,0x11,0x34,0x07,0x2B,0x0B,0x14,0x07,
	0xED,0xA7,0xE5,0x76,0xE3,0x7E,0xC4,0x4B,0x77,0x14,0x34,0x08,0x27,0x07,0x14,0x04,
	0xE7,0x8B,0xD2,0x4C,0xCA,0x56,0x9E,0x31,0x36,0x0C,0x11,0x07,0x14,0x04,0x0A,0x02,
	0xF0,0x9B,0xEA,0x6F,0xE5,0x81,0xC4,0x43,0x74,0x10,0x30,0x0B,0x2D,0x08,0x1B,0x06,
	0xE6,0x83,0xCA,0x48,0xD9,0x56,0xA7,0x23,0x3B,0x09,0x12,0x09,0x15,0x07,0x0A,0x03,
	0xE5,0x5F,0xCB,0x3C,0xCF,0x48,0x91,0x22,0x31,0x0A,0x17,0x08,0x15,0x04,0x0D,0x02,
	0xD1,0x43,0x91,0x20,0xA9,0x2D,0x54,0x12,0x17,0x07,0x09,0x02,0x0C,0x04,0x05,0x03,
};
#endif

// divided into multiple functions to keep rarely-used functionality separate
// so often-used functionality can be optimized better by compiler

// If write isn't preceded by read, data has this added to it
int const no_read_before_write = 0x2000;

void SNES_SPC::cpu_write_smp_reg_( int data, rel_time_t time, int addr )
{
	switch ( addr )
	{
	case r_t0target:
	case r_t1target:
	case r_t2target: {
		Timer* t = &m.timers [addr - r_t0target];
		int period = IF_0_THEN_256( data );
		if ( t->period != period )
		{
			t = run_timer( t, time );
			#if SPC_MORE_ACCURACY
				// Insane behavior when target is written just after counter is
				// clocked and counter matches new period and new period isn't 1, 2, 4, or 8
				if ( t->divider == (period & 0xFF) &&
						t->next_time == time + TIMER_MUL( t, 1 ) &&
						((period - 1) | ~0x0F) & period )
				{
					//dprintf( "SPC pathological timer target write\n" );

					// If the period is 3, 5, or 9, there's a probability this behavior won't occur,
					// based on the previous period
					int prob = 0xFF;
					int old_period = t->period & 0xFF;
					if ( period == 3 ) prob = glitch_probs [0] [old_period];
					if ( period == 5 ) prob = glitch_probs [1] [old_period];
					if ( period == 9 ) prob = glitch_probs [2] [old_period];

					// The glitch suppresses incrementing of one of the counter bits, based on
					// the lowest set bit in the new period
					int b = 1;
					while ( !(period & b) )
						b <<= 1;

					if ( (rand() >> 4 & 0xFF) <= prob )
						t->divider = (t->divider - b) & 0xFF;
				}
			#endif
			t->period = period;
		}
		break;
	}

	case r_t0out:
	case r_t1out:
	case r_t2out:
		if ( !SPC_MORE_ACCURACY )
			dprintf( "SPC wrote to counter %d\n", (int) addr - r_t0out );

		if ( data < no_read_before_write  / 2 )
			run_timer( &m.timers [addr - r_t0out], time - 1 )->counter = 0;
		break;

	// Registers that act like RAM
	case 0x8:
	case 0x9:
		REGS_IN [addr] = (uint8_t) data;
		break;

	case r_test:
		if ( (uint8_t) data != 0x0A )
			dprintf( "SPC wrote to test register\n" );
		break;

	case r_control:
		// port clears
		if ( data & 0x10 )
		{
			REGS_IN [r_cpuio0] = 0;
			REGS_IN [r_cpuio1] = 0;
		}
		if ( data & 0x20 )
		{
			REGS_IN [r_cpuio2] = 0;
			REGS_IN [r_cpuio3] = 0;
		}

		// timers
		{
			for ( int i = 0; i < timer_count; i++ )
			{
				Timer* t = &m.timers [i];
				int enabled = data >> i & 1;
				if ( t->enabled != enabled )
				{
					t = run_timer( t, time );
					t->enabled = enabled;
					if ( enabled )
					{
						t->divider = 0;
						t->counter = 0;
					}
				}
			}
		}
		enable_rom( data & 0x80 );
		break;
	}
}

void SNES_SPC::cpu_write_smp_reg( int data, rel_time_t time, int addr )
{
	if ( addr == r_dspdata ) // 99%
		dsp_write( data, time );
	else
		cpu_write_smp_reg_( data, time, addr );
}

void SNES_SPC::cpu_write_high( int data, int i, rel_time_t time )
{
	if ( i < rom_size )
	{
		m.hi_ram [i] = (uint8_t) data;
		if ( m.rom_enabled )
			RAM [i + rom_addr] = m.rom [i]; // restore overwritten ROM
	}
	else
	{
		assert( RAM [i + rom_addr] == (uint8_t) data );
		RAM [i + rom_addr] = cpu_pad_fill; // restore overwritten padding
		cpu_write( data, i + rom_addr - 0x10000, time );
	}
}

int const bits_in_int = CHAR_BIT * sizeof (int);

void SNES_SPC::cpu_write( int data, int addr, rel_time_t time )
{
	//MEM_ACCESS( time, addr )

	// RAM
	RAM [addr] = (uint8_t) data;
	int reg = addr - 0xF0;
	if ( reg >= 0 ) // 64%
	{
		// $F0-$FF
		if ( reg < reg_count ) // 87%
		{
			REGS [reg] = (uint8_t) data;

			// Ports
			#ifdef SPC_PORT_WRITE_HOOK
				if ( (unsigned) (reg - r_cpuio0) < port_count )
					SPC_PORT_WRITE_HOOK( m.spc_time + time, (reg - r_cpuio0),
							(uint8_t) data, &REGS [r_cpuio0] );
			#endif

			// Registers other than $F2 and $F4-$F7
			//if ( reg != 2 && reg != 4 && reg != 5 && reg != 6 && reg != 7 )
			// TODO: this is a bit on the fragile side
			if ( ((~0x2F00 << (bits_in_int - 16)) << reg) < 0 ) // 36%
				cpu_write_smp_reg( data, time, reg );
		}
		// High mem/address wrap-around
		else
		{
			reg -= rom_addr - 0xF0;
			if ( reg >= 0 ) // 1% in IPL ROM area or address wrapped around
				cpu_write_high( data, reg, time );
		}
	}
}


//// CPU read

inline int SNES_SPC::cpu_read_smp_reg( int reg, rel_time_t time )
{
	int result = REGS_IN [reg];
	reg -= r_dspaddr;
	// DSP addr and data
	if ( (unsigned) reg <= 1 ) // 4% 0xF2 and 0xF3
	{
		result = REGS [r_dspaddr];
		if ( (unsigned) reg == 1 )
			result = dsp_read( time ); // 0xF3
	}
	return result;
}

int SNES_SPC::cpu_read( int addr, rel_time_t time )
{
	//MEM_ACCESS( time, addr )

	// RAM
	int result = RAM [addr];
	int reg = addr - 0xF0;
	if ( reg >= 0 ) // 40%
	{
		reg -= 0x10;
		if ( (unsigned) reg >= 0xFF00 ) // 21%
		{
			reg += 0x10 - r_t0out;

			// Timers
			if ( (unsigned) reg < timer_count ) // 90%
			{
				Timer* t = &m.timers [reg];
				if ( time >= t->next_time )
					t = run_timer_( t, time );
				result = t->counter;
				t->counter = 0;
			}
			// Other registers
			else if ( reg < 0 ) // 10%
			{
				result = cpu_read_smp_reg( reg + r_t0out, time );
			}
			else // 1%
			{
				assert( reg + (r_t0out + 0xF0 - 0x10000) < 0x100 );
				result = cpu_read( reg + (r_t0out + 0xF0 - 0x10000), time );
			}
		}
	}

	return result;
}


//// Run

// Prefix and suffix for CPU emulator function
#define SPC_CPU_RUN_FUNC \
uint8_t* SNES_SPC::run_until_( time_t end_time )\
{\
	rel_time_t rel_time = m.spc_time - end_time;\
	assert( rel_time <= 0 );\
	m.spc_time = end_time;\
	m.dsp_time += rel_time;\
	m.timers [0].next_time += rel_time;\
	m.timers [1].next_time += rel_time;\
	m.timers [2].next_time += rel_time;

#define SPC_CPU_RUN_FUNC_END \
	m.spc_time += rel_time;\
	m.dsp_time -= rel_time;\
	m.timers [0].next_time -= rel_time;\
	m.timers [1].next_time -= rel_time;\
	m.timers [2].next_time -= rel_time;\
	assert( m.spc_time <= end_time );\
	return &REGS [r_cpuio0];\
}

int const cpu_lag_max = 12 - 1; // DIV YA,X takes 12 clocks

void SNES_SPC::end_frame( time_t end_time )
{
	// Catch CPU up to as close to end as possible. If final instruction
	// would exceed end, does NOT execute it and leaves m.spc_time < end.
	if ( end_time > m.spc_time )
		run_until_( end_time );

	m.spc_time     -= end_time;
	m.extra_clocks += end_time;

	// Greatest number of clocks early that emulation can stop early due to
	// not being able to execute current instruction without going over
	// allowed time.
	assert( -cpu_lag_max <= m.spc_time && m.spc_time <= 0 );

	// Catch timers up to CPU
	for ( int i = 0; i < timer_count; i++ )
		run_timer( &m.timers [i], 0 );

	// Catch DSP up to CPU
	if ( m.dsp_time < 0 )
	{
		RUN_DSP( 0, max_reg_time );
	}

	// Save any extra samples beyond what should be generated
	if ( m.buf_begin )
		save_extra();
}

#if SPC_MORE_ACCURACY
	#define SUSPICIOUS_OPCODE( name ) ((void) 0)
#else
	#define SUSPICIOUS_OPCODE( name ) dprintf( "SPC: suspicious opcode: " name "\n" )
#endif

#define CPU_READ( time, offset, addr )\
	cpu_read( addr, time + offset )

#define CPU_WRITE( time, offset, addr, data )\
	cpu_write( data, addr, time + offset )

#if SPC_MORE_ACCURACY
	#define CPU_READ_TIMER( time, offset, addr, out )\
		{ out = CPU_READ( time, offset, addr ); }

#else
	// timers are by far the most common thing read from dp
	#define CPU_READ_TIMER( time, offset, addr_, out )\
	{\
		rel_time_t adj_time = time + offset;\
		int dp_addr = addr_;\
		int ti = dp_addr - (r_t0out + 0xF0);\
		if ( (unsigned) ti < timer_count )\
		{\
			Timer* t = &m.timers [ti];\
			if ( adj_time >= t->next_time )\
				t = run_timer_( t, adj_time );\
			out = t->counter;\
			t->counter = 0;\
		}\
		else\
		{\
			out = ram [dp_addr];\
			int i = dp_addr - 0xF0;\
			if ( (unsigned) i < 0x10 )\
				out = cpu_read_smp_reg( i, adj_time );\
		}\
	}
#endif

#define TIME_ADJ( n )   (n)

#define READ_TIMER( time, addr, out )       CPU_READ_TIMER( rel_time, TIME_ADJ(time), (addr), out )
#define READ(  time, addr )                 CPU_READ ( rel_time, TIME_ADJ(time), (addr) )
#define WRITE( time, addr, data )           CPU_WRITE( rel_time, TIME_ADJ(time), (addr), (data) )

#define DP_ADDR( addr )                     (dp + (addr))

#define READ_DP_TIMER(  time, addr, out )   CPU_READ_TIMER( rel_time, TIME_ADJ(time), DP_ADDR( addr ), out )
#define READ_DP(  time, addr )              READ ( time, DP_ADDR( addr ) )
#define WRITE_DP( time, addr, data )        WRITE( time, DP_ADDR( addr ), data )

#define READ_PROG16( addr )                 GET_LE16( ram + (addr) )

#define SET_PC( n )     (pc = ram + (n))
#define GET_PC()        (pc - ram)
#define READ_PC( pc )   (*(pc))
#define READ_PC16( pc ) GET_LE16( pc )

// TODO: remove non-wrapping versions?
#define SPC_NO_SP_WRAPAROUND 0

#define SET_SP( v )     (sp = ram + 0x101 + (v))
#define GET_SP()        (sp - 0x101 - ram)

#if SPC_NO_SP_WRAPAROUND
#define PUSH16( v )     (sp -= 2, SET_LE16( sp, v ))
#define PUSH( v )       (void) (*--sp = (uint8_t) (v))
#define POP( out )      (void) ((out) = *sp++)

#else
#define PUSH16( data )\
{\
	int addr = (sp -= 2) - ram;\
	if ( addr > 0x100 )\
	{\
		SET_LE16( sp, data );\
	}\
	else\
	{\
		ram [(uint8_t) addr + 0x100] = (uint8_t) data;\
		sp [1] = (uint8_t) (data >> 8);\
		sp += 0x100;\
	}\
}

#define PUSH( data )\
{\
	*--sp = (uint8_t) (data);\
	if ( sp - ram == 0x100 )\
		sp += 0x100;\
}

#define POP( out )\
{\
	out = *sp++;\
	if ( sp - ram == 0x201 )\
	{\
		out = sp [-0x101];\
		sp -= 0x100;\
	}\
}

#endif

#define MEM_BIT( rel ) CPU_mem_bit( pc, rel_time + rel )

unsigned SNES_SPC::CPU_mem_bit( uint8_t const* pc, rel_time_t rel_time )
{
	unsigned addr = READ_PC16( pc );
	unsigned t = READ( 0, addr & 0x1FFF ) >> (addr >> 13);
	return t << 8 & 0x100;
}

//// Status flag handling

// Hex value in name to clarify code and bit shifting.
// Flag stored in indicated variable during emulation
int const n80 = 0x80; // nz
int const v40 = 0x40; // psw
int const p20 = 0x20; // dp
int const b10 = 0x10; // psw
int const h08 = 0x08; // psw
int const i04 = 0x04; // psw
int const z02 = 0x02; // nz
int const c01 = 0x01; // c

int const nz_neg_mask = 0x880; // either bit set indicates N flag set

#define GET_PSW( out )\
{\
	out = psw & ~(n80 | p20 | z02 | c01);\
	out |= c  >> 8 & c01;\
	out |= dp >> 3 & p20;\
	out |= ((nz >> 4) | nz) & n80;\
	if ( !(uint8_t) nz ) out |= z02;\
}

#define SET_PSW( in )\
{\
	psw = in;\
	c   = in << 8;\
	dp  = in << 3 & 0x100;\
	nz  = (in << 4 & 0x800) | (~in & z02);\
}

SPC_CPU_RUN_FUNC
{
	uint8_t* const ram = RAM;
	int a = m.cpu_regs.a;
	int x = m.cpu_regs.x;
	int y = m.cpu_regs.y;
	uint8_t const* pc;
	uint8_t* sp;
	int psw;
	int c;
	int nz;
	int dp;

	SET_PC( m.cpu_regs.pc );
	SET_SP( m.cpu_regs.sp );
	SET_PSW( m.cpu_regs.psw );

	goto loop;


	// Main loop

cbranch_taken_loop:
	pc += *(int8_t const*) pc;
inc_pc_loop:
	pc++;
loop:
{
	unsigned opcode;
	unsigned data;

	opcode = *pc;
	if ( (rel_time += m.cycle_table [opcode]) > 0 )
		goto out_of_time;

	#ifdef SPC_CPU_OPCODE_HOOK
		SPC_CPU_OPCODE_HOOK( GET_PC(), opcode );
	#endif
	/*
	//SUB_CASE_COUNTER( 1 );
	#define PROFILE_TIMER_LOOP( op, addr, len )\
	if ( opcode == op )\
	{\
		int cond = (unsigned) ((addr) - 0xFD) < 3 &&\
				pc [len] == 0xF0 && pc [len+1] == 0xFE - len;\
		SUB_CASE_COUNTER( op && cond );\
	}

	PROFILE_TIMER_LOOP( 0xEC, GET_LE16( pc + 1 ), 3 );
	PROFILE_TIMER_LOOP( 0xEB, pc [1], 2 );
	PROFILE_TIMER_LOOP( 0xE4, pc [1], 2 );
	*/

	// TODO: if PC is at end of memory, this will get wrong operand (very obscure)
	data = *++pc;
	switch ( opcode )
	{

// Common instructions

#define BRANCH( cond )\
{\
	pc++;\
	pc += (int8_t) data;\
	if ( cond )\
		goto loop;\
	pc -= (int8_t) data;\
	rel_time -= 2;\
	goto loop;\
}

	case 0xF0: // BEQ
		BRANCH( !(uint8_t) nz ) // 89% taken

	case 0xD0: // BNE
		BRANCH( (uint8_t) nz )

	case 0x3F:{// CALL
		int old_addr = GET_PC() + 2;
		SET_PC( READ_PC16( pc ) );
		PUSH16( old_addr );
		goto loop;
	}

	case 0x6F:// RET
		#if SPC_NO_SP_WRAPAROUND
		{
			SET_PC( GET_LE16( sp ) );
			sp += 2;
		}
		#else
		{
			int addr = sp - ram;
			SET_PC( GET_LE16( sp ) );
			sp += 2;
			if ( addr < 0x1FF )
				goto loop;

			SET_PC( sp [-0x101] * 0x100 + ram [(uint8_t) addr + 0x100] );
			sp -= 0x100;
		}
		#endif
		goto loop;

	case 0xE4: // MOV a,dp
		++pc;
		// 80% from timer
		READ_DP_TIMER( 0, data, a = nz );
		goto loop;

	case 0xFA:{// MOV dp,dp
		int temp;
		READ_DP_TIMER( -2, data, temp );
		data = temp + no_read_before_write ;
	}
	// fall through
	case 0x8F:{// MOV dp,#imm
		int temp = READ_PC( pc + 1 );
		pc += 2;

		#if !SPC_MORE_ACCURACY
		{
			int i = dp + temp;
			ram [i] = (uint8_t) data;
			i -= 0xF0;
			if ( (unsigned) i < 0x10 ) // 76%
			{
				REGS [i] = (uint8_t) data;

				// Registers other than $F2 and $F4-$F7
				//if ( i != 2 && i != 4 && i != 5 && i != 6 && i != 7 )
				if ( ((~0x2F00 << (bits_in_int - 16)) << i) < 0 ) // 12%
					cpu_write_smp_reg( data, rel_time, i );
			}
		}
		#else
			WRITE_DP( 0, temp, data );
		#endif
		goto loop;
	}

	case 0xC4: // MOV dp,a
		++pc;
		#if !SPC_MORE_ACCURACY
		{
			int i = dp + data;
			ram [i] = (uint8_t) a;
			i -= 0xF0;
			if ( (unsigned) i < 0x10 ) // 39%
			{
				unsigned sel = i - 2;
				REGS [i] = (uint8_t) a;

				if ( sel == 1 ) // 51% $F3
					dsp_write( a, rel_time );
				else if ( sel > 1 ) // 1% not $F2 or $F3
					cpu_write_smp_reg_( a, rel_time, i );
			}
		}
		#else
			WRITE_DP( 0, data, a );
		#endif
		goto loop;

#define CASE( n )   case n:

// Define common address modes based on opcode for immediate mode. Execution
// ends with data set to the address of the operand.
#define ADDR_MODES_( op )\
	CASE( op - 0x02 ) /* (X) */\
		data = x + dp;\
		pc--;\
		goto end_##op;\
	CASE( op + 0x0F ) /* (dp)+Y */\
		data = READ_PROG16( data + dp ) + y;\
		goto end_##op;\
	CASE( op - 0x01 ) /* (dp+X) */\
		data = READ_PROG16( ((uint8_t) (data + x)) + dp );\
		goto end_##op;\
	CASE( op + 0x0E ) /* abs+Y */\
		data += y;\
		goto abs_##op;\
	CASE( op + 0x0D ) /* abs+X */\
		data += x;\
	CASE( op - 0x03 ) /* abs */\
	abs_##op:\
		data += 0x100 * READ_PC( ++pc );\
		goto end_##op;\
	CASE( op + 0x0C ) /* dp+X */\
		data = (uint8_t) (data + x);

#define ADDR_MODES_NO_DP( op )\
	ADDR_MODES_( op )\
		data += dp;\
	end_##op:

#define ADDR_MODES( op )\
	ADDR_MODES_( op )\
	CASE( op - 0x04 ) /* dp */\
		data += dp;\
	end_##op:

// 1. 8-bit Data Transmission Commands. Group I

	ADDR_MODES_NO_DP( 0xE8 ) // MOV A,addr
		a = nz = READ( 0, data );
		goto inc_pc_loop;

	case 0xBF:{// MOV A,(X)+
		int temp = x + dp;
		x = (uint8_t) (x + 1);
		a = nz = READ( -1, temp );
		goto loop;
	}

	case 0xE8: // MOV A,imm
		a  = data;
		nz = data;
		goto inc_pc_loop;

	case 0xF9: // MOV X,dp+Y
		data = (uint8_t) (data + y);
	case 0xF8: // MOV X,dp
		READ_DP_TIMER( 0, data, x = nz );
		goto inc_pc_loop;

	case 0xE9: // MOV X,abs
		data = READ_PC16( pc );
		++pc;
		data = READ( 0, data );
	case 0xCD: // MOV X,imm
		x  = data;
		nz = data;
		goto inc_pc_loop;

	case 0xFB: // MOV Y,dp+X
		data = (uint8_t) (data + x);
	case 0xEB: // MOV Y,dp
		// 70% from timer
		pc++;
		READ_DP_TIMER( 0, data, y = nz );
		goto loop;

	case 0xEC:{// MOV Y,abs
		int temp = READ_PC16( pc );
		pc += 2;
		READ_TIMER( 0, temp, y = nz );
		//y = nz = READ( 0, temp );
		goto loop;
	}

	case 0x8D: // MOV Y,imm
		y  = data;
		nz = data;
		goto inc_pc_loop;

// 2. 8-BIT DATA TRANSMISSION COMMANDS, GROUP 2

	ADDR_MODES_NO_DP( 0xC8 ) // MOV addr,A
		WRITE( 0, data, a );
		goto inc_pc_loop;

	{
		int temp;
	case 0xCC: // MOV abs,Y
		temp = y;
		goto mov_abs_temp;
	case 0xC9: // MOV abs,X
		temp = x;
	mov_abs_temp:
		WRITE( 0, READ_PC16( pc ), temp );
		pc += 2;
		goto loop;
	}

	case 0xD9: // MOV dp+Y,X
		data = (uint8_t) (data + y);
	case 0xD8: // MOV dp,X
		WRITE( 0, data + dp, x );
		goto inc_pc_loop;

	case 0xDB: // MOV dp+X,Y
		data = (uint8_t) (data + x);
	case 0xCB: // MOV dp,Y
		WRITE( 0, data + dp, y );
		goto inc_pc_loop;

// 3. 8-BIT DATA TRANSMISSIN COMMANDS, GROUP 3.

	case 0x7D: // MOV A,X
		a  = x;
		nz = x;
		goto loop;

	case 0xDD: // MOV A,Y
		a  = y;
		nz = y;
		goto loop;

	case 0x5D: // MOV X,A
		x  = a;
		nz = a;
		goto loop;

	case 0xFD: // MOV Y,A
		y  = a;
		nz = a;
		goto loop;

	case 0x9D: // MOV X,SP
		x = nz = GET_SP();
		goto loop;

	case 0xBD: // MOV SP,X
		SET_SP( x );
		goto loop;

	//case 0xC6: // MOV (X),A (handled by MOV addr,A in group 2)

	case 0xAF: // MOV (X)+,A
		WRITE_DP( 0, x, a + no_read_before_write  );
		x++;
		goto loop;

// 5. 8-BIT LOGIC OPERATION COMMANDS

#define LOGICAL_OP( op, func )\
	ADDR_MODES( op ) /* addr */\
		data = READ( 0, data );\
	case op: /* imm */\
		nz = a func##= data;\
		goto inc_pc_loop;\
	{   unsigned addr;\
	case op + 0x11: /* X,Y */\
		data = READ_DP( -2, y );\
		addr = x + dp;\
		goto addr_##op;\
	case op + 0x01: /* dp,dp */\
		data = READ_DP( -3, data );\
	case op + 0x10:{/*dp,imm*/\
		uint8_t const* addr2 = pc + 1;\
		pc += 2;\
		addr = READ_PC( addr2 ) + dp;\
	}\
	addr_##op:\
		nz = data func READ( -1, addr );\
		WRITE( 0, addr, nz );\
		goto loop;\
	}

	LOGICAL_OP( 0x28, & ); // AND

	LOGICAL_OP( 0x08, | ); // OR

	LOGICAL_OP( 0x48, ^ ); // EOR

// 4. 8-BIT ARITHMETIC OPERATION COMMANDS

	ADDR_MODES( 0x68 ) // CMP addr
		data = READ( 0, data );
	case 0x68: // CMP imm
		nz = a - data;
		c = ~nz;
		nz &= 0xFF;
		goto inc_pc_loop;

	case 0x79: // CMP (X),(Y)
		data = READ_DP( -2, y );
		nz = READ_DP( -1, x ) - data;
		c = ~nz;
		nz &= 0xFF;
		goto loop;

	case 0x69: // CMP dp,dp
		data = READ_DP( -3, data );
	case 0x78: // CMP dp,imm
		nz = READ_DP( -1, READ_PC( ++pc ) ) - data;
		c = ~nz;
		nz &= 0xFF;
		goto inc_pc_loop;

	case 0x3E: // CMP X,dp
		data += dp;
		goto cmp_x_addr;
	case 0x1E: // CMP X,abs
		data = READ_PC16( pc );
		pc++;
	cmp_x_addr:
		data = READ( 0, data );
	case 0xC8: // CMP X,imm
		nz = x - data;
		c = ~nz;
		nz &= 0xFF;
		goto inc_pc_loop;

	case 0x7E: // CMP Y,dp
		data += dp;
		goto cmp_y_addr;
	case 0x5E: // CMP Y,abs
		data = READ_PC16( pc );
		pc++;
	cmp_y_addr:
		data = READ( 0, data );
	case 0xAD: // CMP Y,imm
		nz = y - data;
		c = ~nz;
		nz &= 0xFF;
		goto inc_pc_loop;

	{
		int addr;
	case 0xB9: // SBC (x),(y)
	case 0x99: // ADC (x),(y)
		pc--; // compensate for inc later
		data = READ_DP( -2, y );
		addr = x + dp;
		goto adc_addr;
	case 0xA9: // SBC dp,dp
	case 0x89: // ADC dp,dp
		data = READ_DP( -3, data );
	case 0xB8: // SBC dp,imm
	case 0x98: // ADC dp,imm
		addr = READ_PC( ++pc ) + dp;
	adc_addr:
		nz = READ( -1, addr );
		goto adc_data;

// catch ADC and SBC together, then decode later based on operand
#undef CASE
#define CASE( n ) case n: case (n) + 0x20:
	ADDR_MODES( 0x88 ) // ADC/SBC addr
		data = READ( 0, data );
	case 0xA8: // SBC imm
	case 0x88: // ADC imm
		addr = -1; // A
		nz = a;
	adc_data: {
		int flags;
		if ( opcode >= 0xA0 ) // SBC
			data ^= 0xFF;

		flags = data ^ nz;
		nz += data + (c >> 8 & 1);
		flags ^= nz;

		psw = (psw & ~(v40 | h08)) |
				(flags >> 1 & h08) |
				((flags + 0x80) >> 2 & v40);
		c = nz;
		if ( addr < 0 )
		{
			a = (uint8_t) nz;
			goto inc_pc_loop;
		}
		WRITE( 0, addr, /*(uint8_t)*/ nz );
		goto inc_pc_loop;
	}

	}

// 6. ADDITION & SUBTRACTION COMMANDS

#define INC_DEC_REG( reg, op )\
		nz  = reg op;\
		reg = (uint8_t) nz;\
		goto loop;

	case 0xBC: INC_DEC_REG( a, + 1 ) // INC A
	case 0x3D: INC_DEC_REG( x, + 1 ) // INC X
	case 0xFC: INC_DEC_REG( y, + 1 ) // INC Y

	case 0x9C: INC_DEC_REG( a, - 1 ) // DEC A
	case 0x1D: INC_DEC_REG( x, - 1 ) // DEC X
	case 0xDC: INC_DEC_REG( y, - 1 ) // DEC Y

	case 0x9B: // DEC dp+X
	case 0xBB: // INC dp+X
		data = (uint8_t) (data + x);
	case 0x8B: // DEC dp
	case 0xAB: // INC dp
		data += dp;
		goto inc_abs;
	case 0x8C: // DEC abs
	case 0xAC: // INC abs
		data = READ_PC16( pc );
		pc++;
	inc_abs:
		nz = (opcode >> 4 & 2) - 1;
		nz += READ( -1, data );
		WRITE( 0, data, /*(uint8_t)*/ nz );
		goto inc_pc_loop;

// 7. SHIFT, ROTATION COMMANDS

	case 0x5C: // LSR A
		c = 0;
	case 0x7C:{// ROR A
		nz = (c >> 1 & 0x80) | (a >> 1);
		c = a << 8;
		a = nz;
		goto loop;
	}

	case 0x1C: // ASL A
		c = 0;
	case 0x3C:{// ROL A
		int temp = c >> 8 & 1;
		c = a << 1;
		nz = c | temp;
		a = (uint8_t) nz;
		goto loop;
	}

	case 0x0B: // ASL dp
		c = 0;
		data += dp;
		goto rol_mem;
	case 0x1B: // ASL dp+X
		c = 0;
	case 0x3B: // ROL dp+X
		data = (uint8_t) (data + x);
	case 0x2B: // ROL dp
		data += dp;
		goto rol_mem;
	case 0x0C: // ASL abs
		c = 0;
	case 0x2C: // ROL abs
		data = READ_PC16( pc );
		pc++;
	rol_mem:
		nz = c >> 8 & 1;
		nz |= (c = READ( -1, data ) << 1);
		WRITE( 0, data, /*(uint8_t)*/ nz );
		goto inc_pc_loop;

	case 0x4B: // LSR dp
		c = 0;
		data += dp;
		goto ror_mem;
	case 0x5B: // LSR dp+X
		c = 0;
	case 0x7B: // ROR dp+X
		data = (uint8_t) (data + x);
	case 0x6B: // ROR dp
		data += dp;
		goto ror_mem;
	case 0x4C: // LSR abs
		c = 0;
	case 0x6C: // ROR abs
		data = READ_PC16( pc );
		pc++;
	ror_mem: {
		int temp = READ( -1, data );
		nz = (c >> 1 & 0x80) | (temp >> 1);
		c = temp << 8;
		WRITE( 0, data, nz );
		goto inc_pc_loop;
	}

	case 0x9F: // XCN
		nz = a = (a >> 4) | (uint8_t) (a << 4);
		goto loop;

// 8. 16-BIT TRANSMISION COMMANDS

	case 0xBA: // MOVW YA,dp
		a = READ_DP( -2, data );
		nz = (a & 0x7F) | (a >> 1);
		y = READ_DP( 0, (uint8_t) (data + 1) );
		nz |= y;
		goto inc_pc_loop;

	case 0xDA: // MOVW dp,YA
		WRITE_DP( -1, data, a );
		WRITE_DP( 0, (uint8_t) (data + 1), y + no_read_before_write  );
		goto inc_pc_loop;

// 9. 16-BIT OPERATION COMMANDS

	case 0x3A: // INCW dp
	case 0x1A:{// DECW dp
		int temp;
		// low byte
		data += dp;
		temp = READ( -3, data );
		temp += (opcode >> 4 & 2) - 1; // +1 for INCW, -1 for DECW
		nz = ((temp >> 1) | temp) & 0x7F;
		WRITE( -2, data, /*(uint8_t)*/ temp );

		// high byte
		data = (uint8_t) (data + 1) + dp;
		temp = (uint8_t) ((temp >> 8) + READ( -1, data ));
		nz |= temp;
		WRITE( 0, data, temp );

		goto inc_pc_loop;
	}

	case 0x7A: // ADDW YA,dp
	case 0x9A:{// SUBW YA,dp
		int lo = READ_DP( -2, data );
		int hi = READ_DP( 0, (uint8_t) (data + 1) );
		int result;
		int flags;

		if ( opcode == 0x9A ) // SUBW
		{
			lo = (lo ^ 0xFF) + 1;
			hi ^= 0xFF;
		}

		lo += a;
		result = y + hi + (lo >> 8);
		flags = hi ^ y ^ result;

		psw = (psw & ~(v40 | h08)) |
				(flags >> 1 & h08) |
				((flags + 0x80) >> 2 & v40);
		c = result;
		a = (uint8_t) lo;
		result = (uint8_t) result;
		y = result;
		nz = (((lo >> 1) | lo) & 0x7F) | result;

		goto inc_pc_loop;
	}

	case 0x5A: { // CMPW YA,dp
		int temp = a - READ_DP( -1, data );
		nz = ((temp >> 1) | temp) & 0x7F;
		temp = y + (temp >> 8);
		temp -= READ_DP( 0, (uint8_t) (data + 1) );
		nz |= temp;
		c  = ~temp;
		nz &= 0xFF;
		goto inc_pc_loop;
	}

// 10. MULTIPLICATION & DIVISON COMMANDS

	case 0xCF: { // MUL YA
		unsigned temp = y * a;
		a = (uint8_t) temp;
		nz = ((temp >> 1) | temp) & 0x7F;
		y = temp >> 8;
		nz |= y;
		goto loop;
	}

	case 0x9E: // DIV YA,X
	{
		unsigned ya = y * 0x100 + a;

		psw &= ~(h08 | v40);

		if ( y >= x )
			psw |= v40;

		if ( (y & 15) >= (x & 15) )
			psw |= h08;

		if ( y < x * 2 )
		{
			a = ya / x;
			y = ya - a * x;
		}
		else
		{
			a = 255 - (ya - x * 0x200) / (256 - x);
			y = x   + (ya - x * 0x200) % (256 - x);
		}

		nz = (uint8_t) a;
		a = (uint8_t) a;

		goto loop;
	}

// 11. DECIMAL COMPENSATION COMMANDS

	case 0xDF: // DAA
		SUSPICIOUS_OPCODE( "DAA" );
		if ( a > 0x99 || c & 0x100 )
		{
			a += 0x60;
			c = 0x100;
		}

		if ( (a & 0x0F) > 9 || psw & h08 )
			a += 0x06;

		nz = a;
		a = (uint8_t) a;
		goto loop;

	case 0xBE: // DAS
		SUSPICIOUS_OPCODE( "DAS" );
		if ( a > 0x99 || !(c & 0x100) )
		{
			a -= 0x60;
			c = 0;
		}

		if ( (a & 0x0F) > 9 || !(psw & h08) )
			a -= 0x06;

		nz = a;
		a = (uint8_t) a;
		goto loop;

// 12. BRANCHING COMMANDS

	case 0x2F: // BRA rel
		pc += (BOOST::int8_t) data;
		goto inc_pc_loop;

	case 0x30: // BMI
		BRANCH( (nz & nz_neg_mask) )

	case 0x10: // BPL
		BRANCH( !(nz & nz_neg_mask) )

	case 0xB0: // BCS
		BRANCH( c & 0x100 )

	case 0x90: // BCC
		BRANCH( !(c & 0x100) )

	case 0x70: // BVS
		BRANCH( psw & v40 )

	case 0x50: // BVC
		BRANCH( !(psw & v40) )

	#define CBRANCH( cond )\
	{\
		pc++;\
		if ( cond )\
			goto cbranch_taken_loop;\
		rel_time -= 2;\
		goto inc_pc_loop;\
	}

	case 0x03: // BBS dp.bit,rel
	case 0x23:
	case 0x43:
	case 0x63:
	case 0x83:
	case 0xA3:
	case 0xC3:
	case 0xE3:
		CBRANCH( READ_DP( -4, data ) >> (opcode >> 5) & 1 )

	case 0x13: // BBC dp.bit,rel
	case 0x33:
	case 0x53:
	case 0x73:
	case 0x93:
	case 0xB3:
	case 0xD3:
	case 0xF3:
		CBRANCH( !(READ_DP( -4, data ) >> (opcode >> 5) & 1) )

	case 0xDE: // CBNE dp+X,rel
		data = (uint8_t) (data + x);
		// fall through
	case 0x2E:{// CBNE dp,rel
		int temp;
		// 61% from timer
		READ_DP_TIMER( -4, data, temp );
		CBRANCH( temp != a )
	}

	case 0x6E: { // DBNZ dp,rel
		unsigned temp = READ_DP( -4, data ) - 1;
		WRITE_DP( -3, (uint8_t) data, /*(uint8_t)*/ temp + no_read_before_write  );
		CBRANCH( temp )
	}

	case 0xFE: // DBNZ Y,rel
		y = (uint8_t) (y - 1);
		BRANCH( y )

	case 0x1F: // JMP [abs+X]
		SET_PC( READ_PC16( pc ) + x );
		// fall through
	case 0x5F: // JMP abs
		SET_PC( READ_PC16( pc ) );
		goto loop;

// 13. SUB-ROUTINE CALL RETURN COMMANDS

	case 0x0F:{// BRK
		int temp;
		int ret_addr = GET_PC();
		SUSPICIOUS_OPCODE( "BRK" );
		SET_PC( READ_PROG16( 0xFFDE ) ); // vector address verified
		PUSH16( ret_addr );
		GET_PSW( temp );
		psw = (psw | b10) & ~i04;
		PUSH( temp );
		goto loop;
	}

	case 0x4F:{// PCALL offset
		int ret_addr = GET_PC() + 1;
		SET_PC( 0xFF00 | data );
		PUSH16( ret_addr );
		goto loop;
	}

	case 0x01: // TCALL n
	case 0x11:
	case 0x21:
	case 0x31:
	case 0x41:
	case 0x51:
	case 0x61:
	case 0x71:
	case 0x81:
	case 0x91:
	case 0xA1:
	case 0xB1:
	case 0xC1:
	case 0xD1:
	case 0xE1:
	case 0xF1: {
		int ret_addr = GET_PC();
		SET_PC( READ_PROG16( 0xFFDE - (opcode >> 3) ) );
		PUSH16( ret_addr );
		goto loop;
	}

// 14. STACK OPERATION COMMANDS

	{
		int temp;
	case 0x7F: // RET1
		temp = *sp;
		SET_PC( GET_LE16( sp + 1 ) );
		sp += 3;
		goto set_psw;
	case 0x8E: // POP PSW
		POP( temp );
	set_psw:
		SET_PSW( temp );
		goto loop;
	}

	case 0x0D: { // PUSH PSW
		int temp;
		GET_PSW( temp );
		PUSH( temp );
		goto loop;
	}

	case 0x2D: // PUSH A
		PUSH( a );
		goto loop;

	case 0x4D: // PUSH X
		PUSH( x );
		goto loop;

	case 0x6D: // PUSH Y
		PUSH( y );
		goto loop;

	case 0xAE: // POP A
		POP( a );
		goto loop;

	case 0xCE: // POP X
		POP( x );
		goto loop;

	case 0xEE: // POP Y
		POP( y );
		goto loop;

// 15. BIT OPERATION COMMANDS

	case 0x02: // SET1
	case 0x22:
	case 0x42:
	case 0x62:
	case 0x82:
	case 0xA2:
	case 0xC2:
	case 0xE2:
	case 0x12: // CLR1
	case 0x32:
	case 0x52:
	case 0x72:
	case 0x92:
	case 0xB2:
	case 0xD2:
	case 0xF2: {
		int bit = 1 << (opcode >> 5);
		int mask = ~bit;
		if ( opcode & 0x10 )
			bit = 0;
		data += dp;
		WRITE( 0, data, (READ( -1, data ) & mask) | bit );
		goto inc_pc_loop;
	}

	case 0x0E: // TSET1 abs
	case 0x4E: // TCLR1 abs
		data = READ_PC16( pc );
		pc += 2;
		{
			unsigned temp = READ( -2, data );
			nz = (uint8_t) (a - temp);
			temp &= ~a;
			if ( opcode == 0x0E )
				temp |= a;
			WRITE( 0, data, temp );
		}
		goto loop;

	case 0x4A: // AND1 C,mem.bit
		c &= MEM_BIT( 0 );
		pc += 2;
		goto loop;

	case 0x6A: // AND1 C,/mem.bit
		c &= ~MEM_BIT( 0 );
		pc += 2;
		goto loop;

	case 0x0A: // OR1 C,mem.bit
		c |= MEM_BIT( -1 );
		pc += 2;
		goto loop;

	case 0x2A: // OR1 C,/mem.bit
		c |= ~MEM_BIT( -1 );
		pc += 2;
		goto loop;

	case 0x8A: // EOR1 C,mem.bit
		c ^= MEM_BIT( -1 );
		pc += 2;
		goto loop;

	case 0xEA: // NOT1 mem.bit
		data = READ_PC16( pc );
		pc += 2;
		{
			unsigned temp = READ( -1, data & 0x1FFF );
			temp ^= 1 << (data >> 13);
			WRITE( 0, data & 0x1FFF, temp );
		}
		goto loop;

	case 0xCA: // MOV1 mem.bit,C
		data = READ_PC16( pc );
		pc += 2;
		{
			unsigned temp = READ( -2, data & 0x1FFF );
			unsigned bit = data >> 13;
			temp = (temp & ~(1 << bit)) | ((c >> 8 & 1) << bit);
			WRITE( 0, data & 0x1FFF, temp + no_read_before_write  );
		}
		goto loop;

	case 0xAA: // MOV1 C,mem.bit
		c = MEM_BIT( 0 );
		pc += 2;
		goto loop;

// 16. PROGRAM PSW FLAG OPERATION COMMANDS

	case 0x60: // CLRC
		c = 0;
		goto loop;

	case 0x80: // SETC
		c = ~0;
		goto loop;

	case 0xED: // NOTC
		c ^= 0x100;
		goto loop;

	case 0xE0: // CLRV
		psw &= ~(v40 | h08);
		goto loop;

	case 0x20: // CLRP
		dp = 0;
		goto loop;

	case 0x40: // SETP
		dp = 0x100;
		goto loop;

	case 0xA0: // EI
		SUSPICIOUS_OPCODE( "EI" );
		psw |= i04;
		goto loop;

	case 0xC0: // DI
		SUSPICIOUS_OPCODE( "DI" );
		psw &= ~i04;
		goto loop;

// 17. OTHER COMMANDS

	case 0x00: // NOP
		goto loop;

	case 0xFF:{// STOP
		// handle PC wrap-around
		unsigned addr = GET_PC() - 1;
		if ( addr >= 0x10000 )
		{
			addr &= 0xFFFF;
			SET_PC( addr );
			dprintf( "SPC: PC wrapped around\n" );
			goto loop;
		}
	}
	// fall through
	case 0xEF: // SLEEP
		SUSPICIOUS_OPCODE( "STOP/SLEEP" );
		--pc;
		rel_time = 0;
		m.cpu_error = "SPC emulation error";
		goto stop;
	} // switch

	assert( 0 ); // catch any unhandled instructions
}
out_of_time:
	rel_time -= m.cycle_table [*pc]; // undo partial execution of opcode
stop:

	// Uncache registers
	if ( GET_PC() >= 0x10000 )
		dprintf( "SPC: PC wrapped around\n" );
	m.cpu_regs.pc = (uint16_t) GET_PC();
	m.cpu_regs.sp = ( uint8_t) GET_SP();
	m.cpu_regs.a  = ( uint8_t) a;
	m.cpu_regs.x  = ( uint8_t) x;
	m.cpu_regs.y  = ( uint8_t) y;
	{
		int temp;
		GET_PSW( temp );
		m.cpu_regs.psw = (uint8_t) temp;
	}
}
SPC_CPU_RUN_FUNC_END

