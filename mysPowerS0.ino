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
   pwi 2017- 4-20 v1.1
   pwi 2019- 5-26 v2.0-2019 full rewrite using pwiSensor
   pwi 2019- 5-26 v2.1-2019
                    send library version
                    reset power on pulse timeout
                    implement periodic data dump
   pwi 2019- 5-26 v2.1.4-2019
                    present autodump delay sensor (todo #4)
                    send power with one decimal digit (todo #5)
                    force float value in set() method (todo #6)
                    pwiTimer better interprets timer changes
  Sketch uses 21596 bytes (70%) of program storage space. Maximum is 30720 bytes.
  Global variables use 1628 bytes (79%) of dynamic memory, leaving 420 bytes for local variables. Maximum is 2048 bytes.
  
   pwi 2019- 9-15 v2.2-2019
                    update to pwiPrivate v190902
                    update to pwiCommon v190904
                    use PROGMEM macro
                    remove signing code
                    remove untilNow() code
  Sketch uses 22638 bytes (73%) of program storage space. Maximum is 30720 bytes.
Global variables use 1102 bytes (53%) of dynamic memory, leaving 946 bytes for local variables. Maximum is 2048 bytes.
 *
 * pwi 2019-xx-xx v2.3-2019
 *                  use  PGMSTR macro to handle sket name and version
 * pwi 2025- 3-21 v3.0-2025
 *                  no more use interrupts
 * pwi 2025- 3-22 v3.1-2025
 *                  use new pwiSensor interface
 *                  no more send data for not-enabled sensors
 * pwi 2025- 3-23 v3.2-2025
 *                fix enabled counter detection (input mode of enabled pin)
 * Sketch uses 20182 bytes (65%) of program storage space. Maximum is 30720 bytes.
 * Global variables use 1043 bytes (50%) of dynamic memory, leaving 1005 bytes for local variables. Maximum is 2048 bytes.
*/

// uncomment for debugging this sketch
#define SKETCH_DEBUG

static char const sketchName[] PROGMEM    = "mysPowerS0";
static char const sketchVersion[] PROGMEM = "3.2-2025";

enum {
    CHILD_MAIN = 1,
    CHILD_ID_PULSE_1 = 10,
    CHILD_ID_PULSE_2 = 20,
    CHILD_ID_PULSE_3 = 30,
    CHILD_ID_PULSE_4 = 40
};

// each child sensor is able to send up to 6 info messages (from +0 to +5)
#define CHILD_ID_COUNT  6

/* **************************************************************************************
 * MySensors gateway
 */
#define MY_DEBUG
#define MY_RADIO_NRF24
#define MY_RF24_PA_LEVEL RF24_PA_HIGH
#include <pwi_myhmac.h>
#include <pwi_myrf24.h>
#include <MySensors.h>

MyMessage msg;

/*
 * Declare our classes
 */
#include <pwiCommon.h>
#include <pwiTimer.h>
#include "eeprom.h"

sEeprom eeprom;
pwiTimer autosave_timer;
pwiTimer autodump_timer;

/* **************************************************************************************
 * Pulse S0 module management
 * 
 * - up to four modules
 *  The count of defined sensors is in device.h, iself being included in eeprom.h.
 */

#include "power_counter.h"

PowerCounter counter1( CHILD_ID_PULSE_1, 3, A0,  7 );
PowerCounter counter2( CHILD_ID_PULSE_2, 4, A1,  8 );
PowerCounter counter3( CHILD_ID_PULSE_3, 5, A2, A4 );
PowerCounter counter4( CHILD_ID_PULSE_4, 6, A3, A5 );

void powerSend( uint8_t id, uint32_t watt, uint32_t wh );

PowerCounter *sensors[DEVICE_COUNT] = {
    &counter1,
    &counter2,
    &counter3,
    &counter4
};

/*
 * Present a Pulse S0 module
 * At this time, it is not yet initialized.
 * But we present at least the CHILD_ID+0 to CHILD_ID+5 internal sensors.
 */
void powerPresentation( uint8_t idx )
{
    for( uint8_t count=0 ; count<CHILD_ID_COUNT ; ++count ){
        present( sensors[idx]->getId()+count, S_POWER );
    }
}

void powerSetup( uint8_t idx )
{
    // setup computed properties
    sensors[idx]->setDevice( eeprom.device[idx] );
    sensors[idx]->setSendFn( powerSend );
    sensors[idx]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
}

void powerDumpData( uint8_t idx )
{
    uint8_t id = sensors[idx]->getId();
    msg.clear();
    send( msg.setSensor( id+2 ).setType( V_VAR1 ).set( sensors[idx]->getDevice()->impkwh ));
    msg.clear();
    send( msg.setSensor( id+3 ).setType( V_VAR1 ).set( sensors[idx]->getDevice()->implen ));
    msg.clear();
    send( msg.setSensor( id+4 ).setType( V_VAR1 ).set( sensors[idx]->getDevice()->device ));
    msg.clear();
    send( msg.setSensor( id+5 ).setType( V_VAR1 ).set( sensors[idx]->isEnabled()));
}

void powerReceiveSet( uint8_t id, const char *payload )
{
    uint8_t idx = ( uint8_t )( id / 10 );
    if( idx > 0 ){
        PowerCounter *sensor = sensors[idx-1];
        sDevice *device = sensor->getDevice();
        
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
                memset( device->device, '\0', DEVICE_NAME_SIZE+1 );
                strcpy( device->device, payload );
                changed = true;
                break;
        }
    
        if( changed ){
            eepromWrite( eeprom, saveState );
            autosave_timer.restart();
        }

        powerDumpData( idx-1 );
    }
}

void powerSend( uint8_t id, uint32_t watt, uint32_t wh )
{
    msg.clear();
    send( msg.setSensor( id ).setType( V_WATT ).set(( uint32_t ) watt ));
    msg.clear();
    send( msg.setSensor( id+1 ).setType( V_KWH ).set(( uint32_t ) wh ));
}

/* **************************************************************************************
 *  CHILD_MAIN Sensor
 */

void mainAutoSaveCb( void *empty );
void mainAutoDumpCb( void *empty );

void mainPresentation()
{
    //                                   1234567890123456789012345
    present( CHILD_MAIN+1, S_CUSTOM, F( "Min period timer" ));
    present( CHILD_MAIN+2, S_CUSTOM, F( "Max period timer" ));
    present( CHILD_MAIN+3, S_CUSTOM, F( "Enabled modules count" ));
    present( CHILD_MAIN+4, S_CUSTOM, F( "AutoSave delay" ));
    present( CHILD_MAIN+5, S_CUSTOM, F( "AutoDump delay" ));
}

void mainSetup()
{
    autosave_timer.setup( "AutosaveTimer", eeprom.auto_save_ms, false, ( pwiTimerCb ) mainAutoSaveCb );
    autosave_timer.start();
    autodump_timer.setup( "AutodumpTimer", eeprom.auto_dump_ms, false, ( pwiTimerCb ) mainAutoDumpCb );
    autodump_timer.start();
}

void mainAutoDumpCb( void*empty )
{
    dumpData();
}

void mainAutoDumpSend()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN+5 ).setType( V_VAR1 ).set( eeprom.auto_dump_ms ));
}

