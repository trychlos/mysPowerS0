#ifndef __PULSE_S0_H__
#define __PULSE_S0_H__

#include <Arduino.h>

/*
 * Pulse S0 meter module management.
 * 
 * Sortie par impulsion compatible avec "SO" Din-Rail 43864
 * Nombre d'impulsions: dépendant du module (en imp/kWh)
 * Durée d'impulsion: dépendant du module (en ms)
 * 
 * La présence d'un module est signalée par la borne enabledPin à l'état HIGH.
 * Cette présence est testée une fois à chaque lecture.
 */

enum {
    PULSES0_REASON_CHANGE = 1,
    PULSES0_REASON_TIMEOUT
};

/* the size of the model name, including the null-terminating byte */
#define PULSES0_NAME_SIZE            15

// a callback when the module wants output its data 
typedef struct {
    byte          wh_child_id;
    byte          va_child_id;
    unsigned long count_wh;
    float         power_w;
    byte          reason;
    unsigned long last_pulse_ms;
}
  PulseS0Data;

typedef void ( *PulseS0Output )( void *, void *, void * );

// global function
void          pulses0_dump( PulseS0Data &data );

class PulseS0 {
	  public:
		    PulseS0( byte wh_child_id, byte va_child_id, byte irq_pin, byte enabled_pin, byte led_pin );
        ~PulseS0();
        byte getWHChildId();
        byte getVAChildId();
        byte getIrqPin();
        void reqProperties( void );
        void setModel( const char *model );
        void setResolution( uint16_t impkwh, uint16_t implen );
        void runLoop( PulseS0Output output_fn, void *msg_kwh, void *msg_watt );
        void onPulse();

	  private:
        // initialization
        byte          wh_child_id;
        byte          va_child_id;
        byte          enabled_pin;
        byte          led_pin;
        byte          irq_pin;
        // properties
        char          name[PULSES0_NAME_SIZE];
        uint16_t      impkwh;
        uint16_t      implen;
        // runtime
        bool          is_enabled;
        bool          first;
        unsigned long last_output_ms;
        float         energy_pulse_wh;
        float         power_k;

        volatile unsigned long pulse_count;           // count of pulses, used to measure energy
        volatile unsigned long last_pulse_ms;         // time of last interrupt
        volatile float         p_inst_w;              // computed instant power

        bool          getEnabled();
};

#endif // __PULSE_S0_H__

