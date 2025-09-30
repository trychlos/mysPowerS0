/*
 * Pulse S0 meter module management class based on DRS155-D.
 * 
 * pwi 2019- 5-26 v2 based on pwiSensor class
 * pwi 2019- 6- 3 v3 increment the pulse count even on the first impulsion
 * pwi 2019- 9-15 v2.2-2019
 *                remove id private data
 *                remove setupId() method
 * pwi 2025- 3-22 v3.1-2025
 *                take advantage of PowerCounterSensor base virtual class
 * pwi 2025- 3-23 v3.2-2025
 *                fix enabled counter detection (input mode of enabled pin)
 * pwi 2025- 7-31 v3.3-2025
 *                add bounce protection
 */
#include "power_counter.h"

// uncomment to debugging this file
//#define COUNTER_DEBUG

/*
 * Constructor
 */
PowerCounter::PowerCounter( uint8_t id, uint8_t enabled_pin, uint8_t input_pin, uint8_t led_pin )
{
    // setup pwiSensor base class
    this->setId( id );

    // setup pwiPulseSensor base class
    this->setEdge( FALLING );
    this->setInputPin( input_pin );
 
    // setup own properties
    this->device = NULL;
    this->k_energy_wh = 0;
    this->k_power_w = 0;

    this->enabled_pin = enabled_pin;
    if( enabled_pin ){
        pinMode( enabled_pin, INPUT_PULLUP );
    }
    this->led_pin = led_pin;
    if( led_pin ){
        pinMode( led_pin, OUTPUT );
        digitalWrite( led_pin, LOW );
    }

    this->sendFn = NULL;
    this->last_imp_count_sent = 0;

    this->last_ms = 0;
    this->power_inst = 0;
    this->ledoff = 0;
    this->initial_sent = false;
}

/**
 * PowerCounter::getDevice():
 * 
 * Public
 */
sDevice *PowerCounter::getDevice()
{
    return( this->device );
}

/**
 * PowerCounter::initialsSent():
 *
 * The initial messages have been sent at startup.
 * 
 * Public
 */
void PowerCounter::initialsSent( void )
{
    this->initial_sent = true;
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
bool PowerCounter::isEnabled( void )
{
    bool enabled = false;
    if( this->enabled_pin ){
        byte value = digitalRead( this->enabled_pin );
        enabled = ( value == HIGH && this->device != NULL && this->device->impkwh && this->device->implen && strlen( this->device->device ));
        // if we have defined a LED and it is not OFF after a pulse..
        if( this->led_pin && !this->ledoff ){
            digitalWrite( this->led_pin, enabled ? HIGH : LOW );
        }
        /*
#ifdef COUNTER_DEBUG
        Serial.print( F( "PowerCounter::isEnabled() id=" ));
        Serial.print( this->getId());
        Serial.print( F( ", enabled=" ));
        Serial.print( enabled ? "True":"False" );
        Serial.println( "" );
#endif
*/
    }
    return( enabled );
}

/**
 * isInitialSent:
 * 
 * Returns: %TRUE if the initial messages have been sent
 * 
 * Public
 */
bool PowerCounter::isInitialSent( void )
{
    bool sent = this->initial_sent;
#ifdef COUNTER_DEBUG
    Serial.print( F( "PowerCounter::isInitialSent() id=" ));
    Serial.print( this->getId());
    Serial.print( F( ", initial_sent=" ));
    Serial.print( sent ? "True":"False" );
    Serial.println( "" );
#endif
    return( sent );
}

/**
 * PowerCounter::setDevice():
 * 
 * Pre-compute some constants:
 * - k_energy_wh: energy consumed in Wh between two pulses
 * - k_power_w:   a constant to compute the instant power
 *
 * Set the length of the impulsion (to prevent against bounces)
 * 
 * Public
 */
void PowerCounter::setDevice( sDevice &device )
{
    this->device = &device;
    this->k_energy_wh = 1000.0 / ( float ) this->device->impkwh;
    this->k_power_w = 3600000.0 * this->k_energy_wh;
    this->setPulseLength( this->device->implen );
}

/**
 * PowerCounter::setSendFn():
 * 
 * Define the send callback.
 * 
 * Public
 */
void PowerCounter::setSendFn( PowerSendFn pfn )
{
    this->sendFn = pfn;
}

/**
 * PowerCounter::loopInput():
 * 
 * The base class takes care of detecting the impulsion.
 * In an impulsion has been detected, then we are able to compute an instant power consumed since the previous one.
 * NB: when calling pwiPulseSensor::loopInput(), we get at most one more impulsion.
 * 
 * Public
 */
void PowerCounter::loopInput()
{
    if( this->isEnabled()){
        uint32_t prevCount = this->getPulsesCount();
        bool hasPulse = this->pwiPulseSensor::loopInput();
        uint32_t now = millis();
        if( hasPulse ){
            this->power_inst = this->last_ms ? ( uint32_t )( this->k_power_w / ( now - this->last_ms )) : 0;
#ifdef COUNTER_DEBUG
            Serial.print( F( "falling edge detected count=" ));
            Serial.print( prevCount+1 );
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
            // stop the led for a short time after each pulse for 250ms
            if( this->led_pin ){
                digitalWrite( this->led_pin, LOW );
                this->ledoff = now + 250;
            }
        }
        if( this->led_pin && now > this->ledoff ){
            digitalWrite( this->led_pin, HIGH );
            this->ledoff = 0;
        }
    }
}

/**
 * PowerCounter::vMeasure():
 * 
 * Min period, aka max frequency, timer callback.
 * Should measure, returning %TRUE if the counts have changed since last time.
 * 
 * Protected
 */
bool PowerCounter::vMeasure()
{
    return( this->getPulsesCount() != this->last_imp_count_sent );
}

/**
 * PowerCounter::vSend():
 * 
 * Send the messages, whether it has changed or the unchanged timeout is reached.
 * 
 * Protected
 */
void PowerCounter::vSend()
{
    if( this->isEnabled()){
        uint32_t count = this->getPulsesCount();
        this->device->countwh += ( uint32_t )(( count - this->last_imp_count_sent ) * this->k_energy_wh );
        this->sendFn( this->getId(), this->power_inst, this->device->countwh );
#ifdef COUNTER_DEBUG
            Serial.print( F( "sending count=" ));
            Serial.print( count );
            Serial.print( F( " k_energy_wh=" ));
            Serial.print( this->k_energy_wh );
            Serial.print( F( " measure_wh=" ));
            Serial.println( this->device->countwh );
#endif
        this->last_imp_count_sent = count;
        this->power_inst = 0;
    }
}
