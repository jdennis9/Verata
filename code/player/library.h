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
#ifndef LIBRARY_H
#define LIBRARY_H

#include "common.h"

enum {
	SEARCH_TAG_ARTIST = 1<<0,
	SEARCH_TAG_TITLE = 1<<1,
	SEARCH_TAG_PATH = 1<<2,
};

bool is_library_configured();
bool load_library();
// Doesn't update the library. Returns true if the path is allowed.
bool set_library_path(const wchar_t *new_path);
// If the library doesn't exist, create it.
// Pass in NULL to use the current path
bool update_library(const wchar_t *source_path);
void get_all_library_tracks(Large_Auto_Array<u32> *out);
//Large_Auto_Array<Track_Info> *get_library_track_info();
Track_Array *get_library_track_info();
const char *get_library_string(u32 location);
u32 get_track_full_path_from_id(u32 id);
u32 get_track_full_path_from_info(const Track_Info *info, wchar_t *out, u32 out_max);
bool lookup_track(u32 id, Track_Info *out);
void search_library(const char *query, u32 tag_mask, Large_Auto_Array<Track_Info> *out);
u32 get_track_id(const Track_Info *info);
const Track_Info *lookup_track(u32 id);

#endif //LIBRARY_H
