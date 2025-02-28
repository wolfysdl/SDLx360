/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2012 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_config.h"
#include <xtl.h>
#include "SDL_audio.h"
#include "SDL_audio_c.h"
#include "SDL_sysaudio.h"
#include <assert.h>

#define INITGUID 1
#include <XAudio2.h>

/* Hidden "this" pointer for the audio functions */
#define _THIS	SDL_AudioDevice *this

struct SDL_PrivateAudioData
{
    IXAudio2 *ixa2;
    IXAudio2SourceVoice *source;
    IXAudio2MasteringVoice *mastering;
    HANDLE semaphore;
    Uint8 *mixbuf;
    int mixlen;
    Uint8 *nextbuf;
};

static void
XAUDIO2_DetectDevices(int iscapture, SDL_AddAudioDevice addfn)
{
    IXAudio2 *ixa2 = NULL;
    UINT32 devcount = 0;
    UINT32 i = 0;
    void *ptr = NULL;

    if (iscapture) {
        SDL_SetError("XAudio2: capture devices unsupported.");
        return;
    } else if (XAudio2Create(&ixa2, 0, XAUDIO2_DEFAULT_PROCESSOR) != S_OK) {
        SDL_SetError("XAudio2: XAudio2Create() failed.");
        return;
    } else if (IXAudio2_GetDeviceCount(ixa2, &devcount) != S_OK) {
        SDL_SetError("XAudio2: IXAudio2::GetDeviceCount() failed.");
        IXAudio2_Release(ixa2);
        return;
    }
#if 0 
    for (i = 0; i < devcount; i++) {
        XAUDIO2_DEVICE_DETAILS details;
        if (IXAudio2_GetDeviceDetails(ixa2, i, &details) == S_OK) {
            char *str = utf16_to_utf8(details.DisplayName);
            if (str != NULL) {
                addfn(str);
                free(str);  /* addfn() made a copy of the string. */
            }
        }
    }
#endif
    IXAudio2_Release(ixa2);
}

static void STDMETHODCALLTYPE
VoiceCBOnBufferEnd(THIS_ void *data)
{
    /* Just signal the SDL audio thread and get out of XAudio2's way. */
    SDL_AudioDevice *this = (SDL_AudioDevice *) data;
    ReleaseSemaphore(this->hidden->semaphore, 1, NULL);
}

static void STDMETHODCALLTYPE
VoiceCBOnVoiceError(THIS_ void *data, HRESULT Error)
{
    /* !!! FIXME: attempt to recover, or mark device disconnected. */
    assert(0 && "write me!");
}

/* no-op callbacks... */
static void STDMETHODCALLTYPE VoiceCBOnStreamEnd(THIS) {}
static void STDMETHODCALLTYPE VoiceCBOnVoiceProcessPassStart(THIS_ UINT32 b) {}
static void STDMETHODCALLTYPE VoiceCBOnVoiceProcessPassEnd(THIS) {}
static void STDMETHODCALLTYPE VoiceCBOnBufferStart(THIS_ void *data) {}
static void STDMETHODCALLTYPE VoiceCBOnLoopEnd(THIS_ void *data) {}


static Uint8 *
XAUDIO2_GetDeviceBuf(_THIS)
{
    return this->hidden->nextbuf;
}

static void
XAUDIO2_PlayDevice(_THIS)
{
    XAUDIO2_BUFFER buffer;
    Uint8 *mixbuf = this->hidden->mixbuf;
    Uint8 *nextbuf = this->hidden->nextbuf;
    const int mixlen = this->hidden->mixlen;
    IXAudio2SourceVoice *source = this->hidden->source;
    HRESULT result = S_OK;

    if (!this->enabled) { /* shutting down? */
        return;
    }

    /* Submit the next filled buffer */
    //SDL_zero(buffer);
	//memset(&(x), 0, sizeof((x)))
    memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = mixlen;
    buffer.pAudioData = nextbuf;
    buffer.pContext = this;

    if (nextbuf == mixbuf) {
        nextbuf += mixlen;
    } else {
        nextbuf = mixbuf;
    }
    this->hidden->nextbuf = nextbuf;

    result = IXAudio2SourceVoice_SubmitSourceBuffer(source, &buffer, NULL);
    if (result == XAUDIO2_E_DEVICE_INVALIDATED) {
        /* !!! FIXME: possibly disconnected or temporary lost. Recover? */
    }

    if (result != S_OK) {  /* uhoh, panic! */
        IXAudio2SourceVoice_FlushSourceBuffers(source);
        this->enabled = 0;
    }
}

