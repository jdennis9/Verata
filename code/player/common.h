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
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <limits.h>
#include <assert.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VERATA_VERSION_MAJOR 0
#define VERATA_VERSION_MINOR 0
#define VERATA_VERSION_PATCH 3
#define VERATA_VERSION_STRING "0.0.3"

#ifndef NDEBUG
#define DEBUG_ASSERT(expression) assert(expression)
#else
#define DEBUG_ASSERT(expression) expression
#endif

// Assertions for the user. If the assertion fails, show an error message and exit.
#define USER_ASSERT_FMT(expression, message, ...) if (!(expression)) fatal_error(message, __VA_ARGS__)
#define USER_ASSERT(expression, message) if (!(expression)) fatal_error(message);

void fatal_error(const char *message, ...);
void user_warning(const char *message, ...);

enum {
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
};

enum Codec {
	CODEC_NONE,
	CODEC_MP3,
	CODEC_OPUS,
	CODEC_WAV,
	CODEC_FLAC,
};

enum PCM_Type {
	PCM_TYPE_S24,
	PCM_TYPE_S16,
	PCM_TYPE_F32,
};

struct PCM_Format {
	u64 total_samples;
	u32 sample_rate;
	u32 sample_size;
	PCM_Type sample_type;
};

void log_info(const char *msg, ...);
void log_warning(const char *msg, ...);
void log_error(const char *msg, ...);
void log_debug(const char *msg, ...);

void *system_allocate(u32 size);
void *system_reallocate(void *address, u32 old_size, u32 new_size);
void system_free(void *address, u32 size);

u32 utf8_to_utf16(const char *in, wchar_t *out, u32 max_out);
u32 utf16_to_utf8(const wchar_t *in, char *out, u32 max_out);

enum Codec find_codec_from_file_name(const wchar_t *file_name);

template<typename T>
struct Large_Auto_Array {
	T *elements;
	u32 count;
	u64 allocated_elements;
	
	void remove(u32 i);
	void remove_range(u32 start, u32 end);
	T *push();
	T *push_n(u32 n);
	void push_value(T value);
	u32 push_offset_n(u32 n);
	void reset();
	void free();
};

struct Track_Info {
	u32 album;
	u32 artist;
	u32 title;
	u32 relative_file_path;
};


struct Track_Array {
	Large_Auto_Array<u32> ids;
	Large_Auto_Array<Track_Info> info;
	u32 count;
	
	void add_from_id(u32 id);
	void add_from_info(const Track_Info *track);
	void add(u32 id, const Track_Info *track);
	void remove(u32 i);
	void remove_range(u32 start, u32 end);
	void reset();
	void free();
};

struct Playlist {
	// Keep a separate array for all ids because invalid ids are stil allowed in the playlist
	Large_Auto_Array<u32> track_ids;
	//Large_Auto_Array<Track_Info> tracks;
	Track_Array tracks;
	char name[64];
	
	// Update tracks after a library scan
	void update_tracks();
	u32 get_id();
	bool has_track(u32 id);
	void add_track(const Track_Info *track);
	void remove(u32 index);
	void remove_range(u32 start, u32 end);
	void save_to_file();
	void free();
};

// Delete playlist file
void delete_playlist(Playlist *playlist);

extern template Large_Auto_Array<Track_Info>;
extern template Large_Auto_Array<u32>;
extern template Large_Auto_Array<char>;
extern template Large_Auto_Array<Playlist>;

void load_playlists(Large_Auto_Array<Playlist> *out);

// Helpers
void filter_tracks(const Track_Array *src, const char *query, u32 tag_mask, 
				   Track_Array *out);
bool track_meets_filter(const Track_Info *track, const char *query, u32 tag_mask);
bool path_exists(const char *path);
bool path_exists_w(const wchar_t *path);
u64 time_get_tick();
float time_ticks_to_milliseconds(u64 ticks);

#endif //COMMON_H
