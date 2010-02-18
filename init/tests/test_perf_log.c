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

#include <nih/test.h>

#include <ctype.h>
#include <limits.h>

#include <nih/error.h>
#include <nih/file.h>

#include "perf_log.h"


static void
check_file_contents (const char *file,
		     const char *expected)
{
	char *buf;
	size_t len = 0;
	buf = nih_file_read (NULL, file, &len);
	if (! expected) {
		NihError *err;
		err = nih_error_get ();
		TEST_EQ (err->number, ENOENT);
		TEST_EQ (len, 0);
		TEST_EQ_P (buf, NULL);
		nih_free (err);
		return;
	}
	TEST_NE_P (buf, NULL);
	TEST_EQ (len, strlen(expected));
	TEST_TRUE (strncmp(buf, expected, len) == 0);
	nih_free (buf);
}


static void
create_test_file (const char *filename,
		  const char *contents)
{
	FILE *fp;
	fp = fopen(filename, "w");
	TEST_NE_P (fp, NULL);
	fwrite (contents, 1, strlen(contents), fp);
	fclose (fp);
}


void
test_get_file_fields (void)
{
	char test_file[PATH_MAX];
	TEST_FUNCTION ("get_file_field");
	TEST_FILENAME (test_file);
	char **result;
	int fields = 0;

	TEST_FEATURE ("with NULL file");
	TEST_ALLOC_FAIL {
		result = get_file_fields (NULL, NULL, " ", &fields);
		TEST_EQ_P (result, NULL);
		TEST_EQ (fields, 0);
	}

	TEST_FEATURE ("with non-existent file");
	TEST_ALLOC_FAIL {
		result = get_file_fields (NULL, test_file, " ", &fields);
		TEST_EQ_P (result, NULL);
		TEST_EQ (fields, 0);
	}

	TEST_FEATURE ("regular space delimiter");
	create_test_file (test_file, "0.1564 1234\n");
	TEST_ALLOC_FAIL {
		result = get_file_fields (NULL, test_file, " ", &fields);
		if (! test_alloc_failed || result) {
			/* if the allocation that failed is for the
			   file contents, allow NULL. */
			TEST_EQ (fields, 2);
			TEST_EQ_STR (result[0], "0.1564");
			TEST_EQ_STR (result[1], "1234\n");
			nih_free (result);
		}
	}

	TEST_FEATURE ("repeated delimiters");
	create_test_file (test_file, " 0.1564  1234\n");
	result = get_file_fields (NULL, test_file, " \n", &fields);
	TEST_EQ (fields, 2);
	TEST_EQ_STR (result[0], "0.1564");
	TEST_EQ_STR (result[1], "1234");
	nih_free (result);

	TEST_FEATURE ("non space delimiter");
	create_test_file (test_file, "123,456");
	result = get_file_fields (NULL, test_file, " ", &fields);
	TEST_EQ (fields, 1);
	TEST_EQ_STR (result[0], "123,456");
	nih_free (result);
	result = get_file_fields (NULL, test_file, ",", &fields);
	TEST_EQ (fields, 2);
	TEST_EQ_STR (result[0], "123");
	TEST_EQ_STR (result[1], "456");
	nih_free (result);

	TEST_FEATURE ("read from special file");
	result = get_file_fields (NULL, "/proc/uptime", " \n", &fields);
	TEST_EQ (fields, 2);
	TEST_TRUE (isdigit (result[0][0]));
	TEST_TRUE (isdigit (result[1][0]));
	nih_free (result);
}


void
test_perf_log_message (void)
{
	char log_file[PATH_MAX];
	char uptime_file[PATH_MAX];
	char diskstats_file[PATH_MAX];
	TEST_FUNCTION ("perf_log_message");
	TEST_FILENAME (log_file);
	TEST_FILENAME (uptime_file);
	TEST_FILENAME (diskstats_file);

	create_test_file(uptime_file, "a1 b\n");
	create_test_file(diskstats_file, "a b c1 d e f g\n");

	/* By setting a NULL log_file, we accumulate the log messages. */
	perf_log_set_files (uptime_file, diskstats_file, NULL);

	perf_log_message ("test %d\n", 1);

	create_test_file (uptime_file, "a2 b\n");
	create_test_file (diskstats_file, "a b c2 d e f g\n");

	perf_log_message ("test %d\n", 2);

	check_file_contents (log_file, NULL);

	/* Set the log_file, which flushes. */
	perf_log_set_files (uptime_file, diskstats_file, log_file);

	check_file_contents (log_file, "a1 c1 test 1\na2 c2 test 2\n");

	create_test_file (uptime_file, "a3 b\n");
	create_test_file (diskstats_file, "a b c3 d e f g\n");

	perf_log_message ("test %d\n", 3);

	check_file_contents (log_file,
			     "a1 c1 test 1\na2 c2 test 2\na3 c3 test 3\n");

	TEST_ALLOC_FAIL {
		perf_log_message ("test");
	}

	/* Clear the log file. */
	create_test_file (log_file, "");
	TEST_FEATURE ("with bad input files");

	create_test_file (uptime_file, "\n");
	create_test_file (diskstats_file, "a b c\n");
	perf_log_message ("test bad uptime\n");

	check_file_contents (log_file,
			     "- c test bad uptime\n");

	create_test_file (uptime_file, "a b\n");
	create_test_file (diskstats_file, "a b\n");
	perf_log_message ("test bad diskstats\n");

	check_file_contents (log_file,
			     "- c test bad uptime\na - test bad diskstats\n");

	TEST_FEATURE ("with unwritable output");
	unlink (log_file);
	TEST_EQ (mkdir (log_file, 0777), 0);
	perf_log_message ("Cannot be written\n");
	rmdir (log_file);
}


int
main (int   argc,
      char *argv[])
{
	test_get_file_fields ();
	test_perf_log_message ();

	return 0;
}
