/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: SDL_audio.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* Allow access to a raw mixing buffer */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_timer.h"
#include "SDL_error.h"
#include "SDL_audio_c.h"
#include "SDL_audiomem.h"
#include "SDL_sysaudio.h"

/* Available audio drivers */
static AudioBootStrap *bootstrap[] = {
#ifdef OPENBSD_AUDIO_SUPPORT
	&OPENBSD_AUDIO_bootstrap,
#endif
#ifdef OSS_SUPPORT
	&DSP_bootstrap,
	&DMA_bootstrap,
#endif
#ifdef ALSA_SUPPORT
	&ALSA_bootstrap,
#endif
#ifdef SUNAUDIO_SUPPORT
	&SUNAUDIO_bootstrap,
#endif
#ifdef DMEDIA_SUPPORT
	&DMEDIA_bootstrap,
#endif
#ifdef ARTSC_SUPPORT
	&ARTSC_bootstrap,
#endif
#ifdef ESD_SUPPORT
	&ESD_bootstrap,
#endif
#ifdef NAS_SUPPORT
	&NAS_bootstrap,
#endif
#ifdef ENABLE_DIRECTX
	&DSOUND_bootstrap,
#endif
#ifdef ENABLE_WINDIB
	&WAVEOUT_bootstrap,
#endif
#ifdef __BEOS__
	&BAUDIO_bootstrap,
#endif
#if defined(macintosh) || TARGET_API_MAC_CARBON
	&SNDMGR_bootstrap,
#endif
#ifdef _AIX
	&Paud_bootstrap,
#endif
#ifdef ENABLE_AHI
	&AHI_bootstrap,
#endif
#ifdef MINTAUDIO_SUPPORT
	&MINTAUDIO_bootstrap,
#endif
#ifdef DISKAUD_SUPPORT
	&DISKAUD_bootstrap,
#endif
#ifdef ENABLE_DC
	&DCAUD_bootstrap,
#endif
	NULL
};
SDL_AudioDevice *current_audio = NULL;

/* Various local functions */
int SDL_AudioInit(const char *driver_name);
void SDL_AudioQuit(void);

#ifdef ENABLE_AHI
static int audio_configured = 0;
#endif

/* The general mixing thread function */
int SDL_RunAudio(void *audiop)
{
	SDL_AudioDevice *audio = (SDL_AudioDevice *)audiop;
	Uint8 *stream;
	int    stream_len;
	void  *udata;
	void (*fill)(void *userdata,Uint8 *stream, int len);
	int    silence;
#ifdef ENABLE_AHI
	int started = 0;

/* AmigaOS NEEDS that the audio driver is opened in the thread that uses it! */

	D(bug("Task audio started audio struct:<%lx>...\n",audiop));

	D(bug("Before Openaudio..."));
	if(audio->OpenAudio(audio, &audio->spec)==-1)
	{
		D(bug("Open audio failed...\n"));
		return(-1);
	}
	D(bug("OpenAudio...OK\n"));
#endif

	/* Perform any thread setup */
	if ( audio->ThreadInit ) {
		audio->ThreadInit(audio);
	}
	audio->threadid = SDL_ThreadID();

	/* Set up the mixing function */
	fill  = audio->spec.callback;
	udata = audio->spec.userdata;

#ifdef ENABLE_AHI
	audio_configured = 1;

	D(bug("Audio configured... Checking for conversion\n"));
	SDL_mutexP(audio->mixer_lock);
	D(bug("Semaphore obtained...\n"));
#endif

	if ( audio->convert.needed ) {
		if ( audio->convert.src_format == AUDIO_U8 ) {
			silence = 0x80;
		} else {
			silence = 0;
		}
		stream_len = audio->convert.len;
	} else {
		silence = audio->spec.silence;
		stream_len = audio->spec.size;
	}
	stream = audio->fake_stream;

#ifdef ENABLE_AHI
	SDL_mutexV(audio->mixer_lock);
	D(bug("Entering audio loop...\n"));
#endif


	/* Loop, filling the audio buffers */
	while ( audio->enabled ) {

		/* Wait for new current buffer to finish playing */
		if ( stream == audio->fake_stream ) {
			SDL_Delay((audio->spec.samples*1000)/audio->spec.freq);
		} else {
#ifdef ENABLE_AHI
			if ( started > 1 )
#endif
			audio->WaitAudio(audio);
		}

		/* Fill the current buffer with sound */
		if ( audio->convert.needed ) {
			if ( audio->convert.buf ) {
				stream = audio->convert.buf;
			} else {
				continue;
			}
		} else {
			stream = audio->GetAudioBuf(audio);
			if ( stream == NULL ) {
				stream = audio->fake_stream;
			}
		}
		memset(stream, silence, stream_len);

		if ( ! audio->paused ) {
			SDL_mutexP(audio->mixer_lock);
			(*fill)(udata, stream, stream_len);
			SDL_mutexV(audio->mixer_lock);
		}

		/* Convert the audio if necessary */
		if ( audio->convert.needed ) {
			SDL_ConvertAudio(&audio->convert);
			stream = audio->GetAudioBuf(audio);
			if ( stream == NULL ) {
				stream = audio->fake_stream;
			}
			memcpy(stream, audio->convert.buf,
			               audio->convert.len_cvt);
		}

		/* Ready current buffer for play and change current buffer */
		if ( stream != audio->fake_stream ) {
			audio->PlayAudio(audio);
#ifdef ENABLE_AHI
/* AmigaOS don't have to wait the first time audio is played! */
			started++;
#endif
		}
	}
	/* Wait for the audio to drain.. */
	if ( audio->WaitDone ) {
		audio->WaitDone(audio);
	}

#ifdef ENABLE_AHI
	D(bug("WaitAudio...Done\n"));

	audio->CloseAudio(audio);

	D(bug("CloseAudio..Done, subtask exiting...\n"));
	audio_configured = 0;
#endif
	return(0);
}

