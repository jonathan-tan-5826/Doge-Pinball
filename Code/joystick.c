/*
* joystick.c
*
* Created: 3/9/2017 1:35:21 PM
*  Author: Jonathan Tan
*/

#include <avr/io.h>
#include <util/delay.h>
#include "io.h"
#include "a2d.h"
#include "joystick.h"

#define JOYSTICK_THRESHOLD_HIGH 950
#define JOYSTICK_THRESHOLD_LOW 150

unsigned short GetJoystick_X()
{
	Set_A2D_Pin(5);
	_delay_ms(1);
	return ADC;
}

unsigned short GetJoystick_Y()
{
	Set_A2D_Pin(6);
	_delay_ms(1);
	return ADC;
}