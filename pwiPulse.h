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
 */

#include <Arduino.h>
#include <pwiSensor.h>
#include "device.h"

// MySensors::send()
typedef void ( *pPulseSend )( uint8_t, float, unsigned long );

class pwiPulse : public pwiSensor {
	  public:
		                  pwiPulse();
        sDevice      *getDevice();
        bool          isEnabled();
        void          onPulse();
        void          setupDevice( sDevice &device );
        void          setupId( byte id );
        void          setupPins( byte enabled_pin, byte led_pin );
        void          setupSendCb( pPulseSend pfn );
        void          setupTimers( unsigned long min_ms, unsigned long max_ms );
                  
	  private:
        // setId
        byte          id;

        // setDevice
        sDevice      *device;
        float         k_energy_pulse_wh;
        float         k_power;

        // setupPins
        byte          enabled_pin;
        byte          led_pin;

        // setupSendCb
        pPulseSend    pSend;
        unsigned long last_sent_ms;
        unsigned long last_sent_wh;

        // runtime
        bool          enabled;

        // isr vars
        volatile  unsigned long irq_pulse_last_ms;     // time of last interrupt
        volatile  unsigned long irq_pulse_count;       // count of pulses, used to measure energy

        // private methods
        bool          onMeasure();
        void          onSend();

        // static private methods
        static    bool          MeasureCb( pwiPulse *pulse );
        static    void          UnchangedCb( pwiPulse *pulse );
};

#endif // __PWI_PULSE_H__