static void SDL_LockAudio_Default(SDL_AudioDevice *audio)
{
	if ( audio->thread && (SDL_ThreadID() == audio->threadid) ) {
		return;
	}
	SDL_mutexP(audio->mixer_lock);
}

static void SDL_UnlockAudio_Default(SDL_AudioDevice *audio)
{
	if ( audio->thread && (SDL_ThreadID() == audio->threadid) ) {
		return;
	}
	SDL_mutexV(audio->mixer_lock);
}

int SDL_AudioInit(const char *driver_name)
{
	SDL_AudioDevice *audio;
	int i = 0, idx;

	/* Check to make sure we don't overwrite 'current_audio' */
	if ( current_audio != NULL ) {
		SDL_AudioQuit();
	}

	/* Select the proper audio driver */
	audio = NULL;
	idx = 0;
#ifdef unix
	if ( (driver_name == NULL) && (getenv("ESPEAKER") != NULL) ) {
		/* Ahem, we know that if ESPEAKER is set, user probably wants
		   to use ESD, but don't start it if it's not already running.
		   This probably isn't the place to do this, but... Shh! :)
		 */
		for ( i=0; bootstrap[i]; ++i ) {
			if ( strcmp(bootstrap[i]->name, "esd") == 0 ) {
				const char *esd_no_spawn;

				/* Don't start ESD if it's not running */
				esd_no_spawn = getenv("ESD_NO_SPAWN");
				if ( esd_no_spawn == NULL ) {
					putenv("ESD_NO_SPAWN=1");
				}
				if ( bootstrap[i]->available() ) {
					audio = bootstrap[i]->create(0);
					break;
				}
#ifdef linux	/* No unsetenv() on most platforms */
				if ( esd_no_spawn == NULL ) {
					unsetenv("ESD_NO_SPAWN");
				}
#endif
			}
		}
	}
#endif /* unix */
	if ( audio == NULL ) {
		if ( driver_name != NULL ) {
#if 0	/* This will be replaced with a better driver selection API */
			if ( strrchr(driver_name, ':') != NULL ) {
				idx = atoi(strrchr(driver_name, ':')+1);
			}
#endif
			for ( i=0; bootstrap[i]; ++i ) {
				if (strncmp(bootstrap[i]->name, driver_name,
				            strlen(bootstrap[i]->name)) == 0) {
					if ( bootstrap[i]->available() ) {
						audio=bootstrap[i]->create(idx);
						break;
					}
				}
			}
		} else {
			for ( i=0; bootstrap[i]; ++i ) {
				if ( bootstrap[i]->available() ) {
					audio = bootstrap[i]->create(idx);
					if ( audio != NULL ) {
						break;
					}
				}
			}
		}
		if ( audio == NULL ) {
			SDL_SetError("No available audio device");
#if 0 /* Don't fail SDL_Init() if audio isn't available.
         SDL_OpenAudio() will handle it at that point.  *sigh*
       */
			return(-1);
#endif
		}
	}
	current_audio = audio;
	if ( current_audio ) {
		current_audio->name = bootstrap[i]->name;
		if ( !current_audio->LockAudio && !current_audio->UnlockAudio ) {
			current_audio->LockAudio = SDL_LockAudio_Default;
			current_audio->UnlockAudio = SDL_UnlockAudio_Default;
		}
	}
	return(0);
}

