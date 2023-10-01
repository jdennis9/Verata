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
#include <stdarg.h>
#include <stdio.h>

static int g_log_level = LOG_LEVEL_DEBUG;

void log_message(int log_level, const char *format, va_list args) {
	if (log_level > g_log_level) return;
	
	const char *level_names[4];
	level_names[LOG_LEVEL_DEBUG] = "\033[35m[DEBUG] ";
	level_names[LOG_LEVEL_INFO] = "\033[34m[INFO] ";
	level_names[LOG_LEVEL_WARNING] = "\033[33m[WARNING] ";
	level_names[LOG_LEVEL_ERROR] = "\033[31m[ERROR] ";
	printf("%s\033[0m", level_names[log_level]);
	vprintf(format, args);
}

void log_info(const char *format, ...) {
	va_list va;
	va_start(va, format);
	log_message(LOG_LEVEL_INFO, format, va);
	va_end(va);
}

void log_debug(const char *format, ...) {
	va_list va;
	va_start(va, format);
	log_message(LOG_LEVEL_DEBUG, format, va);
	va_end(va);
}

void log_warning(const char *format, ...) {
	va_list va;
	va_start(va, format);
	log_message(LOG_LEVEL_WARNING, format, va);
	va_end(va);
}

void log_error(const char *format, ...) {
	va_list va;
	va_start(va, format);
	log_message(LOG_LEVEL_ERROR, format, va);
	va_end(va);
}
