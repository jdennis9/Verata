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
#ifndef DECODERS_H
#define DECODERS_H

#include "common.h"

// Return true on success. Needs to write the format to the given pointer
typedef int Decoder_Open_Function(const wchar_t *path, float buffer_duration_ms, PCM_Format *format);
// Return false to close the stream
typedef int Decoder_Decode_Function(u32 num_frames, float *buffer);
// Get the current sample number
typedef u64 Decoder_Get_Sample_Function();
typedef int Decoder_Seek_Function(u64 sample);
typedef void Decoder_Close_Function();

struct Decoder {
	Decoder_Open_Function *open_func;
	Decoder_Decode_Function *decode_func;
	Decoder_Get_Sample_Function *get_sample_func;
	Decoder_Seek_Function *seek_func;
	Decoder_Close_Function *close_func;
};

int open_opus(const wchar_t *path, float buffer_duration_ms, PCM_Format *format);
int decode_opus(u32 num_frames, float *buffer);
int seek_opus(u64 sample);
u64 get_sample_opus();
void close_opus();

int open_mp3(const wchar_t *path, float buffer_duration_ms, PCM_Format *format);
int decode_mp3(u32 num_frames, float *buffer);
int seek_mp3(u64 sample);
u64 get_sample_mp3();
void close_mp3();

int open_flac(const wchar_t *path, float buffer_duration_ms, PCM_Format *format);
int decode_flac(u32 num_frames, float *buffer);
int seek_flac(u64 sample);
u64 get_sample_flac();
void close_flac();

int open_wav(const wchar_t *path, float buffer_duration_ms, PCM_Format *format);
int decode_wav(u32 num_frames, float *buffer);
int seek_wav(u64 sample);
u64 get_sample_wav();
void close_wav();

#endif //DECODERS_H