char *SDL_AudioDriverName(char *namebuf, int maxlen)
{
	if ( current_audio != NULL ) {
		strncpy(namebuf, current_audio->name, maxlen-1);
		namebuf[maxlen-1] = '\0';
		return(namebuf);
	}
	return(NULL);
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	SDL_AudioDevice *audio;

	/* Start up the audio driver, if necessary */
	if ( ! current_audio ) {
		if ( (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) ||
		     (current_audio == NULL) ) {
			return(-1);
		}
	}
	audio = current_audio;

	if (audio->opened) {
		SDL_SetError("Audio device is already opened");
		return(-1);
	}

	/* Verify some parameters */
	if ( desired->callback == NULL ) {
		SDL_SetError("SDL_OpenAudio() passed a NULL callback");
		return(-1);
	}
	switch ( desired->channels ) {
	    case 1:	/* Mono */
	    case 2:	/* Stereo */
		break;
	    default:
		SDL_SetError("1 (mono) and 2 (stereo) channels supported");
		return(-1);
	}

#ifdef macintosh
	/* FIXME: Need to implement PPC interrupt asm for SDL_LockAudio() */
#else
#if defined(__MINT__) && !defined(ENABLE_THREADS)
	/* Uses interrupt driven audio, without thread */
#else
	/* Create a semaphore for locking the sound buffers */
	audio->mixer_lock = SDL_CreateMutex();
	if ( audio->mixer_lock == NULL ) {
		SDL_SetError("Couldn't create mixer lock");
		SDL_CloseAudio();
		return(-1);
	}
#endif /* __MINT__ */
#endif /* macintosh */

	/* Calculate the silence and size of the audio specification */
	SDL_CalculateAudioSpec(desired);

	/* Open the audio subsystem */
	memcpy(&audio->spec, desired, sizeof(audio->spec));
	audio->convert.needed = 0;
	audio->enabled = 1;
	audio->paused  = 1;

#ifndef ENABLE_AHI

/* AmigaOS opens audio inside the main loop */
	audio->opened = audio->OpenAudio(audio, &audio->spec)+1;

	if ( ! audio->opened ) {
		SDL_CloseAudio();
		return(-1);
	}
#else
	D(bug("Locking semaphore..."));
	SDL_mutexP(audio->mixer_lock);

	audio->thread = SDL_CreateThread(SDL_RunAudio, audio);
	D(bug("Created thread...\n"));

	if ( audio->thread == NULL ) {
		SDL_mutexV(audio->mixer_lock);
		SDL_CloseAudio();
		SDL_SetError("Couldn't create audio thread");
		return(-1);
	}

	while(!audio_configured)
		SDL_Delay(100);
#endif

	/* If the audio driver changes the buffer size, accept it */
	if ( audio->spec.samples != desired->samples ) {
		desired->samples = audio->spec.samples;
		SDL_CalculateAudioSpec(desired);
	}

	/* Allocate a fake audio memory buffer */
	audio->fake_stream = SDL_AllocAudioMem(audio->spec.size);
	if ( audio->fake_stream == NULL ) {
		SDL_CloseAudio();
		SDL_OutOfMemory();
		return(-1);
	}

	/* See if we need to do any conversion */
	if ( memcmp(desired, &audio->spec, sizeof(audio->spec)) == 0 ) {
		/* Just copy over the desired audio specification */
		if ( obtained != NULL ) {
			memcpy(obtained, &audio->spec, sizeof(audio->spec));
		}
	} else {
		/* Copy over the audio specification if possible */
		if ( obtained != NULL ) {
			memcpy(obtained, &audio->spec, sizeof(audio->spec));
		} else {
			/* Build an audio conversion block */
			if ( SDL_BuildAudioCVT(&audio->convert,
				desired->format, desired->channels,
						desired->freq,
				audio->spec.format, audio->spec.channels,
						audio->spec.freq) < 0 ) {
				SDL_CloseAudio();
				return(-1);
			}
			if ( audio->convert.needed ) {
				audio->convert.len = desired->size;
				audio->convert.buf =(Uint8 *)SDL_AllocAudioMem(
				   audio->convert.len*audio->convert.len_mult);
				if ( audio->convert.buf == NULL ) {
					SDL_CloseAudio();
					SDL_OutOfMemory();
					return(-1);
				}
			}
		}
	}

#ifndef ENABLE_AHI
	/* Start the audio thread if necessary */
	switch (audio->opened) {
		case  1:
			/* Start the audio thread */
			audio->thread = SDL_CreateThread(SDL_RunAudio, audio);
			if ( audio->thread == NULL ) {
				SDL_CloseAudio();
				SDL_SetError("Couldn't create audio thread");
				return(-1);
			}
			break;

		default:
			/* The audio is now playing */
			break;
	}
#else
	SDL_mutexV(audio->mixer_lock);
	D(bug("SDL_OpenAudio USCITA...\n"));

#endif

	return(0);
}

