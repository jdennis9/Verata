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
#include "library.h"
#include "tags.h"
#include <stdio.h>
#include <xxhash.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Library_Header {
	u32 magic;
	u32 version;
	u32 track_count;
	u32 string_pool_size;
	u32 base_path;
};

struct Library {
	Track_Array tracks;
	Large_Auto_Array<char> string_pool;
	u64 track_count;
	u64 string_pool_size;
	wchar_t base_path[512];
};

static Library g_library;

bool is_library_configured() {
	return g_library.base_path[0] != 0;
}

static void hash_ids() {
	log_debug("Hashing library track IDs\n");
	
	g_library.tracks.ids.reset();
	const u32 count = g_library.tracks.info.count;
	u32 *ids = g_library.tracks.ids.push_n(count);
	for (u32 i = 0; i < count; ++i) {
		ids[i] = get_track_id(&g_library.tracks.info.elements[i]);
	}
}

bool load_library() {
	FILE *file = fopen("../library.dat", "rb");
	
	if (!file) {
		log_warning("Failed to load library: File does not exist\n");
		return false;
	}
	
	Library_Header header;
	fread(&header, 1, sizeof(header), file);
	
	g_library.tracks.ids.reset();
	g_library.tracks.info.reset();
	g_library.string_pool.reset();
	
	g_library.tracks.count = header.track_count;
	g_library.tracks.ids.push_n(header.track_count);
	g_library.tracks.info.push_n(header.track_count);
	g_library.string_pool.push_n(header.string_pool_size);
	
	fread(g_library.tracks.info.elements, header.track_count, sizeof(Track_Info), file);
	fread(g_library.string_pool.elements, header.string_pool_size, 1, file);
	
	hash_ids();
	
	fclose(file);
	
	utf8_to_utf16(get_library_string(header.base_path), g_library.base_path, ARRAY_LENGTH(g_library.base_path));
	return true;
}

// Path must be a full path and within the library
// @TODO: Read tags
static void add_track_to_library_from_file(const wchar_t *path) {
	enum Codec codec = find_codec_from_file_name(path);
	char path_utf8[512];
	u32 base_length = wcslen(g_library.base_path);
	u32 relative_name_length = utf16_to_utf8(&path[base_length], path_utf8, sizeof(path_utf8));
	
	//u32 *track_id = g_library.tracks.ids.push();
	//Track_Info *track_info = g_library.tracks.info.push();
	
	
	u32 track_id = XXH32(path_utf8, relative_name_length, 0);
	Track_Info track_info = {};
	
	track_info.relative_file_path = g_library.string_pool.push_offset_n(relative_name_length + 1);
	strcpy(&g_library.string_pool.elements[track_info.relative_file_path], path_utf8);
	
	struct {
		char artist[128];
		char title[128];
	} tags = {};
	
	read_tags(codec, path, tags.artist, sizeof(tags.artist), tags.title, sizeof(tags.title));
	
	if (!tags.title[0]) {
		const wchar_t *file_name = wcsrchr((wchar_t*)path, L'\\');
		if (!file_name) {
			file_name = wcsrchr((wchar_t*)path, L'/');
			if (!file_name) file_name = path;
			else file_name++;
		}
		else {
			file_name++;
		}
		
		u32 file_name_length = utf16_to_utf8(file_name, path_utf8, sizeof(path_utf8));
		track_info.title = g_library.string_pool.push_offset_n(file_name_length + 1);
		strcpy(&g_library.string_pool.elements[track_info.title], path_utf8);
		
		track_info.artist = 0;
	}
	else {
		u32 title_length = strlen(tags.title);
		track_info.title = g_library.string_pool.push_offset_n(title_length + 1);
		strcpy(&g_library.string_pool.elements[track_info.title], tags.title);
	}
	
	if (tags.artist[0]) {
		u32 artist_length = strlen(tags.artist);
		track_info.artist = g_library.string_pool.push_offset_n(artist_length + 1);
		strcpy(&g_library.string_pool.elements[track_info.artist], tags.artist);
	}
	
	g_library.tracks.add(track_id, &track_info);
}

