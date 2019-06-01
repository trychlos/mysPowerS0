#include "device.h"

/* **************************************************************************************
 *  Device management.
 *  
 * pwi 2019- 5-26 v1 creation
 */

// uncomment for debugging device functions
#define DEVICE_DEBUG

/**
   deviceDump:
*/
void deviceDump( sDevice &data )
{
#ifdef DEVICE_DEBUG
  Serial.print( F( "[deviceDump] impkwh=" )); Serial.print( data.impkwh );   Serial.print( F( " imp/kWh" ));
  Serial.print( F( ", implen=" ));            Serial.print( data.implen );   Serial.print( F( " ms/imp" ));
  Serial.print( F( ", countwh=" ));           Serial.print( data.countwh );  Serial.print( F( " Wh" ));
  Serial.print( F( ", name='" ));             Serial.print( data.device );   Serial.println( "'" );
#endif
}

/**
   deviceReset:
*/
void deviceReset( sDevice *data )
{
    data->impkwh = 1000;
    data->implen = 90;
    data->countwh = 0;
    //                     1234567890123456789012345
    strcpy( data->device, "DEFAULT" );
}

