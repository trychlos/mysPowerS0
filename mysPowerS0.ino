/*
   MysPowerS0
   Copyright (C) 2017 Pierre Wieser <pwieser@trychlos.org>

   Description:
   Manages in one MySensors-compatible board up to four pwiPulse energy meters.

   Radio module:
   Is implemented with a NRF24L01+ radio module

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   version 2 as published by the Free Software Foundation.

   NOTE: this sketch has been written for MySensor v 2.1 library.

   One Arduino Nano is able to manage up to four Pulse S0 modules.
   Each S0 module has two child id for the powers:
   - 0: appearent power (VA)
   - 1: energy (Wh)
   and four children for the configuration:
   - 2: count of impulsions per kwh
   - 3: impulsion length (ms)
   - 4: measure module model
   - 5: enabled/disabled.

   At startup time, each module is initizalised with some suitable values
   - impkwh = 1000 imp. / kW.h
   - implen = 50 ms (length of an impulsion).

   pwi 2017- 3-26 creation
   pwi 2017- 4-20 v 1.1
   pwi 2019- 5-26 v 2.0-2019 full rewrite using pwiSensor

  Sketch uses 20862 bytes (67%) of program storage space. Maximum is 30720 bytes.
  Global variables use 1504 bytes (73%) of dynamic memory, leaving 544 bytes for local variables. Maximum is 2048 bytes.
*/

// uncomment for debugging this sketch
#define DEBUG_ENABLED

// uncomment for having mySensors radio enabled
#define HAVE_NRF24_RADIO

static const char * const thisSketchName    = "mysPowerS0";
static const char * const thisSketchVersion = "2.0-2019";

enum {
    CHILD_MAIN = 1,
    CHILD_ID_PULSE_1 = 10,
    CHILD_ID_PULSE_2 = 20,
    CHILD_ID_PULSE_3 = 30,
    CHILD_ID_PULSE_4 = 40
};

// each child sensor is able to send up to 6 info messages
#define CHILD_ID_COUNT  6

/* **************************************************************************************
 * MySensors gateway
 */
//#define MY_DEBUG
#define MY_RADIO_NRF24
#define MY_SIGNING_SOFT
#define MY_SIGNING_SOFT_RANDOMSEED_PIN 7
// do not check the uplink if we choose to disable the radio
#ifndef HAVE_NRF24_RADIO
#define MY_TRANSPORT_WAIT_READY_MS 1000
#define MY_TRANSPORT_UPLINK_CHECK_DISABLED
#endif // HAVE_NRF24_RADIO
#include <pwi_myhmac.h>
#include <pwi_myrf24.h>
#include <MySensors.h>

MyMessage msg;


/* **************************************************************************************
 *  EEPROM
 */
#include "eeprom.h"
sEeprom eeprom;

// auto save timer declared here to be available in pulse* functions
#include <pwiTimer.h>
pwiTimer main_timer;

/* **************************************************************************************
 * Pulse S0 module management
 * 
 * - up to four modules
 * - only use PCINT1 interrupts (Port C)
 * - Our two first modules are DRS155-D
 *  The count of defined sensors is in device.h, iself being included in eeprom.h.
 */
#define NO_PIN_STATE                  // to indicate that you don't need the pinState
#define NO_PIN_NUMBER                 // to indicate that you don't need the arduinoPin
#define NO_PORTB_PINCHANGES
#define NO_PORTD_PINCHANGES
#include <PinChangeInt.h>

#include "pwiPulse.h"

typedef struct {
    uint8_t           id;
    pwiPulse         *pulse;
    uint8_t           enabled_pin;
    uint8_t           irq_pin;
    uint8_t           led_pin;
    PCIntvoidFuncPtr  irqfn;
}
  sensor_t;

pwiPulse pulse1, pulse2, pulse3, pulse4;

void pulse1_irq() { pulse1.onPulse(); }
void pulse2_irq() { pulse2.onPulse(); }
void pulse3_irq() { pulse3.onPulse(); }
void pulse4_irq() { pulse4.onPulse(); }

void pulseSend( uint8_t, float, unsigned long );

