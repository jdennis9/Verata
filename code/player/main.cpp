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
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <windows.h>
#include <Shobjidl.h>
#include <d3d9.h> 
#include <wchar.h>
#include <time.h>

#include "common.h"
#include "player.h"
#include "library.h"

enum Track_List_ID {
	TRACK_LIST_NONE,
	TRACK_LIST_LIBRARY,
	TRACK_LIST_QUEUE,
	TRACK_LIST_PLAYLIST,
	TRACK_LIST_SEARCH_RESULTS,
};

enum Selection_Type {
	SELECTION_TYPE_NONE,
	SELECTION_TYPE_SINGLE,
	SELECTION_TYPE_RANGE,
	SELECTION_TYPE_LIST,
};

enum View_ID {
	VIEW_TRACK_LIST,
	VIEW_SETUP,
	VIEW_CONFIGURATION,
	VIEW_HOTKEYS,
	VIEW_ABOUT,
};

enum Hotkey_ID {
	HOTKEY_PREVIOUS_TRACK,
	HOTKEY_NEXT_TRACK,
	HOTKEY_TOGGLE_PLAYBACK,
};

static struct {
	s32 resize_width;
	s32 resize_height;
	u32 width, height;
} g_window;


static struct {
	// Need to keep a cache of track IDs for the queue because
	// they are frequently needed and hashing them every time they
	// are needed is very slow.
	/*Large_Auto_Array<u32> track_queue_ids;
	Large_Auto_Array<Track_Info> track_queue;
	Large_Auto_Array<Track_Info> search_results;*/
	Track_Array queue;
	Track_Array search_results;
	Large_Auto_Array<Playlist> playlists;
	
	u32 current_track_id;
	Track_Info current_track_info;
	s32 queue_next_position;
	u32 playing_track_list;
	u32 selected_playlist_index;
	float seek_target;
	enum Track_List_ID viewing_track_list;
	char track_filter[512];
	struct {
		enum Selection_Type type;
		enum Track_List_ID track_list;
		union {
			u32 single;
			struct {u32 start, end;} range;
			// @TODO: List selection
		};
	} selection;
	
	enum View_ID view;
	
	u64 time_of_last_input;
	bool shuffle_enabled;
	bool show_search_results;
	bool seeking;
	bool naming_playlist;
	bool is_light_mode;
	bool inactive_mode;
} G;

static LPDIRECT3D9 g_d3d;
static LPDIRECT3DDEVICE9 g_d3d_device;
static D3DPRESENT_PARAMETERS g_present_params;

static void show_gui(u32 width, u32 height);
static void on_track_end();

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

Playlist *get_selected_playlist() {
	if (G.playlists.count) {
		return &G.playlists.elements[G.selected_playlist_index];
	}
	else return NULL;
}

static void switch_main_view(enum View_ID new_view) {
	G.view = new_view;
}

static void shuffle_queue(u32 min_index = 0) {
	const u32 count = G.queue.info.count;
	Track_Info swapper;
	u32 id_swapper;
	s32 src;
	for (u32 i = min_index; i < count; ++i) {
		src = rand() % count;
		swapper = G.queue.info.elements[i];
		id_swapper = G.queue.ids.elements[i];
		G.queue.ids.elements[i] = G.queue.ids.elements[src];
		G.queue.ids.elements[src] = id_swapper;
		G.queue.info.elements[i] = G.queue.info.elements[src];
		G.queue.info.elements[src] = swapper;
	}
	
	G.queue_next_position = 0;
}

static int get_track_index_in_queue(u32 id) {
	const Large_Auto_Array<u32> *ids = &G.queue.ids;
	const u32 count = ids->count;
	
	for (int i = 0; i < count; ++i) {
		if (id == ids->elements[i]) {
			return i;
		}
	}
	
	return -1;
}

// Returns the index of the first queued track
static u32 queue_tracks(const Track_Array *tracks, u32 array_offset = 0, u32 array_count = UINT32_MAX) {
	const u32 count = MIN(tracks->count, array_count);
	const u32 shuffle_start = G.queue.info.count;
	Track_Info *out;
	u32 *out_id;
	G.queue_next_position = 0;
	
	/*for (u32 i = 0; i < count; ++i) {
		u32 track_id = get_track_id(&tracks->elements[i+array_offset]);
		if (get_track_index_in_queue(track_id) != -1) continue;
		
		out_id = G.queue.ids.push();
		out = G.queue.info.push();
		*out_id = track_id;
		*out = tracks->elements[i+array_offset];
	}*/
	
	for (u32 i = array_offset; i < (count+array_offset); ++i) {
		if (get_track_index_in_queue(tracks->ids.elements[i]) != -1) continue;
		G.queue.add(tracks->ids.elements[i], &tracks->info.elements[i]);
	}
	
	if (G.shuffle_enabled) shuffle_queue(shuffle_start);
	
	return shuffle_start;
}

