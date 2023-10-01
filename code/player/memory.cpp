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
#include "common.h"
#include <string.h>

template<typename T>
void Large_Auto_Array<T>::remove(u32 index) {
	this->count--;
	this->elements[index] = this->elements[this->count];
}

template<typename T>
u32 Large_Auto_Array<T>::push_offset_n(u32 n) {
	if (this->count + n > this->allocated_elements) {
		u32 old_count = this->allocated_elements;
		while (this->allocated_elements <= (this->count + n)) {
			u32 additional_elements = 4096 / sizeof(T);
			this->allocated_elements += additional_elements ? additional_elements : 1;
		}
		this->elements = (T*)system_reallocate(this->elements, old_count * sizeof(T),
											   this->allocated_elements * sizeof(T));
	}
	
	u32 ret = this->count;
	this->count += n;
	return ret;
}

template<typename T>
T *Large_Auto_Array<T>::push_n(u32 n) {
	u32 offset = this->push_offset_n(n);
	return &this->elements[offset];
}

template<typename T>
T *Large_Auto_Array<T>::push() {
	return push_n(1);
}

template<typename T>
void Large_Auto_Array<T>::push_value(T value) {
	T *out = this->push();
	*out = value;
}

template<typename T>
void Large_Auto_Array<T>::reset() {
	this->count = 0;
}

template<typename T>
void Large_Auto_Array<T>::free() {
	system_free(this->elements, this->allocated_elements * sizeof(T));
	this->count = 0;
	this->allocated_elements = 0;
	this->elements = NULL;
}

template<typename T>
void Large_Auto_Array<T>::remove_range(u32 start, u32 end) {
	u32 range = (end - start) + 1;
	u32 n = this->count - end - 1;
	memmove(&this->elements[start], &this->elements[end+1], n * sizeof(T));
	this->count -= range;
}

template Large_Auto_Array<Track_Info>;
template Large_Auto_Array<u32>;
template Large_Auto_Array<char>;
template Large_Auto_Array<Playlist>;
