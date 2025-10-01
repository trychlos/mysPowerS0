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
   pwi 2019- 9-15 v2.2-2019
                    update to pwiPrivate v190902
                    update to pwiCommon v190904
                    use PROGMEM macro
                    remove signing code
                    remove untilNow() code
   pwi 2019-xx-xx v2.3-2019
                    use  PGMSTR macro to handle sketch name and version
   pwi 2025- 3-21 v3.0-2025
                    no more use interrupts
   pwi 2025- 3-22 v3.1-2025
                    use new pwiSensor interface
                    no more send data for not-enabled sensors
   pwi 2025- 3-23 v3.2-2025
                  fix enabled counter detection (input mode of enabled pin)
   pwi 2025- 7-31 v3.3-2025
                  add bounce protection
                  enabled-led flashes briefly (250ms) on each pulse
   pwi 2025- 8- 4 v3.4-2025
                  let the controller reset the device countwh data
                  rationale: try to align our internal counter (Wh) with the device display (kWh)
   pwi 2025- 9-30 v4.0-2025
                  review the whole children identifiers and types (align on mysCellar, adapt to HA)
                  PowerCounter is now initialized to empty, being fully set marking the enable status
                  Instant power and consumed energy are on the same S_POWER child id
                  NB: energy stored is Wh while energy sent is kWh
   pwi 2025- 9-30 v4.1-2025
                  Remove the 'period=' payload prefix from configuration parameters

  Sketch uses 23162 bytes (75%) of program storage space. Maximum is 30720 bytes.
  Global variables use 1175 bytes (57%) of dynamic memory, leaving 873 bytes for local variables. Maximum is 2048 bytes.
 */

// uncomment for debugging this sketch
#define SKETCH_DEBUG

static char const sketchName[] PROGMEM    = "mysPowerS0";
static char const sketchVersion[] PROGMEM = "4.1-2025";

enum {
    CHILD_MAIN                  = 1,
    CHILD_MAIN_LOG              = CHILD_MAIN+0,
    CHILD_MAIN_ACTION_RESET     = CHILD_MAIN+1,
    CHILD_MAIN_ACTION_DUMP      = CHILD_MAIN+2,
    CHILD_MAIN_ACTION_SAVE      = CHILD_MAIN+3,
    CHILD_MAIN_PARM_SAVE_PERIOD = CHILD_MAIN+5,
    CHILD_MAIN_PARM_DUMP_PERIOD = CHILD_MAIN+6,
    CHILD_MAIN_PARM_MIN_PERIOD  = CHILD_MAIN+7,
    CHILD_MAIN_PARM_MAX_PERIOD  = CHILD_MAIN+8,

    CHILD_ID_PULSE_1            = 10,
    CHILD_ID_PULSE_2            = 20,
    CHILD_ID_PULSE_3            = 30,
    CHILD_ID_PULSE_4            = 40
};

#define MY_DEBUG
#define MY_RADIO_RF24
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

/* **********************************************************************************************************
 * Pulse S0 module management
 * 
 * - up to four modules
 *  The max count of defined sensors (which depends of the board) is in device.h, iself being included in eeprom.h.
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
 */
void powerPresentation( uint8_t idx )
{
    char payload [1+MAX_PAYLOAD];
    //                                             1234567890123456789012345
    memset( payload, '\0', sizeof( payload ));
    snprintf_P( payload, MAX_PAYLOAD, PSTR( "Power sensor id=%u" ), sensors[idx]->getId());
    present( sensors[idx]->getId()+0, S_POWER, payload );
    memset( payload, '\0', sizeof( payload ));
    snprintf_P( payload, MAX_PAYLOAD, PSTR( "Impulsions/kwh id=%u" ), sensors[idx]->getId());
    present( sensors[idx]->getId()+1, S_INFO, payload );
    memset( payload, '\0', sizeof( payload ));
    snprintf_P( payload, MAX_PAYLOAD, PSTR( "Impulsion length id=%u" ), sensors[idx]->getId());
    present( sensors[idx]->getId()+2, S_INFO, payload );
    memset( payload, '\0', sizeof( payload ));
    snprintf_P( payload, MAX_PAYLOAD, PSTR( "Model id=%u" ), sensors[idx]->getId());
    present( sensors[idx]->getId()+3, S_INFO, payload );
    memset( payload, '\0', sizeof( payload ));
    snprintf_P( payload, MAX_PAYLOAD, PSTR( "Initial energy id=%u" ), sensors[idx]->getId());
    present( sensors[idx]->getId()+4, S_INFO, payload );
}