void mainAutoDumpSet( unsigned long ulong )
{
    eeprom.auto_dump_ms = ulong;
    eepromWrite( eeprom, saveState );
    autodump_timer.setDelay( ulong );
}

void mainAutoSaveCb( void*empty )
{
    eepromWrite( eeprom, saveState );
    autosave_timer.restart();
}

void mainAutoSaveSend()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN+4 ).setType( V_VAR1 ).set( eeprom.auto_save_ms ));
}

void mainAutoSaveSet( unsigned long ulong )
{
    eeprom.auto_save_ms = ulong;
    eepromWrite( eeprom, saveState );
    autosave_timer.setDelay( ulong );
}

void mainEnabledCountSend()
{
    uint8_t count = 0;
    bool enabled = false;
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        enabled = sensors[i]->isEnabled();
        count += ( enabled ? 1 : 0 );
    }
#ifdef SKETCH_DEBUG
    Serial.print( F( "counted " ));
    Serial.print( count );
    Serial.println( F( " enabled modules" ));
#endif
    msg.clear();
    send( msg.setSensor( CHILD_MAIN+3 ).setType( V_VAR1 ).set( count ));
}

void mainMaxFrequencySend()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN+1 ).setType( V_VAR1 ).set( eeprom.min_period_ms ));
}

