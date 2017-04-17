#ifndef __DRS155_H__
#define __DRS155_H__

#include "pulses0.h"

/*
 * DSR155-D module management.
 * 
 * Sortie par impulsion compatible avec "SO" Din-Rail 43864 (27V, 27mA)
 * Nombre d'impulsions: 1000/kWh
 * Durée d'impulsion: 90ms
 */

class Drs155 : public PulseS0 {
	  public:
		    Drs155( byte wh_child_id, byte va_child_id, byte irq_pin, byte enabled_pin, byte led_pin );
};

#endif // __DRS155_H__

