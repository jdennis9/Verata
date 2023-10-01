#include "common.h"
#include "library.h"

void Track_Array::add_from_id(u32 id) {
	const Track_Info *track = lookup_track(id);
	if (track) {
		this->ids.push_value(id);
		this->info.push_value(*track);
		this->count++;
	}
}

void Track_Array::add_from_info(const Track_Info *track) {
	u32 id = get_track_id(track);
	this->ids.push_value(id);
	this->info.push_value(*track);
	this->count++;
}

void Track_Array::add(u32 id, const Track_Info *track) {
	this->ids.push_value(id);
	this->info.push_value(*track);
	this->count++;
}

void Track_Array::remove(u32 i) {
	this->ids.remove(i);
	this->info.remove(i);
	this->count--;
}

void Track_Array::remove_range(u32 start, u32 end) {
	this->ids.remove_range(start, end);
	this->info.remove_range(start, end);
	this->count = this->ids.count;
}

void Track_Array::reset() {
	this->ids.reset();
	this->info.reset();
	this->count = 0;
}

void Track_Array::free() {
	this->ids.free();
	this->info.free();
}