void mainMaxFrequencySet( unsigned long ulong )
{
    eeprom.min_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

void mainUnchangedSend()
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN+2 ).setType( V_VAR1 ).set( eeprom.max_period_ms ));
}

void mainUnchangedSet( unsigned long ulong )
{
    eeprom.max_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

/* **************************************************************************************
    MAIN CODE
*/
void presentation()
{
#ifdef SKETCH_DEBUG
    Serial.println( "presentation()" );
#endif
    mainPresentation();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        powerPresentation( i );
    }
}

void setup()
{
#ifdef SKETCH_DEBUG
    Serial.begin( 115200 );
    Serial.println( F( "setup()" ));
#endif
    sendSketchInfo( PGMSTR( sketchName ), PGMSTR( sketchVersion ));

    // library version
    msg.clear();
    mSetCommand( msg, C_INTERNAL );
    sendAsIs( msg.setSensor( 255 ).setType( I_VERSION ).set( MYSENSORS_LIBRARY_VERSION ));

    //eepromReset( eeprom, saveState );
    eepromRead( eeprom, loadState, saveState );
    eepromDump( eeprom );

    mainSetup();
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        powerSetup( i );
    }
}

void loop()
{
#ifdef SKETCH_DEBUG
    //Serial.println( F( "[loop]" ));
#endif
    // check for each pwiPulse sensor whether it detects a falling edge on its input pin
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->loopInput();
    }
    // and then let the timers do their jobs
    pwiTimer::Loop();
}

void receive(const MyMessage &message)
{
    uint8_t cmd = message.getCommand();

    char payload[MAX_PAYLOAD+1];
    memset( payload, '\0', sizeof( payload ));
    message.getString( payload );

#ifdef SKETCH_DEBUG
    Serial.print( F( "receive() sensor=" ));
    Serial.print( message.sensor );
    Serial.print( F( ", type=" ));
    Serial.print( message.type );
    Serial.print( F( ", cmd=" ));
    Serial.print( cmd );
    Serial.print( F( ", payload='" ));
    Serial.print( payload );
    Serial.println( F( "'" ));
#endif

    // all received messages should be V_CUSTOM
    if( message.type != V_CUSTOM ){
#ifdef SKETCH_DEBUG
        Serial.println( F( "receive() message should be V_CUSTOM, ignored" ));
#endif
        return;
    }

    if( cmd == C_REQ ){
          uint8_t ureq = strlen( payload ) > 0 ? atoi( payload ) : 0;
#ifdef SKETCH_DEBUG
          Serial.print( F( "receive() C_REQ: ureq=" ));
          Serial.println( ureq );
#endif
          switch( message.sensor ){
              case CHILD_MAIN:
                  switch ( ureq ) {
                    case 1:
                        eepromReset( eeprom, saveState );
                        break;
                    case 2:
                        dumpData();
                        break;
                    case 3:
                        eepromWrite( eeprom, saveState );
                        break;
                  }
                  break;
              case CHILD_MAIN+3:
                  mainEnabledCountSend();
                  break;
          }

    } else if( cmd == C_SET ){
        unsigned long ulong = strlen( payload ) > 0 ? atol( payload ) : 0;
#ifdef SKETCH_DEBUG
        Serial.print( F( "receive() C_SET: ulong=" ));
        Serial.println( ulong );
#endif
        switch( message.sensor ){
            case CHILD_MAIN+1:
                mainMaxFrequencySet( ulong );
                mainMaxFrequencySend();
                break;
            case CHILD_MAIN+2:
                mainUnchangedSet( ulong );
                mainUnchangedSend();
                break;
            case CHILD_MAIN+4:
                mainAutoSaveSet( ulong );
                mainAutoSaveSend();
                break;
            case CHILD_MAIN+5:
                mainAutoDumpSet( ulong );
                mainAutoDumpSend();
                break;
            default:
                powerReceiveSet( message.sensor, payload );
                break;
        }
    }
}

void dumpData()
{
    mainMaxFrequencySend();
    mainUnchangedSend();
    mainEnabledCountSend();
    mainAutoSaveSend();
    mainAutoDumpSend();

    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        powerDumpData( i );
    }
}

