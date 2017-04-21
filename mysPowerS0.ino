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
 * One Arduino Nano is able to manage up to four Pulse S0 modules.
 * Each S0 module has two child ids:
 * - 0: appearent power (VA)
 * - 1: energy (Wh).
 * 
 * At startup time, each module is initizalised with some suitable values
 * - impkwh = 1000 imp. / kW.h
 * - implen = 50 ms (length of an impulsion).
 * 
 * These configuration parameters can be modified from the controller via
 * commands adressed to any of the two child ids of the module:
 * - C_SET:
 *   payload = '1=<value>' count of impulsions per kW.h (default=1000)
 *   payload = '2=<value>' length of the impulsion (ms)
 *   payload = '3=<value>' model name (22 chars max)
 *   payload = '4=<impkwh>,<implen>,<name>'
 * - C_REQ:
 *   payload = '1' send count of impulsions per kWh (impkwh) as a string 'kwh=<value>'
 *   payload = '2' send the minimal length of the impulsion as a string 'l=<value>'
 *   payload = '3' send the preset model name as a string 'n=<value>'
 *   payload = '4' send all the previous three messages
 * 
 * Global configuration can be:
 * - C_SET:
 *   payload = '1'         reset the loop interval to its default value
 *   payload = '2=<value>' set the loop interval to the provided value (ms)
 * - C_REQ:
 *   payload = '1' send the default loop interval
 *   payload = '2' send the current loop interval
 *   payload = '3' send the current count of enabled modules
 * 
 * pwi 2017- 3-26 creation
 * pwi 2017- 4-20 v 1.1
 */

// uncomment for debugging this sketch
#define DEBUG_ENABLED

// uncomment for debugging eeprom functions
#define EEPROM_DEBUG

// uncomment for having mySensors radio enabled
#define HAVE_NRF24_RADIO

// uncomment for having PulseS0 code
#define HAVE_PULSES0

static const char * const thisSketchName    = "mysPowerS0";
static const char * const thisSketchVersion = "1.2-2017";

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
    CHILD_ID_GLOBAL = 1,
    CHILD_ID_PULSE_VA_1 = 10,
    CHILD_ID_PULSE_WH_1,
    CHILD_ID_PULSE_VA_2 = 20,
    CHILD_ID_PULSE_WH_2,
    CHILD_ID_PULSE_VA_3 = 30,
    CHILD_ID_PULSE_WH_3,
    CHILD_ID_PULSE_VA_4 = 40,
    CHILD_ID_PULSE_WH_4,
};

static const uint16_t wait_interval_default = 1000;      /* 1000 ms = 1 sec. */
static uint16_t wait_interval = wait_interval_default;

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
    present( CHILD_ID_GLOBAL, S_CUSTOM );
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
#include "pulses0.h"
#include "eeprom.h"

MyMessage msgWATT( 0, V_WATT );       // actually VA
MyMessage msgKWH( 0, V_KWH );         // actually WH
MyMessage msgCustom( 0, V_CUSTOM );

// PulseS0 energy meters
// - mySensors WH node_child_id
// - mySensors VA node_child_id
// - data input (digital pin)
// - enabled (digital pin)
// - led (digital pin)
// Two first modules are DRS155-D
// Only declare these two modules because we do not know (at time of creation)
// which type of modules will be used for the two supplementaries
PulseS0 pulse_1( CHILD_ID_PULSE_VA_1, CHILD_ID_PULSE_WH_1, A0, 3,  7 );
PulseS0 pulse_2( CHILD_ID_PULSE_VA_2, CHILD_ID_PULSE_WH_2, A1, 4,  8 );
PulseS0 pulse_3( CHILD_ID_PULSE_VA_3, CHILD_ID_PULSE_WH_3, A2, 5, A4 );
PulseS0 pulse_4( CHILD_ID_PULSE_VA_4, CHILD_ID_PULSE_WH_4, A3, 6, A5 );

void pulse_irq_1(){ pulse_1.onPulse(); }
void pulse_irq_2(){ pulse_2.onPulse(); }
void pulse_irq_3(){ pulse_3.onPulse(); }
void pulse_irq_4(){ pulse_4.onPulse(); }

/*
 * Setup the Pulse S0 modules
 * ret=1 is OK.
 */
void presentation_pulse( void *vdrs, void *irq_fn )
{
    PulseS0 *module = ( PulseS0 * ) vdrs;
    byte pin = module->getIrqPin();
    uint8_t ret = PCintPort::attachInterrupt( pin, ( PCIntvoidFuncPtr ) irq_fn, FALLING );
#ifdef DEBUG_ENABLED
    Serial.print( F( "[presentation_pulses0] pin=" ));
    Serial.print( pin );        
    Serial.print( F( ", ret=" ));
    Serial.println( ret );        
#endif
#ifdef HAVE_NRF24_RADIO
    present( module->getVAChildId(), S_POWER );
    present( module->getWHChildId(), S_POWER );
#endif
}

