/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"
#ifdef HAVE_LIBESD
#include <esd.h>
#endif
#include <gnome.h>

#include "balsa-app.h"
#include "sound.h"

typedef struct _sample
  {
    int rate;
    int format;
    int samples;
    short *data;
    int id;
  }
Sample;


typedef struct _WAVFormatChunk
  {
    char chunkID[4];
    int chunkSize;

    short wFormatTag;
    unsigned short wChannels;
    unsigned int dwSamplesPerSec;
    unsigned int dwAvgBytesPerSec;
    unsigned short wBlockAlign;
    unsigned short wBitsPerSample;
  }
WAVFormatChunk;

#ifdef WORDS_BIGENDIAN
#define SWAP_SHORT( x ) x = ( ( x & 0x00ff ) << 8 ) | ( ( x >> 8 ) & 0x00ff )
#define SWAP_LONG( x ) x = ( ( ( x & 0x000000ff ) << 24 ) |\
( ( x & 0x0000ff00 ) << 8 ) |\
( ( x & 0x00ff0000 ) >> 8 ) |\
( ( x & 0xff000000 ) >> 24 ) )
#endif

Sample *
LoadWav (char *file)
{
  FILE *f;
  Sample *s;
  char buf[4];
  WAVFormatChunk fmt;
  int skipl = 0;
  int skipr = 0;
  char bytes = 0;
  char stereo = 0;
  int len;

  /* int                 count; */

#ifdef HAVE_LIBESD
  f = fopen (file, "r");
  if (!f)
    return NULL;
  s = g_malloc (sizeof (Sample));
  if (!s)
    {
      fclose (f);
      return NULL;
    }
  s->rate = 44100;
  s->format = ESD_STREAM | ESD_PLAY;
  s->samples = 0;
  s->data = NULL;
  s->id = 0;
  fread (buf, 1, 4, f);
  if ((buf[0] != 'R') ||
      (buf[1] != 'I') ||
      (buf[2] != 'F') ||
      (buf[3] != 'F'))
    {
      /* not a RIFF WAV file */
      fclose (f);
      g_free (s);
      return NULL;
    }
  fread (buf, 1, 4, f);
  fread (buf, 1, 4, f);
  fread (fmt.chunkID, 1, 4, f);
  fread (&(fmt.chunkSize), 1, 4, f);

#ifdef WORDS_BIGENDIAN
  SWAP_LONG (fmt.chunkSize);
#endif

  if ((fmt.chunkID[0] == 'f') &&
      (fmt.chunkID[1] == 'm') &&
      (fmt.chunkID[2] == 't') &&
      (fmt.chunkID[3] == ' ') &&
      16 == fmt.chunkSize)
    /* fmt chunk */
    {
      fread (&(fmt.wFormatTag), 1, 2, f);
      fread (&(fmt.wChannels), 1, 2, f);
      fread (&(fmt.dwSamplesPerSec), 1, 4, f);
      fread (&(fmt.dwAvgBytesPerSec), 1, 4, f);
      fread (&(fmt.wBlockAlign), 1, 2, f);
      fread (&(fmt.wBitsPerSample), 1, 2, f);
#ifdef WORDS_BIGENDIAN
      SWAP_SHORT (fmt.wFormatTag);
      SWAP_SHORT (fmt.wChannels);
      SWAP_LONG (fmt.dwSamplesPerSec);
      SWAP_LONG (fmt.dwAvgBytesPerSec);
      SWAP_SHORT (fmt.wBlockAlign);
      SWAP_SHORT (fmt.wBitsPerSample);
#endif

      if (fmt.wFormatTag != 1)
	{
	  /* unknown WAV encoding format - exit */
	  fclose (f);
	  g_free (s);
	  return NULL;
	}
      skipl = 0;
      skipr = 0;
      bytes = 0;
      stereo = 0;
      if (fmt.wChannels == 1)
	s->format |= ESD_MONO;
      else if (fmt.wChannels == 2)
	{
	  stereo = 1;
	  s->format |= ESD_STEREO;
	}
      else
	{
	  stereo = 1;
	  s->format |= ESD_STEREO;
	  if (fmt.wChannels == 3)
	    {
	      skipl = 0;
	      skipr = 1;
	    }
	  else if (fmt.wChannels == 4)
	    {
	      skipl = 0;
	      skipr = 2;
	    }
	  else if (fmt.wChannels == 4)
	    {
	      skipl = 0;
	      skipr = 2;
	    }
	  else if (fmt.wChannels == 6)
	    {
	      skipl = 3;
	      skipr = 1;
	    }
	  else
	    {
	      /* unknown channel encoding */
	      fclose (f);
	      g_free (s);
	      return NULL;
	    }
	}
      s->rate = fmt.dwSamplesPerSec;
      if (fmt.wBitsPerSample <= 8)
	{
	  bytes = 1;
	  s->format |= ESD_BITS8;
	}
      else if (fmt.wBitsPerSample <= 16)
	s->format |= ESD_BITS16;
      else
	{
	  /* unknown bits encoding encoding */
	  fclose (f);
	  g_free (s);
	  return NULL;
	}
    }
  for (;;)
    {
      if (fread (buf, 1, 4, f) &&
	  fread (&len, 4, 1, f))
	{
#ifdef WORDS_BIGENDIAN
	  SWAP_LONG (len);
#endif

	  if ((buf[0] != 'd') ||
	      (buf[1] != 'a') ||
	      (buf[2] != 't') ||
	      (buf[3] != 'a'))
	    fseek (f, len, SEEK_CUR);
	  else
	    {
	      s->data = g_malloc (len);
	      if (!s->data)
		{
		  fclose (f);
		  g_free (s);
		  return NULL;
		}
	      if ((skipl == 0) && (skipr == 0))
		{
		  fread (s->data, len, 1, f);
#ifdef WORDS_BIGENDIAN
		  if (fmt.wBitsPerSample > 8 && fmt.wBitsPerSample <= 16)
		    {
		      char *tmp;
		      char tmpval;
		      int i;

		      tmp = (char *) (s->data);

		      for (i = 0; i < len; i++)
			{
			  tmpval = tmp[i];
			  tmp[i] = tmp[i + 1];
			  tmp[i + 1] = tmpval;
			}
		    }
#endif
		}
	      else
		{
		}
	      s->samples = len;
	      if (stereo)
		s->samples /= 2;
	      if (!bytes)
		s->samples /= 2;
	      fclose (f);
	      return s;
	    }
	}
      else
	{
	  fclose (f);
	  return NULL;
	}
    }
  fclose (f);
  g_free (s);
  if (s->data)
    g_free (s->data);
#endif
  return NULL;
}

