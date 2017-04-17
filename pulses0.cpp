#include "pulses0.h"
#include "until_now.h"

// uncomment to debugging this file
#define PULSES0_DEBUG

#define PULSES0_CHANGE_TIMEOUT  60000           /* send status at least every minute */

/**
 * pulses0_dump:
 * @data: the #PulseS0Data to be displayed.
 * 
 * Dump @data to serial monitor.
 * 
 * Global function.
 */
void pulses0_dump( PulseS0Data &data )
{
    Serial.print( F( "[PulseS0::dump] now=" ));           Serial.println( millis());
    Serial.print( F( "[PulseS0::dump] wh_child_id=" ));   Serial.println( data.wh_child_id );
    Serial.print( F( "[PulseS0::dump] va_child_id=" ));   Serial.println( data.va_child_id );
    Serial.print( F( "[PulseS0::dump] count_wh=" ));      Serial.println( data.count_wh );
    Serial.print( F( "[PulseS0::dump] power_w=" ));       Serial.println( data.power_w );
    Serial.print( F( "[PulseS0::dump] reason=" ));        Serial.print( data.reason );
    Serial.print( " " );
    switch( data.reason ){
        case PULSES0_REASON_CHANGE:
            Serial.println( F( "(PULSES0_REASON_CHANGE)" ));
            break;
        case PULSES0_REASON_TIMEOUT:
            Serial.println( F( "(PULSES0_REASON_TIMEOUT)" ));
            break;
        default:
            Serial.println( F( "(other)" ));
            break;
    }
    Serial.print( F( "[PulseS0::dump] last_pulse_ms=" )); Serial.println( data.last_pulse_ms );
}

/*
 * Constructor
 */
PulseS0::PulseS0( byte wh_child_id, byte va_child_id, byte irq_pin, byte enabled_pin, byte led_pin )
{
    this->wh_child_id = wh_child_id;
    this->va_child_id = va_child_id;

    this->irq_pin = irq_pin;
    digitalWrite( this->irq_pin, HIGH );
    pinMode( this->irq_pin, INPUT );
    
    this->enabled_pin = enabled_pin;
    pinMode( this->enabled_pin, INPUT_PULLUP );
    
    this->led_pin = led_pin;
    digitalWrite( this->led_pin, LOW );
    pinMode( this->led_pin, OUTPUT );

    memset( this->name, '\0', PULSES0_NAME_SIZE );
    this->impkwh = 0;
    this->implen = 0;
    this->is_enabled = false;
    this->last_output_ms = 0;
    this->pulse_count = 0;
    this->last_pulse_ms = 0;
    this->p_inst_w = 0.0;
}

/*
 * Destructor
 * rather useless but just for fun: make sure the led is off
 */
PulseS0::~PulseS0()
{
    digitalWrite( this->led_pin, LOW );
}

/*
 * Public
 */
byte PulseS0::getWHChildId()
{
    return( this->wh_child_id );
}

/*
 * Public
 */
byte PulseS0::getVAChildId()
{
    return( this->va_child_id );
}

/*
 * Public
 */
byte PulseS0::getIrqPin()
{
    return( this->irq_pin );
}

/**
 * reqProperties:
 * 
 * Public
 */
void PulseS0::reqProperties( void )
{
#ifdef PULSES0_DEBUG
    Serial.println( F( "[PulseS0::reqProperties]" ));
    Serial.print( F( "[PulseS0::reqProperties] name=" ));   Serial.println( this->name );
    Serial.print( F( "[PulseS0::reqProperties] impkwh=" )); Serial.println( this->impkwh );
    Serial.print( F( "[PulseS0::reqProperties] implen=" )); Serial.println( this->implen );
#endif
}

/**
 * setModel:
 * @model: the model name.
 * 
 * Public
 */
void PulseS0::setModel( const char *model )
{
    memset( this->name, '\0', PULSES0_NAME_SIZE );
    strncpy( this->name, model, PULSES0_NAME_SIZE-1 );
}

/**
 * setResolution:
 * @impkwh: count of impulsions per kWh
 * @implen: length of the low pulse
 * 
 * Public
 */
