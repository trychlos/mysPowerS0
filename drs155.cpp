#include "drs155.h"

// uncomment to debugging this file
#define DRS155_DEBUG

#define DRS155_IMP_KWH       1000         /* 1000 pulses per kWh */
#define DRS155_IMP_LENGTH    90           /* pulse length (ms - from specs) */

/*
 * constructor
 */
Drs155::Drs155( byte wh_child_id, byte va_child_id, byte irq_pin, byte enabled_pin, byte led_pin ) : PulseS0( wh_child_id, va_child_id, irq_pin, enabled_pin, led_pin )
{
    this->setProperties( DRS155_IMP_KWH, DRS155_IMP_LENGTH );
}

