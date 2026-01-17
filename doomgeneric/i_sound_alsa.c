#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"

#include "music/oplplayer.h"
#include "music/opl.h"
#include "mus2mid.h"

#define SAMPLERATE 22050
#define NUM_CHANNELS 16
#define BUFFER_SAMPLES 1024

static snd_pcm_t *pcm_handle = NULL;
static pthread_t audio_thread;
static boolean audio_thread_running = false;
int mus_opl_gain = 50;

typedef struct {
    int16_t *data;
    int length;
    int position;
    int volume;
    boolean active;
} channel_t;

static channel_t channels[NUM_CHANNELS];
static int16_t mix_buffer[BUFFER_SAMPLES];
static int16_t music_buffer[BUFFER_SAMPLES * 2];
static boolean music_playing = false;

static boolean I_ALSA_InitMusic(void)
{
    OPL_Init(SAMPLERATE);
    I_OPL_InitMusic(SAMPLERATE);
    printf("OPL music initialized\n");
    return true;
}

static void I_ALSA_ShutdownMusic(void)
{
    I_OPL_ShutdownMusic();
    OPL_Shutdown();
}

static void I_ALSA_SetMusicVolume(int volume)
{
    // Volume is 0-15, convert to OPL gain 0-100
    mus_opl_gain = (volume * 100) / 15;
}

static void I_ALSA_PauseMusic(void)
{
    I_OPL_PauseSong();
    music_playing = false;
}

static void I_ALSA_ResumeMusic(void)
{
    I_OPL_ResumeSong();
    music_playing = true;
}

static void* I_ALSA_RegisterSong(void *data, int len)
{
    MEMFILE *instream = mem_fopen_read(data, len);
    MEMFILE *outstream = mem_fopen_write();

    if (!instream) {
        printf("Failed to open MUS data\n");
        return NULL;
    }

    if (!outstream) {
        printf("Failed to open output stream for writing\n");
        mem_fclose(instream);
        return NULL;
    }

    if (mus2mid(instream, outstream)) {
        printf("Failed to convert MUS to MIDI\n");
        mem_fclose(instream);
        mem_fclose(outstream);
        return NULL;
    }

    mem_fclose(instream);

    void *midi_data;
    size_t midi_len;
    mem_get_buf(outstream, &midi_data, &midi_len);

    const void *handle = I_OPL_RegisterSong(midi_data, midi_len);

    mem_fclose(outstream);

    return (void *)handle;
}

static void I_ALSA_UnRegisterSong(void *handle)
{
    I_OPL_UnRegisterSong(handle);
}

static void I_ALSA_PlaySong(void *handle, boolean looping)
{
    I_OPL_PlaySong(handle, looping);
    music_playing = true;
}

static void I_ALSA_StopSong(void)
{
    I_OPL_StopSong();
    music_playing = false;
}

static boolean I_ALSA_MusicIsPlaying(void)
{
    return music_playing && I_OPL_MusicIsPlaying();
}

music_module_t DG_music_module = {
    NULL,
    0,
    I_ALSA_InitMusic,
    I_ALSA_ShutdownMusic,
    I_ALSA_SetMusicVolume,
    I_ALSA_PauseMusic,
    I_ALSA_ResumeMusic,
    I_ALSA_RegisterSong,
    I_ALSA_UnRegisterSong,
    I_ALSA_PlaySong,
    I_ALSA_StopSong,
    I_ALSA_MusicIsPlaying,
    NULL,
};

static int16_t* convert_sound(byte *data, int length)
{
    int16_t *converted = malloc(length * sizeof(int16_t));
    if (!converted) return NULL;

    for (int i = 0; i < length; i++) {
        int16_t sample = data[i] << 8;
        sample ^= 0x8000;
        converted[i] = sample;
    }

    return converted;
}

