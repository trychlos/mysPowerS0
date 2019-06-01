#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <Arduino.h>
#include "device.h"

/* **************************************************************************************
 *  EEPROM description
 *  This is the data structure saved in the EEPROM.
 * 
 *  MySensors leave us with 256 bytes to save configuration data in the EEPROM :
 *  - the mark takes 4 bytes
 *  - 252/4 = 63 bytes per module
 * 
 * pwi 2019- 5-26 v1 creation
 */

typedef uint8_t pEepromRead( uint8_t );
typedef void    pEepromWrite( uint8_t, uint8_t );

typedef struct {
    char          mark[4];
    uint8_t       version;            // 1
    unsigned long min_period_ms;
    unsigned long max_period_ms;
    unsigned long auto_save_ms;
    sDevice       device[DEVICE_COUNT];
}
  sEeprom;

void eepromDump( sEeprom &data );
void eepromRead( sEeprom &data, pEepromRead pfnRead, pEepromWrite pfnWrite );
void eepromReset( sEeprom &data, pEepromWrite pfn );
void eepromWrite( sEeprom &data, pEepromWrite pfn );

#endif // __EEPROM_H__