static void clear_queue() {
	log_debug("Clearing playback queue\n");
	G.queue.reset();
}

static bool play_track(const Track_Info *track) {
	wchar_t path[512];
	get_track_full_path_from_info(track, path, ARRAY_LENGTH(path));
	G.current_track_id = get_track_id(track);
	G.current_track_info = *track;
	return open_track(path);
}

static bool move_queue_to_position(u32 position) {
	while ((G.queue.info.count > position) && !play_track(&G.queue.info.elements[position])) {
		position++;
	}
	G.queue_next_position = position + 1;
	
	return G.queue_next_position < G.queue.info.count;
}

static void previous_track() {
	const Track_Info *track;
	s32 position = G.queue_next_position - 2;
	if (position < 0) position = 0;
	move_queue_to_position(position);
}

static void next_track() {
	const Track_Info *track;
	
	if (G.queue_next_position >= G.queue.info.count) {
		// @TODO: Only do this when repeat is enabled
		G.queue_next_position = 0;
	}
	
	do {
		track = &G.queue.info.elements[G.queue_next_position];
		G.queue_next_position++;
		if (G.queue_next_position > G.queue.info.count) return;
	} while (!play_track(track));
}

static void queue_track_and_play(const Track_Info *track) {
	// Check if the track is already in the queue
	const u32 track_id = get_track_id(track);
	const u32 count = G.queue.info.count;
	for (u32 i = 0; i < count; ++i) {
		// If the track is already in the queue, set the queue position on the track
		if (track_id == G.queue.ids.elements[i]) {
			move_queue_to_position(i);
			return;
		}
	}
	
	// If it's not in the queue, append it and set the queue position
	G.queue.add(track_id, track);
	move_queue_to_position(G.queue.info.count-1);
}

static void play_playlist(u32 index) {
	clear_queue();
	move_queue_to_position(queue_tracks(&G.playlists.elements[index].tracks));
}

static bool create_d3d_device(HWND hwnd) {
	assert((g_d3d = Direct3DCreate9(D3D_SDK_VERSION)));
	
	g_present_params.Windowed = TRUE;
	g_present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_present_params.BackBufferFormat = D3DFMT_UNKNOWN;
	g_present_params.EnableAutoDepthStencil = TRUE;
	g_present_params.AutoDepthStencilFormat = D3DFMT_D16;
	g_present_params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	
	assert(!g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, 
								D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_present_params, &g_d3d_device));
	
	return true;
}

static void reset_d3d_device() {
	ImGui_ImplDX9_InvalidateDeviceObjects();
	g_d3d_device->Reset(&g_present_params);
	ImGui_ImplDX9_CreateDeviceObjects();
}

int main(int argc, char *argv[]) {
	srand(time(NULL));
	log_debug("Debug logging is ON\n");
	log_info("Info logging is ON\n");
	log_warning("Warning logging is ON\n");
	log_error("Error logging is ON\n");
	
	CoInitializeEx(NULL, COINITBASE_MULTITHREADED);
	start_playback_stream(&on_track_end);
	
	if (load_library()) 
		switch_main_view(VIEW_TRACK_LIST);
	else 
		switch_main_view(VIEW_SETUP);
	
	load_playlists(&G.playlists);
	
	WNDCLASSEX wndclass = {};
	wndclass.cbSize = sizeof(wndclass);
	wndclass.style = CS_OWNDC;
	wndclass.lpfnWndProc = &window_proc;
	wndclass.lpszClassName = "verata_window_class";
	wndclass.hInstance = GetModuleHandle(NULL);
	
	RegisterClassEx(&wndclass);
	HWND hwnd = CreateWindow("verata_window_class", "Verata", WS_OVERLAPPEDWINDOW, 
							 100, 100, 1280, 720, 
							 NULL, NULL, wndclass.hInstance, NULL);
	
	// Register hotkeys
	RegisterHotKey(hwnd, HOTKEY_PREVIOUS_TRACK, MOD_CONTROL|MOD_SHIFT|MOD_ALT, VK_LEFT);
	RegisterHotKey(hwnd, HOTKEY_NEXT_TRACK, MOD_CONTROL|MOD_SHIFT|MOD_ALT, VK_RIGHT);
	RegisterHotKey(hwnd, HOTKEY_TOGGLE_PLAYBACK, MOD_CONTROL|MOD_SHIFT|MOD_ALT, VK_DOWN);
	
	create_d3d_device(hwnd);
	
	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);
	
	// Initialize ImGui
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	
	ImGui::StyleColorsDark();
	
	ImGui_ImplWin32_InitForOpenGL(hwnd);
	ImGui_ImplDX9_Init(g_d3d_device);
	
	// Load fonts
	{
		io.Fonts->AddFontFromFileTTF("../NotoSans-SemiBold.ttf", 16.f);
		
		ImFontConfig font_config = {};
		font_config.MergeMode = true;
		const ImWchar icon_range[] = {
			0xf048, 0xf052,
			0xf026, 0xf028,
			0xf074, 0xf074,
			0
		};
		io.Fonts->AddFontFromFileTTF("../NotoMonoNerdFont-Regular.ttf", 14.f, &font_config, icon_range);
	}
	
	bool running = true;
	
	while (running) {
		MSG msg;
		
		if (!G.inactive_mode || (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0)) {
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (msg.message == WM_QUIT) running = false;
			}
		}
		
		if (!running) break;
		
		if (g_window.resize_width && g_window.resize_height) {
			g_present_params.BackBufferWidth = g_window.resize_width;
			g_present_params.BackBufferHeight = g_window.resize_height;
			g_window.resize_width = 0;
			g_window.resize_height = 0;
			reset_d3d_device();
		}
		
		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		show_gui(g_window.width, g_window.height);
		
		ImGui::EndFrame();
		
		g_d3d_device->SetRenderState(D3DRS_ZENABLE, FALSE);
		g_d3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_d3d_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		g_d3d_device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(0, 0, 0, 255), 1.f, 0);
		if (g_d3d_device->BeginScene() >= 0) {
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			g_d3d_device->EndScene();
		}
		
		HRESULT result = g_d3d_device->Present(NULL, NULL, NULL, NULL);
		
		if ((result == D3DERR_DEVICELOST) && (g_d3d_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)) {
			reset_d3d_device();
		}
		
		if (!G.inactive_mode && (time_ticks_to_milliseconds(time_get_tick() - G.time_of_last_input) >= 100.f)) {
			G.inactive_mode = true;
		}
	}
	
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	DestroyWindow(hwnd);
	UnregisterClass("verata_window_class", wndclass.hInstance);
	
	return 0;
}

