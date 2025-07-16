/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of the  nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <stdio.h>
#include <pthread.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"

struct _logging {
	pthread_mutex_t mutex;
	FILE *err_fp;
	FILE *fp;
	int level;
    int log_count;
};

#ifndef GLOBAL_LOG
#define GLOBAL_LOG
static struct _logging global_log = {.err_fp = NULL, .fp = NULL};
#endif

void logging_init(void)
{
	if (global_log.fp && global_log.err_fp) return;
	
	global_log.err_fp = stderr;
	global_log.fp = stdout;
	global_log.level = LOGGING_LEVEL_DEBUG;
    global_log.log_count = 0;
	pthread_mutex_init(&global_log.mutex, NULL);
}

int logging_set_file(const char *path)
{
	FILE *fp = fopen(path, "w");
	if (!fp)
		return -1;

	pthread_mutex_lock(&global_log.mutex);
	global_log.fp = fp;
	global_log.err_fp = fp;
	pthread_mutex_unlock(&global_log.mutex);

	return 0;
}

void logging_set_level(int level)
{
	assert(level >= LOGGING_LEVEL_INFO && level <= LOGGING_LEVEL_DEBUG);
	pthread_mutex_lock(&global_log.mutex);
	global_log.level = level;
	pthread_mutex_unlock(&global_log.mutex);
}

void logging_printf(FILE *fp, const char *level_str, const char *format, va_list ap)
{
    assert(global_log.fp && global_log.err_fp);
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char fmt[1024];
    sprintf(fmt, "%d/%02d/%02d %02d:%02d:%02d %5s %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, level_str, format);
	
    pthread_mutex_lock(&global_log.mutex);
    vfprintf(fp, fmt, ap);
    global_log.log_count++;
    if (fileno(global_log.fp) != STDOUT_FILENO && fileno(global_log.err_fp) != STDERR_FILENO && global_log.log_count >= 5) {
        fflush(global_log.err_fp);
    }

    pthread_mutex_unlock(&global_log.mutex);
}

void logdebugf(const char *args, ...)
{
	if (global_log.level < LOGGING_LEVEL_DEBUG) return;
	va_list ap;
	va_start(ap, args);
	logging_printf(global_log.err_fp, "debug", args, ap);
	va_end(ap);
}

void logerrf(const char *args, ...)
{
	if (global_log.level < LOGGING_LEVEL_ERROR) return;
	va_list ap;
	va_start(ap, args);
	logging_printf(global_log.err_fp, "error", args, ap);
	va_end(ap);
}

void loginfof(const char *args, ...)
{
	if (global_log.level < LOGGING_LEVEL_INFO) return;
	va_list ap;
	va_start(ap, args);
	logging_printf(global_log.fp, "info", args, ap);
	va_end(ap);
}
