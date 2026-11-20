/*
FMSONG.H: Wacky Wheels' FM song player (FMSONG.C; names SPECULATIVE).
*/

#ifndef __FMSONG_H
#define __FMSONG_H

enum FMSONG_Errors
   {
   FMSONG_Error = -1,
   FMSONG_Ok    = 0
   };

int  FMSONG_Init( int card );
void FMSONG_Shutdown( void );
void FMSONG_StopSong( void );
void FMSONG_PlaySong( unsigned char *song, int loopflag );
void FMSONG_SetLoopFlag( int loopflag );
int  FMSONG_SongPlaying( void );
void FMSONG_Continue( void );
void FMSONG_Pause( void );
void FMSONG_SetVolume( int volume );
int  FMSONG_GetVolume( void );

#endif
