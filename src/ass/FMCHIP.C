/*
FMCHIP.C: the OPL2 layer of Wacky Wheels' FM song player (FMSONG.C).

Not part of any released Apogee Sound System: reconstructed from WW.EXE 0x50280-0x50A43 (findings/
executables.md "MUSIC"). The game links it with the sound library, just before the GF1 library. The
names are this reconstruction's own (SPECULATIVE); the structure follows AL_MIDI.C's.

Eleven channels, as the KLM song format has them: 0-5 melodic (two operators each), 6 bass drum (two
operators), 7 snare, 8 tom tom, 9 top cymbal, 10 hi-hat (OPL rhythm mode, one operator each).
*/

#include <stdlib.h>
#include <conio.h>
#include "dpmi.h"
#include "interrupt.h"
#include "fmchip.h"

#define ADLIB_PORT     0x388
#define NUM_CHANNELS   11
#define NUM_SLOTS      18
#define FIRST_RHYTHM   6

static int FM_Volume = FM_MaxVolume;

// 0xBD bits of the rhythm instruments, channels 6-10
static int FM_RhythmBits[ NUM_CHANNELS - FIRST_RHYTHM ] = { 0x10, 0x08, 0x04, 0x02, 0x01 };

// The operator slots of each channel: modulator, carrier (rhythm channels 7-10 have one)
static int FM_ChannelSlot[ NUM_CHANNELS ][ 2 ] =
   {
   {  0,  3 }, {  1,  4 }, {  2,  5 }, {  6,  9 }, {  7, 10 }, {  8, 11 },
   { 12, 15 }, { 16,  0 }, { 14,  0 }, { 17,  0 }, { 13,  0 }
   };

// The register offset of each operator slot
static int FM_SlotOffset[ NUM_SLOTS ] =
   {
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0a,
   0x0b, 0x0c, 0x0d, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
   };

static unsigned       FM_FrequencyLow[ 16 ];   // the 0xA0 byte of each channel
static unsigned       FM_FrequencyHigh[ 16 ];  // the 0xB0 byte (block, key on)
static unsigned       FM_SlotVolume[ NUM_SLOTS ];
static unsigned       FM_SlotLevel[ NUM_SLOTS ];
static unsigned       FM_SlotKSL[ NUM_SLOTS ];
static unsigned char *FM_ChannelTimbre[ NUM_CHANNELS ];
static int            FM_ChannelVolume[ NUM_CHANNELS ];
static int            FM_RhythmRegister;

static void FM_SendOutput( int reg, int data );

typedef struct
   {
   unsigned char Connection : 1;
   unsigned char Feedback   : 3;
   } FEEDBACK;

#pragma aux FM_SetChannelTimbre frame;


/*---------------------------------------------------------------------
   Function: FM_SetSlotVolume

   Writes an operator's output level: its timbre's level scaled by
   the slot volume.
---------------------------------------------------------------------*/

static void FM_SetSlotVolume
   (
   int slot
   )

   {
   unsigned level;

   level = FM_SlotLevel[ slot ] * FM_SlotVolume[ slot ];
   level = ( level + 127 + level ) / 254;

   FM_SendOutput( FM_SlotOffset[ slot ] + 0x40,
      ( 63 - level ) | FM_SlotKSL[ slot ] );
   }


/*---------------------------------------------------------------------
   Function: FM_SendOutput

   Writes a byte to an OPL register.
---------------------------------------------------------------------*/

static void FM_SendOutput
   (
   int reg,
   int data
   )

   {
   int      delay;
   int      port;
   unsigned flags;

   port = ADLIB_PORT;

   flags = DisableInterrupts();

   outp( port, reg );

   for( delay = 6; delay > 0 ; delay-- )
      {
      inp( port );
      }

   outp( port + 1, data );

   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );
   inp( port );

   RestoreInterrupts( flags );
   }


/*---------------------------------------------------------------------
   Function: FM_Reset

   Silences the card and sets up rhythm mode.
---------------------------------------------------------------------*/

