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
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
#include "../decoders.h"
#include <minimp3.h>
#include <minimp3_ex.h>

static mp3dec_ex_t g_mp3;

int open_mp3(const wchar_t *path, float buffer_duration_ms, PCM_Format *format) {
	if (mp3dec_ex_open_w(&g_mp3, path, MP3D_SEEK_TO_SAMPLE)) {
		log_error("Failed to open mp3 stream \"%ls\"\n", path);
		return false;
	}
	
	format->total_samples = g_mp3.samples;
	format->sample_rate = g_mp3.info.hz;
	
	return true;
}

int decode_mp3(u32 num_frames, float *buffer) {
	return mp3dec_ex_read(&g_mp3, buffer, num_frames*2) == (num_frames*2);
}

u64 get_sample_mp3() {
	return g_mp3.cur_sample;
}

int seek_mp3(u64 sample) {
	mp3dec_ex_seek(&g_mp3, sample);
	return true;
}

void close_mp3() {
	mp3dec_ex_close(&g_mp3);
}