void powerSetup( uint8_t idx )
{
    // setup computed properties
    sensors[idx]->setDevice( eeprom.device[idx] );
    sensors[idx]->setSendFn( powerSend );
    sensors[idx]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    powerSend( idx, 0, eeprom.device[idx].countwh );
    powerDumpData( idx );
    // this is a fake message to let the controller initialize the initial energy entity
    msg.clear();
    send( msg.setSensor( sensors[idx]->getId()+4 ).setType( V_TEXT ).set( 0 ));
    sensors[idx]->initialsSent();
}

void powerDumpData( uint8_t idx )
{
    uint8_t id = sensors[idx]->getId();
    char payload[1+MAX_PAYLOAD];
    msg.clear();
    // the first time this message is sent, it is not ackownledged by the gateway
    send( msg.setSensor( id+1 ).setType( V_TEXT ).set( sensors[idx]->getDevice()->impkwh ));
    wait( 5 );
    send( msg.setSensor( id+1 ).setType( V_TEXT ).set( sensors[idx]->getDevice()->impkwh ));
    msg.clear();
    send( msg.setSensor( id+2 ).setType( V_TEXT ).set( sensors[idx]->getDevice()->implen ));
    msg.clear();
    send( msg.setSensor( id+3 ).setType( V_TEXT ).set( sensors[idx]->getDevice()->device ));
}

// a message has been received which is not managed by the main sensor
// expects it will be by a power sensor (but not sure)
void powerReceive( uint8_t id, const char *payload )
{
    uint8_t idx = ( uint8_t )( id / 10 );
    if( idx > 0 ){
        PowerCounter *sensor = sensors[idx-1];
        sDevice *device = sensor->getDevice();
        
        uint8_t cmd = id - 10*idx;
        unsigned long ulong = atol( payload );
        bool changed = false;
    
        switch( cmd ){
            case 1:
                // payload is the count of pulses per kWh (e.g. 1000)
                device->impkwh = ulong;
                changed = true;
                break;
            case 2:
                // payload is the length in ms of each pulse (e.g. 90)
                device->implen = ulong;
                changed = true;
                break;
            case 3:
                // payload is the device name (e.g. "DRS155-D")
                memset( device->device, '\0', DEVICE_NAME_SIZE+1 );
                strcpy( device->device, payload );
                changed = true;
                break;
            case 4:
                // payload is the device display in kWh, with decimales
                // it must replace the current energy count which is incremented and sent back to the controller
                device->countwh = 1000.0 * atof( payload );
                changed = true;
                break;
        }

        if( changed ){
            eepromWrite( eeprom, saveState );
            autosave_timer.restart();
            mainLogSend(( char * ) "Change done" );
            powerDumpData( idx-1 );
        }
    }
}

void powerSend( uint8_t id, uint32_t watt, uint32_t wh )
{
    msg.clear();
    send( msg.setSensor( id ).setType( V_WATT ).set(( uint32_t ) watt ));
    msg.clear();
    send( msg.setSensor( id ).setType( V_KWH ).set( wh/1000.0, 2 ));
}

/* **********************************************************************************************************
 *  CHILD_MAIN Sensor
 */

void mainAutoSaveCb( void *empty );
void mainAutoDumpCb( void *empty );

bool main_initial_sents = false;
bool main_log_initial_sent = false;