void FM_Reset
   (
   void
   )

   {
   int channel;
   int slot;
   int volume;

   volume = ( FM_Volume * 127 ) / 255;
   for( slot = 0; slot < NUM_SLOTS; slot++ )
      {
      FM_SlotVolume[ slot ] = volume;
      }

   // Enable wave select
   FM_SendOutput( 0x01, 0x20 );
   FM_SendOutput( 0x08, 0 );

   for( channel = 0; channel < 9; channel++ )
      {
      FM_SendOutput( 0xa0 + channel, 0 );
      FM_SendOutput( 0xb0 + channel, 0 );
      FM_ChannelTimbre[ channel ] = NULL;
      }

   for( channel = 0; channel < NUM_CHANNELS; channel++ )
      {
      FM_ChannelVolume[ channel ] = 127;
      }

   for( slot = 0; slot < NUM_SLOTS; slot++ )
      {
      FM_SendOutput( FM_SlotOffset[ slot ] + 0xe0, 0 );
      }

   // The rhythm instruments' fixed pitches
   FM_SendOutput( 0xa8, 0x57 );
   FM_SendOutput( 0xb8, 0x09 );
   FM_SendOutput( 0xa7, 0x03 );
   FM_SendOutput( 0xb7, 0x0a );

   FM_RhythmRegister = 0x20;
   FM_SendOutput( 0xbd, FM_RhythmRegister );
   }


/*---------------------------------------------------------------------
   Function: FM_SetChannelTimbre

   Programs a channel's operators from an 11-byte timbre.

   Near miss (0x50478, 22 instruction rows, same 344 bytes): the game
   passes the modulator's level and KSL through a byte stack slot and
   the level through an int slot (two volatile locals give that frame,
   sub esp,0xc), and the carrier's slot is a second local loaded before
   the 0xC0 write (it lives in ECX). What differs: the game keeps the
   byte temp and 63 - lvl in DL/EDX, we get AL/EAX. Tried without a
   change: declaration order (all 120), statement order, chained
   assignment, unsigned/long types, volatile arrays, &-taken locals
   instead of volatile, inlined helpers (-oe: no slots), every compiler.
---------------------------------------------------------------------*/

void FM_SetChannelTimbre
   (
   int channel,
   unsigned char *timbre
   )

   {
   int slot;
   int slot2;
   int offset;
   volatile unsigned char t;   // the game's byte slot [ebp-0x14]
   volatile int lvl;           // the game's int slot [ebp-0x1c]

   if ( timbre == FM_ChannelTimbre[ channel ] )
      {
      return;
      }

   FM_ChannelTimbre[ channel ] = timbre;

   t = timbre[ 0 ] & 0x3f;
   slot = FM_ChannelSlot[ channel ][ 0 ];
   lvl = t;
   FM_SlotLevel[ slot ] = 63 - lvl;
   t = timbre[ 0 ] & 0xc0;
   FM_SlotKSL[ slot ]   = t;

   offset = FM_SlotOffset[ slot ];
   FM_SendOutput( offset + 0x60, timbre[ 2 ] );
   FM_SendOutput( offset + 0x80, timbre[ 4 ] );
   FM_SendOutput( offset + 0x20, timbre[ 6 ] );
   FM_SendOutput( offset + 0xe0, timbre[ 8 ] );

   if ( channel > FIRST_RHYTHM )
      {
      FM_SetSlotVolume( slot );
      return;
      }

   FM_SendOutput( offset + 0x40, timbre[ 0 ] );
   slot2 = FM_ChannelSlot[ channel ][ 1 ];
   FM_SendOutput( channel + 0xc0, timbre[ 10 ] );

   FM_SlotLevel[ slot2 ] = 63 - ( timbre[ 1 ] & 0x3f );
   FM_SlotKSL[ slot2 ]   = timbre[ 1 ] & 0xc0;
   FM_SetSlotVolume( slot2 );

   offset = FM_SlotOffset[ slot2 ];
   FM_SendOutput( offset + 0x60, timbre[ 3 ] );
   FM_SendOutput( offset + 0x80, timbre[ 5 ] );
   FM_SendOutput( offset + 0x20, timbre[ 7 ] );
   FM_SendOutput( offset + 0xe0, timbre[ 9 ] );
   }