static void
XAUDIO2_WaitDevice(_THIS)
{
    if (this->enabled) {
        WaitForSingleObject(this->hidden->semaphore, INFINITE);
    }
}

static void
XAUDIO2_WaitDone(_THIS)
{
    IXAudio2SourceVoice *source = this->hidden->source;
    XAUDIO2_VOICE_STATE state;
    assert(!this->enabled);  /* flag that stops playing. */
    IXAudio2SourceVoice_Discontinuity(source);
    IXAudio2SourceVoice_GetState(source, &state, 0);
    while (state.BuffersQueued > 0) {
        WaitForSingleObject(this->hidden->semaphore, INFINITE);
        IXAudio2SourceVoice_GetState(source, &state, 0);
    }
}


static void
XAUDIO2_CloseDevice(_THIS)
{
    if (this->hidden != NULL) {
        IXAudio2 *ixa2 = this->hidden->ixa2;
        IXAudio2SourceVoice *source = this->hidden->source;
        IXAudio2MasteringVoice *mastering = this->hidden->mastering;

        if (source != NULL) {
            IXAudio2SourceVoice_Stop(source, 0, XAUDIO2_COMMIT_NOW);
            IXAudio2SourceVoice_FlushSourceBuffers(source);
            IXAudio2SourceVoice_DestroyVoice(source);
        }
        if (ixa2 != NULL) {
            IXAudio2_StopEngine(ixa2);
        }
        if (mastering != NULL) {
            IXAudio2MasteringVoice_DestroyVoice(mastering);
        }
        if (ixa2 != NULL) {
            IXAudio2_Release(ixa2);
        }
        if (this->hidden->mixbuf != NULL) {
            free(this->hidden->mixbuf);
        }
        if (this->hidden->semaphore != NULL) {
            CloseHandle(this->hidden->semaphore);
        }

        free(this->hidden);
        this->hidden = NULL;
    }
}

