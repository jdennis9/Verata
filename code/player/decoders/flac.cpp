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
#include <assert.h>
#include <stdlib.h>
#include <FLAC/stream_decoder.h>
#include "../decoders.h"

static struct Flac_Stream {
	FLAC__StreamDecoder *decoder;
	float *buffer;
	float *overflow_buffer;
	u32 overflow_position;
	u32 overflow_size;
	u32 buffer_position;
	u32 buffer_size;
	u32 current_sample;
} g_flac;

static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, 
														  const FLAC__Frame *frame, const FLAC__int32 *const buffer[], 
														  void *client_data) {
	Flac_Stream *stream = (Flac_Stream*)client_data;
	
	u32 bits_per_sample = FLAC__stream_decoder_get_bits_per_sample(g_flac.decoder);
	s32 sample_max = 0;
	for (u32 i = 0; i < bits_per_sample; ++i) sample_max |= 1<<i;
	
	sample_max /= 2;
	
	bool fill_overflow = false;
	u32 i = 0;
	
	for (; (i < frame->header.blocksize) && !fill_overflow; ++i) {
		g_flac.buffer[g_flac.buffer_position+0] = (double)buffer[0][i] / (double)sample_max;
		g_flac.buffer[g_flac.buffer_position+1] = (double)buffer[1][i] / (double)sample_max;
		
		g_flac.buffer_position += 2;
		
		if (g_flac.buffer_position >= g_flac.buffer_size) fill_overflow = true;
	}
	
	for (; (i < frame->header.blocksize) && (g_flac.overflow_position < g_flac.overflow_size); ++i) {
		g_flac.overflow_buffer[g_flac.overflow_position+0] = (double)buffer[0][i] / (double)sample_max;
		g_flac.overflow_buffer[g_flac.overflow_position+1] = (double)buffer[1][i] / (double)sample_max;
		g_flac.overflow_position += 2;
	}
	
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void error_callback(const FLAC__StreamDecoder *decoder, 
								FLAC__StreamDecoderErrorStatus status, 
								void *client_data) {
	log_error("FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

static void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, 
								   void *client_data) {
	PCM_Format *format = (PCM_Format*)client_data;
	
	if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		format->total_samples = metadata->data.stream_info.total_samples;
		format->sample_rate = metadata->data.stream_info.sample_rate;
		
		log_debug("Sample rate: %u Hz\n", format->sample_rate);
		log_debug("Total samples: %u\n", format->total_samples);
	}
}

int open_flac(const wchar_t *path, PCM_Format *format) {
	FLAC__StreamDecoderInitStatus status;
	FILE *file = _wfopen(path, L"rb");
	
	g_flac.decoder = FLAC__stream_decoder_new();
	g_flac.buffer = NULL;
	g_flac.current_sample = 0;
	g_flac.overflow_buffer = (float*)malloc(48000*sizeof(float));
	g_flac.overflow_size = 48000;
	g_flac.overflow_position = 0;
	
	FLAC__stream_decoder_set_md5_checking(g_flac.decoder, true);
	status = FLAC__stream_decoder_init_FILE(g_flac.decoder, file, &write_callback, 
											&metadata_callback, 
											&error_callback, format);
	assert(status == FLAC__STREAM_DECODER_INIT_STATUS_OK);
	
	FLAC__stream_decoder_process_until_end_of_metadata(g_flac.decoder);
	
	return true;
}

int decode_flac(u32 num_frames, float *buffer) {
	g_flac.buffer = buffer;
	g_flac.buffer_position = 0;
	g_flac.buffer_size = num_frames*2;
	
	for (u32 i = 0; i < g_flac.overflow_position; i += 2) {
		buffer[i+0] = g_flac.overflow_buffer[i+0];
		buffer[i+1] = g_flac.overflow_buffer[i+1];
	}
	
	g_flac.buffer_position += g_flac.overflow_position;
	g_flac.overflow_position = 0;
	
	while (g_flac.buffer_position < (num_frames*2)) {
		FLAC__stream_decoder_process_single(g_flac.decoder);
	}
	
	g_flac.current_sample += g_flac.buffer_position;
	
	return true;
}

u64 get_sample_flac() {
	return g_flac.current_sample;
}

int seek_flac(u64 sample) {
	FLAC__stream_decoder_seek_absolute(g_flac.decoder, sample);
	g_flac.current_sample = sample;
	return true;
}

void close_flac() {
	free(g_flac.overflow_buffer);
}
