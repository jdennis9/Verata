/*
   Copyright 2023 Jamie Dennis

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "player.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <samplerate.h>
#include <math.h>
#include <string.h>
#include <opus/opusfile.h>
#include <FLAC/stream_decoder.h>
#include "decoders.h"

const CLSID g_device_enumerator_clsid = __uuidof(MMDeviceEnumerator);
const IID g_device_enumerator_iid = __uuidof(IMMDeviceEnumerator);
const IID g_audio_client_iid = __uuidof(IAudioClient);
const IID g_audio_render_client_iid = __uuidof(IAudioRenderClient);
const IID g_audio_stream_volume_iid = __uuidof(IAudioStreamVolume);

static IAudioStreamVolume *g_volume_controller;

static struct {
	HANDLE mutex;
	enum Player_State state;
	enum Codec codec;
	void *sample_rate_converter;
	Player_End_Callback *end_callback;
	
	// Signal to interrupt audio thread sleep and reset the audio clock
	HANDLE interrupt_semaphore;
	HANDLE ready_semaphore;
	HANDLE audio_thread;
	
	PCM_Format format;
	
	IMMDevice *device;
	IMMDeviceEnumerator *device_enumerator;
	IAudioClient *audio_client;
	IAudioRenderClient *render_client;
	
	bool file_loaded;
} g_stream;

static Decoder g_decoder;

static inline const char *get_codec_name(enum Codec codec) {
	static const char *opus = "OPUS";
	static const char *wav = "WAV";
	static const char *mp3 = "MP3";
	static const char *unknown = "Unrecognized";
	static const char *flac = "FLAC";
	
	switch (codec) {
		case CODEC_OPUS: return opus;
		case CODEC_WAV: return wav;
		case CODEC_MP3: return mp3;
		case CODEC_FLAC: return flac;
		default: return unknown;
	}
}

static inline bool get_decoder_functions(enum Codec codec) {
	switch (codec) {
		
		case CODEC_FLAC:
		g_decoder.open_func = &open_flac;
		g_decoder.decode_func = &decode_flac;
		g_decoder.get_sample_func = &get_sample_flac;
		g_decoder.seek_func = &seek_flac;
		g_decoder.close_func = &close_flac;
		break;
		
		case CODEC_MP3:
		g_decoder.open_func = &open_mp3;
		g_decoder.decode_func = &decode_mp3;
		g_decoder.get_sample_func = &get_sample_mp3;
		g_decoder.seek_func = &seek_mp3;
		g_decoder.close_func = &close_mp3;
		break;
		
		case CODEC_OPUS:
		g_decoder.open_func = &open_opus;
		g_decoder.decode_func = &decode_opus;
		g_decoder.get_sample_func = &get_sample_opus;
		g_decoder.seek_func = &seek_opus;
		g_decoder.close_func = &close_opus;
		break;
		
		case CODEC_WAV:
		g_decoder.open_func = &open_wav;
		g_decoder.decode_func = &decode_wav;
		g_decoder.get_sample_func = &get_sample_wav;
		g_decoder.seek_func = &seek_wav;
		g_decoder.close_func = &close_wav;
		break;
		
		default: return false;
	}
	
	return true;
}

static inline bool is_file_loaded() {
	return g_stream.file_loaded;
}

static inline void reset_audio_clock() {
	ReleaseSemaphore(g_stream.interrupt_semaphore, 1, NULL);
}

enum Codec find_codec_from_file_name(const wchar_t *path) {
	wchar_t *extension = wcsrchr((wchar_t*)path, '.');
	
	if (!extension) {
		return CODEC_NONE;
	}
	else if (!wcscmp(extension, L".mp3")) {
		return CODEC_MP3;
	}
	else if (!wcscmp(extension, L".opus") || !wcscmp(extension, L".ogg")) {
		return CODEC_OPUS;
	}
	else if (!wcscmp(extension, L".wav")) {
		return CODEC_WAV;
	}
	else if (!wcscmp(extension, L".flac")) {
		return CODEC_FLAC;
	}
	else {
		return CODEC_NONE;
	}	
}

bool stream_to_buffer(const PCM_Format *output_format, int num_frames, float *output_buffer);

static void close_stream_source() {
	g_decoder.close_func();
	g_stream.file_loaded = false;
}

static void clean_up() {
	CloseHandle(g_stream.interrupt_semaphore);
	CloseHandle(g_stream.mutex);
	if (g_stream.audio_client) g_stream.audio_client->Release();
	if (g_stream.render_client) g_stream.render_client->Release();
	if (g_stream.device) g_stream.device->Release();
	if (g_stream.device_enumerator) g_stream.device_enumerator->Release();
	if (is_file_loaded()) close_stream_source();
	if (g_volume_controller) g_volume_controller->Release();
}

DWORD audio_thread_entry(LPVOID user_data) {
	WAVEFORMATEX *mix_format = NULL;
	u32 num_buffer_frames;
	u8 *output_buffer;
	DWORD flags = 0;
	PCM_Format pcm_format;
	bool format_is_float;
	
	CoCreateInstance(g_device_enumerator_clsid, NULL, 
					 CLSCTX_ALL, g_device_enumerator_iid, (void**)&g_stream.device_enumerator);
	g_stream.device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &g_stream.device);
	g_stream.device->Activate(__uuidof(g_stream.audio_client), CLSCTX_ALL, NULL, (void**)&g_stream.audio_client);
	g_stream.audio_client->GetMixFormat(&mix_format);
	
	if (mix_format->cbSize >= 22) {
		WAVEFORMATEXTENSIBLE *mix_format_ex = (WAVEFORMATEXTENSIBLE*)mix_format;
		GUID sub_format = mix_format_ex->SubFormat;
		format_is_float = sub_format.Data1 == 3;
	}
	
	log_info("Endpoint sample rate: %dHz\n", mix_format->nSamplesPerSec);
	
	DEBUG_ASSERT(format_is_float);
	
	pcm_format.sample_rate = mix_format->nSamplesPerSec;
	pcm_format.sample_type = PCM_TYPE_F32;
	pcm_format.sample_size = 4;
	
	g_stream.audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, (u64)1e7, 0, mix_format, NULL);
	g_stream.audio_client->GetBufferSize(&num_buffer_frames);
	g_stream.audio_client->GetService(g_audio_render_client_iid, (void**)&g_stream.render_client);
	g_stream.audio_client->GetService(g_audio_stream_volume_iid, (void**)&g_volume_controller);
	
	ReleaseSemaphore(g_stream.ready_semaphore, 1, NULL);
	g_stream.render_client->GetBuffer(num_buffer_frames, &output_buffer);
	g_stream.render_client->ReleaseBuffer(num_buffer_frames, 0);
	
	const DWORD buffer_duration_ms = ((mix_format->nSamplesPerSec*1000) / num_buffer_frames) / 2;
	log_info("Buffer duration: %dms\n", buffer_duration_ms);
	CoTaskMemFree(mix_format);
	
	g_stream.audio_client->Start();
	
	while (1) {
		u32 frame_padding;
		u32 available_frames = 0;
		
		// If the sleep is interrupted, we need to reset the audio clock
		if (WaitForSingleObject(g_stream.interrupt_semaphore, buffer_duration_ms) != WAIT_TIMEOUT) {
			g_stream.audio_client->Stop();
			g_stream.audio_client->Reset();
			g_stream.audio_client->Start();
		}
		
		g_stream.audio_client->GetCurrentPadding(&frame_padding);
		available_frames = num_buffer_frames - frame_padding;
		
		// Need this to make sure the buffer is at least 120ms for opus decoding.
		if (g_stream.codec == CODEC_OPUS) while (available_frames < (2 * 120 * (48000/1000))) {
			g_stream.audio_client->GetCurrentPadding(&frame_padding);
			available_frames = num_buffer_frames - frame_padding;
		}
		
		g_stream.render_client->GetBuffer(available_frames, &output_buffer);
		
		if (stream_to_buffer(&pcm_format, available_frames, (float*)output_buffer)) {
			if (g_stream.end_callback) g_stream.end_callback();
		}
		
		g_stream.render_client->ReleaseBuffer(available_frames, 0);
	}
	
	g_stream.audio_client->Stop();
	if (!format_is_float) free(output_buffer);
	
	return 0;
}

void start_playback_stream(Player_End_Callback *end_callback) {
	int error;
	g_stream.mutex = CreateMutex(NULL, FALSE, NULL);
	g_stream.end_callback = end_callback;
	g_stream.interrupt_semaphore = CreateSemaphore(NULL, 0, 1, NULL);
	g_stream.ready_semaphore = CreateSemaphore(NULL, 0, 1, NULL);
	g_stream.sample_rate_converter = src_new(SRC_SINC_BEST_QUALITY, 2, &error);
	g_stream.audio_thread = CreateThread(NULL, 256<<10, &audio_thread_entry, NULL, 0, NULL);
	
	WaitForSingleObject(g_stream.ready_semaphore, INFINITE);
	CloseHandle(g_stream.ready_semaphore);
	atexit(clean_up);
}

static inline void lock_stream() {
	WaitForSingleObject(g_stream.mutex, INFINITE);
}

static inline void unlock_stream() {
	ReleaseMutex(g_stream.mutex);
}

bool open_track(const wchar_t *path) {
	lock_stream();
	if (is_file_loaded()) {
		close_stream_source();
	}
	
	enum Codec codec = find_codec_from_file_name(path);
	g_stream.codec = codec;
	
	if (!get_decoder_functions(codec)) {
		unlock_stream();
		user_warning("No decoder available for codec \"%s\"\n", get_codec_name(codec));
		return false;
	}
	
	if (!g_decoder.open_func(path, &g_stream.format)) {
		unlock_stream();
		return false;
	}
	
	g_stream.file_loaded = true;
	log_info("Now playing: %ls\n", path);
	g_stream.state = PLAYER_STATE_PLAYING;
	unlock_stream();
	reset_audio_clock();
	return true;
}

bool track_is_playing() {
	return g_stream.state == PLAYER_STATE_PLAYING;
}

void pause_playback() {
	g_stream.state = PLAYER_STATE_PAUSED;
	reset_audio_clock();
}

void resume_playback() {
	g_stream.state = PLAYER_STATE_PLAYING;
	reset_audio_clock();
}

int toggle_playback() {
	int ret = g_stream.state;
	if (g_stream.state == PLAYER_STATE_PLAYING) {
		g_stream.state = PLAYER_STATE_PAUSED;
		pause_playback();
	}
	else if (g_stream.state == PLAYER_STATE_PAUSED) {
		g_stream.state = PLAYER_STATE_PLAYING;
		resume_playback();
	}
	return ret;
}

void set_playback_volume(float volume) {
	DEBUG_ASSERT(volume <= 1.f);
	float volumes[2] = {volume, volume};
	g_volume_controller->SetAllVolumes(2, volumes);
}

float get_playback_volume() {
	float ret;
	g_volume_controller->GetChannelVolume(0, &ret);
	return ret;
}

bool stream_to_buffer(const PCM_Format *output_format, int num_frames, float *output_buffer) {
	lock_stream();
	// If we aren't playing, zero the output buffer
	if (!is_file_loaded() || g_stream.state != PLAYER_STATE_PLAYING) {
		memset(output_buffer, 0, num_frames * 2 * sizeof(float));
		unlock_stream();
		return false;
	}
	
	float *decode_buffer;
	int num_input_frames;
	const float sample_rate_ratio = (float)output_format->sample_rate / (float)g_stream.format.sample_rate;
	bool needs_sample_rate_conversion = g_stream.format.sample_rate != output_format->sample_rate;
	bool end_of_file = false;
	
	if (needs_sample_rate_conversion) {
		num_input_frames = ceil(num_frames / sample_rate_ratio);
		decode_buffer = (float*)malloc(sizeof(float) * num_input_frames * 2);
	}
	else {
		num_input_frames = num_frames;
		decode_buffer = output_buffer;
	}
	
	// Decode into PCM
	if (!g_decoder.decode_func(num_input_frames, decode_buffer)) {
		unlock_stream();
		return true;
	}
	
	if (g_decoder.get_sample_func() >= g_stream.format.total_samples) {
		unlock_stream();
		return true;
	}
	
	// Convert sample rate if needed
	if (needs_sample_rate_conversion) {
		SRC_STATE *converter = (SRC_STATE*)g_stream.sample_rate_converter;
		SRC_DATA data = {};
		data.input_frames = num_input_frames;
		data.data_in = decode_buffer;
		data.src_ratio = sample_rate_ratio;
		data.output_frames = num_frames;
		data.data_out = output_buffer;
		
		src_set_ratio(converter, sample_rate_ratio);
		src_process(converter, &data);
		
		free(decode_buffer);
	}
	
	unlock_stream();
	return end_of_file;
}

void seek_playback_to_seconds(float seconds) {
	lock_stream();
	if (!is_file_loaded()) return;
	
	u64 sample = (u64)(g_stream.format.sample_rate * seconds) * 2;
	g_decoder.seek_func(sample);
	
	// Reset the audio stream so we instantly skip to the new position
	reset_audio_clock();
	unlock_stream();
}

float get_playback_length() {
	if (!is_file_loaded()) return 0.f;
	if (g_stream.format.sample_rate) return (float)g_stream.format.total_samples / (float)g_stream.format.sample_rate / 2.f;
	else return 0.f;
}

float get_playback_position() {
	float ret = 0.f;
	u32 sample;
	if (!is_file_loaded()) return 0.f;
	
	lock_stream();
	sample = g_decoder.get_sample_func();
	unlock_stream();
	
	return sample / (float)g_stream.format.sample_rate / 2.f;
}

