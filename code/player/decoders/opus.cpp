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
#include <opus/opusfile.h>

static OggOpusFile *g_opus;

int open_opus(const wchar_t *path, PCM_Format *format) {
	char path_u8[512];
	utf16_to_utf8(path, path_u8, sizeof(path_u8));
	
	int error;
	g_opus = op_open_file(path_u8, &error);
	
	if (!g_opus || error) {
		log_error("Failed to open opus stream \"%s\"\n", path_u8);
		return false;
	}
	
	format->total_samples = op_pcm_total(g_opus, -1);
	format->sample_rate = 48000;
	
	return true;
}

int decode_opus(u32 num_frames, float *buffer) {
	int total_read = 0;
	
	while (total_read < num_frames) {
		int max_readable = (num_frames - total_read) * 2;
		int read = op_read_float_stereo(g_opus, &buffer[total_read*2], max_readable);
		
		if (read == 0) return false;
		else if (read < 0) {
			log_error("An OPUS streaming error occured\n");
			return false;
		}
		
		total_read += read;
	}

	return true;
}

u64 get_sample_opus() {
	return op_pcm_tell(g_opus);
}

int seek_opus(u64 sample) {
	int error;
	error = op_pcm_seek(g_opus, sample);
	
	if (error) {
		log_error("op_pcm_seek() failed with code %d\n", error);
		return false;
	}
	
	return true;
}

void close_opus() {
	op_free(g_opus);
}