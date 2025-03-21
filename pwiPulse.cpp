/*
 * Pulse S0 meter module management class.
 * 
 * pwi 2019- 5-26 v2 based on pwiSensor class
 * pwi 2019- 6- 3 v3 increment the pulse count even on the first impulsion
 * pwi 2019- 9-15 v2.2-2019
 *                remove id private data
 *                remove setupId() method
 */
#include "pwiPulse.h"

// uncomment to debugging this file
#define PULSE_DEBUG

/*
 * Constructor
 */
pwiPulse::pwiPulse( uint8_t id, uint8_t enabled_pin, uint8_t input_pin, uint8_t led_pin )
{
    // setup pwiSensor base class
    this->setId( id );
    this->setPin( input_pin );
 
    // setup own properties
    this->device = NULL;
    this->k_energy_wh = 0;
    this->k_power_w = 0;

    this->enabled_pin = enabled_pin;
    this->led_pin = led_pin;

    this->pSend = NULL;
    this->last_sent_imp_count = 0;

    this->enabled = false;
    this->last_ms = 0;
    this->last_state = 0;
    this->imp_count = 0;
    this->power_inst = 0;
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
            Serial.print( F( "pwiPulse::isEnabled() id=" )); Serial.print( this->getId());
            Serial.print( F( ", enabled=" ));                Serial.println( this->enabled ? "True":"False" );
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
#if 0
void pwiPulse::onPulse()
{
    if( this->isEnabled()){
#ifdef PULSE_DEBUG
    Serial.print( F( "[pwiPulse::onPulse]: id=" )); Serial.println( this->getId());
#endif
        unsigned long now_ms = millis();
        
        // If this is the first impulsion, just get it along with its time,
        //  but do not compute yet energy nor power.
        if( this->irq_pulse_last_ms == 0 ){
            this->irq_pulse_last_ms = now_ms;

        } else {
            // compute the elapsed time since last interrupt
            //  taking into account the case where the counter is in overflow
            unsigned long length_ms = now_ms - this->irq_pulse_last_ms;
            this->irq_pulse_last_ms = now_ms;

            // As there may be a train of impulsions, just ignore if interval < length of low
            //  part of the pulse (this is debouncing)
            if( length_ms < this->device->implen ){
                return;
            }
        }

        // inc the pulse (energy) counter
        this->irq_pulse_count += 1;
    }
}
#endif

/**
 * pwiPulse::setupDevice():
 * 
 * Pre-compute some constants:
 * - k_energy_wh: energy consumed in Wh between two pulses
 * - k_power_w:   a constant to compute the instant power
 * 
 * Public
 */
void pwiPulse::setupDevice( sDevice &device )
{
    this->device = &device;
    this->k_energy_wh = 1000 / this->device->impkwh;
    this->k_power_w = 3600000 * this->k_energy_wh;
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
    this->setup( min_ms, max_ms, ( pwiMeasureCb ) pwiPulse::MeasureCb, ( pwiSendCb ) pwiPulse::UnchangedCb, this );
}

/**
 * pwiPulse::testInput():
 * 
 * Test for a falling edge on the input pin: this is counted as *one* impulsion
 * 
 * Public
 */
void pwiPulse::testInput()
{
    uint8_t state = digitalRead( this->getPin());
    // falling edge
    if( state == LOW && this->last_state == HIGH ){
        this->imp_count += 1;
        uint32_t now = millis();
        this->power_inst = this->last_ms ? this->k_power_w / ( now - this->last_ms ) : 0;
#ifdef SKETCH_DEBUG
        Serial.print( F( "falling edge detected count=" ));
        Serial.print( this->imp_count );
        Serial.print( F( " now=" ));
        Serial.print( now );
        if( this->last_ms ){
            Serial.print( F( " P.Inst=" ));
            Serial.print( this->power_inst );
            Serial.println( F( " W" ));
        } else {
            Serial.println( F( " P.Inst not computed" ));
        }
#endif
        this->last_ms = now;
    }
    this->last_state = state;
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
    return( this->imp_count != this->last_sent_imp_count );
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
    this->device->countwh += this->imp_count * this->k_energy_wh;
    this->pSend( this->getId(), this->power_inst, this->device->countwh );
    this->last_sent_imp_count = this->imp_count;
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

