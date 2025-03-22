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
 */
#include "power_counter.h"

// uncomment to debugging this file
#define PULSE_DEBUG

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
    this->led_pin = led_pin;

    this->sendFn = NULL;
    this->last_imp_count_sent = 0;

    this->enabled = false;
    this->last_ms = 0;
    this->power_inst = 0;
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
    if( this->enabled_pin ){
        bool was_enabled = this->enabled;
        byte value = digitalRead( this->enabled_pin );
        this->enabled = ( value == HIGH && this->device != NULL );
        if( was_enabled != this->enabled ){
            digitalWrite( this->led_pin, this->enabled ? HIGH : LOW );
#ifdef PULSE_DEBUG
            Serial.print( F( "PowerCounter::isEnabled() id=" )); Serial.print( this->getId());
            Serial.print( F( ", enabled=" ));                Serial.println( this->enabled ? "True":"False" );
        }
    }
#endif
    return( this->enabled );
}

/**
 * PowerCounter::setDevice():
 * 
 * Pre-compute some constants:
 * - k_energy_wh: energy consumed in Wh between two pulses
 * - k_power_w:   a constant to compute the instant power
 * 
 * Public
 */
void PowerCounter::setDevice( sDevice &device )
{
    this->device = &device;
    this->k_energy_wh = 1000.0 / ( float ) this->device->impkwh;
    this->k_power_w = 3600000.0 * this->k_energy_wh;
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
 * 
 * Public
 */
void PowerCounter::loopInput()
{
    if( this->isEnabled()){
        uint32_t prevCount = this->getImpulsionsCount();
        this->pwiPulseSensor::loopInput();
        uint32_t newCount = this->getImpulsionsCount();
        if( newCount > prevCount ){
            uint32_t now = millis();
            this->power_inst = this->last_ms ? ( uint32_t )( this->k_power_w / ( now - this->last_ms )) : 0;
#ifdef SKETCH_DEBUG
            Serial.print( F( "falling edge detected count=" ));
            Serial.print( newCount );
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
    return( this->getImpulsionsCount() != this->last_imp_count_sent );
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
        uint32_t count = this->getImpulsionsCount();
        this->device->countwh += ( uint32_t )( count * this->k_energy_wh );
        this->sendFn( this->getId(), this->power_inst, this->device->countwh );
        this->last_imp_count_sent = count;
        this->power_inst = 0;
    }
}