static void new_playlist() {
	Playlist *playlist = G.playlists.push();
	memset(playlist, 0, sizeof(*playlist));
	G.naming_playlist = true;
}

static void select_single_track(u32 index) {
	G.selection.type = SELECTION_TYPE_SINGLE;
	G.selection.single = index;
	G.selection.track_list = G.viewing_track_list;
}

static void select_range_of_tracks(s32 start, s32 end) {
	if (start > end) {
		s32 temp = end;
		end = start;
		start = temp;
	}
	
	G.selection.type = SELECTION_TYPE_RANGE;
	G.selection.range.start = start;
	G.selection.range.end = end;
	G.selection.track_list = G.viewing_track_list;
}

static Track_Array *get_track_list(enum Track_List_ID list) {
	switch (list) {
		case TRACK_LIST_LIBRARY:
		return get_library_track_info();
		
		case TRACK_LIST_QUEUE:
		return &G.queue;
		
		case TRACK_LIST_PLAYLIST: {
			Playlist *playlist = get_selected_playlist();
			if (playlist) return &playlist->tracks;
			break;
		}
		
		case TRACK_LIST_SEARCH_RESULTS:
		return &G.search_results;
	}
	
	return NULL;
}

static Track_Array *get_selected_track_list() {
	return get_track_list(G.selection.track_list);
}

static u32 get_lowest_selection_index() {
	switch (G.selection.type) {
		case SELECTION_TYPE_SINGLE:
		return G.selection.single;
		case SELECTION_TYPE_RANGE:
		return G.selection.range.start;
		default:
		return 0;
	}
}

static bool track_is_selected(u32 index) {
	if (G.viewing_track_list != G.selection.track_list) return false;
	
	switch (G.selection.type) {
		case SELECTION_TYPE_SINGLE:
		return index == G.selection.single;
		
		case SELECTION_TYPE_RANGE:
		return (index >= G.selection.range.start) && (index <= G.selection.range.end);
		
		default: return false;
	}
}

static u32 add_selection_to_queue() {
	Track_Array *tracks = get_selected_track_list();
	if (!tracks) return 0;
	
	switch (G.selection.type) {
		case SELECTION_TYPE_SINGLE:
		return queue_tracks(tracks, G.selection.single, 1);
		case SELECTION_TYPE_RANGE:
		return queue_tracks(tracks, G.selection.range.start, 
					 (G.selection.range.end - G.selection.range.start) + 1);
	}
	
	return 0;
}

static void add_selection_to_playlist() {
	Track_Array *tracks = get_selected_track_list();
	Playlist *playlist = get_selected_playlist();
	if (!playlist || !tracks) return;
	
	switch (G.selection.type) {
		case SELECTION_TYPE_SINGLE: {
			playlist->add_track(&tracks->info.elements[G.selection.single]);
			break;
		}
		case SELECTION_TYPE_RANGE: {
			const u32 count = (G.selection.range.end - G.selection.range.start) + 1;
			for (u32 i = 0; i < count; ++i) {
				playlist->add_track(&tracks->info.elements[i + G.selection.range.start]);
			}
			
			break;
		}
	}
	
	playlist->save_to_file();
}

