#ifndef __PWI_PULSE_H__
#define __PWI_PULSE_H__
/*
 * Pulse S0 meter module management class.
 * 
 * Sortie par impulsion compatible avec "SO" Din-Rail 43864
 * Nombre d'impulsions: dépendant du module (en imp/kWh)
 * Durée d'impulsion: dépendant du module (en ms)
 * 
 * La présence d'un module est signalée par la borne enabledPin à l'état HIGH.
 * Cette présence est testée une fois à chaque lecture.
 * 
 * The object has to be initialized with :
 * - the IRQ pin, and a callback function to 
 * 
 * pwi 2019- 5-26 v2 based on pwiSensor class
 * pwi 2019- 9-15 v2.2-2019
 *                remove id private data
 *                remove setupId() method
 */

#include <Arduino.h>
#include <pwiSensor.h>
#include "device.h"

// MySensors::send()
typedef void ( *pPulseSend )( uint8_t, uint32_t, uint32_t );

class pwiPulse : public pwiSensor {
	  public:
		                  pwiPulse( uint8_t id, uint8_t enabled_pin, uint8_t input_pin, uint8_t led_pin );
        sDevice      *getDevice();
        bool          isEnabled();
        void          setupDevice( sDevice &device );
        void          setupSendCb( pPulseSend pfn );
        void          setupTimers( unsigned long min_ms, unsigned long max_ms );
        void          testInput();
                  
	  private:
        // setupDevice
        sDevice      *device;
        uint32_t      k_energy_wh;
        uint32_t      k_power_w;

        // setupPins
        byte          enabled_pin;
        byte          led_pin;

        // setupSendCb
        pPulseSend    pSend;
        uint32_t      last_sent_imp_count;

        // runtime
        bool          enabled;
        uint32_t      last_ms;
        uint8_t       last_state;
        uint32_t      imp_count;
        uint32_t      power_inst;

        // isr vars
        //volatile  unsigned long irq_pulse_last_ms;     // time of last interrupt
        //volatile  unsigned long irq_pulse_count;       // count of pulses, used to measure energy

        // private methods
        bool          onMeasure();
        void          onSend();

        // static private methods
        static    bool          MeasureCb( pwiPulse *pulse );
        static    void          UnchangedCb( pwiPulse *pulse );
};

#endif // __PWI_PULSE_H__

