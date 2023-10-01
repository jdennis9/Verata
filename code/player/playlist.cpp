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
#define WIN32_LEAN_AND_MEAN
#include "common.h"
#include "library.h"
#include <stdio.h>
#include <string.h>
#include <xxhash.h>
#include <windows.h>

struct Playlist_Header {
	u32 magic;
	u32 version;
	u32 track_count;
	char name[64];
};

void Playlist::update_tracks() {
	const u32 count = this->track_ids.count;
	this->tracks.reset();
	
	for (u32 i = 0; i < count; ++i) {
		const Track_Info *in = lookup_track(this->track_ids.elements[i]);
		if (in) {
			this->tracks.add_from_info(in);
		}
	}
	
	this->save_to_file();
}

void Playlist::add_track(const Track_Info *track) {
	u32 id = get_track_id(track);
	this->track_ids.push_value(id);
	this->tracks.add_from_id(id);
}

u32 Playlist::get_id() {
	return XXH32(this->name, strlen(this->name), 0);
}

void Playlist::save_to_file() {
	// @Note: Calling context needs to check if this playlist file already exists
	
	// Check if the playlist folder exists
	DWORD file_attr = GetFileAttributesA("..\\Playlists");
	if (file_attr == INVALID_FILE_ATTRIBUTES) {
		// Folder doesn't exist. Create it
		DEBUG_ASSERT(CreateDirectoryA("..\\Playlists", NULL));
	}
	
	
	u32 id = this->get_id();
	char out_path[64];
	snprintf(out_path, sizeof(out_path), "../Playlists/%x", id);
	
	Playlist_Header header;
	FILE *out = fopen(out_path, "wb");
	
	if (!out) return;
	
	header.magic = *(u32*)"PLYL";
	header.version = 1;
	header.track_count = this->track_ids.count;
	strcpy(header.name, this->name);
	
	fwrite(&header, sizeof(header), 1, out);
	fwrite(this->track_ids.elements, 4, header.track_count, out);
	
	fclose(out);
}

static void remove_track_id(Playlist *playlist, u32 id) {
	for (u32 i = 0; i < playlist->track_ids.count; ++i) {
		if (playlist->track_ids.elements[i] == id) {
			playlist->track_ids.remove(i);
			return;
		}
	}
}

void Playlist::remove(u32 index) {
	u32 id = this->tracks.ids.elements[index];
	this->tracks.remove(index);
	remove_track_id(this, id);
	this->save_to_file();
}

void Playlist::remove_range(u32 start, u32 end) {
	for (u32 i = start; i <= end; ++i) {
		remove_track_id(this, this->tracks.ids.elements[i]);
	}
	
	this->tracks.remove_range(start, end);
	this->save_to_file();
}

void load_playlists(Large_Auto_Array<Playlist> *out) {
	const char *search_path = "..\\Playlists\\*";
	char path_buffer[128] = "..\\Playlists\\";
	u32 base_path_length = strlen(path_buffer);
	
	HANDLE find_handle;
	WIN32_FIND_DATA find_data;
	
	find_handle = FindFirstFile(search_path, &find_data);
	
	if (find_handle == INVALID_HANDLE_VALUE) return;
	
	while (FindNextFile(find_handle, &find_data)) {
		if (!strcmp(find_data.cFileName, "..") || !strcmp(find_data.cFileName, ".")) continue;
		
		strcpy(&path_buffer[base_path_length], find_data.cFileName);
		
		FILE *in = fopen(path_buffer, "rb");
		if (in) {
			Playlist *playlist = out->push();
			u32 *ids;
			Playlist_Header header;
			fread(&header, sizeof(header), 1, in);
			
			if (header.magic != *(u32*)"PLYL") {
				continue;
			}
			
			strncpy(playlist->name, header.name, 64);
			
			ids = playlist->track_ids.push_n(header.track_count);
			fread(ids, sizeof(u32), header.track_count, in);
			
			playlist->update_tracks();
			log_debug("Load playlist %s\n", playlist->name);
			
			fclose(in);
		}
		
		memset(&path_buffer[base_path_length], 0, strlen(find_data.cFileName));
	}
}

void Playlist::free() {
	this->track_ids.free();
	this->tracks.free();
}

void delete_playlist(Playlist *playlist) {
	char path[512];
	u32 id = playlist->get_id();
	snprintf(path, sizeof(path), "..\\Playlists\\%x", id);
	DeleteFileA(path);
}
	