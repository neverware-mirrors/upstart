// Copyright (c) 2009 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