sensor_t sensors[DEVICE_COUNT] = {
    { CHILD_ID_PULSE_1, &pulse1, 3, A0,  7, ( PCIntvoidFuncPtr ) pulse1_irq },
    { CHILD_ID_PULSE_2, &pulse2, 4, A1,  8, ( PCIntvoidFuncPtr ) pulse2_irq },
    { CHILD_ID_PULSE_3, &pulse3, 5, A2, A4, ( PCIntvoidFuncPtr ) pulse3_irq },
    { CHILD_ID_PULSE_4, &pulse4, 6, A3, A5, ( PCIntvoidFuncPtr ) pulse4_irq },
};

/*
 * Present a Pulse S0 module
 * At this time, it is not yet initialized.
 * But we present at least the CHILD_ID to CHILD_ID+5 internal sensors.
 */
void pulsePresentation( uint8_t idx )
{
    for( uint8_t count=0 ; count<CHILD_ID_COUNT ; ++count ){
        present( sensors[idx].id+count, S_POWER );
    }
}

void pulseSetup( uint8_t idx )
{
    sensors[idx].pulse->setupId( sensors[idx].id );
    sensors[idx].pulse->setupDevice( eeprom.device[idx] );
    sensors[idx].pulse->setupPins( sensors[idx].enabled_pin, sensors[idx].led_pin );
    sensors[idx].pulse->setupSendCb( pulseSend );
    sensors[idx].pulse->setupTimers( eeprom.min_period_ms, eeprom.max_period_ms );

    digitalWrite( sensors[idx].irq_pin, HIGH );
    pinMode( sensors[idx].irq_pin, INPUT );
    PCintPort::attachInterrupt( sensors[idx].irq_pin, sensors[idx].irqfn, FALLING );
}

void pulseSend( uint8_t id, float watt, unsigned long wh )
{
    msg.clear();
    send( msg.setSensor( id ).setType( V_WATT ).set( watt, 0 ));
    msg.clear();
    send( msg.setSensor( id+1 ).setType( V_KWH ).set( wh ));
}

void pulseDumpConfiguration( uint8_t id, pwiPulse *pulse )
{
    msg.clear();
    send( msg.setSensor( id+2 ).setType( V_VAR1 ).set( pulse->getDevice()->impkwh ));
    msg.clear();
    send( msg.setSensor( id+3 ).setType( V_VAR1 ).set( pulse->getDevice()->implen ));
    msg.clear();
    send( msg.setSensor( id+4 ).setType( V_VAR1 ).set( pulse->getDevice()->device ));
    msg.clear();
    send( msg.setSensor( id+5 ).setType( V_VAR1 ).set( pulse->isEnabled()));
}

#if 0
    void pulseReceiveReq( uint8_t id, const char *payload )
    {
        uint8_t idx = ( uint8_t )( id / 10 );
        if( idx > 0 ){
            pwiPulse *pulse = sensors[idx-1].pulse;
            sDevice *device = pulse->getDevice();
            uint8_t cmd = id - 10*idx;
        
            switch( cmd ){
                case 1:
                    if( !strcmp( payload, "RAZ" )){
                        device->countwh = 0;
                    }
                    break;
            }
        }
    }
#endif

void pulseReceiveSet( uint8_t id, const char *payload )
{
    uint8_t idx = ( uint8_t )( id / 10 );
    if( idx > 0 ){
        pwiPulse *pulse = sensors[idx-1].pulse;
        sDevice *device = pulse->getDevice();
        
        uint8_t cmd = id - 10*idx;
        unsigned long ulong = atol( payload );
        bool changed = false;
    
        switch( cmd ){
            case 2:
                device->impkwh = ulong;
                changed = true;
                break;
            case 3:
                device->implen = ulong;
                changed = true;
                break;
            case 4:
                memset( device->device, '\0', DEVICE_NAME_SIZE );
                strcpy( device->device, payload );
                changed = true;
                break;
        }
    
        if( changed ){
            eepromWrite( eeprom, saveState );
            main_timer.restart();
        }
    
        pulseDumpConfiguration( 10*idx, pulse );
    }
}

