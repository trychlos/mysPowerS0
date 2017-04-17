/*
 * MysPowerS0
 * Copyright (C) 2017 Pierre Wieser <pwieser@trychlos.org>
 *
 * Description:
 * Manages in one MySensors-compatible board up to four PulseS0 energy meters.
 *
 * Radio module:
 * Is implemented with a NRF24L01+ radio module
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * NOTE: this sketch has been written for MySensor v 2.1 library.
 * 
 * pwi 2017- 3-26 creation
 */

// uncomment for debugging this sketch
#define DEBUG_ENABLED

// uncomment for having mySensors radio enabled
#define HAVE_NRF24_RADIO

// uncomment for having PulseS0 code
#define HAVE_PULSES0

static const char * const thisSketchName    = "mysPowerS0";
static const char * const thisSketchVersion = "1.0-2017";

//#define MY_DEBUG
#define MY_RADIO_NRF24
#define MY_RF24_CHANNEL 103
#define MY_SIGNING_SOFT
#define MY_SIGNING_SOFT_RANDOMSEED_PIN 7
#define MY_SOFT_HMAC_KEY 0xe5,0xc5,0x36,0xd8,0x4b,0x45,0x49,0x25,0xaa,0x54,0x3b,0xcc,0xf4,0xcb,0xbb,0xb7,0x77,0xa4,0x80,0xae,0x83,0x75,0x40,0xc2,0xb1,0xcb,0x72,0x50,0xaa,0x8a,0xf1,0x6d
// do not check the uplink if we choose to disable the radio
#ifndef HAVE_NRF24_RADIO
#define MY_TRANSPORT_WAIT_READY_MS 1000
#define MY_TRANSPORT_UPLINK_CHECK_DISABLED
#endif // HAVE_NRF24_RADIO
#include <MySensors.h>

enum {
    CHILD_ID_PULSE_WH_1 = 10,
    CHILD_ID_PULSE_VA_1,
    CHILD_ID_PULSE_WH_2 = 20,
    CHILD_ID_PULSE_VA_2,
    CHILD_ID_PULSE_WH_3 = 30,
    CHILD_ID_PULSE_VA_3,
    CHILD_ID_PULSE_WH_4 = 40,
    CHILD_ID_PULSE_VA_4,
};

/* **************************************************************************************
 * MySensors gateway
 */
void presentation_my_sensors()  
{
#ifdef DEBUG_ENABLED
    Serial.println( F( "[presentation_my_sensors]" ));
#endif
#ifdef HAVE_NRF24_RADIO
    sendSketchInfo( thisSketchName, thisSketchVersion );
#endif // HAVE_NRF24_RADIO
}

/* **************************************************************************************
 * PulseS0 module management
 * - up to four modules
 * - only use PCINT1 interrupts (Port C)
 */
#ifdef HAVE_PULSES0

#define NO_PIN_STATE                  // to indicate that you don't need the pinState
#define NO_PIN_NUMBER                 // to indicate that you don't need the arduinoPin
#define NO_PORTB_PINCHANGES
#define NO_PORTD_PINCHANGES
#include <PinChangeInt.h>
#include "drs155.h"

MyMessage msgWATT( 0, V_WATT );       // actually VA
MyMessage msgKWH( 0, V_KWH );         // actually WH

// PulseS0 energy meters
// - mySensors WH node_child_id
// - mySensors VA node_child_id
// - data input (digital pin)
// - enabled (digital pin)
// - led (digital pin)
// Two first modules are DRS155-D
// Only declare these two modules because we do not know (at time of creation)
// which type of modules will be used for the two supplementaries
Drs155 pulse_1( CHILD_ID_PULSE_WH_1, CHILD_ID_PULSE_VA_1, A0, 3,  7 );
Drs155 pulse_2( CHILD_ID_PULSE_WH_2, CHILD_ID_PULSE_VA_2, A1, 4,  8 );

void pulse_irq_1(){ pulse_1.onPulse(); }
void pulse_irq_2(){ pulse_2.onPulse(); }

/*
 * Setup the DSR155 modules
 * ret=1 is OK.
 */
void presentation_pulse( void *vdrs, void *irq_fn )
{
    Drs155 *module = ( Drs155 * ) vdrs;
    byte pin = module->getIrqPin();
    uint8_t ret = PCintPort::attachInterrupt( pin, ( PCIntvoidFuncPtr ) irq_fn, FALLING );
#ifdef DEBUG_ENABLED
    Serial.print( F( "[presentation_pulses0] pin=" ));
    Serial.print( pin );        
    Serial.print( F( ", ret=" ));
    Serial.println( ret );        
#endif
#ifdef HAVE_NRF24_RADIO
    present( module->getWHChildId(), S_POWER );
    present( module->getVAChildId(), S_POWER );
#endif
}

/*
 * sends the PulseS0 data, either because the measure has changed
 * or because the timeout has been reached
 */
void pulse_output( void *vdata, void *kwh_msg, void *watt_msg )
{
    PulseS0Data *data = ( PulseS0Data * ) vdata;
#ifdef DEBUG_ENABLED
    pulses0_dump( *data );
#endif
#ifdef HAVE_NRF24_RADIO
    MyMessage *msg_kwh = ( MyMessage * ) kwh_msg;
    MyMessage *msg_watt = ( MyMessage * ) watt_msg;
    msg_kwh->setSensor( data->wh_child_id );
    msg_kwh->set( data->count_wh );
    send( *msg_kwh );
    msg_watt->setSensor( data->va_child_id );
    msg_watt->set( data->power_w, 0 );
    send( *msg_watt );
#endif
}
#endif // HAVE_PULSES0

/* **************************************************************************************
 *  MAIN CODE
 */
void presentation()
{
#ifdef DEBUG_ENABLED
    Serial.println( "[presentation]" );
#endif
    presentation_my_sensors();
#ifdef HAVE_PULSES0
    presentation_pulse(( void * ) &pulse_1, ( void * ) pulse_irq_1 );
    presentation_pulse(( void * ) &pulse_2, ( void * ) pulse_irq_2 );
#endif
}

void setup()  
{
#ifdef DEBUG_ENABLED
    Serial.begin( 115200 );
    Serial.println( F( "[setup]" ));
#endif
}

void loop()      
{
#ifdef DEBUG_ENABLED
    Serial.println( F( "[loop]" ));
#endif
#ifdef HAVE_PULSES0
    pulse_1.runLoop( pulse_output, &msgKWH, &msgWATT );
    pulse_2.runLoop( pulse_output, &msgKWH, &msgWATT );
#endif
    wait( 500 );
}

