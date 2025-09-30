#ifndef __POWER_COUNTER_H__
#define __POWER_COUNTER_H__
/*
 * Pulse S0 meter module management class based on DRS155-D.
 * 
 * Sortie par impulsion compatible avec "SO" Din-Rail 43864
 * Nombre d'impulsions: dépendant du module (en imp/kWh)
 * Durée d'impulsion: dépendant du module (en ms)
 * 
 * La présence d'un module est signalée par la borne enabledPin à l'état HIGH.
 * Cette présence est testée une fois à chaque lecture.
 * 
 * pwi 2019- 5-26 v2 based on pwiSensor class
 * pwi 2019- 9-15 v2.2-2019
 *                remove id private data
 *                remove setupId() method
 * pwi 2025- 3-22 v3.1-2025
 *                take advantage of pwiPulseSensor base virtual class
 */

#include <pwiPulseSensor.h>
#include "device.h"

// MySensors::send()
typedef void ( *PowerSendFn )( uint8_t, uint32_t, uint32_t );

class PowerCounter : public pwiPulseSensor {
	  public:
		              PowerCounter( uint8_t id, uint8_t enabled_pin, uint8_t input_pin, uint8_t led_pin );
        sDevice      *getDevice();
        void          initialsSent( void );
        bool          isEnabled();
        bool          isInitialSent();
        void          setDevice( sDevice &device );
        void          setSendFn( PowerSendFn pfn );
        void          loopInput();

	  private:
        // setupDevice
        sDevice      *device;
        float         k_energy_wh;
        float         k_power_w;

        // setupPins
        uint8_t       enabled_pin;
        uint8_t       led_pin;

        // setupSendCb
        PowerSendFn   sendFn;
        uint32_t      last_imp_count_sent;

        // runtime
        uint32_t      last_ms;
        uint32_t      power_inst;
        uint32_t      ledoff;
        bool          initial_sent;

    protected:
        bool          vMeasure();
        void          vSend();
};

#endif // __POWER_COUNTER_H__