/*
 * sends the PulseS0 data, either because the measure has changed
 * or because the timeout has been reached
 */
void pulse_output( void *vdata, void *kwh_msg, void *watt_msg )
{
    PulseS0Data *data = ( PulseS0Data * ) vdata;
#ifdef HAVE_NRF24_RADIO
    // VA
    MyMessage *msg_watt = ( MyMessage * ) watt_msg;
    msg_watt->setSensor( data->va_child_id );
    msg_watt->set( data->power_w, 0 );
    send( *msg_watt );
    // Wh
    MyMessage *msg_kwh = ( MyMessage * ) kwh_msg;
    msg_kwh->setSensor( data->wh_child_id );
    msg_kwh->set( data->count_wh );
    send( *msg_kwh );
#endif
}

/*
 * sends a custom message
 */
void pulse_custom( byte child_id, const char *content )
{
#ifdef DEBUG_ENABLED
    Serial.print( F( "[pulse_custom] child_id=" )); Serial.print( child_id );
    Serial.print( F( ", content=" ));               Serial.println( content );
#endif
#ifdef HAVE_NRF24_RADIO
    msgCustom.setSensor( child_id );
    msgCustom.set( content );
    send( msgCustom );
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
    presentation_pulse(( void * ) &pulse_3, ( void * ) pulse_irq_3 );
    presentation_pulse(( void * ) &pulse_4, ( void * ) pulse_irq_4 );
#endif
}

void setup()  
{
#ifdef DEBUG_ENABLED
    Serial.begin( 115200 );
    Serial.println( F( "[setup]" ));
#endif
  /* eeprom_raz(); */
}

void loop()      
{
#ifdef DEBUG_ENABLED
    Serial.println( F( "[loop]" ));
#endif
#ifdef HAVE_PULSES0
    pulse_1.runLoop( pulse_output, &msgKWH, &msgWATT );
    pulse_2.runLoop( pulse_output, &msgKWH, &msgWATT );
    pulse_3.runLoop( pulse_output, &msgKWH, &msgWATT );
    pulse_4.runLoop( pulse_output, &msgKWH, &msgWATT );
#endif
    wait( wait_interval );
}

/*
 * Handle a V_CUSTOM request:
 * 
 * C_SET/C_REQ on a S0 sensor: set/get one the three settable properties
 * (impkwh, implen, name)
 * 
 * C_SET on the GLOBAL child id:
 * - 0: reset the default loop delay
 * - 1;<value>: set the loop delay
 * 
 * C_REQ on the GLOBAL child id:
 * - 0: get the default loop delay
 * - 1: get the current loop delay
 * - 2: get the current count of enabled S0 modules
 */
void receive(const MyMessage &message)
{
    char buffer[2*MAX_PAYLOAD+1];
    if( message.type == V_CUSTOM ){
        uint8_t cmd = message.getCommand();
        memset( buffer, '\0', sizeof( buffer ));
        message.getString( buffer );
        if( cmd == C_SET ){
            if( message.sensor == CHILD_ID_GLOBAL ){
                set_from_payload( buffer );
            }
            else if( message.sensor == CHILD_ID_PULSE_VA_1 || message.sensor == CHILD_ID_PULSE_WH_1 ){
                pulse_1.setFromPayload( buffer );
            } else if( message.sensor == CHILD_ID_PULSE_VA_2 || message.sensor == CHILD_ID_PULSE_WH_2 ){
                pulse_2.setFromPayload( buffer );
            } else if( message.sensor == CHILD_ID_PULSE_VA_3 || message.sensor == CHILD_ID_PULSE_WH_3 ){
                pulse_3.setFromPayload( buffer );
            } else if( message.sensor == CHILD_ID_PULSE_VA_4 || message.sensor == CHILD_ID_PULSE_WH_4 ){
                pulse_4.setFromPayload( buffer );
            }
        } else if( cmd == C_REQ ){
            if( message.sensor == CHILD_ID_GLOBAL ){
                get_from_payload( buffer );
            } else if( message.sensor == CHILD_ID_PULSE_VA_1 || message.sensor == CHILD_ID_PULSE_WH_1 ){
                pulse_1.getFromPayload( buffer, pulse_custom );
            } else if( message.sensor == CHILD_ID_PULSE_VA_2 || message.sensor == CHILD_ID_PULSE_WH_2 ){
                pulse_2.getFromPayload( buffer, pulse_custom );
            } else if( message.sensor == CHILD_ID_PULSE_VA_3 || message.sensor == CHILD_ID_PULSE_WH_3 ){
                pulse_3.getFromPayload( buffer, pulse_custom );
            } else if( message.sensor == CHILD_ID_PULSE_VA_4 || message.sensor == CHILD_ID_PULSE_WH_4 ){
                pulse_4.getFromPayload( buffer, pulse_custom );
            }
        }
    }
}