// @TODO: Custom bindable hotkeys
static void handle_hotkeys() {
	ImGuiIO& io = ImGui::GetIO();
	ImGuiKeyChord mod = io.KeyMods;
	
	if (mod == ImGuiMod_Ctrl) {
		if (ImGui::IsKeyPressed(ImGuiKey_S)) {
			shuffle_queue();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_P)) {
			add_selection_to_playlist();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
			add_selection_to_queue();
		}
	}
	else if (mod == (ImGuiMod_Ctrl|ImGuiMod_Shift)) {
		if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
			clear_queue();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_N)) {
			new_playlist();
		}
	}
	
}

// Returns the number of tracks shown in the list
static u32 show_track_list(Track_Array *tracks) {
	u32 table_flags = 
		ImGuiTableFlags_BordersInner |
		ImGuiTableFlags_SizingFixedFit |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_ScrollY;
	
	u32 displayed_track_count = 0;
	bool table_is_focused = false;
	
	if (G.viewing_track_list != TRACK_LIST_SEARCH_RESULTS && 
		ImGui::InputTextWithHint("##search", "Search", G.track_filter, sizeof(G.track_filter), 
								 ImGuiInputTextFlags_EnterReturnsTrue)) {
		G.show_search_results = true;
		G.search_results.reset();
		filter_tracks(tracks, G.track_filter, UINT32_MAX, &G.search_results);
		memset(G.track_filter, 0, sizeof(G.track_filter));
	}
	
	if (G.viewing_track_list == TRACK_LIST_QUEUE) {
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			clear_queue();
		}
		
		ImGui::SameLine();
		if (ImGui::Button("Shuffle")) {
			shuffle_queue(0);
		}
	}
	
	if (G.viewing_track_list == TRACK_LIST_PLAYLIST) {
		ImGui::SameLine();
		if (ImGui::Button("Play")) {
			play_playlist(G.selected_playlist_index);
		}
	}
	
	if (ImGui::BeginTable("##track_table", 3, table_flags)) {
		ImGui::TableSetupColumn("Status", 0, 100.f);
		ImGui::TableSetupColumn("Artist", 0, 200.f);
		ImGui::TableSetupColumn("Title", 0, 300.f);
		ImGui::TableSetupScrollFreeze(1, 1);
		
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Status");
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("Artist");
		ImGui::TableSetColumnIndex(2);
		ImGui::Text("Title");
		
		for (u32 i = 0; i < tracks->count; ++i) {
			u32 track_id = tracks->ids.elements[i];
			
			if (G.track_filter[0] && 
				!track_meets_filter(&tracks->info.elements[i], G.track_filter, UINT32_MAX)) {
				continue;
			}
			
			displayed_track_count++;
			ImGui::TableNextRow();
			
			bool selected = track_is_selected(i);
			
			// Status
			ImGui::TableSetColumnIndex(0);
			if (G.current_track_id == track_id) {
				ImGui::Text("Playing");
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0xcc007aff);
			}
			
			// Artist
			ImGui::TableSetColumnIndex(1);
			ImGui::Text(get_library_string(tracks->info.elements[i].artist));
			
			// Title
			ImGui::TableSetColumnIndex(2);
			if (ImGui::Selectable(get_library_string(tracks->info.elements[i].title), selected,
									  ImGuiSelectableFlags_SpanAllColumns)) {
				// Only allow range selection when there is no track filter
				if (!G.track_filter[0] && ImGui::IsKeyDown(ImGuiMod_Shift))
					select_range_of_tracks(get_lowest_selection_index(), i);
				else select_single_track(i);
			}
			
			// If this track is focused, mark the table as focused for keyboard input
			if (!table_is_focused && ImGui::IsItemFocused()) table_is_focused = true;
			
			if (selected && table_is_focused && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
				u32 index = get_lowest_selection_index();
				if (G.viewing_track_list == TRACK_LIST_QUEUE) move_queue_to_position(index);
				else queue_track_and_play(&tracks->info.elements[index]);
			}
			
			// Don't update for this item if it isn't visible
			if (!ImGui::IsItemVisible()) continue;

			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle) || 
				(ImGui::IsItemClicked(ImGuiMouseButton_Left) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
				queue_track_and_play(&tracks->info.elements[i]);
			}
			else if (!selected && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				select_single_track(i);
				selected = true;
			}
			else if (selected && ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Play")) {
					if (G.viewing_track_list == TRACK_LIST_QUEUE) {
						move_queue_to_position(get_lowest_selection_index());
					}
					else {
						move_queue_to_position(add_selection_to_queue());
					}
				}
				
				if (ImGui::MenuItem("Queue tracks")) {
					add_selection_to_queue();
				}
				
				if ((G.viewing_track_list != TRACK_LIST_PLAYLIST) && ImGui::MenuItem("Add to playlist")) {
					Playlist *playlist = get_selected_playlist();
					add_selection_to_playlist();
				}
				
				// Remove selected tracks from list
				if ((G.viewing_track_list != TRACK_LIST_LIBRARY) && ImGui::MenuItem("Remove")) {
					switch (G.selection.type) {
						case SELECTION_TYPE_RANGE:
						if (G.viewing_track_list != TRACK_LIST_PLAYLIST)
							tracks->remove_range(G.selection.range.start, G.selection.range.end);
						else
							G.playlists.elements[G.selected_playlist_index].remove_range(G.selection.range.start,
																						 G.selection.range.end);
						break;
						case SELECTION_TYPE_SINGLE:
						if (G.viewing_track_list != TRACK_LIST_PLAYLIST)
							tracks->remove(G.selection.single);
						else
							G.playlists.elements[G.selected_playlist_index].remove(G.selection.single);
						break;
					}
					
					
				}
				ImGui::EndPopup();
			}
			
		}
		
		ImGui::EndTable();
	}
	
	return displayed_track_count;
}