static int
XAUDIO2_OpenDevice(_THIS, const char *devname, int iscapture)
{
    HRESULT result = S_OK;
    WAVEFORMATEX waveformat;
    int valid_format = 0;
    Uint16 test_format = SDL_FirstAudioFormat(this->spec.format);
    IXAudio2 *ixa2 = NULL;
    IXAudio2SourceVoice *source = NULL;
    UINT32 devId = 0;  /* 0 == system default device. */

	static IXAudio2VoiceCallbackVtbl callbacks_vtable = {
	    VoiceCBOnVoiceProcessPassStart,
        VoiceCBOnVoiceProcessPassEnd,
        VoiceCBOnStreamEnd,
        VoiceCBOnBufferStart,
        VoiceCBOnBufferEnd,
        VoiceCBOnLoopEnd,
        VoiceCBOnVoiceError
	};

	static IXAudio2VoiceCallback callbacks = { &callbacks_vtable };

    if (iscapture) {
        SDL_SetError("XAudio2: capture devices unsupported.");
        return 0;
    } else if (XAudio2Create(&ixa2, 0, XAUDIO2_DEFAULT_PROCESSOR) != S_OK) {
        SDL_SetError("XAudio2: XAudio2Create() failed.");
        return 0;
    }

    if (devname != NULL) {
        UINT32 devcount = 0;
        UINT32 i = 0;

        if (IXAudio2_GetDeviceCount(ixa2, &devcount) != S_OK) {
            IXAudio2_Release(ixa2);
            SDL_SetError("XAudio2: IXAudio2_GetDeviceCount() failed.");
            return 0;
        }
        for (i = 0; i < devcount; i++) {
            XAUDIO2_DEVICE_DETAILS details;
            if (IXAudio2_GetDeviceDetails(ixa2, i, &details) == S_OK) {                
                devId = i;
                break;
            }
        }

        if (i == devcount) {
            IXAudio2_Release(ixa2);
            SDL_SetError("XAudio2: Requested device not found.");
            return 0;
        }
    }

    /* Initialize all variables that we clean on shutdown */
    this->hidden = (struct SDL_PrivateAudioData *)
        malloc((sizeof *this->hidden));
    if (this->hidden == NULL) {
        IXAudio2_Release(ixa2);
        return 0;
    }
    memset(this->hidden, 0, (sizeof *this->hidden));

    this->hidden->ixa2 = ixa2;
    this->hidden->semaphore = CreateSemaphore(NULL, 1, 2, NULL);
    if (this->hidden->semaphore == NULL) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: CreateSemaphore() failed!");
        return 0;
    }

    while ((!valid_format) && (test_format)) {
        switch (test_format) {
        case AUDIO_U8:
        case AUDIO_S16:
            break;
       
		}
        test_format = SDL_NextAudioFormat();
    }

    if (!valid_format) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: Unsupported audio format");
        return 0;
    }

    /* Update the fragment size as size in bytes */
    SDL_CalculateAudioSpec(&this->spec);

    /* We feed a Source, it feeds the Mastering, which feeds the device. */
    this->hidden->mixlen = this->spec.size;
    this->hidden->mixbuf = (Uint8 *) malloc(2 * this->hidden->mixlen);
    if (this->hidden->mixbuf == NULL) {
        XAUDIO2_CloseDevice(this);
        return 0;
    }
    this->hidden->nextbuf = this->hidden->mixbuf;
    memset(this->hidden->mixbuf, 0, 2 * this->hidden->mixlen);

    /* We use XAUDIO2_DEFAULT_CHANNELS instead of this->spec.channels. On
       Xbox360, this means 5.1 output, but on Windows, it means "figure out
       what the system has." It might be preferable to let XAudio2 blast
       stereo output to appropriate surround sound configurations
       instead of clamping to 2 channels, even though we'll configure the
       Source Voice for whatever number of channels you supply. */
    result = IXAudio2_CreateMasteringVoice(ixa2, &this->hidden->mastering,
                                           XAUDIO2_DEFAULT_CHANNELS,
                                           this->spec.freq, 0, devId, NULL);
    if (result != S_OK) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: Couldn't create mastering voice");
        return 0;
    }

    SDL_zero(waveformat);
    if (SDL_AUDIO_ISFLOAT(this->spec.format)) {
        waveformat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    } else {
        waveformat.wFormatTag = WAVE_FORMAT_PCM;
    }
    waveformat.wBitsPerSample = SDL_AUDIO_BITSIZE(this->spec.format);
    waveformat.nChannels = this->spec.channels;
    waveformat.nSamplesPerSec = this->spec.freq;
    waveformat.nBlockAlign =
        waveformat.nChannels * (waveformat.wBitsPerSample / 8);
    waveformat.nAvgBytesPerSec =
        waveformat.nSamplesPerSec * waveformat.nBlockAlign;

    result = IXAudio2_CreateSourceVoice(ixa2, &source, &waveformat, 0,
                                        1.0f, &callbacks, NULL, NULL);
    if (result != S_OK) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: Couldn't create source voice");
        return 0;
    }
    this->hidden->source = source;

    /* Start everything playing! */
    result = IXAudio2_StartEngine(ixa2);
    if (result != S_OK) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: Couldn't start engine");
        return 0;
    }

    result = IXAudio2SourceVoice_Start(source, 0, XAUDIO2_COMMIT_NOW);
    if (result != S_OK) {
        XAUDIO2_CloseDevice(this);
        SDL_SetError("XAudio2: Couldn't start source voice");
        return 0;
    }

    return 1; /* good to go. */
}

static void
XAUDIO2_Deinitialize(void)
{
    
}

static int
XAUDIO2_Init(SDL_AudioDevice * impl)
{
    /* XAudio2Create() is a macro that uses COM; we don't load the .dll */
    IXAudio2 *ixa2 = NULL;

    if (XAudio2Create(&ixa2, 0, XAUDIO2_DEFAULT_PROCESSOR) != S_OK) {
        SDL_SetError("XAudio2: XAudio2Create() failed");
        return 0;  /* not available. */
    }
    IXAudio2_Release(ixa2);

    /* Set the function pointers */
    impl->DetectDevices = XAUDIO2_DetectDevices;
    impl->OpenDevice = XAUDIO2_OpenDevice;
    impl->PlayDevice = XAUDIO2_PlayDevice;
    impl->WaitDevice = XAUDIO2_WaitDevice;
    impl->WaitDone = XAUDIO2_WaitDone;
    impl->GetDeviceBuf = XAUDIO2_GetDeviceBuf;
    impl->CloseDevice = XAUDIO2_CloseDevice;
    impl->Deinitialize = XAUDIO2_Deinitialize;

    return 1;   /* this audio target is available. */
}

AudioBootStrap XAUDIO2_bootstrap = {
    "xaudio2", "XAudio2", XAUDIO2_Init, 0
};

/* vi: set ts=4 sw=4 expandtab: */