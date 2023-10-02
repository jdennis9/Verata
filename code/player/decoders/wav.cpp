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
#include "../decoders.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct WAV_Fmt_Chunk {
	u16 sample_type;
	u16 num_channels;
	u32 sample_rate;
	u32 bytes_per_second;
	u16 bits_per_frame;
	u16 bits_per_sample;
};

struct WAV_Chunk_Header {
	char type[4];
	u32 length;
};

struct WAV_Header {
	char signature[4];
	u32 file_size;
	char wave_header[4];
};

static struct WAV_Stream {
	FILE *io;
	// For converting from integer to float and vice versa
	s32 *conversion_buffer;
	u64 current_sample;
	u32 pcm_start_offset;
	u32 conversion_buffer_size;
	u32 sample_size;
	u64 total_samples;
} g_wav;

int open_wav(const wchar_t *path, float buffer_duration_ms, PCM_Format *format) {
	WAV_Header header;
	WAV_Fmt_Chunk fmt;
	WAV_Chunk_Header chunk;
	
	g_wav.io = _wfopen(path, L"rb");
	
	if (!g_wav.io) {
		log_error("Failed to open WAV stream \"%ls\"\n", path);
		return false;
	}
	
	fread(&header, sizeof(header), 1, g_wav.io);
	
	if (strncmp(header.signature, "RIFF", 4)) {
		log_error("Malformed WAV header for \"%ls\"\n", path);
		return false;
	}
	
	while (1) {
		fread(&chunk, sizeof(chunk), 1, g_wav.io);
		if (!strncmp(chunk.type, "fmt", 3)) {
			fread(&fmt, sizeof(fmt), 1, g_wav.io);
			format->sample_rate = fmt.sample_rate;
			format->sample_size = fmt.bits_per_sample / 8;
		} 
		else if (!strncmp(chunk.type, "data", 4)) {
			format->total_samples = chunk.length / format->sample_size;
			g_wav.pcm_start_offset = ftell(g_wav.io);
			break;
		}
		else {
			log_debug("Skipping chunk \"%s\"\n", chunk.type);
			fseek(g_wav.io, chunk.length, SEEK_CUR);
		}
	};
	
	log_debug("WAV Header:\n"
			  "Channels: %d\n"
			  "Sample rate: %u Hz\n"
			  "Sample size: %u bytes\n"
			  "Total samples: %u\n",
			  fmt.num_channels,
			  format->sample_rate,
			  format->sample_size,
			  format->total_samples);
	
	if (fmt.num_channels != 2) {
		log_error("Non-stereo WAV streaming not implemented\n");
		return false;
	}
	
	g_wav.total_samples = format->total_samples;
	g_wav.sample_size = format->sample_size;
	g_wav.current_sample = 0;
	// 1 second of buffer.
	g_wav.conversion_buffer_size = (buffer_duration_ms / 1000.f) * format->sample_rate;
	g_wav.conversion_buffer = (s32*)malloc(g_wav.conversion_buffer_size * 4 * 2);
	return true;
}

static void convert_24bit_to_float(void *in_bytes, float *out, u32 count) {
	u32 bytes_read = 0;
	u8 *in = (u8*)in_bytes;
	for (u32 i = 0; i < count*3; i += 3) {		
		out[i/3] = ((in[i] << 8) | (in[i+1] << 16) | (in[i+2] << 24)) / (float)INT32_MAX;
	}
}

static void convert_16bit_to_float(void *in_bytes, float *out, u32 count) {
	s16 *in = (s16*)in_bytes;
	for (u32 i = 0; i < count; ++i) {
		out[i] = in[i] / (float)INT16_MAX;
	}
}

int decode_wav(u32 num_frames, float *out_buffer) {
	u8 *buffer = (u8*)g_wav.conversion_buffer;
	
	fread(buffer, num_frames*2, g_wav.sample_size, g_wav.io);
	
	if (feof(g_wav.io) || g_wav.current_sample >= g_wav.total_samples)
		return false;
	
	u32 num_samples = num_frames * 2;
	
	switch (g_wav.sample_size) {
		case 3:
		convert_24bit_to_float(buffer, out_buffer, num_samples);
		break;
		case 2:
		convert_16bit_to_float(buffer, out_buffer, num_samples);
		break;
	}
	
	g_wav.current_sample += num_samples;
	return true;
}

int seek_wav(u64 sample) {
	fseek(g_wav.io, g_wav.pcm_start_offset + (g_wav.sample_size * sample), SEEK_SET);
	g_wav.current_sample = sample;
	return true;
}

u64 get_sample_wav() {
	return g_wav.current_sample;
}

void close_wav() {
	fclose(g_wav.io);
	free(g_wav.conversion_buffer);
}