static u32 show_track_list_view() {
	if (!is_library_configured()) {
		switch_main_view(VIEW_SETUP);
		return 0;
	}
	
	u32 displayed_track_count = 0;
	ImGui::BeginTabBar("##track_list_tabs", ImGuiTabBarFlags_AutoSelectNewTabs);
	
	if (ImGui::BeginTabItem("Playlist")) {
		Playlist *playlist = get_selected_playlist();
		if (playlist) {
			Track_Array *tracks = &playlist->tracks;
			G.viewing_track_list = TRACK_LIST_PLAYLIST;
			displayed_track_count = show_track_list(tracks);
		}
		ImGui::EndTabItem();
	}
	
	if (ImGui::BeginTabItem("Browse")) {
		G.viewing_track_list = TRACK_LIST_LIBRARY;
		displayed_track_count = show_track_list(get_library_track_info());
		ImGui::EndTabItem();
	}
	
	if (ImGui::BeginTabItem("Queue")) {
		G.viewing_track_list = TRACK_LIST_QUEUE;
		displayed_track_count = show_track_list(&G.queue);
		ImGui::EndTabItem();
	}	
	
	if (ImGui::BeginTabItem("Search Results", &G.show_search_results, ImGuiTabItemFlags_Trailing)) {
		G.viewing_track_list = TRACK_LIST_SEARCH_RESULTS;
		displayed_track_count = show_track_list(&G.search_results);
		ImGui::EndTabItem();
	}
	
	ImGui::EndTabBar(); 
	
	return displayed_track_count;
}

static void show_setup_view() {
	static char path[512];
	bool commit = 0;
	bool allow_cancel = is_library_configured();
	
	ImGui::Text("Choose library path:");
	commit |= ImGui::InputText("##library_path", path, sizeof(path), ImGuiInputTextFlags_EnterReturnsTrue);
	
	ImGui::SameLine();
	if (ImGui::Button("Browse")) {
		IFileOpenDialog *file_dialog;
		IShellItem *folder;
		
		CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&file_dialog);
		file_dialog->SetOptions(FOS_PATHMUSTEXIST | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		
		if (SUCCEEDED(file_dialog->Show(NULL))) {
			DWORD count;
			file_dialog->GetResult(&folder);
			LPWSTR folder_name;
			folder->GetDisplayName(SIGDN_FILESYSPATH, &folder_name);
			utf16_to_utf8(folder_name, path, sizeof(path));
			folder->Release();
			CoTaskMemFree(folder_name);
		}
		
		file_dialog->Release();
	}

	ImGui::Text("This path will be scanned for music. Scanning may take a few minutes for large libraries.");
	ImGui::Text("You can rescan your library at any time by going to Library -> Rescan library.");
	ImGui::Text("You can change your library library at any time by going to Library -> Change library path.");
	
	commit |= ImGui::Button("Scan library");
	
	if (allow_cancel) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			switch_main_view(VIEW_TRACK_LIST);
		}
	}
	
	if (commit) {
		clear_queue();
		wchar_t path_w[512];
		swprintf(path_w, ARRAY_LENGTH(path_w), L"%hs\\", path);
		if (update_library(path_w)) {
			for (u32 i = 0; i < G.playlists.count; ++i) {
				G.playlists.elements[i].update_tracks();
			}
			switch_main_view(VIEW_TRACK_LIST);
		}
	}
}

static void show_hotkeys_view() {
	ImGui::Text("Ctrl+P: Add selection to playlist");
	ImGui::Text("Ctrl+Q: Add selection to queue");
	ImGui::Text("Ctrl+Shift+N: New playlist");
	ImGui::Text("Ctrl+Shift+Q: Clear queue");
	ImGui::Text("Ctrl+S: Shuffle");
	ImGui::Text("Middle Mouse Click: Play track/playlist");
	ImGui::Text("Enter: Play first selected track/playlist");
	if (ImGui::Button("Ok") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
		switch_main_view(VIEW_TRACK_LIST);
	}
}

