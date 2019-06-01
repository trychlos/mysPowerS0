#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <Arduino.h>

/* **************************************************************************************
 *  Device management.
 *  
 *  Our Arduino Nano-based board let us manage 4 devices simultaneously.
 *  
 *  These devices are managed individually, one at once.
 *  We record in the EEPROM their individual characteristics, along with (on request) the
 *  current WH energy counter.
 *  
 *  The energy counter would be nice to be saved automatically, but we avoid this due to
 *  the max write cycles count the EEPROM can handle (~100 000 cycles).
 *  Instead, this data may be still be saved on program request.
 * 
 * pwi 2019- 5-26 v1 creation
 */

/* count of manageable devices
 * size of the device model name, including the null-terminating byte
 */
#define DEVICE_COUNT          4
#define DEVICE_NAME_SIZE     23

typedef struct {
    uint16_t      impkwh;
    uint16_t      implen;
    char          device[DEVICE_NAME_SIZE];
    unsigned long countwh;
}
  sDevice;

void deviceDump( sDevice &data );
void deviceReset( sDevice *data );

#endif // __DEVICE_H__

