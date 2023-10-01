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
#include "tags.h"
#include <opus/opusfile.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

// Some ID3 numbers are stored as "synchsafe" integers and
// need to be altered before use
static u32 synch_safe_integer(u32 i) {
	u32 x = (i & 0x000000ff);
	u32 a = (i & 0x0000ff00) >> 1;
	u32 b = (i & 0x00ff0000) >> 2;
	u32 c = (i & 0xff000000) >> 3;
	
	return x | a | b | c;
}

static u32 reverse_endian(u32 i) {
	u32 ret;
	u8 *in = (u8*)&i;
	u8 *out = (u8*)&ret;
	
	out[3] = in[0];
	out[2] = in[1];
	out[1] = in[2];
	out[0] = in[3];
	
	return ret;
}

static inline char *read_and_increment(char *in, int bytes, void *out) {
	memcpy(out, in, bytes);
	return in + bytes;
}

bool read_id3_tags(FILE *file, char *artist, int artist_max, char *title, int title_max) {
	struct ID3 {
		char signature[3];
		u8 version[2];
		u8 flags;
		u8 size[4];
	};
	
	struct ID3_Frame {
		char id[4];
		u32 size;
		u8 flags[2];
	};
	
	ID3 id3;
	u32 id3_structure_size;
	void *id3_structure;
	char *frame_data;
	char *frame_data_end;
	
	fread(&id3, 10, 1, file);
	
	// Check if structure is ID3v2
	if (strncmp(id3.signature, "ID3", 3)) return false;
	
	memcpy(&id3_structure_size, id3.size, 4);
	id3_structure_size = synch_safe_integer(reverse_endian(id3_structure_size));
	
	//log_debug("ID3 signature: %c%c%c\n", id3.signature[0], id3.signature[1], id3.signature[2]);
	//log_debug("ID3 version: %u.%u\n", id3.version[0], id3.version[1]);
	//log_debug("ID3 flags: %u\n", id3.flags);
	//log_debug("ID3 structure size: %uB (%uKB)\n", id3_structure_size, id3_structure_size >> 10);
	
	if (id3.flags & (1 << 7)) {
		struct {
			u32 size;
			u8 flag_bytes;
			u8 flags;
		} extended_header;
		
		fread(&extended_header, sizeof(extended_header), 1, file);
		fseek(file, synch_safe_integer(reverse_endian(extended_header.size)), SEEK_CUR);
	}
	
	struct {
		const char *id;
		char *out;
		int max;
	} associations[] = {
		{"TIT2", title, title_max},
		{"TPE1", artist, artist_max},
	};
	
	id3_structure = malloc(id3_structure_size);
	frame_data = (char*)id3_structure;
	frame_data_end = frame_data + id3_structure_size;
	fread(frame_data, id3_structure_size, 1, file);
	
	bool association_found;
	
	while (frame_data < frame_data_end) {
		ID3_Frame frame;
		frame_data = read_and_increment(frame_data, 10, &frame);
		frame.size = synch_safe_integer(reverse_endian(frame.size));
		association_found = false;
		
		for (u32 i = 0; i < ARRAY_LENGTH(associations); ++i) {
			if (!strncmp(frame.id, associations[i].id, 4)) {
				u8 encoding;
				frame_data = read_and_increment(frame_data, 1, &encoding);
				frame.size--;
				if (frame.size >= associations[i].max) {
					log_error("%s tag too large!\n", associations[i].id);
					free(id3_structure);
					return false;
				}
				frame_data = read_and_increment(frame_data, frame.size, associations[i].out);
				associations[i].out[frame.size] = 0;
				
				//log_debug("ID3 %s value: %s\n", associations[i].id, associations[i].out);
				association_found = true;
			}
		}
		
		if (!association_found) {
			// Check if we've entered padding
			u32 ok = 0;
			for (int i = 0; i < 4; ++i) {
				ok &= ((frame.id[i] >= 'A') && (frame.id[i] <= 'Z')) 
					|| ((frame.id[i] >= '0') && (frame.id[i] <= '9'));
			}
			
			if (!ok) {
				//log_debug("Encountered padding after ID3 frames\n");
				break;
			}
			frame_data += frame.size;
		}
	}
	
	free(id3_structure);
	return true;
}

bool read_tags(enum Codec codec, const wchar_t *file_path, char *artist, int artist_max, char *title, int title_max) {
	FILE *file;
	bool ret;
	
	ret = false;
	file = _wfopen(file_path, L"rb");
	if (!file) return false;
	
	switch (codec) {
		case CODEC_MP3: {
			ret = read_id3_tags(file, artist, artist_max, title, title_max);
			break;
		}
		default: {
			artist[0] = 0;
			title[0] = 0;
			break;
		}
	}
	
	fclose(file);
	
	return ret;
}
	