static void show_about_view() {
	ImGui::SeparatorText("Build");
	ImGui::Text("Version: %s", VERATA_VERSION_STRING);
	ImGui::Text("Build date: %s", __DATE__);
	
	ImGui::SeparatorText("License");
	ImGui::Text("Apache-2.0");
	ImGui::Text("Copyright 2023 Jamie Dennis");
	
	ImGui::SeparatorText("Third-party Licenses");
	
	ImGui::Text("Opus");
	ImGui::Text("Copyright 2001-2011 Xiph.Org, Skype Limited, Octasic,");
	ImGui::Text("Jean-Marc Valin, Timothy B. Terriberry,");
	ImGui::Text("CSIRO, Gregory Maxwell, Mark Borgerding,");
	ImGui::Text("Erik de Castro Lopo");
	
	ImGui::NewLine();
	ImGui::Text("OpusFile");
	ImGui::Text("Copyright (c) 1994-2013 Xiph.Org Foundation and contributors");
	
	ImGui::NewLine();
	ImGui::Text("FLAC - Free Lossless Audio Codec");
	ImGui::Text("Copyright (C) 2000-2009  Josh Coalson");
	ImGui::Text("Copyright (C) 2011-2023  Xiph.Org Foundation");
	
	ImGui::NewLine();
	ImGui::Text("OGG");
	ImGui::Text("Copyright (c) 2002, Xiph.org Foundation");
	
	ImGui::NewLine();
	ImGui::Text("ImGui");
	ImGui::Text("Copyright (c) 2014-2023 Omar Cornut");
	
	ImGui::NewLine();
	ImGui::Text("libsamplerate");
	ImGui::Text("Copyright (c) 2012-2016, Erik de Castro Lopo <erikd@mega-nerd.com>");
	ImGui::Text("All rights reserved.");
	
	ImGui::NewLine();
	ImGui::Text("xxHash Library");
	ImGui::Text("Copyright (c) 2012-2021 Yann Collet");
	ImGui::Text("All rights reserved.");
	
	ImGui::NewLine();
	ImGui::Text("FreeType");
	ImGui::Text("Copyright 1996-2002, 2006 by");
	ImGui::Text("David Turner, Robert Wilhelm, and Werner Lemberg");
	
	ImGui::NewLine();
	ImGui::Text("zlib");
	ImGui::Text("Copyright (C) 1995-2023 Jean-loup Gailly and Mark Adler");
	
	ImGui::NewLine();
	ImGui::Text("bzip2");
	ImGui::Text("Copyright (C) 1996-2010 Julian R Seward. All rights reserved.");
	
	ImGui::NewLine();
	ImGui::Text("libpng");
	ImGui::Text("Copyright (c) 1995-2023 The PNG Reference Library Authors.");
	ImGui::Text("Copyright (c) 2018-2023 Cosmin Truta.");
	
	if (ImGui::Button("Ok") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
		switch_main_view(VIEW_TRACK_LIST);
	}
}

static void delete_and_free_playlist(u32 index) {
	Playlist *playlist = &G.playlists.elements[index];
	delete_playlist(playlist);
	playlist->free();
	G.playlists.remove(index);
}

