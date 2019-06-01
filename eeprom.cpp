#include "eeprom.h"
#include <untilNow.h>

/* **************************************************************************************
 *  EEPROM management
 *  
 * pwi 2019- 5-26 v1 creation
 */

// uncomment for debugging eeprom functions
#define EEPROM_DEBUG

/**
 * eepromDump:
 */
void eepromDump( sEeprom &data )
{
#ifdef EEPROM_DEBUG
    Serial.print( F( "[eepromDump] mark='" )); Serial.print( data.mark ); Serial.println( F( "'" ));
    Serial.print( F( "[eepromDump] version='" )); Serial.println( data.version );
    Serial.print( F( "[eepromDump] min_period_ms='" )); Serial.println( data.min_period_ms );
    Serial.print( F( "[eepromDump] max_period_ms='" )); Serial.println( data.max_period_ms );
#endif
    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
#ifdef EEPROM_DEBUG
        Serial.print( F( "[eepromDump] device #" )); Serial.println( i );
#endif
        deviceDump( data.device[i] );
    }
}

/**
 * eepromRead:
 */
void eepromRead( sEeprom &data, pEepromRead pfnRead, pEepromWrite pfnWrite )
{
    for( uint8_t i=0 ; i<sizeof( sEeprom ); ++i ){
        (( uint8_t * ) &data )[i] = pfnRead( i );
    }
    // initialize with default values if mark not found
    if( data.mark[0] != 'P' || data.mark[1] != 'W' || data.mark[2] != 'I' || data.mark[3] != 0 ){
        eepromReset( data, pfnWrite );
    }
    // dump the full EEPROM every time
    eepromDump( data );
}

/**
 * eepromReset:
 */
void eepromReset( sEeprom &data, pEepromWrite pfnWrite )
{
#ifdef EEPROM_DEBUG
    unsigned long start_ms = millis();
    Serial.print( F( "[eepromReset] begin=" )); Serial.println( start_ms );
#endif
    memset( &data, '\0', sizeof( sEeprom ));
    strcpy( data.mark, "PWI" );
    data.version = 1;

    data.min_period_ms = 60000;           // at most every minute
    data.max_period_ms = 24*3600000;      // at least once per day
    data.auto_save_ms = 7*24*3600000;     // weekly save

    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        deviceReset( &data.device[i] );
    }
    eepromWrite( data, pfnWrite );
#ifdef EEPROM_DEBUG
    unsigned long end_ms = millis();
    Serial.print( F( "[eepromReset] rewrite=" )); Serial.print( end_ms );
    Serial.print( F( ", elapsed=" ));  Serial.print( untilNow( end_ms, start_ms ));
    Serial.println( F( " ms" ));
#endif
}

/**
 * eepromWrite:
 */
void eepromWrite( sEeprom &data, pEepromWrite pfnWrite )
{
    for( uint8_t i=0 ; i<sizeof( sEeprom ); ++i ){
        pfnWrite( i, (( uint8_t * ) &data )[i] );
    }
}