/* **************************************************************************************
 *  CHILD_MAIN Sensor
 */

void mainOnAutoSave( void*empty )
{
    eepromWrite( eeprom, saveState );
    main_timer.restart();
}

void mainSetup()
{
    main_timer.setup( "MainTimer", eeprom.auto_save_ms, false, ( pwiTimerCb ) mainOnAutoSave );
    main_timer.start();
}

void mainReceiveReq( uint8_t ureq, unsigned long ulong )
{
    switch( ureq ){
        case 1:
            eepromReset( eeprom, saveState );
            break;
        case 2:
            dumpConfiguration();
            break;
        case 3:
            mainSetMaxFrequency( ulong );
            mainSendMaxFrequency();
            break;
        case 4:
            mainSetUnchanged( ulong );
            mainSendUnchanged();
            break;
        case 5:
            eepromWrite( eeprom, saveState );
            main_timer.restart();
            break;
        case 6:
            mainSetAutosave( ulong );
            mainSendAutosave();
            break;
    }
}

void mainSetAutosave( unsigned long ulong )
{
    eeprom.auto_save_ms = ulong;
    eepromWrite( eeprom, saveState );
    main_timer.setDelay( ulong );
    main_timer.restart();
}

void mainSendAutosave()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN ).setType( V_VAR3 ).set( eeprom.auto_save_ms ));
}

void mainSetMaxFrequency( unsigned long ulong )
{
    eeprom.min_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    main_timer.restart();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i].pulse->setupTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

void mainSendMaxFrequency()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN ).setType( V_VAR1 ).set( eeprom.min_period_ms ));
}

void mainSetUnchanged( unsigned long ulong )
{
    eeprom.max_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    main_timer.restart();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i].pulse->setupTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

void mainSendUnchanged()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN ).setType( V_VAR2 ).set( eeprom.max_period_ms ));
}

void mainSendEnabledCount()
{
    uint8_t count = 0;
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        if( sensors[i].pulse->isEnabled()){
            count += 1;
        }
    }
    msg.clear();
    send( msg.setSensor( CHILD_MAIN ).setType( V_VAR5 ).set( count ));
}

/* **************************************************************************************
    MAIN CODE
*/
void presentation()
{
#ifdef DEBUG_ENABLED
    Serial.println( "[presentation]" );
#endif
    sendSketchInfo( thisSketchName, thisSketchVersion );
    present( CHILD_MAIN, S_CUSTOM );
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        pulsePresentation( i );
    }
}

void setup()
{
#ifdef DEBUG_ENABLED
    Serial.begin( 115200 );
    Serial.println( F( "[setup]" ));
#endif
    //eepromReset( eeprom, saveState );
    eepromRead( eeprom, loadState, saveState );
    mainSetup();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        pulseSetup( i );
    }
}

void loop()
{
#ifdef DEBUG_ENABLED
    //Serial.println( F( "[loop]" ));
#endif
    pwiTimer::Loop();
}

/**
 * Handle a V_CUSTOM request:
 */
void receive(const MyMessage &message)
{
    if( message.type != V_CUSTOM ){
        return;
    }

    uint8_t cmd = message.getCommand();
    uint8_t sensor = message.sensor;

    char payload[2*MAX_PAYLOAD+1];
    memset( payload, '\0', sizeof( payload ));
    message.getString( payload );

    uint8_t ureq = strlen( payload ) > 0 ? atoi( payload ) : 0;
    unsigned long ulong = strlen( payload ) > 2 ? atol( payload+2 ) : 0;

    switch( cmd ){
        case C_REQ:
            if( sensor == CHILD_MAIN ){
                mainReceiveReq( ureq, ulong );
            }
            break;
        case C_SET:
            if( sensor > CHILD_ID_PULSE_1 ){
                pulseReceiveSet( sensor, payload );
            }
            break;
    }
}

void dumpConfiguration()
{
    mainSendAutosave();
    mainSendMaxFrequency();
    mainSendUnchanged();
    mainSendEnabledCount();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        pulseDumpConfiguration( sensors[i].id, sensors[i].pulse );
    }
}

