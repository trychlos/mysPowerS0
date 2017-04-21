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

/* the size of the model name, including the null-terminating byte
 */
#define PULSES0_NAME_SIZE            23

/* the data structure passed to the ouput callback
 */
typedef struct {
    byte          va_child_id;
    byte          wh_child_id;
    unsigned long count_wh;
    float         power_w;
    byte          reason;
    unsigned long last_pulse_ms;
}
  PulseS0Data;

/* the output callback function 
 */
typedef void ( *PulseS0Output )( void *, void *, void * );

/* the custom callback to handle incoming message
 */
typedef void ( *PulseS0Custom )( byte child_id, const char *content );

/**
 * sModule:
 * 
 * The data structure saved in the EEPROM for each module.
 * 
 * We have 256 bytes to save modules configuration data in the EEPROM
 * 256/4 = 64 bytes per module
 * pos  size  content
 * ---  ----  -------------------------------------------------------
 *   0     1  module child id
 *   1     2  impkwh: count of impulsions per kWh
 *   3     2  implen: minimal length of the impulsion
 *   5    23  model name of the module, including the null terminating byte
 *  28        36 unused bytes
 *  
 *  Count_wh would be nice to be saved, but we avoid this due to the max
 *  write cycles count the EEPROM can handle (~100 000 cycles).
 */
typedef struct {
    uint8_t  child_id;
    uint16_t impkwh;
    uint16_t implen;
    char     name[PULSES0_NAME_SIZE];
}
  sModule;

// global functions
void          pulses0_dump( PulseS0Data &data );

class PulseS0 {
	  public:
		    PulseS0( byte va_child_id, byte wh_child_id, byte irq_pin, byte enabled_pin, byte led_pin );
        ~PulseS0();
        byte getVAChildId();
        byte getWHChildId();
        byte getIrqPin();
        void getFromPayload( const char *payload, PulseS0Custom fn );
        void setFromPayload( const char *payload );
        bool isEnabled( void );
        void runLoop( PulseS0Output output_fn, void *msg_kwh, void *msg_watt );
        void onPulse();

	  private:
        // initialization
        byte          va_child_id;
        byte          wh_child_id;
        byte          enabled_pin;
        byte          led_pin;
        byte          irq_pin;

        // properties read from / written to EEPROM
        sModule       sdata;

        // runtime
        bool          is_enabled;
        bool          first;
        bool          initialized;
        unsigned long last_output_ms;
        float         energy_pulse_wh;
        float         power_k;

        // isr vars
        volatile unsigned long pulse_count;           // count of pulses, used to measure energy
        volatile unsigned long last_pulse_ms;         // time of last interrupt
        volatile float         p_inst_w;              // computed instant power

        // 
        bool          getEnabled( void );
        void          runSetup( void );
        void          setImpkwh( uint16_t impkwh );
        void          setImplen( uint16_t implen );
        void          setName( const char *model );
};

#endif // __PULSE_S0_H__

