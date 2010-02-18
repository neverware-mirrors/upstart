/* Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>

#include <stdio.h>
#include <string.h>

#include <nih/alloc.h>
#include <nih/error.h>
#include <nih/file.h>
#include <nih/list.h>
#include <nih/logging.h>
#include <nih/string.h>

#include "perf_log.h"

static NihList    *message_list;
static const char *perf_log_file;
static const char *perf_uptime_file;
static const char *perf_diskstats_file;

/**
 * load_special_file_contents:
 * @file: file to load
 *
 * Loads an ASCII text file and returns as a string. File must be less than
 * MAX_FILE_SIZE.  Do not use nih_file_read as it requires reading a
 * normal file whose size is correct (unlike /proc and /sys files).
 *
 * Returns: none
 **/
static char *
load_special_file_contents (void *parent,
			    const char *file)
{
	const int MAX_FILE_SIZE = 512;
	size_t len = 0;
	char *contents;
	FILE *fp = NULL;

	if (! file) {
		return NULL;
	}
	fp = fopen (file, "r");
	if (! fp) {
		return NULL;
	}
	contents = NIH_MUST (nih_alloc (parent, MAX_FILE_SIZE + 1));
	len = fread (contents, 1, MAX_FILE_SIZE, fp);
	fclose (fp);
	if (len < 0) {
		nih_free(contents);
		return NULL;
	}
	contents[len] = '\0';
	return contents;
}

/**
 * get_file_fields:
 * @parent: parent context
 * @file: file to load and parse
 * @delimiters: NULL terminated list of characters that delimits fields
 * @fields: point to integer that is set to the number of fields found
 *
 * Loads @file and returns an array of fields delimited by the given
 * @delimiters array.  Avoid nih_file_read as it requires reading a
 * normal file whose size is correct (unlike /proc and /sys files).
 *
 * Returns: array of delimited fields or NULL on error.
 **/
char **
get_file_fields (void *parent,
		 const char *file,
		 char *delimiters,
		 int *fields)
{
	int i;
	char *result = NULL;
	char **array = NULL;
	char *contents = NULL;

	nih_assert (fields != NULL);
	*fields = 0;
	contents = load_special_file_contents (NULL,
					       file);
	if (! contents) {
		return NULL;
	}
	array = nih_str_split (parent,
			       contents,
			       delimiters,
			       TRUE);
	nih_free (contents);
	if (! array) {
		return NULL;
	}
	i = 0;
	while (array[i] != NULL) {
		++i;
	}
	*fields = i;
	return array;
}

/**
 * perf_log_init:
 *
 * Initialise the message_list list.
 *
 * Returns: none
 **/
void
perf_log_init (void)
{
	if (! message_list) {
		message_list = NIH_MUST (nih_list_new (NULL));
	}
}

/**
 * perf_log_flush:
 *
 * Attempt to write any enqueued perf log messages.
 *
 * Returns: none
 **/
void
perf_log_flush (void)
{
	FILE *fp = NULL;

	if (perf_log_file)
		fp = fopen (perf_log_file, "a");
	if (! fp)
		return;
	NIH_LIST_FOREACH_SAFE (message_list, iter) {
		NihListEntry *entry = (NihListEntry*)iter;
		int result;

		result = fputs (entry->str, fp);
		if (result < 0) {
			/* This is an unexpected error.  We retry
			 * writing the message later.
			 */
			break;
		}
		nih_list_remove (iter);
		nih_free (iter);
	}

	fclose (fp);
}

/**
 * perf_log_message:
 * @format: format string
 *
 * Log the given formatted message.  If the file cannot be written at
 * this time, we enqueue the message and try later.  We grab
 * performance stats now, and those stats are enqueued to write later.
 * If the performance stats are not readable at this time, we log "-"
 * instead.
 *
 * Returns: none
 **/
void
perf_log_message (const char *format,
		  ...)
{
	NihListEntry *new_entry;
	va_list args;
	int uptime_fields = 0;
	char **uptimes;
	int diskstats_fields = 0;
	char **diskstats;
	char *uptime_busy;
	char *sectors_read;
	char *message;

	perf_log_init ();
	uptimes = get_file_fields (NULL,
				   perf_uptime_file,
				   " \n",
				   &uptime_fields);
	diskstats = get_file_fields (NULL,
				     perf_diskstats_file,
				     " \n",
				     &diskstats_fields);

	if (uptime_fields < 1)
		uptime_busy = "-";
	else
		uptime_busy = uptimes[0];
	if (diskstats_fields < 3)
		sectors_read = "-";
	else
		sectors_read = diskstats[2];

	va_start (args, format);
	message = NIH_MUST (nih_vsprintf (NULL,
					  format,
					  args));
	va_end (args);

	/* Create a log entry and add it to the queue. */
	new_entry = NIH_MUST (nih_list_entry_new (NULL));
	new_entry->str = NIH_MUST (nih_sprintf (new_entry, "%s %s %s",
						uptime_busy, sectors_read,
						message));
	nih_list_add (message_list, &new_entry->entry);

	nih_free (message);
	if (uptimes)
		nih_free (uptimes);
	if (diskstats)
		nih_free (diskstats);

	perf_log_flush ();
}

/**
 * perf_log_job_state_change:
 * @job: job whose state is changing,
 * @new_state: new state
 *
 * Causes the given job's state transition to be logged to the
 * performance log.
 *
 * Returns: none
 **/
void
perf_log_job_state_change (Job      *job,
			   JobState  new_state)
{
	perf_log_message ("statechange %s %s\n",
                          job_name (job),
                          job_state_name (new_state));
}

/**
 * perf_log_set_file:
 * @file: file to write to
 *
 * Sets the performance logging file name and writes to it if possible.
 *
 * Returns: none
 **/
void
perf_log_set_files (const char *uptime_file,
		    const char *diskstats_file,
		    const char *log_file)
{
	perf_log_init ();
	perf_log_file = log_file;
	perf_uptime_file = uptime_file;
	perf_diskstats_file = diskstats_file;
	perf_log_flush ();
}
