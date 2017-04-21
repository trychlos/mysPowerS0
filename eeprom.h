#ifndef __MYSPOWERS0_EEPROM_H__
#define __MYSPOWERS0_EEPROM_H__

uint8_t eeprom_get_pos( uint8_t child_id );
void    eeprom_dump( sModule &sdata );
bool    eeprom_read( uint8_t child_id, sModule &sdata );
bool    eeprom_write( uint8_t child_id, sModule &sdata );
void    eeprom_raz( void );

#endif // __MYSPOWERS0_EEPROM_H__