void mainPresentation()
{
    //                                                  1234567890123456789012345
    present( CHILD_MAIN_LOG,              S_INFO,   F( "Board logs" ));
    present( CHILD_MAIN_ACTION_RESET,     S_BINARY, F( "Action: reset eeprom" ));
    present( CHILD_MAIN_ACTION_DUMP,      S_BINARY, F( "Action: dump eeprom" ));
    present( CHILD_MAIN_ACTION_SAVE,      S_BINARY, F( "Action: write in eeprom" ));
    present( CHILD_MAIN_PARM_SAVE_PERIOD, S_INFO,   F( "Parm: eeprom save period" ));
    present( CHILD_MAIN_PARM_DUMP_PERIOD, S_INFO,   F( "Parm: eeprom dump period" ));
    present( CHILD_MAIN_PARM_MIN_PERIOD,  S_INFO,   F( "Parm: report min period" ));
    present( CHILD_MAIN_PARM_MAX_PERIOD,  S_INFO,   F( "Parm: report max period" ));
}

void mainSetup()
{
    autosave_timer.setup( "AutosaveTimer", eeprom.auto_save_ms, false, ( pwiTimerCb ) mainAutoSaveCb );
    autosave_timer.start();
    autodump_timer.setup( "AutodumpTimer", eeprom.auto_dump_ms, false, ( pwiTimerCb ) mainAutoDumpCb );
    autodump_timer.start();
    mainActionResetSend();
    mainActionDumpSend();
    mainActionSaveSend();
    mainAutoSaveSend();
    mainAutoDumpSend();
    mainMinPeriodSend();
    mainMaxPeriodSend();
    main_initial_sents = true;
}

/* called from main loop() function
 * make sure we send an initial log value (e.g. 'Ready' ) to the controller at startup after all other children
 */
void mainInitialLoop( void )
{
    if( main_initial_sents && !main_log_initial_sent ){
        // are all the initial message for all sensors have been sent ?
        bool sensors_sent = true;
        for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
            bool sent = sensors[i]->isInitialSent();
            if( !sent ){
                sensors_sent = false;
                break;
            }
        }
        if( sensors_sent ){
            mainLogSend(( char * ) "Node ready" );
            main_log_initial_sent = true;
        }
    }
}

void mainActionDumpDo()
{
    dumpData();
}