static void show_gui(u32 window_width, u32 window_height) {
	ImGuiWindowFlags window_flags = 
		ImGuiWindowFlags_NoResize|
		ImGuiWindowFlags_NoTitleBar|
		ImGuiWindowFlags_NoMove|
		ImGuiWindowFlags_NoCollapse;
	
	u32 layout_x = 0, layout_y = 0;
	u32 layout_width = window_width, layout_height = window_height;
	ImVec2 last_window_size;
	u32 displayed_track_count = 0;
	
	handle_hotkeys();
	
	// Menu bar
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New playlist")) {
				new_playlist();
			}
			if (ImGui::MenuItem("Rescan library")) {
				clear_queue();
				update_library(NULL);
				// Update playlists to new library
				for (u32 i = 0; i < G.playlists.count; ++i) {
					G.playlists.elements[i].update_tracks();
				}
			}
			
			if (ImGui::MenuItem("Change library path")) {
				switch_main_view(VIEW_SETUP);
			}
			
			if (ImGui::MenuItem("Exit")) {
				PostQuitMessage(0);
			}
			
			ImGui::EndMenu();
		}
		
		if (ImGui::BeginMenu("View")) {
			if (G.is_light_mode) {
				if (ImGui::MenuItem("Switch to dark mode")) {
					ImGui::StyleColorsDark();
					G.is_light_mode = false;
				}
			}
			else {
				if (ImGui::MenuItem("Switch to light mode")) {
					ImGui::StyleColorsLight();
					G.is_light_mode = true;
				}
			}
			ImGui::EndMenu();
		}
		
		if (ImGui::BeginMenu("Help")) {
			if (ImGui::MenuItem("Hotkeys")) {
				switch_main_view(VIEW_HOTKEYS);
			}
			if (ImGui::MenuItem("About")) {
				switch_main_view(VIEW_ABOUT);
			}
			
			ImGui::EndMenu();
		}
		
		
		ImGui::EndMainMenuBar();
	}
	
	last_window_size = ImGui::GetItemRectSize();
	layout_height -= last_window_size.y;
	layout_y += last_window_size.y;
	
	// Control panel
	ImGui::SetNextWindowPos(ImVec2(layout_x, layout_y));
	ImGui::SetNextWindowSize(ImVec2(layout_width, 65));
	if (ImGui::Begin("##control_panel", NULL, window_flags)) {
		// @TODO: Remember volume
		static float volume_slider = 1.f;
		ImVec2 button_size = ImVec2(12.f, 14.f);
		
		// @TODO: These buttons are terrible. Make them look nice
		ImGui::Selectable(u8"\xf074", &G.shuffle_enabled, 0, button_size);
		
		button_size.x = 10.f;
		
		ImGui::SameLine();
		if (ImGui::Selectable(u8"\xf048", false, 0, button_size)) {
			previous_track();
		}
		
		ImGui::SameLine();
		if (ImGui::Selectable(track_is_playing() ? u8"\xf04c" : u8"\xf04b", false, 0, button_size)) {
			toggle_playback();
		}
		
		ImGui::SameLine();
		if (ImGui::Selectable(u8"\xf051", false, 0, button_size)) {
			next_track();
		}
		
		ImGui::SetNextItemWidth(layout_width * 0.1f);
		ImGui::SameLine();
		if (ImGui::SliderFloat("##volume", &volume_slider, 0.f, 1.f, "%.2f")) {
			set_playback_volume(volume_slider);
		}
		
		ImGui::SameLine();
		ImGui::Text("%s - %s", get_library_string(G.current_track_info.artist),
					get_library_string(G.current_track_info.title));
		
		ImGui::SetNextItemWidth(layout_width - 16.f);
		if (ImGui::SliderFloat("##seek_slider", &G.seek_target, 0, get_playback_length(), "%.2f")) {
			G.seeking = true;
		}
		
		if (G.seeking && ImGui::IsItemDeactivatedAfterEdit()) {
			seek_playback_to_seconds(G.seek_target);
			G.seeking = false;
		}
		
		if (!G.seeking) {
			G.seek_target = get_playback_position();
		}
	}
	ImGui::End();
	layout_y += 65;
	layout_height -= 65;
	
	// Playlist list
	ImGui::SetNextWindowPos(ImVec2(layout_x, layout_y));
	ImGui::SetNextWindowSize(ImVec2(layout_width * 0.2f, layout_height - 30));
	layout_x += layout_width * 0.2f;
	layout_width -= layout_width * 0.2f;
	
	if (ImGui::Begin("Playlists", NULL, window_flags ^ ImGuiWindowFlags_NoTitleBar)) {
		
		if (ImGui::Button("Play")) {
			play_playlist(G.selected_playlist_index);
		}
		
		ImGui::SameLine();
		
		if (ImGui::Button("New")) {
			new_playlist();
		}
		
		ImGui::SameLine();
		if (ImGui::Button("Remove")) {
			delete_and_free_playlist(G.selected_playlist_index);
		}
		
		ImGui::NewLine();
		
		u32 count = G.playlists.count;
		if (G.naming_playlist) count--;
		for (u32 i = 0; i < count; ++i) {
			if (ImGui::Selectable(G.playlists.elements[i].name, i == G.selected_playlist_index)) {
				G.selected_playlist_index = i;
			}
			
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Play")) {
					play_playlist(i);
					G.selected_playlist_index = i;
				}
				if (ImGui::MenuItem("Delete")) {
					delete_and_free_playlist(i);
					count--;
				}
				ImGui::EndPopup();
			}
			
			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
				play_playlist(i);
				G.selected_playlist_index = i;
			}
		}
		
		if (G.naming_playlist) {
			u32 index = G.playlists.count - 1;
			
			ImGui::SetKeyboardFocusHere();
			bool done = ImGui::InputText("##playlist_name_input", 
										 G.playlists.elements[index].name, 
										 sizeof(G.playlists.elements[index].name), 
										 ImGuiInputTextFlags_EnterReturnsTrue);
			if (done) {
				// Check if the playlist already exists
				bool already_exists = false;
				
				for (u32 i = 0; i < G.playlists.count-1; ++i) {
					if (!strcmp(G.playlists.elements[index].name, G.playlists.elements[i].name)) {
						already_exists = true;
					}
				}
				
				G.naming_playlist = already_exists;
			}
			
			// Cancel naming the playlist
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				G.naming_playlist = false;
				G.playlists.count--;
			}
		}
#if 0
		if (ImGui::BeginPopupContextWindow()) {
			if (ImGui::MenuItem("New playlist")) {
				new_playlist();
			}
			ImGui::EndPopup();
		}