void
SoundPlay (Sample * s)
{
  int size;

  int confirm = 0;

#ifdef HAVE_LIBESD
  if (!s)
    return;

  size = s->samples;
  if (s->format & ESD_STEREO)
    size *= 2;

  if (!s->id)
    {
      if (balsa_app.esound)
	{
	  if (s->data)
	    {
	      /* "name" of all samples is currently "E", should be name of sound 
	       * file, or event type, for later identification */
	      s->id = esd_sample_cache (balsa_app.esound, s->format, s->rate,
					size * 2, "E");
	      write (balsa_app.esound, s->data, size * 2);
	      confirm = esd_confirm_sample_cache (balsa_app.esound);
	      if (s->id <= 0 || confirm != s->id)
		{
		  printf ("SoundPlay: error caching sample <%d>!\n", s->id);
		  s->id = 0;
		}
	      g_free (s->data);
	      s->data = NULL;
	    }
	}
    }
  if (s->id > 0)
    esd_sample_play (balsa_app.esound, s->id);

  fsync (balsa_app.esound);

#endif
}

void
DestroySample (Sample * s)
{
#ifdef HAVE_LIBESD
  if (s->id)
    {
      /*      Why the hell is this symbol not in esd? */
/*      it's in esd.h - evil evil evil */
/*      esd_sample_kill(sound_fd,s->id); */
      esd_sample_free (balsa_app.esound, s->id);
    }
#endif
  if (s->data)
    g_free (s->data);
  g_free (s);
}

void 
play_sound (gchar * file)
{
  Sample *sample;

  sample = LoadWav (file);
  SoundPlay (sample);
  DestroySample (sample);
}
