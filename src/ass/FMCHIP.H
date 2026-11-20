/*
FMCHIP.H: the OPL2 layer of Wacky Wheels' FM song player (FMCHIP.C; names SPECULATIVE).
*/

#ifndef __FMCHIP_H
#define __FMCHIP_H

#define FM_MaxVolume 255

enum FM_Errors
   {
   FM_Error = -1,
   FM_Ok    = 0
   };

#ifndef max
#define max( a, b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif
#ifndef min
#define min( a, b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#endif

void           FM_Reset( void );
void           FM_SetChannelTimbre( int channel, unsigned char *timbre );
void           FM_SetChannelVolume( int channel, int volume );
unsigned char *FM_NoteOn( int channel, unsigned char *data );
void           FM_NoteOff( int channel );
void           FM_KeyOn( int channel );
void           FM_RestoreTimbres( void );
void           FM_AllNotesOff( void );
void           FM_SetVolume( int volume );
int            FM_GetVolume( void );
void           FM_Shutdown( void );
int            FM_Init( void );

#endif
