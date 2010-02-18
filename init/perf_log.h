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

#ifndef PERF_LOG_H
#define PERF_LOG_H

#include "job.h"

NIH_BEGIN_EXTERN

char      **get_file_fields            (void *parent,
                                        const char *file,
                                        char *delimiters,
                                        int *fields);

void        perf_log_init              (void);

void        perf_log_flush             (void);

void        perf_log_set_files         (const char *uptime_file,
                                        const char *diskstats_file,
                                        const char *timestamp_file);

void        perf_log_message           (const char *format,
                                        ...);

void        perf_log_job_state_change  (Job     *job,
                                        JobState new_state);

NIH_END_EXTERN

#endif /* PERF_LOG_H */
