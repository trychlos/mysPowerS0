/*
 * Pulse S0 meter module management class.
 * 
 * pwi 2019- 5-26 v2 based on pwiSensor class
 */
#include "pwiPulse.h"
#include <untilNow.h>

// uncomment to debugging this file
#define PULSE_DEBUG

/*
 * Constructor
 */
pwiPulse::pwiPulse()
{
    this->id = 0;

    this->device = NULL;
    this->k_energy_pulse_wh = 0;
    this->k_power = 0;

    this->enabled_pin = 0;
    this->led_pin = 0;

    this->pSend = NULL;
    this->last_sent_ms = 0;
    this->last_sent_wh = 0;

    this->enabled = false;

    this->irq_pulse_last_ms = 0;
    this->irq_pulse_count = 0;
}

/**
 * pwiPulse::getDevice():
 * 
 * Public
 */
sDevice *pwiPulse::getDevice()
{
    return( this->device );
}

/**
 * isEnabled:
 * 
 * Returns: %TRUE if the module is enabled
 * 
 *    It must be physically enabled on the board
 *    It must have been initialized with the connected device
 * 
 * Public
 */
bool pwiPulse::isEnabled( void )
{
    if( this->enabled_pin ){
        bool was_enabled = this->enabled;
        byte value = digitalRead( this->enabled_pin );
        this->enabled = ( value == HIGH && this->device != NULL );
        if( was_enabled != this->enabled ){
            digitalWrite( this->led_pin, this->enabled ? HIGH : LOW );
#ifdef PULSE_DEBUG
            Serial.print( F( "[pwiPulse::isEnabled] id=" )); Serial.print( this->id );
            Serial.print( F( ", enabled=" ));               Serial.println( this->enabled ? "True":"False" );
        }
    }
#endif
    return( this->enabled );
}

/**
 * pwiPulse::onPulse():
 * 
 * The interrupt routine.
 * Is triggered on the falling edge of every pulse.
 * 
 * Public
 */
void pwiPulse::onPulse()
{
    if( this->isEnabled()){
#ifdef PULSE_DEBUG
    Serial.print( F( "[pwiPulse::onPulse]: id=" )); Serial.println( this->id );
#endif
        unsigned long now_ms = millis();
        
        // If this is the first impulsion, just get it along with its time,
        //  but do not compute yet energy nor power.
        if( this->irq_pulse_last_ms == 0 ){
            this->irq_pulse_last_ms = now_ms;

        } else {
            // compute the elapsed time since last interrupt
            //  taking into account the case where the counter is in overflow
            unsigned long length_ms = untilNow( now_ms, this->irq_pulse_last_ms );
            this->irq_pulse_last_ms = now_ms;

            // As there may be a train of impulsions, just ignore if interval < length of low
            //  part of the pulse (this is debouncing)
            if( length_ms < this->device->implen ){
                return;
            }

            // inc the pulse (energy) counter
            this->irq_pulse_count += 1;
        }
    }
}

/**
 * pwiPulse::setupDevice():
 * 
 * Pre-compute some constants:
 * - k_energy_pulse_wh: energy consumed in Wh between two pulses
 * - k_power:         a constant to compute the instant power
 * 
 * Public
 */
void pwiPulse::setupDevice( sDevice &device )
{
    this->device = &device;
    this->k_energy_pulse_wh = 1000.0 / ( float ) this->device->impkwh;
    this->k_power = 3600000.0 * this->k_energy_pulse_wh;
}

/**
 * pwiPulse::setupId():
 * 
 * Public
 */
void pwiPulse::setupId( byte id )
{
    this->id = id;
}

/**
 * pwiPulse::setupPins():
 * 
 * If both irq_pin, pfn and pFunc are defined, then attach the trigger to the falling edge
 * of the specified IRQ.
 * 
 * Public
 */
void pwiPulse::setupPins( byte enabled_pin, byte led_pin )
{
    this->enabled_pin = enabled_pin;
    if( enabled_pin ){
        pinMode( this->enabled_pin, INPUT_PULLUP );
    }

    this->led_pin = led_pin;
    if( led_pin ){
        digitalWrite( this->led_pin, LOW );
        pinMode( this->led_pin, OUTPUT );
    }
}

/**
 * pwiPulse::setupSendCb():
 * 
 * Define the send callback.
 * 
 * Public
 */
void pwiPulse::setupSendCb( pPulseSend pfn )
{
    this->pSend = pfn;
}

/**
 * pwiPulse::setupTimers():
 * 
 * Define min and max periods.
 * 
 * Public
 */
void pwiPulse::setupTimers( unsigned long min_ms, unsigned long max_ms )
{
    this->setup( max_ms, min_ms, ( pwiMeasureCb ) pwiPulse::MeasureCb, ( pwiSendCb ) pwiPulse::UnchangedCb, this );
}

/**
 * pwiPulse::onMeasure():
 * 
 * Min period, aka max frequency, timer callback.
 * Should measure, returning %TRUE if the counts have changed since last time.
 * 
 * Private
 */
bool pwiPulse::onMeasure()
{
    return( this->irq_pulse_count != 0 );
}

/**
 * pwiPulse::onSend():
 * 
 * Send the messages, whether it has changed or the unchanged timeout is reached.
 * 
 * Private
 */
void pwiPulse::onSend()
{
    this->device->countwh += this->irq_pulse_count * this->k_energy_pulse_wh;
    this->irq_pulse_count = 0;

    // compute the average consumed power since last sent message
    // inst_power = delta_energy / delta_time
    // by definition, delta_energy here is the energy consumed between two pulses
    // i.e. delta_energy (Wh)   = 1000 / impkwh
    //   so delta_energy (W.ms) = 1000 / impkwh * 3600 (s/h) * 1000 (ms/s)
    //   so inst_power   (W)    = 1000 / impkwh * 3600 (s/h) * 1000 (ms/s) / delay_between_pulses
    // see http://openenergymonitor.org/emon/buildingblocks/introduction-to-pulse-counting
    unsigned long now_ms = millis();
    unsigned long delay_ms = untilNow( now_ms, this->last_sent_ms );
    unsigned long consumed_wh = this->device->countwh - this->last_sent_wh;
    float power_w = this->k_power * ( float ) consumed_wh / ( float ) delay_ms;

    this->pSend( this->id, power_w, this->device->countwh );
    this->last_sent_ms = now_ms;
    this->last_sent_wh = this->device->countwh;
}

/**
 * pwiPulse::MeasureCb():
 * 
 * Private static
 */
static bool pwiPulse::MeasureCb( pwiPulse *pulse )
{
    return( pulse->onMeasure());
}

/**
 * pwiPulse::SendCb():
 * 
 * Private static
 */
static void pwiPulse::UnchangedCb( pwiPulse *pulse )
{
    pulse->onSend();
}