void PulseS0::setResolution( uint16_t impkwh, uint16_t implen )
{
    this->impkwh = impkwh;
    this->implen = implen;

    // pre-compute some constants:
    // - energy_pulse_wh: energy consumed in Wh between two pulses
    // - power_k:         a constant to compute the inst. power
    this->energy_pulse_wh = 1000000.0 / ( float ) impkwh;
    this->power_k = 3600.0 * this->energy_pulse_wh;
}

/*
 * This is the interrupt routine.
 * As there may be a train of impulsions, just ignore if interval < length of low part of the pulse
 * (debounce when less than specified pulse length) when computing instant power.
 * 
 * Public
 */
void PulseS0::onPulse()
{
    if( this->is_enabled ){

        // compute the elapsed time since last interrupt
        //  taking into account the case where the counter is in overflow
        unsigned long now_ms = millis();
        unsigned long length_ms = untilNow( now_ms, this->last_pulse_ms );
        this->last_pulse_ms = now_ms;
 
        // debounce
        // i.e. ignore the impulsion if the length is less than the spec says for just the low part
        if( length_ms < this->implen ){
            return;
        }

        // inc the pulse (energy) counter
        this->pulse_count += 1;

        // compute the consumed power since last pulse time
        // inst_power = delta_energy / delta_time
        // by definition, delta_energy here is the energy consumed between two pulses
        // i.e. delta_energy (Wh)   = 1000 / impkwh
        //   so delta_energy (W.ms) = 1000 / impkwh * 3600 (s/h) * 1000 (ms/s)
        //   so inst_power   (W)    = 1000 / impkwh * 3600 (s/h) * 1000 (ms/s) / delay_between_pulses
        // see http://openenergymonitor.org/emon/buildingblocks/introduction-to-pulse-counting
        this->p_inst_w = this->power_k / ( float ) length_ms;
    }
}

/*
 * loopModule:
 * Sends the data if the timeout is reached (and the module is enabled)
 * 
 * Public
 */
void PulseS0::runLoop( PulseS0Output output_fn, void *msg_kwh, void *msg_watt )
{
    PulseS0Data power_data;

#ifdef PULSES0_DEBUG
    Serial.println( F( "[PulseS0::loop]" ));
#endif
    this->getEnabled();

    if( this->is_enabled ){
        
        byte reason = 0;
        unsigned long now_ms = millis();
        if( this->last_pulse_ms > this->last_output_ms ){
            reason = PULSES0_REASON_CHANGE;
        } else {
            unsigned long delay_ms = untilNow( now_ms, this->last_output_ms );
            if( delay_ms > PULSES0_CHANGE_TIMEOUT ){
                reason = PULSES0_REASON_TIMEOUT;
            }
        }
        if( reason != 0 ){
            memset( &power_data, '\0', sizeof( power_data ));
            power_data.wh_child_id = this->wh_child_id;
            power_data.va_child_id = this->va_child_id;
            power_data.count_wh = this->pulse_count * this->energy_pulse_wh;
            power_data.power_w = this->p_inst_w;
            power_data.reason = reason;
            power_data.last_pulse_ms = this->last_pulse_ms;
#ifdef PULSES0_DEBUG
            pulses0_dump( power_data );
#endif
            output_fn( &power_data, msg_kwh, msg_watt );
            this->last_output_ms = now_ms;
        }
    }
}

/*
 * This is called in each loop
 * Only debug status changes
 * 
 * Private
 */
bool PulseS0::getEnabled()
{
    bool was_enabled = this->is_enabled;
    byte value = digitalRead( this->enabled_pin );
    this->is_enabled = ( value == HIGH && this->impkwh > 0 && this->implen > 0 );
    digitalWrite( this->led_pin, this->is_enabled ? HIGH : LOW );
#ifdef PULSES0_DEBUG
    if( was_enabled != this->is_enabled ){
        Serial.print( F( "PulseS0: wh_child=" )); Serial.print( this->wh_child_id );
        Serial.print( F( "PulseS0: va_child=" )); Serial.print( this->va_child_id );
        Serial.print( F( ", enabled=" ));         Serial.println( this->is_enabled ? "True":"False" );
    }
#endif
}

