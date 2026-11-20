/*
FMSONG.C: Wacky Wheels' FM song player, for the KLM songs of WACKY.DAT.

Not part of any released Apogee Sound System: reconstructed from WW.EXE 0x4A490-0x4A886 (findings/
executables.md "MUSIC"). MUSIC.C calls it for Sound Blaster, Pro Audio Spectrum and Adlib; it plays
through FMCHIP.C. The names are this reconstruction's own (SPECULATIVE).

A song: word 0 = rate of the song task (ticks per second), word at +3 = offset of the events,
11-byte timbres from +5. An event byte 0xFD n or 0xFE nn waits n ticks, 0xFF ends the song
(it restarts when looping), else the high nibble is a command and the low nibble the channel.
*/

#include <stdlib.h>
#include "dpmi.h"
#include "task_man.h"
#include "fmchip.h"
#include "fmsong.h"

#define TIMBRE_SIZE  11

#define TRUE  ( 1 == 1 )
#define FALSE ( !TRUE )

static task          *FMSONG_Task        = NULL;
static volatile int   FMSONG_SongActive  = FALSE;
static int            FMSONG_SongLoaded  = FALSE;
static volatile int   FMSONG_LoopSong    = FALSE;

static volatile unsigned FMSONG_Delay;
static unsigned char * volatile FMSONG_SongPtr;
static unsigned char *FMSONG_SongStart;
static int            FMSONG_TaskRate;
static unsigned char *FMSONG_Timbres;


/*---------------------------------------------------------------------
   Function: FMSONG_ServiceRoutine

   Task that plays the song: runs the events due on this tick.
---------------------------------------------------------------------*/

static void FMSONG_ServiceRoutine
   (
   task *Task
   )

   {
   unsigned event;
   int      channel;

   if ( !FMSONG_SongActive )
      {
      return;
      }

   while( FMSONG_Delay == 0 )
      {
      event = *FMSONG_SongPtr;
      FMSONG_SongPtr++;

      if ( event == 0xfd )
         {
         FMSONG_Delay = *FMSONG_SongPtr;
         FMSONG_SongPtr++;
         }
      else if ( event == 0xfe )
         {
         FMSONG_Delay = *( unsigned short * )FMSONG_SongPtr;
         FMSONG_SongPtr += 2;
         }
      else if ( event == 0xff )
         {
         FMSONG_SongPtr = FMSONG_SongStart;
         if ( !FMSONG_LoopSong )
            {
            FMSONG_SongActive = FALSE;
            FMSONG_Delay = 0;
            return;
            }
         }
      else
         {
         channel = event & 0xf;
         switch( event >> 4 )
            {
            case 1 :
               FMSONG_SongPtr = FM_NoteOn( channel, FMSONG_SongPtr );
               break;

            case 0 :
               FM_NoteOff( channel );
               break;

            case 2 :
               FM_SetChannelVolume( channel, *FMSONG_SongPtr );
               FMSONG_SongPtr++;
               break;

            case 3 :
               FM_SetChannelTimbre( channel,
                  FMSONG_Timbres + *FMSONG_SongPtr * TIMBRE_SIZE );
               FMSONG_SongPtr++;
               break;

            case 4 :
               FM_KeyOn( channel );
               break;
            }
         }
      }

   FMSONG_Delay--;
   }


/*---------------------------------------------------------------------
   Function: FMSONG_SetLoopFlag

   Sets whether the song restarts at its end.
---------------------------------------------------------------------*/

void FMSONG_SetLoopFlag
   (
   int loopflag
   )

   {
   FMSONG_LoopSong = loopflag;
   }


/*---------------------------------------------------------------------
   Function: FMSONG_StopSong

   Stops the song task and silences the card.
---------------------------------------------------------------------*/

void FMSONG_StopSong
   (
   void
   )

   {
   if ( FMSONG_SongLoaded )
      {
      TS_Terminate( FMSONG_Task );
      FMSONG_Task = NULL;
      }

   FM_AllNotesOff();
   }


/*---------------------------------------------------------------------
   Function: FMSONG_PlaySong

   Starts playing a song.
---------------------------------------------------------------------*/