void mainActionDumpSend()
{
    uint8_t sensor_id = CHILD_MAIN_ACTION_DUMP;
    uint8_t msg_type = V_STATUS;
    uint8_t payload = 0;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainActionDumpSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainActionResetDo()
{
    eepromReset( eeprom, saveState );
}

void mainActionResetSend()
{
    uint8_t sensor_id = CHILD_MAIN_ACTION_RESET;
    uint8_t msg_type = V_STATUS;
    uint8_t payload = 0;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainActionResetSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainActionSaveDo()
{
    eepromWrite( eeprom, saveState );
}

void mainActionSaveSend()
{
    uint8_t sensor_id = CHILD_MAIN_ACTION_SAVE;
    uint8_t msg_type = V_STATUS;
    uint8_t payload = 0;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainActionSaveSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainAutoDumpCb( void*empty )
{
    dumpData();
}

void mainAutoDumpSend()
{
    uint8_t sensor_id = CHILD_MAIN_PARM_DUMP_PERIOD;
    uint8_t msg_type = V_TEXT;
    unsigned long payload = eeprom.auto_dump_ms;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainAutoDumpSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
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
    uint8_t sensor_id = CHILD_MAIN_PARM_SAVE_PERIOD;
    uint8_t msg_type = V_TEXT;
    unsigned long payload = eeprom.auto_save_ms;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainAutoSaveSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainAutoSaveSet( unsigned long ulong )
{
    eeprom.auto_save_ms = ulong;
    eepromWrite( eeprom, saveState );
    autosave_timer.setDelay( ulong );
}

void mainLogSend( char *log )
{
    msg.clear();
    send( msg.setSensor( CHILD_MAIN_LOG ).setType( V_TEXT ).set( log ));
}

void mainMaxPeriodSend()
{
    uint8_t sensor_id = CHILD_MAIN_PARM_MAX_PERIOD;
    uint8_t msg_type = V_TEXT;
    unsigned long payload = eeprom.max_period_ms;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainAutoSaveSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainMaxPeriodSet( unsigned long ulong )
{
    eeprom.max_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

void mainMinPeriodSend()
{
    uint8_t sensor_id = CHILD_MAIN_PARM_MIN_PERIOD;
    uint8_t msg_type = V_TEXT;
    unsigned long payload = eeprom.min_period_ms;
#ifdef SKETCH_DEBUG
    Serial.print( F( "[mainAutoSaveSend] sensor=" ));
    Serial.print( sensor_id );
    Serial.print( F( ", type=" ));
    Serial.print( msg_type );
    Serial.print( F( ", payload=" ));
    Serial.println( payload );
#endif
    msg.clear();
    send( msg.setSensor( sensor_id ).setType( msg_type ).set( payload ));
}

void mainMinPeriodSet( unsigned long ulong )
{
    eeprom.min_period_ms = ulong;
    eepromWrite( eeprom, saveState );
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->setTimers( eeprom.min_period_ms, eeprom.max_period_ms );
    }
}

/* **********************************************************************************************************
 * **********************************************************************************************************
 *  MAIN CODE
 * **********************************************************************************************************
 * ********************************************************************************************************** */

// As of MySensors v2.x, presentation() is called before setup().
// pwi 2022- 4- 7 do not send rthe library version here as this is nonetheless the first thing the Arduino
//  MySensors library sends at startup
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
    // sends a 'Node ready' message when all initializations are done
    mainInitialLoop();
    // check for each pwiPulse sensor whether it detects a falling edge on its input pin
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        sensors[i]->loopInput();
    }
    // and let the timers do their job
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

    if( cmd == C_SET ){
        uint8_t ureq = strlen( payload ) > 0 ? atoi( payload ) : 0;
        unsigned long ulong = strlen( payload ) ? atol( payload ) : 0;
        switch( message.sensor ){
            case CHILD_MAIN_ACTION_RESET:
                if( message.type == V_STATUS && ureq == 1 ){
                    mainActionResetDo();
                    mainActionResetSend();
                    mainLogSend(( char * ) "eeprom reset done" );
                }
                break;
            case CHILD_MAIN_ACTION_DUMP:
                if( message.type == V_STATUS && ureq == 1 ){
                    mainActionDumpDo();
                    mainActionDumpSend();
                    mainLogSend(( char * ) "eeprom dump done" );
                }
                break;
            case CHILD_MAIN_ACTION_SAVE:
                if( message.type == V_STATUS && ureq == 1 ){
                    mainActionSaveDo();
                    mainActionSaveSend();
                    mainLogSend(( char * ) "eeprom save done" );
                }
                break;
            case CHILD_MAIN_PARM_SAVE_PERIOD:
                if( message.type == V_TEXT && strlen( payload )){
                    mainAutoSaveSet( ulong );
                    mainAutoSaveSend();
                }
                break;
            case CHILD_MAIN_PARM_DUMP_PERIOD:
                if( message.type == V_TEXT && strlen( payload )){
                    mainAutoDumpSet( ulong );
                    mainAutoDumpSend();
                }
                break;
            case CHILD_MAIN_PARM_MAX_PERIOD:
                if( message.type == V_TEXT && strlen( payload )){
                    mainMaxPeriodSet( ulong );
                    mainMaxPeriodSend();
                }
                break;
            case CHILD_MAIN_PARM_MIN_PERIOD:
                if( message.type == V_TEXT && strlen( payload )){
                    mainMinPeriodSet( ulong );
                    mainMinPeriodSend();
                }
                break;
            default:
                powerReceive( message.sensor, payload );
                break;
        }
    } // end of cmd == C_SET
}

void dumpData()
{
    mainAutoSaveSend();
    mainAutoDumpSend();
    mainMinPeriodSend();
    mainMaxPeriodSend();

    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        powerDumpData( i );
    }
}