static void* audio_thread_func(void *arg)
{
    while (audio_thread_running) {
        memset(mix_buffer, 0, sizeof(mix_buffer));

        // First, render music into mix_buffer if playing
        if (music_playing) {
            OPL_Render_Samples(music_buffer, BUFFER_SAMPLES);
            // Convert stereo to mono and add to mix buffer
            for (int s = 0; s < BUFFER_SAMPLES; s++) {
                int music_sample = (music_buffer[s * 2] + music_buffer[s * 2 + 1]) / 2;
                // Scale music down a bit to leave headroom for sound effects
                mix_buffer[s] = (music_sample * mus_opl_gain) / 100;
            }
        }

        // Mix all active sound channels on top of music
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (!channels[i].active || !channels[i].data) continue;

            for (int s = 0; s < BUFFER_SAMPLES && channels[i].position < channels[i].length; s++) {
                int sample = channels[i].data[channels[i].position];
                // Apply channel volume (0-127 scale)
                sample = (sample * channels[i].volume) / 127;

                // ADD to mix buffer (don't replace!)
                int mixed = mix_buffer[s] + sample;

                // Clamp to prevent distortion
                if (mixed > 32767) mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                mix_buffer[s] = mixed;

                channels[i].position++;
            }

            if (channels[i].position >= channels[i].length) {
                channels[i].active = false;
            }
        }

        // Write to ALSA
        if (pcm_handle) {
            int err = snd_pcm_writei(pcm_handle, mix_buffer, BUFFER_SAMPLES);
            if (err == -EPIPE) {
                snd_pcm_prepare(pcm_handle);
            } else if (err < 0) {
                snd_pcm_recover(pcm_handle, err, 0);
            }
        }
    }
    return NULL;
}

static boolean I_ALSA_InitSound(boolean _use_sfx_prefix)
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    printf("I_ALSA_InitSound: Initializing ALSA (mono) at %d Hz\n", SAMPLERATE);

    err = snd_pcm_open(&pcm_handle, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "ALSA open failed: %s\n", snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, 1); // MONO

    unsigned int rate = SAMPLERATE;
    snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);

    snd_pcm_uframes_t period_size = BUFFER_SAMPLES;
    snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params, &period_size, 0);

    err = snd_pcm_hw_params(pcm_handle, hw_params);
    if (err < 0) {
        fprintf(stderr, "ALSA hw_params failed: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return false;
    }

    snd_pcm_prepare(pcm_handle);
    memset(channels, 0, sizeof(channels));

    audio_thread_running = true;
    pthread_create(&audio_thread, NULL, audio_thread_func, NULL);

    printf("ALSA initialized: mono, %d Hz\n", rate);
    return true;
}

static void I_ALSA_ShutdownSound(void)
{
    audio_thread_running = false;
    pthread_join(audio_thread, NULL);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i].data) {
            free(channels[i].data);
            channels[i].data = NULL;
        }
    }

    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
    }
}

static int I_ALSA_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

static void I_ALSA_UpdateSound(void)
{
    // empty
}

static void I_ALSA_UpdateSoundParams(int handle, int vol, int sep)
{
    if (handle < 0 || handle >= NUM_CHANNELS) return;
    channels[handle].volume = vol;
    // sep ignored for mono
}

static int I_ALSA_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    int lumpnum, lumplen;
    byte *data;

    if (channel < 0 || channel >= NUM_CHANNELS) return -1;

    if (channels[channel].data) {
        channels[channel].active = false;
    }

    lumpnum = I_ALSA_GetSfxLumpNum(sfxinfo);
    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    if (lumplen > 8) {
        data += 8;
        lumplen -= 8;
    }

    channels[channel].data = convert_sound(data, lumplen);
    if (!channels[channel].data) return -1;

    channels[channel].length = lumplen;
    channels[channel].position = 0;
    channels[channel].volume = vol;
    channels[channel].active = true;

    return channel;
}

static void I_ALSA_StopSound(int handle)
{
    if (handle < 0 || handle >= NUM_CHANNELS) return;
    channels[handle].active = false;
}

static boolean I_ALSA_SoundIsPlaying(int handle)
{
    if (handle < 0 || handle >= NUM_CHANNELS) return false;
    return channels[handle].active;
}

static snddevice_t sound_devices[] = { SNDDEVICE_SB };

sound_module_t DG_sound_module = {
    sound_devices,
    1,
    I_ALSA_InitSound,
    I_ALSA_ShutdownSound,
    I_ALSA_GetSfxLumpNum,
    I_ALSA_UpdateSound,
    I_ALSA_UpdateSoundParams,
    I_ALSA_StartSound,
    I_ALSA_StopSound,
    I_ALSA_SoundIsPlaying,
    NULL,
};

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