void FMSONG_PlaySong
   (
   unsigned char *song,
   int loopflag
   )

   {
   unsigned offset;

   if ( FMSONG_SongLoaded )
      {
      FMSONG_StopSong();
      }

   FMSONG_LoopSong = loopflag;

   FM_AllNotesOff();
   FM_Reset();

   FMSONG_TaskRate   = *( short * )song;
   offset            = *( unsigned short * )( song + 3 );
   FMSONG_Timbres    = song + 5;
   FMSONG_SongStart  = song + offset;
   FMSONG_SongPtr    = FMSONG_SongStart;
   FMSONG_Delay      = 0;

   FMSONG_Task = TS_ScheduleTask( FMSONG_ServiceRoutine, FMSONG_TaskRate, 1, NULL );
   TS_Dispatch();

   FMSONG_SongLoaded = TRUE;
   FMSONG_SongActive = TRUE;
   }


/*---------------------------------------------------------------------
   Function: FMSONG_Continue

   Continues a paused song.
---------------------------------------------------------------------*/

void FMSONG_Continue
   (
   void
   )

   {
   if ( FMSONG_SongLoaded )
      {
      FM_RestoreTimbres();
      FMSONG_SongActive = TRUE;
      }
   }


/*---------------------------------------------------------------------
   Function: FMSONG_Pause

   Pauses the song.
---------------------------------------------------------------------*/

void FMSONG_Pause
   (
   void
   )

   {
   if ( FMSONG_SongLoaded )
      {
      FMSONG_SongActive = FALSE;
      FM_AllNotesOff();
      }
   }


/*---------------------------------------------------------------------
   Function: FMSONG_SongPlaying

   Returns whether the song is playing.
---------------------------------------------------------------------*/

int FMSONG_SongPlaying
   (
   void
   )

   {
   return( FMSONG_SongActive );
   }


/*---------------------------------------------------------------------
   Function: FMSONG_LockEnd

   Used for determining the length of the functions to lock in memory.
---------------------------------------------------------------------*/

static void FMSONG_LockEnd
   (
   void
   )

   {
   }


/*---------------------------------------------------------------------
   Function: FMSONG_SetVolume

   Sets the FM volume (0-255).
---------------------------------------------------------------------*/

void FMSONG_SetVolume
   (
   int volume
   )

   {
   FM_SetVolume( volume );
   }


/*---------------------------------------------------------------------
   Function: FMSONG_GetVolume

   Returns the FM volume.
---------------------------------------------------------------------*/

int FMSONG_GetVolume
   (
   void
   )

   {
   return( FM_GetVolume() );
   }


/*---------------------------------------------------------------------
   Function: FMSONG_Shutdown

   Shuts down the FM layer and unlocks the player. It does not stop
   the song (MUSIC_Shutdown stops it first).
---------------------------------------------------------------------*/

void FMSONG_Shutdown
   (
   void
   )

   {
   FM_Shutdown();

   DPMI_UnlockMemoryRegion( FMSONG_ServiceRoutine, FMSONG_LockEnd );
   DPMI_Unlock( FMSONG_Delay );
   DPMI_Unlock( FMSONG_SongPtr );
   DPMI_Unlock( FMSONG_SongStart );
   DPMI_Unlock( FMSONG_Task );
   DPMI_Unlock( FMSONG_TaskRate );
   DPMI_Unlock( FMSONG_Timbres );
   DPMI_Unlock( FMSONG_SongActive );
   DPMI_Unlock( FMSONG_SongLoaded );
   DPMI_Unlock( FMSONG_LoopSong );
   }


/*---------------------------------------------------------------------
   Function: FMSONG_Init

   Sets up the FM layer and locks the player. The game's version locks
   the code but calls DPMI_Unlock on the data (CONFIRMED in WW.EXE
   0x4A7B0), like FM_Init.
---------------------------------------------------------------------*/

int FMSONG_Init
   (
   int card
   )

   {
   int status;

   if ( FM_Init() != FM_Ok )
      {
      return( FMSONG_Error );
      }

   status  = DPMI_LockMemoryRegion( FMSONG_ServiceRoutine, FMSONG_LockEnd );
   status |= DPMI_Unlock( FMSONG_Delay );
   status |= DPMI_Unlock( FMSONG_SongPtr );
   status |= DPMI_Unlock( FMSONG_SongStart );
   status |= DPMI_Unlock( FMSONG_Task );
   status |= DPMI_Unlock( FMSONG_TaskRate );
   status |= DPMI_Unlock( FMSONG_Timbres );
   status |= DPMI_Unlock( FMSONG_SongActive );
   status |= DPMI_Unlock( FMSONG_SongLoaded );
   status |= DPMI_Unlock( FMSONG_LoopSong );

   if ( status != DPMI_Ok )
      {
      FM_Shutdown();
      FMSONG_Shutdown();
      return( FMSONG_Error );
      }

   return( FMSONG_Ok );
   }
