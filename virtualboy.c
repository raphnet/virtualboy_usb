/* VBoy2USB: Virtual Boy to USB adapter
 * Copyright (C) 2009 Raphaël Assénat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The author may be contacted at raph@raphnet.net
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "gamepad.h"
#include "virtualboy.h"

#define REPORT_SIZE		6

/******** IO port definitions **************/
#define VBOY_LATCH_DDR	DDRC
#define VBOY_LATCH_PORT	PORTC
#define VBOY_LATCH_BIT	(1<<4)

#define VBOY_CLOCK_DDR	DDRC
#define VBOY_CLOCK_PORT	PORTC
#define VBOY_CLOCK_BIT	(1<<5)

#define VBOY_DATA_PORT	PORTC
#define VBOY_DATA_DDR	DDRC
#define VBOY_DATA_PIN	PINC
#define VBOY_DATA_BIT	(1<<3)

/********* IO port manipulation macros **********/
#define VBOY_LATCH_LOW()	do { VBOY_LATCH_PORT &= ~(VBOY_LATCH_BIT); } while(0)
#define VBOY_LATCH_HIGH()	do { VBOY_LATCH_PORT |= VBOY_LATCH_BIT; } while(0)
#define VBOY_CLOCK_LOW()	do { VBOY_CLOCK_PORT &= ~(VBOY_CLOCK_BIT); } while(0)
#define VBOY_CLOCK_HIGH()	do { VBOY_CLOCK_PORT |= VBOY_CLOCK_BIT; } while(0)

#define VBOY_GET_DATA()	(VBOY_DATA_PIN & VBOY_DATA_BIT)


// report matching the most recent bytes from the controller
static unsigned char last_read_controller_bytes[REPORT_SIZE];

// the most recently reported bytes
static unsigned char last_reported_controller_bytes[REPORT_SIZE];

static char right_analog_mode = 0;

static void virtualboyUpdate(void)
{
	int i,j;
	unsigned char tmp=0;
	unsigned rawdat[2];
	int x=128,y=128,rx=128,ry=128;

	VBOY_LATCH_HIGH();
	_delay_us(12);
	VBOY_LATCH_LOW();

	for (j=0; j<2; j++)
	{
		for (i=0; i<8; i++)
		{
			_delay_us(6);
			VBOY_CLOCK_LOW();

			tmp <<= 1;
			if (!VBOY_GET_DATA()) { tmp |= 1; }

			_delay_us(6);
			VBOY_CLOCK_HIGH();
		}

		rawdat[j] = tmp;
	}


	if (rawdat[0] & 0x08) // Up
		y = 0;
	if (rawdat[0] & 0x04) // Down
		y = 255;
	if (rawdat[0] & 0x02) // Left
		x = 0;
	if (rawdat[0] & 0x01) // Right
		x = 255;
	

	last_read_controller_bytes[0]=x;
	last_read_controller_bytes[1]=y;


 	last_read_controller_bytes[4]=0;
 	last_read_controller_bytes[5]=0;

	if (rawdat[1] & 0x4) // A
		last_read_controller_bytes[4] |= 0x01;
	if (rawdat[1] & 0x8) // B
		last_read_controller_bytes[4] |= 0x02;
	if (rawdat[0] & 0x10) // Start
		last_read_controller_bytes[4] |= 0x04;
	if (rawdat[0] & 0x20) // Select
		last_read_controller_bytes[4] |= 0x08;
	if (rawdat[1] & 0x10) // R
		last_read_controller_bytes[4] |= 0x10;
	if (rawdat[1] & 0x20) // L
		last_read_controller_bytes[4] |= 0x20;

	if (!right_analog_mode) {
		if (rawdat[1] & 0x40) // Up
			last_read_controller_bytes[4] |= 0x40;
		if (rawdat[1] & 0x80) // Right
			last_read_controller_bytes[4] |= 0x80;
		if (rawdat[0] & 0x80) // Down
			last_read_controller_bytes[5] |= 0x01;
		if (rawdat[0] & 0x40) // Left	
			last_read_controller_bytes[5] |= 0x02;
	}
	else {
		if (rawdat[1] & 0x40) // Up
			ry = 0;
		if (rawdat[1] & 0x80) // Right
			rx = 255;
		if (rawdat[0] & 0x80) // Down
			ry = 255;
		if (rawdat[0] & 0x40) // Left	
			rx = 0;
	}

 	last_read_controller_bytes[2]=rx;
 	last_read_controller_bytes[3]=ry;
}	

static void virtualboyInit(void)
{
	unsigned char sreg;
	sreg = SREG;
	cli();
	
	// clock and latch as output
	VBOY_LATCH_DDR |= VBOY_LATCH_BIT;
	VBOY_CLOCK_DDR |= VBOY_CLOCK_BIT;
	
	// data as input
	VBOY_DATA_DDR &= ~(VBOY_DATA_BIT);
	// enable pullup. This should prevent random toggling of pins
	// when no controller is connected.
	VBOY_DATA_PORT |= VBOY_DATA_BIT;

	// clock is normally high
	VBOY_CLOCK_PORT |= VBOY_CLOCK_BIT;

	// LATCH is Active HIGH
	VBOY_LATCH_PORT &= ~(VBOY_LATCH_BIT);

	virtualboyUpdate();

	if (last_read_controller_bytes[4] & 0x04) {
		right_analog_mode = 1;
	}

	SREG = sreg;
}
static char virtualboyChanged(void)
{
	static int first = 1;
	if (first) { first = 0;  return 1; }
	
	return memcmp(last_read_controller_bytes, 
					last_reported_controller_bytes, REPORT_SIZE);
}

static void virtualboyBuildReport(unsigned char *reportBuffer)
{
	if (reportBuffer != NULL)
	{
		memcpy(reportBuffer, last_read_controller_bytes, REPORT_SIZE);
	}
	memcpy(last_reported_controller_bytes, 
			last_read_controller_bytes, 
			REPORT_SIZE);	
}

#include "report_desc_4axes_10btns.c"

Gamepad virtualboyGamepad = {
	report_size: 		REPORT_SIZE,
	reportDescriptorSize:	sizeof(usbHidReportDescriptor_4axes_10btns),
	init: 			virtualboyInit,
	update: 		virtualboyUpdate,
	changed:		virtualboyChanged,
	buildReport:		virtualboyBuildReport
};

Gamepad *virtualboyGetGamepad(void)
{
	virtualboyGamepad.reportDescriptor = (void*)usbHidReportDescriptor_4axes_10btns;

	return &virtualboyGamepad;
}