#endif
	}
	ImGui::End();
	
	// Main view
	ImGui::SetNextWindowPos(ImVec2(layout_x, layout_y));
	ImGui::SetNextWindowSize(ImVec2(layout_width, layout_height - 30));
	
	if (ImGui::Begin("##main_view", NULL, window_flags)) {
		switch (G.view) {
			case VIEW_TRACK_LIST:
			displayed_track_count = show_track_list_view();
			break;
			case VIEW_SETUP:
			show_setup_view();
			break;
			case VIEW_HOTKEYS:
			show_hotkeys_view();
			break;
			case VIEW_ABOUT:
			show_about_view();
			break;
		}
	}	
	ImGui::End();
	
	layout_y += (layout_height - 30);
	layout_height = 30;
	
	// Status bar
	layout_width = window_width;
	layout_x = 0;
	ImGui::SetNextWindowPos(ImVec2(layout_x, layout_y));
	ImGui::SetNextWindowSize(ImVec2(layout_width, layout_height));
	if (ImGui::Begin("##status", NULL, window_flags | ImGuiWindowFlags_NoScrollbar)) {
		ImGui::Text("%u tracks", displayed_track_count);
	}
	ImGui::End();
	
}


static LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
		return true;
	
	G.time_of_last_input = time_get_tick();
	G.inactive_mode = false;
	
	switch (msg) {
		case WM_HOTKEY: {
			switch (wparam) {
				case HOTKEY_PREVIOUS_TRACK:
				previous_track();
				break;
				case HOTKEY_NEXT_TRACK:
				next_track();
				break;
				case HOTKEY_TOGGLE_PLAYBACK:
				toggle_playback();
				break;
			}
			return 0;
		}
		case WM_SIZE: {
			g_window.resize_width = LOWORD(lparam);
			g_window.resize_height = HIWORD(lparam);
			g_window.width = g_window.resize_width;
			g_window.height = g_window.resize_height;
			return 0;
		}
		case WM_DESTROY: {
			PostQuitMessage(0);
			return 0;
		}
	}
	
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void on_track_end() {
	log_debug("End of playback\n");
	next_track();
}

u32 utf8_to_utf16(const char *in, wchar_t *out, u32 max_out) {
	int ret = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, max_out) - 1;
	if (ret == -1) return 0;
	return (u32)ret;
}

u32 utf16_to_utf8(const wchar_t *in, char *out, u32 max_out) {
	int ret = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, max_out, NULL, NULL) - 1;
	if (ret == -1) return 0;
	return (u32)ret;
}

void *system_allocate(u32 size) {
	void *ret = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
	return ret;
}

void *system_reallocate(void *address, u32 old_size, u32 new_size) {
	void *ret = VirtualAlloc(address, new_size, MEM_COMMIT, PAGE_READWRITE);
	
	if (!ret) {
		ret = system_allocate(new_size);
		memcpy(ret, address, MIN(old_size, new_size));
		return ret;
	}
	
	return ret;
}

void system_free(void *address, u32 size) {
	VirtualFree(address, size, MEM_DECOMMIT);
}

void filter_tracks(const Track_Array *src, const char *query, u32 tag_mask, 
				   Track_Array *out) {
	u32 count = src->count;
	
	for (u32 i = 0; i < count; ++i) {
		if (track_meets_filter(&src->info.elements[i], query, tag_mask)) {
			out->add(src->ids.elements[i], &src->info.elements[i]);
		}
	}
}

static inline bool string_contains(const char *haystack, const char *needle) {
	u32 needle_pos = 0;
	for (; *haystack; ++haystack) {
		if (tolower(*haystack) == tolower(needle[needle_pos])) needle_pos++;
		else needle_pos = 0;
		if (!needle[needle_pos]) return true;
	}
	
	return false;
}

bool track_meets_filter(const Track_Info *track, const char *query, u32 tag_mask) {
	if ((tag_mask & SEARCH_TAG_PATH) && string_contains(get_library_string(track->relative_file_path), query)) return true;
	if ((tag_mask & SEARCH_TAG_TITLE) && string_contains(get_library_string(track->title), query)) return true;
	if ((tag_mask & SEARCH_TAG_ARTIST) && string_contains(get_library_string(track->artist), query)) return true;
	
	return false;
}
	
bool path_exists(const char *path) {
	DWORD file_attr = GetFileAttributesA(path);
	return file_attr != INVALID_FILE_ATTRIBUTES;
}

bool path_exists_w(const wchar_t *path) {
	DWORD file_attr = GetFileAttributesW(path);
	return file_attr != INVALID_FILE_ATTRIBUTES;
}

u64 time_get_tick() {
	LARGE_INTEGER ret;
	QueryPerformanceCounter(&ret);
	return ret.QuadPart;
}

float time_ticks_to_milliseconds(u64 ticks) {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return ((double)ticks / (double)frequency.QuadPart) * 1000.f;
}
	
	