SDL_audiostatus SDL_GetAudioStatus(void)
{
	SDL_AudioDevice *audio = current_audio;
	SDL_audiostatus status;

	status = SDL_AUDIO_STOPPED;
	if ( audio && audio->enabled ) {
		if ( audio->paused ) {
			status = SDL_AUDIO_PAUSED;
		} else {
			status = SDL_AUDIO_PLAYING;
		}
	}
	return(status);
}

void SDL_PauseAudio (int pause_on)
{
	SDL_AudioDevice *audio = current_audio;

	if ( audio ) {
		audio->paused = pause_on;
	}
}

void SDL_LockAudio (void)
{
	SDL_AudioDevice *audio = current_audio;

	/* Obtain a lock on the mixing buffers */
	if ( audio && audio->LockAudio ) {
		audio->LockAudio(audio);
	}
}

void SDL_UnlockAudio (void)
{
	SDL_AudioDevice *audio = current_audio;

	/* Release lock on the mixing buffers */
	if ( audio && audio->UnlockAudio ) {
		audio->UnlockAudio(audio);
	}
}

void SDL_CloseAudio (void)
{
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDL_AudioQuit(void)
{
	SDL_AudioDevice *audio = current_audio;

	if ( audio ) {
		audio->enabled = 0;
		if ( audio->thread != NULL ) {
			SDL_WaitThread(audio->thread, NULL);
		}
		if ( audio->mixer_lock != NULL ) {
			SDL_DestroyMutex(audio->mixer_lock);
		}
		if ( audio->fake_stream != NULL ) {
			SDL_FreeAudioMem(audio->fake_stream);
		}
		if ( audio->convert.needed ) {
			SDL_FreeAudioMem(audio->convert.buf);

		}
#ifndef ENABLE_AHI
		if ( audio->opened ) {
			audio->CloseAudio(audio);
			audio->opened = 0;
		}
#endif
		/* Free the driver data */
		audio->free(audio);
		current_audio = NULL;
	}
}

#define NUM_FORMATS	6
static int format_idx;
static int format_idx_sub;
static Uint16 format_list[NUM_FORMATS][NUM_FORMATS] = {
 { AUDIO_U8, AUDIO_S8, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB },
 { AUDIO_S8, AUDIO_U8, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB },
 { AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_S16MSB, AUDIO_S16LSB, AUDIO_U16MSB, AUDIO_U16LSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_U16LSB, AUDIO_U16MSB, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_U16MSB, AUDIO_U16LSB, AUDIO_S16MSB, AUDIO_S16LSB, AUDIO_U8, AUDIO_S8 },
};

Uint16 SDL_FirstAudioFormat(Uint16 format)
{
	for ( format_idx=0; format_idx < NUM_FORMATS; ++format_idx ) {
		if ( format_list[format_idx][0] == format ) {
			break;
		}
	}
	format_idx_sub = 0;
	return(SDL_NextAudioFormat());
}

Uint16 SDL_NextAudioFormat(void)
{
	if ( (format_idx == NUM_FORMATS) || (format_idx_sub == NUM_FORMATS) ) {
		return(0);
	}
	return(format_list[format_idx][format_idx_sub++]);
}

void SDL_CalculateAudioSpec(SDL_AudioSpec *spec)
{
	switch (spec->format) {
		case AUDIO_U8:
			spec->silence = 0x80;
			break;
		default:
			spec->silence = 0x00;
			break;
	}
	spec->size = (spec->format&0xFF)/8;
	spec->size *= spec->channels;
	spec->size *= spec->samples;
}

