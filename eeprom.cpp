#include "eeprom.h"

/* **********************************************************************************************************
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
    Serial.print( F( "[eepromDump] mark='" ));          Serial.print( data.mark ); Serial.println( F( "'" ));
    Serial.print( F( "[eepromDump] version=" ));        Serial.println( data.version );
    Serial.print( F( "[eepromDump] min_period_ms=" ));  Serial.println( data.min_period_ms );
    Serial.print( F( "[eepromDump] max_period_ms=" ));  Serial.println( data.max_period_ms );
    Serial.print( F( "[eepromDump] auto_save_ms=" ));   Serial.println( data.auto_save_ms );
    Serial.print( F( "[eepromDump] auto_dump_ms=" ));   Serial.println( data.auto_dump_ms );
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
#ifdef EEPROM_DEBUG
    Serial.println( F( "[eepromRead]" ));
#endif

    for( uint8_t i=0 ; i<sizeof( sEeprom ); ++i ){
        (( uint8_t * ) &data )[i] = pfnRead( i );
    }
    // initialize with default values if mark not found
    if( data.mark[0] != 'P' || data.mark[1] != 'W' || data.mark[2] != 'I' || data.mark[3] != 0 ){
        eepromReset( data, pfnWrite );
    }
}

/**
 * eepromReset:
 */
void eepromReset( sEeprom &data, pEepromWrite pfnWrite )
{
#ifdef EEPROM_DEBUG
    Serial.println( F( "[eepromReset]" ));
#endif

    memset( &data, '\0', sizeof( sEeprom ));
    strcpy( data.mark, "PWI" );
    data.version = EEPROM_VERSION;

    data.min_period_ms = 60000;           // at most every minute
    data.max_period_ms = 24*3600000;      // at least once per day
    data.auto_save_ms = 7*24*3600000;     // weekly save
    data.auto_dump_ms = 86400000;         // daily dump

    for( uint8_t i=0 ; i<DEVICE_COUNT ; ++i ){
        deviceReset( &data.device[i] );
    }

    eepromWrite( data, pfnWrite );
}

/**
 * eepromWrite:
 */
void eepromWrite( sEeprom &data, pEepromWrite pfnWrite )
{
#ifdef EEPROM_DEBUG
    Serial.println( F( "[eepromWrite]" ));
#endif

    for( uint8_t i=0 ; i<sizeof( sEeprom ); ++i ){
        pfnWrite( i, (( uint8_t * ) &data )[i] );
    }
}

