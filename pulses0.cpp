#include "pulses0.h"
#include "until_now.h"
#include "eeprom.h"

// uncomment to debugging this file
#define PULSES0_DEBUG

#define PULSES0_CHANGE_TIMEOUT  60000             /* send status at least every minute with va=0 */
#define PULSES0_IMPKWH          1000              /* 1000 pulses per kWh */
#define PULSES0_IMPLENGTH       90                /* pulse length (ms) */

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
    Serial.print( F( "[PulseS0::dump] va_child_id=" ));   Serial.println( data.va_child_id );
    Serial.print( F( "[PulseS0::dump] wh_child_id=" ));   Serial.println( data.wh_child_id );
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
PulseS0::PulseS0( byte va_child_id, byte wh_child_id, byte irq_pin, byte enabled_pin, byte led_pin )
{
    this->va_child_id = va_child_id;
    this->wh_child_id = wh_child_id;

    this->irq_pin = irq_pin;
    digitalWrite( this->irq_pin, HIGH );
    pinMode( this->irq_pin, INPUT );
    
    this->enabled_pin = enabled_pin;
    pinMode( this->enabled_pin, INPUT_PULLUP );
    
    this->led_pin = led_pin;
    digitalWrite( this->led_pin, LOW );
    pinMode( this->led_pin, OUTPUT );

    this->energy_pulse_wh = 0;
    this->power_k = 0;

    // has not yet received any pulse
    this->initialized = false;
    this->is_enabled = false;
    this->first = true;
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
byte PulseS0::getVAChildId()
{
    return( this->va_child_id );
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
byte PulseS0::getIrqPin()
{
    return( this->irq_pin );
}

/**
 * getFromPayload:
 * @payload: the payload of the message
 * 
 * Public
 */
void PulseS0::getFromPayload( const char *payload, PulseS0Custom fn )
{
    char buffer[24];
    int varid = atoi( payload );
    switch( varid ){
        case 1:
            snprintf( buffer, sizeof( buffer )-1, "kwh=%u", this->sdata.impkwh );
            fn( this->va_child_id, buffer );
            break;
        case 2:
            snprintf( buffer, sizeof( buffer )-1, "l=%u", this->sdata.implen );
            fn( this->va_child_id, buffer );
            break;
        case 3:
            snprintf( buffer, sizeof( buffer )-1, "n=%s", this->sdata.name );
            fn( this->va_child_id, buffer );
            break;
        case 4:
            this->getFromPayload( "1", fn );
            this->getFromPayload( "2", fn );
            this->getFromPayload( "3", fn );
            break;
    }
}

/**
 * setFromPayload:
 * @payload: the payload of the message
 * 
 * Public
 */
void PulseS0::setFromPayload( const char *payload )
{
    uint16_t value;
    char *p = ( char * ) payload;
    char *str = strtok_r( p, "=", &p );
    if( str && strlen( str ) > 0 ){
        int varid = atoi( str );
        switch( varid ){
            case 1:
                this->setImpkwh( atoi( p ));
                break;
            case 2:
                this->setImplen( atoi( p ));
                break;
            case 3:
                this->setName( p );
                break;
            case 4:
                str = strtok_r( p, ",", &p );
                if( str && strlen( str )){
                    this->setImpkwh( atoi( str ));
                }
                str = strtok_r( p, ",", &p );
                if( str && strlen( str )){
                    this->setImplen( atoi( str ));
                }
                this->setName( p );
                break;
        }
    }
}

/**
 * isEnabled:
 * 
 * Returns: %TRUE if the module is enabled
 * 
 * Public
 */
bool PulseS0::isEnabled( void )
{
    return( this->is_enabled );
}

/*
 * This is the interrupt routine.
 * 
 * Public
 */
void PulseS0::onPulse()
{
    if( this->is_enabled ){

#ifdef PULSES0_DEBUG
    Serial.print( F( "[PulseS0::onPulse]: id=" ));   Serial.println( this->va_child_id );
#endif
        unsigned long now_ms = millis();
        
        // If this is the first impulsion, just count it and get its time,
        //  but do not compute energy nor power.
        if( this->first ){
            this->last_pulse_ms = now_ms;
            this->first = false;
        
        } else {
            // compute the elapsed time since last interrupt
            //  taking into account the case where the counter is in overflow
            unsigned long length_ms = untilNow( now_ms, this->last_pulse_ms );
            this->last_pulse_ms = now_ms;
     
            // As there may be a train of impulsions, just ignore if interval < length of low
            //  part of the pulse (this is debouncing)
            if( length_ms < this->sdata.implen ){
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
}

/**
 * runLoop:
 * Sends the data if the timeout is reached (and the module is enabled)
 * 
 * Public
 */
void PulseS0::runLoop( PulseS0Output output_fn, void *msg_kwh, void *msg_watt )
{
    PulseS0Data power_data;

#ifdef PULSES0_DEBUG
    Serial.print( F( "[PulseS0::loop] id=" )); Serial.println( this->va_child_id );
#endif
    if( !this->initialized ){
        this->runSetup();
    }
    
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
                this->p_inst_w = 0;
            }
        }
        if( reason != 0 ){
            memset( &power_data, '\0', sizeof( power_data ));
            power_data.va_child_id = this->va_child_id;
            power_data.wh_child_id = this->wh_child_id;
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
 * Only print debug if status changes
 * 
 * Private
 */
bool PulseS0::getEnabled()
{
    bool was_enabled = this->is_enabled;
    byte value = digitalRead( this->enabled_pin );
    this->is_enabled = ( value == HIGH && this->sdata.impkwh > 0 && this->sdata.implen > 0 );
    digitalWrite( this->led_pin, this->is_enabled ? HIGH : LOW );
#ifdef PULSES0_DEBUG
    if( was_enabled != this->is_enabled ){
        Serial.print( F( "PulseS0: va_child=" )); Serial.print( this->va_child_id );
        Serial.print( F( ", wh_child=" ));        Serial.print( this->wh_child_id );
        Serial.print( F( ", enabled=" ));         Serial.println( this->is_enabled ? "True":"False" );
    }
#endif
}

/**
 * runSetup:
 * 
 * Initialize the Pulse S0 Module.
 * 
 * Private
 */
void PulseS0::runSetup( void )
{
    // initialize default values
    if( !eeprom_read( va_child_id, this->sdata )){
        this->setImpkwh( PULSES0_IMPKWH );
        this->setImplen( PULSES0_IMPLENGTH );
                     // 1234567890123456789012
        this->setName( "Default PulseS0" );
    }
    
    // pre-compute some constants:
    // - energy_pulse_wh: energy consumed in Wh between two pulses
    // - power_k:         a constant to compute the inst. power
    this->energy_pulse_wh = 1000.0 / ( float ) this->sdata.impkwh;
    this->power_k = 3600000.0 * this->energy_pulse_wh;

    this->initialized = true;
}

/**
 * setImpkwh:
 * @impkwh: count of impulsions per kWh
 * 
 * Command: C_SET, payload='1;<value>'
 * 
 * Private
 */
void PulseS0::setImpkwh( uint16_t impkwh )
{
    if( impkwh > 0 ){
        this->sdata.impkwh = impkwh;
        eeprom_write( this->va_child_id, this->sdata );
    }
}

/**
 * setImplen:
 * @implen: length of the low pulse
 * 
 * Command: C_SET, payload='2;<value>'
 * 
 * Private
 */
void PulseS0::setImplen( uint16_t implen )
{
    if( implen > 0 ){
        this->sdata.implen = implen;
        eeprom_write( this->va_child_id, this->sdata );
    }
}

/**3
 * setName:
 * @model: the model name.
 * 
 * Command: C_SET, payload='3;<value>'
 * 
 * Private
 */
void PulseS0::setName( const char *model )
{
    if( model && strlen( model )){
        memset( this->sdata.name, '\0', PULSES0_NAME_SIZE );
        strncpy( this->sdata.name, model, PULSES0_NAME_SIZE-1 );
        eeprom_write( this->va_child_id, this->sdata );
    }
}