static u32 scan_folder(wchar_t *path_buffer, u32 path_buffer_max, u32 path_length) {
	HANDLE find_handle;
	WIN32_FIND_DATAW find_data;
	u32 ret = 0;
	enum Codec codec;
	
	path_buffer[path_length] = '*';
	find_handle = FindFirstFileW(path_buffer, &find_data);
	path_buffer[path_length] = 0;
	
	if (find_handle == INVALID_HANDLE_VALUE) return 0;
	
	while (FindNextFileW(find_handle, &find_data)) {
		if (!wcscmp(find_data.cFileName, L"..") || !wcscmp(find_data.cFileName, L".")) continue;
		
		int length = swprintf(&path_buffer[path_length], path_buffer_max - path_length, 
							  L"%s", find_data.cFileName);
				
		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			path_buffer[path_length + length] = '\\';
			ret += scan_folder(path_buffer, path_buffer_max, path_length + length + 1);
			path_buffer[path_length + length] = 0;
		} else {
			codec = find_codec_from_file_name(find_data.cFileName);
			if (codec != CODEC_NONE) {
				add_track_to_library_from_file(path_buffer);
				ret++;
			}
		}
		
		memset(&path_buffer[path_length], 0, length*sizeof(wchar_t));
	}
	
	return ret;
}

bool update_library(const wchar_t *source_path) {
	g_library.string_pool.reset();
	g_library.tracks.reset();
	
	// A string location of 0 should point to an empty string
	g_library.string_pool.push();
	g_library.string_pool.elements[0] = 0;
	
	if (!source_path) source_path = g_library.base_path;
	else memset(g_library.base_path, sizeof(g_library.base_path), 0);
	
	if (!path_exists_w(source_path)) {
		return false;
	}
	
	log_debug("Scanning library path \"%ls\"\n", source_path);
	
	wchar_t path_buffer[512] = {};
	u32 base_path_length = wcslen(source_path);
	wcscpy(path_buffer, source_path);
	wcsncpy(g_library.base_path, source_path, ARRAY_LENGTH(g_library.base_path));
	
	u32 track_count = scan_folder(path_buffer, ARRAY_LENGTH(path_buffer), base_path_length);
	
	log_debug("Scanned %u tracks in library\n", track_count);
	
	if (track_count) {
		Library_Header header;
		FILE *output = fopen("../library.dat", "wb");
		header.magic = *(u32*)"TLIB";
		header.version = 1;
		header.track_count = track_count;
		header.base_path = g_library.string_pool.push_offset_n(base_path_length + 1);
		header.string_pool_size = g_library.string_pool.count;
		
		utf16_to_utf8(source_path, (char*)get_library_string(header.base_path), base_path_length);
		
		log_debug("%s\n", get_library_string(header.base_path));
		
		fwrite(&header, sizeof(header), 1, output);
		fwrite(g_library.tracks.info.elements, sizeof(Track_Info), track_count, output);
		fwrite(g_library.string_pool.elements, 1, g_library.string_pool.count, output);
		
		fclose(output);
		
		hash_ids();
	}
	
	return true;
}

Track_Array *get_library_track_info() {
	return &g_library.tracks;
}

const char *get_library_string(u32 location) {
	return &g_library.string_pool.elements[location];
}

u32 get_track_full_path_from_info(const Track_Info *info, wchar_t *out, u32 out_max) {
	wchar_t relative_path[256];
	u32 ret;
	utf8_to_utf16(get_library_string(info->relative_file_path), relative_path, ARRAY_LENGTH(relative_path));
	ret = swprintf(out, out_max, L"%s%s", g_library.base_path, relative_path);
	return ret;
}

void search_library(const char *query, u32 tag_mask, Track_Array *out) {
	filter_tracks(&g_library.tracks, query, tag_mask, out);
}

u32 get_track_id(const Track_Info *info) {
	const char *path = get_library_string(info->relative_file_path);
	const char *filename = strrchr((char*)path, '\\');
	if (!filename) filename = path;
	else filename++;
	
	return XXH32(filename, strlen(filename), 0);
}

const Track_Info *lookup_track(u32 id) {
	const u32 count = g_library.tracks.ids.count;
	for (u32 i = 0; i < count; ++i) {
		if (g_library.tracks.ids.elements[i] == id) return &g_library.tracks.info.elements[i];
	}
	
	return NULL;
}
	