void set_from_payload( const char *payload )
{
    char *p = ( char * ) payload;
    char *str = strtok_r( p, "=", &p );
    if( str && strlen( str ) > 0 ){
        int reqid = atoi( str );
        switch( reqid ){
            /* reset the default loop interval */
            case 1:
                wait_interval = wait_interval_default;
                break;
            /* set the loop interval to the provided value */
            case 2:
                if( p && strlen( p ) > 0 ){
                    wait_interval = atoi( p );
                }
                break;
        }
    }
}

void get_from_payload( const char *payload )
{
    msgCustom.setSensor( CHILD_ID_GLOBAL );
    int reqid = atoi( payload );
    switch( reqid ){
        /* sends the default loop interval */
        case 1:
            msgCustom.set( wait_interval_default );
            send( msgCustom );
            break;
        /* sends the current loop interval */
        case 2:
            msgCustom.set( wait_interval );
            send( msgCustom );
            break;
        /* sends the current count of enabled modules */
        case 3:
            uint8_t count = 0;
            if( pulse_1.isEnabled()) count += 1;
            if( pulse_2.isEnabled()) count += 1;
            if( pulse_3.isEnabled()) count += 1;
            if( pulse_4.isEnabled()) count += 1;
            msgCustom.set( count );
            send( msgCustom );
            break;
    }
}

/**
 * eeprom_get_pos:
 * @child_id: the VA child_id (10, 20, 30, 40).
 * 
 * Returns: the position of the sModule data for this child.
 */
uint8_t eeprom_get_pos( uint8_t child_id )
{
    return(((( uint8_t )( child_id / 10 )) - 1 ) * 64 );
}

/**
 * eeprom_dump:
 */
void eeprom_dump( sModule &sdata )
{
#ifdef EEPROM_DEBUG
    Serial.print( F( "[eeprom_dump] child_id=" )); Serial.print( sdata.child_id );
    Serial.print( F( ", impkwh=" ));               Serial.print( sdata.impkwh );
    Serial.print( F( ", implen=" ));               Serial.print( sdata.implen );
    Serial.print( F( ", name=" ));                 Serial.println( sdata.name );
#endif
}

/**
 * eeprom_read:
 * @child_id: the VA child_id (10, 20, 30, 40)
 * @pdata: the sModule buffer to be filled.
 *
 * Read the data from the EEPROM.
 * 
 * Returns: %TRUE if the data has been found,
 * %FALSE if the data was not yet initialized.
 */
bool eeprom_read( uint8_t child_id, sModule &sdata )
{
    memset( &sdata, '\0', sizeof( sModule ));
    uint8_t pos = eeprom_get_pos( child_id );
    uint8_t read_id = loadState( pos );
    if( read_id == child_id ){
        sdata.child_id = read_id;
        uint8_t i;
        for( i=1 ; i<sizeof( sModule ); ++i ){
            (( uint8_t * ) &sdata )[i] = loadState( pos+i );
        }
#ifdef EEPROM_DEBUG
        Serial.print( F( "[eeprom_read] read data at pos=" ));
        Serial.print( pos );
        Serial.println( ":" );
        eeprom_dump( sdata );
    } else {
          Serial.print( F( "[eeprom_read] child_id=" )); Serial.print( child_id );
          Serial.print( F( ", read_id=" ));              Serial.print( read_id );
          Serial.println( F( ": returning false" ));
#endif
    }
    return( read_id == child_id );
}

/**
 * eeprom_write:
 * @child_id: the VA child_id (10, 20, 30, 40)
 * @pdata: the sModule struct to be written.
 *
 * Write the data from the EEPROM.
 * 
 * Returns: %TRUE if successfull.
 */
bool eeprom_write( uint8_t child_id, sModule &sdata )
{
    uint8_t pos = eeprom_get_pos( child_id );
    sdata.child_id = child_id;
    uint8_t i;
    for( i=0 ; i<sizeof( sModule) ; ++i ){
        saveState( pos+i, (( uint8_t * ) &sdata )[i] );
    }
#ifdef EEPROM_DEBUG
    Serial.print( F( "[eeprom_write] written data at pos=" ));
    Serial.print( pos );
    Serial.println( ":" );
    eeprom_dump( sdata );
#endif
    return( true );
}

/**
 * eeprom_raz:
 *
 * RAZ the user data of the EEPROM.
 */
void eeprom_raz( void )
{
#ifdef EEPROM_DEBUG
    Serial.print( F( "[eeprom_raz] begin=" )); Serial.println( millis());
#endif
    uint16_t i;
    for( i=0 ; i<256 ; ++i ){
        saveState(( uint8_t ) i, 0 );
    }
#ifdef EEPROM_DEBUG
    Serial.print( F( "[eeprom_raz] end=" )); Serial.println( millis());
#endif
}