/*---------------------------------------------------------------------
   Function: FM_SetChannelVolume

   Sets a channel's volume (0-127), scaled by the global volume.
---------------------------------------------------------------------*/

void FM_SetChannelVolume
   (
   int channel,
   int volume
   )

   {
   int slot;

   FM_ChannelVolume[ channel ] = volume;

   if ( channel <= FIRST_RHYTHM )
      {
      slot = FM_ChannelSlot[ channel ][ 1 ];
      FM_SlotVolume[ slot ] = ( FM_Volume * volume ) / 255;
      FM_SetSlotVolume( slot );

      // Additive synthesis: the modulator is heard too
      if ( ( ( FEEDBACK * )&FM_ChannelTimbre[ channel ][ 10 ] )->Connection )
         {
         slot = FM_ChannelSlot[ channel ][ 0 ];
         FM_SlotVolume[ slot ] = ( FM_Volume * volume ) / 255;
         FM_SetSlotVolume( slot );
         }
      }
   else
      {
      slot = FM_ChannelSlot[ channel ][ 0 ];
      FM_SlotVolume[ slot ] = ( FM_Volume * volume ) / 255;
      FM_SetSlotVolume( slot );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_NoteOn

   Sets a channel's frequency from the song data and starts the note.
   Returns the song data after the frequency bytes.
---------------------------------------------------------------------*/

unsigned char *FM_NoteOn
   (
   int channel,
   unsigned char *data
   )

   {
   if ( channel < FIRST_RHYTHM )
      {
      FM_SendOutput( 0xa0 + channel, *data );
      FM_FrequencyLow[ channel ] = *data++;
      FM_SendOutput( 0xb0 + channel, *data );
      FM_FrequencyHigh[ channel ] = *data++;
      }
   else
      {
      if ( channel == 6 )
         {
         FM_FrequencyLow[ 6 ] = *data++;
         FM_SendOutput( 0xa6, FM_FrequencyLow[ 6 ] );
         FM_FrequencyHigh[ 6 ] = *data++;
         FM_SendOutput( 0xb6, FM_FrequencyHigh[ 6 ] );
         }
      else if ( channel == 8 )
         {
         FM_FrequencyLow[ 8 ] = *data++;
         FM_SendOutput( 0xa8, FM_FrequencyLow[ 8 ] );
         FM_FrequencyHigh[ 8 ] = *data++;
         FM_SendOutput( 0xb8, FM_FrequencyHigh[ 8 ] );
         FM_SendOutput( 0xa7, *data++ );
         FM_SendOutput( 0xb7, *data++ );
         }

      FM_RhythmRegister |= FM_RhythmBits[ channel - FIRST_RHYTHM ];
      FM_SendOutput( 0xbd, FM_RhythmRegister );
      }

   return( data );
   }


/*---------------------------------------------------------------------
   Function: FM_NoteOff

   Releases a channel's note.
---------------------------------------------------------------------*/

void FM_NoteOff
   (
   int channel
   )

   {
   if ( channel < FIRST_RHYTHM )
      {
      FM_SendOutput( 0xb0 + channel, FM_FrequencyHigh[ channel ] & 0xdf );
      }
   else
      {
      FM_RhythmRegister &= ~FM_RhythmBits[ channel - FIRST_RHYTHM ];
      FM_SendOutput( 0xbd, FM_RhythmRegister );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_KeyOn

   Restarts a channel's note at its last frequency.
---------------------------------------------------------------------*/

void FM_KeyOn
   (
   int channel
   )

   {
   if ( channel < FIRST_RHYTHM )
      {
      FM_SendOutput( 0xa0 + channel, FM_FrequencyLow[ channel ] );
      FM_SendOutput( 0xb0 + channel, FM_FrequencyHigh[ channel ] );
      }
   else
      {
      FM_RhythmRegister |= FM_RhythmBits[ channel - FIRST_RHYTHM ];
      FM_SendOutput( 0xbd, FM_RhythmRegister );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_RestoreTimbres

   Programs the melodic channels' timbres again.
---------------------------------------------------------------------*/

void FM_RestoreTimbres
   (
   void
   )

   {
   int channel;
   unsigned char *timbre;

   for( channel = 0; channel < 9; channel++ )
      {
      timbre = FM_ChannelTimbre[ channel ];
      FM_ChannelTimbre[ channel ] = NULL;
      FM_SetChannelTimbre( channel, timbre );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_AllNotesOff

   Releases every operator quickly and every channel's note.
---------------------------------------------------------------------*/

void FM_AllNotesOff
   (
   void
   )

   {
   int slot;
   int channel;

   for( slot = 0; slot < NUM_SLOTS; slot++ )
      {
      FM_SendOutput( FM_SlotOffset[ slot ] + 0x80, 0x0f );
      }

   for( channel = 0; channel < NUM_CHANNELS; channel++ )
      {
      FM_NoteOff( channel );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_SetVolume

   Sets the global volume (0-255).
---------------------------------------------------------------------*/

void FM_SetVolume
   (
   int volume
   )

   {
   int channel;

   volume = min( volume, FM_MaxVolume );
   volume = max( volume, 0 );
   FM_Volume = volume;

   for( channel = 0; channel < NUM_CHANNELS; channel++ )
      {
      FM_SetChannelVolume( channel, FM_ChannelVolume[ channel ] );
      }
   }


/*---------------------------------------------------------------------
   Function: FM_GetVolume

   Returns the global volume.
---------------------------------------------------------------------*/

int FM_GetVolume
   (
   void
   )

   {
   return( FM_Volume );
   }


/*---------------------------------------------------------------------
   Function: FM_LockEnd

   Used for determining the length of the functions to lock in memory.
---------------------------------------------------------------------*/

static void FM_LockEnd
   (
   void
   )

   {
   }


/*---------------------------------------------------------------------
   Function: FM_Shutdown

   Unlocks the layer's code and data.
---------------------------------------------------------------------*/

void FM_Shutdown
   (
   void
   )

   {
   DPMI_UnlockMemoryRegion( FM_SetSlotVolume, FM_LockEnd );
   DPMI_Unlock( FM_Volume );
   DPMI_Unlock( FM_FrequencyLow );
   DPMI_Unlock( FM_FrequencyHigh );
   DPMI_Unlock( FM_RhythmRegister );
   DPMI_Unlock( FM_RhythmBits );
   DPMI_Unlock( FM_SlotVolume );
   DPMI_Unlock( FM_SlotLevel );
   DPMI_Unlock( FM_SlotKSL );
   DPMI_Unlock( FM_ChannelVolume );
   DPMI_Unlock( FM_ChannelTimbre );
   DPMI_Unlock( FM_ChannelSlot );
   DPMI_Unlock( FM_SlotOffset );
   }


/*---------------------------------------------------------------------
   Function: FM_Init

   Locks the layer's code and data. The game's version locks the code
   but calls DPMI_Unlock on the data (CONFIRMED in WW.EXE 0x50950).
---------------------------------------------------------------------*/

int FM_Init
   (
   void
   )

   {
   int status;

   status  = DPMI_LockMemoryRegion( FM_SetSlotVolume, FM_LockEnd );
   status |= DPMI_Unlock( FM_Volume );
   status |= DPMI_Unlock( FM_FrequencyLow );
   status |= DPMI_Unlock( FM_FrequencyHigh );
   status |= DPMI_Unlock( FM_RhythmRegister );
   status |= DPMI_Unlock( FM_RhythmBits );
   status |= DPMI_Unlock( FM_SlotVolume );
   status |= DPMI_Unlock( FM_SlotLevel );
   status |= DPMI_Unlock( FM_SlotKSL );
   status |= DPMI_Unlock( FM_ChannelVolume );
   status |= DPMI_Unlock( FM_ChannelTimbre );
   status |= DPMI_Unlock( FM_ChannelSlot );
   status |= DPMI_Unlock( FM_SlotOffset );

   if ( status != DPMI_Ok )
      {
      FM_Shutdown();
      return( FM_Error );
      }

   return( FM_Ok );
   }
