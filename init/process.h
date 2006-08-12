/* upstart
 *
 * Copyright © 2006 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef UPSTART_PROCESS_H
#define UPSTART_PROCESS_H

#include <sys/types.h>

#include <nih/macros.h>

#include "job.h"


/**
 * SHELL:
 *
 * This is the shell binary used whenever we need special processing for
 * a command or when we need to run a script.
 **/
#define SHELL "/bin/sh"

/**
 * CONSOLE:
 *
 * This is the console device we give to processes that want one.
 **/
#define CONSOLE "/dev/console"

/**
 * DEV_NULL:
 *
 * This is the console device we give to processes that do not want any
 * console.
 **/
#define DEV_NULL "/dev/null"


NIH_BEGIN_EXTERN

pid_t process_spawn (Job *job, char * const argv[]);
int   process_kill  (Job *job, pid_t pid, int force);

NIH_END_EXTERN

#endif /* UPSTART_PROCESS_H */
