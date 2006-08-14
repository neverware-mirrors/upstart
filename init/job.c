/* upstart
 *
 * job.c - handling of tasks and services
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>
#include <nih/errors.h>

#include "process.h"
#include "job.h"


/* Prototypes for static functions */
static void job_run_process (Job *job, char * const  argv[]);
static void job_kill_timer  (Job *job, NihTimer *timer);


/**
 * jobs:
 *
 * This list holds the list of known jobs, each entry is of the Job
 * structure.  No particular order is maintained.
 **/
static NihList *jobs = NULL;


/**
 * job_init:
 *
 * Initialise the list of jobs.
 **/
static inline void
job_init (void)
{
	if (! jobs)
		NIH_MUST (jobs = nih_list_new ());
}


/**
 * job_new:
 * @parent: parent of new job,
 * @name: name of new job,
 *
 * Allocates and returns a new Job structure with the @name given, and
 * appends it to the internal list of registered jobs.  It is up to the
 * caller to ensure that @name is unique amongst the job list.
 *
 * The job can be removed using #nih_list_free.
 *
 * Returns: newly allocated job structure or %NULL if insufficient memory.
 **/
Job *
job_new (void       *parent,
	 const char *name)
{
	Job *job;
	int  i;

	nih_assert (name != NULL);
	nih_assert (strlen (name) > 0);

	job_init ();

	job = nih_new (parent, Job);
	if (! job)
		return NULL;

	nih_list_init (&job->entry);

	job->name = nih_strdup (job, name);
	if (! job->name) {
		nih_free (job);
		return NULL;
	}

	job->description = NULL;
	job->author = NULL;
	job->version = NULL;

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;

	job->process_state = PROCESS_NONE;
	job->pid = 0;
	job->kill_timeout = JOB_DEFAULT_KILL_TIMEOUT;
	job->kill_timer = NULL;

	job->spawns_instance = 0;
	job->is_instance = 0;

	job->respawn = 0;
	job->normalexit = NULL;
	job->normalexit_len = 0;

	job->daemon = 0;
	job->pidfile = NULL;
	job->binary = NULL;
	job->pid_timeout = JOB_DEFAULT_PID_TIMEOUT;
	job->pid_timer = NULL;

	job->command = NULL;
	job->script = NULL;
	job->start_script = NULL;
	job->stop_script = NULL;
	job->respawn_script = NULL;

	job->console = CONSOLE_LOGGED;
	job->env = NULL;

	job->umask = JOB_DEFAULT_UMASK;
	job->nice = 0;

	for (i = 0; i < RLIMIT_NLIMITS; i++)
		job->limits[i] = NULL;

	job->chroot = NULL;
	job->chdir = NULL;

	nih_list_add (jobs, &job->entry);

	return job;
}


/**
 * job_find_by_name:
 * @name: name of job.
 *
 * Finds the job with the given @name in the list of known jobs.
 *
 * Returns: job found or %NULL if not known.
 **/
Job *
job_find_by_name (const char *name)
{
	Job *job;

	nih_assert (name != NULL);

	job_init ();

	NIH_LIST_FOREACH (jobs, iter) {
		job = (Job *)iter;

		if (! strcmp (job->name, name))
			return job;
	}

	return NULL;
}

/**
 * job_find_by_pid:
 * @pid: process id of job.
 *
 * Finds the job with a process of the given @pid in the list of known jobs.
 *
 * Returns: job found or %NULL if not known.
 **/
Job *
job_find_by_pid (pid_t pid)
{
	Job *job;

	nih_assert (pid > 0);

	job_init ();

	NIH_LIST_FOREACH (jobs, iter) {
		job = (Job *)iter;

		if (job->pid == pid)
			return job;
	}

	return NULL;
}


/**
 * job_change_state:
 * @job: job to change state of,
 * @state: state to change to.
 *
 * This function changes the current state of a @job to the new @state
 * given, performing any actions to correctly enter the new state (such
 * as spawning scripts or processes).
 *
 * It does NOT perform any actions to leave the current state, so this
 * function may only be called when there is no active process.
 *
 * Some state transitions are not be permitted and will result in an
 * assertion failure.  Also some state transitions may result in further
 * transitions, so the state when this function returns may not be the
 * state requested.
 **/
void
job_change_state (Job      *job,
		  JobState  state)
{
	nih_assert (job != NULL);
	nih_assert (job->process_state == PROCESS_NONE);

	while (job->state != state) {
		JobState old_state;

		nih_info ("%s: %s: %s to %s", _("State change"), job->name,
			  job_state_name (job->state), job_state_name (state));
		old_state = job->state;
		job->state = state;

		/* Check for invalid state changes; if ok, run the
		 * appropriate script or command, or change the state
		 * or goal.
		 */
		switch (job->state) {
		case JOB_WAITING:
			nih_assert (old_state == JOB_STOPPING);
			nih_assert (job->goal == JOB_STOP);

			/* FIXME
			 * instances need to be cleaned up */

			break;
		case JOB_STARTING:
			nih_assert ((old_state == JOB_WAITING)
				    || (old_state == JOB_STOPPING));

			if (job->start_script) {
				job_run_script (job, job->start_script);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_RUNNING:
			nih_assert ((old_state == JOB_STARTING)
				    || (old_state == JOB_RESPAWNING));

			/* If we don't have anything to do, we need to
			 * change the goal to STOP otherwise the next
			 * state is respawning and we'll just loop
			 */
			if (job->script) {
				job_run_script (job, job->script);
			} else if (job->command) {
				job_run_command (job, job->command);
			} else {
				job->goal = JOB_STOP;
				state = job_next_state (job);
			}

			break;
		case JOB_STOPPING:
			nih_assert ((old_state == JOB_STARTING)
				    || (old_state == JOB_RUNNING)
				    || (old_state == JOB_RESPAWNING));

			if (job->stop_script) {
				job_run_script (job, job->stop_script);
			} else {
				state = job_next_state (job);
			}

			break;
		case JOB_RESPAWNING:
			nih_assert (old_state == JOB_RUNNING);

			if (job->respawn_script) {
				job_run_script (job, job->respawn_script);
			} else {
				state = job_next_state (job);
			}

			break;
		}
	}
}

/**
 * job_next_state:
 * @job: job undergoing state change.
 *
 * The next state a job needs to change into is not always obvious as it
 * depends both on the current state and the ultimate goal of the job, ie.
 * whether we're moving towards stop or start.
 *
 * This function contains the logic to decide the next state the job should
 * be in based on the current state and goal.
 *
 * It is up to the caller to ensure the goal is set appropriately before
 * calling this function, for example setting it to JOB_STOP if something
 * failed.  It is also up to the caller to actually set the new state as
 * this simply returns the suggested one.
 *
 * Returns: suggested state to change to.
 **/
JobState
job_next_state (Job *job)
{
	nih_assert (job != NULL);

	switch (job->state) {
	case JOB_WAITING:
		return job->state;
	case JOB_STARTING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	case JOB_RUNNING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RESPAWNING;
		}
	case JOB_STOPPING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_WAITING;
		case JOB_START:
			return JOB_STARTING;
		}
	case JOB_RESPAWNING:
		switch (job->goal) {
		case JOB_STOP:
			return JOB_STOPPING;
		case JOB_START:
			return JOB_RUNNING;
		}
	default:
		return job->state;
	}
}

/**
 * job_state_name:
 * @state: state to convert.
 *
 * Converts an enumerated job state into the string used for the event
 * and for logging purposes.
 *
 * Returns: static string or %NULL if state not known.
 **/
const char *
job_state_name (JobState state)
{
	switch (state) {
	case JOB_WAITING:
		return "waiting";
	case JOB_STARTING:
		return "starting";
	case JOB_RUNNING:
		return "running";
	case JOB_STOPPING:
		return "stopping";
	case JOB_RESPAWNING:
		return "respawning";
	default:
		return NULL;
	}
}


/**
 * job_run_command:
 * @job: job to run process for,
 * @command: command and arguments to be run.
 *
 * This function splits @command into whitespace separated program name
 * and arguments and calls #job_run_process with the result.
 *
 * As a bonus, if @command contains any special shell characters such
 * as variables, redirection, or even just quotes; it arranges for the
 * command to instead be run by the shell so we don't need any complex
 * argument parsing of our own.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_command (Job        *job,
		 const char *command)
{
	char **argv;

	nih_assert (job != NULL);
	nih_assert (command != NULL);

	/* Use the shell for non-simple commands */
	if (strpbrk (command, "~`!$^&*()=|\\{}[];\"'<>?")) {
		argv = nih_alloc (NULL, sizeof (char *) * 4);
		argv[0] = SHELL;
		argv[1] = "-c";
		argv[2] = nih_sprintf (argv, "exec %s", command);
		argv[3] = NULL;
	} else {
		argv = nih_str_split (NULL, command, " ", TRUE);
	}

	job_run_process (job, argv);

	nih_free (argv);
}

/**
 * job_run_script:
 * @job: job to run process for,
 * @script: shell script to be run.
 *
 * This function takes the shell script code stored verbatim in @script
 * and arranges for it to be run by the system shell.
 *
 * If @script is reasonably small (less than 1KB) it is passed to the
 * shell using the POSIX-specified -c option.  Otherwise the shell is told
 * to read commands from one of the special /dev/fd/NN devices and #NihIo
 * used to feed the script into that device.  A pointer to the #NihIo object
 * is not kept or stored because it will automatically clean itself up should
 * the script go away as the other end of the pipe will be closed.
 *
 * In either case the shell is run with the -e option so that commands will
 * fail if their exit status is not checked.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
void
job_run_script (Job        *job,
		const char *script)
{
	char *argv[5];

	nih_assert (job != NULL);
	nih_assert (script != NULL);

	/* Normally we just pass the script to the shell using the -c
	 * option, however there's a limit to the length of a command line
	 * (about 4KB) and that just looks bad in ps as well.
	 *
	 * So as an alternative we use the magic /dev/fd/NN devices and
	 * give the shell a script to run by piping it down.  Of course,
	 * the pipe buffer may not be big enough either, so we use NihIo
	 * to do it all asynchronously in the background.
	 */
	if (strlen (script) > 1024) {
		NihIo *io;
		int    fds[2];

		/* Close the writing end when the child is exec'd */
		NIH_MUST (pipe (fds) == 0);
		nih_io_set_cloexec (fds[1]);

		argv[0] = SHELL;
		argv[1] = "-e";
		argv[2] = nih_sprintf (NULL, "/dev/fd/%d", fds[0]);
		argv[3] = NULL;

		job_run_process (job, argv);

		/* Clean up and close the reading end (we don't need it) */
		nih_free (argv[2]);
		close (fds[0]);

		/* Put the entire script into an NihIo send buffer and
		 * then mark it for closure so that the shell gets EOF
		 * and the structure gets cleaned up automatically.
		 */
		NIH_MUST (io = nih_io_reopen (job, fds[1], NULL, NULL,
					      NULL, NULL));
		NIH_MUST (nih_io_write (io, script, strlen (script)) == 0);
		nih_io_shutdown (io);
	} else {
		/* Pass the script using -c */
		argv[0] = SHELL;
		argv[1] = "-e";
		argv[2] = "-c";
		argv[3] = (char *)script;
		argv[4] = NULL;

		job_run_process (job, argv);
	}
}

/**
 * job_run_process:
 * @job: job to run process for,
 * @argv: %NULL-terminated list of arguments for the process.
 *
 * This function spawns a new process for @job storing the pid and new
 * process state back in that object.  This can only be called when there
 * is not already a process, and the state is one that permits a process
 * (ie. everything except %JOB_WAITING).
 *
 * The caller should have already prepared the arguments, the list is
 * passed directly to #process_spawn.
 *
 * No error is returned from this function because it will block until
 * the #process_spawn calls succeeds, that can only fail for temporary
 * reasons (such as a lack of process ids) which would cause problems
 * carrying on anyway.
 **/
static void
job_run_process (Job          *job,
		 char * const  argv[])
{
	pid_t pid;
	int   error = FALSE;

	nih_assert (job != NULL);
	nih_assert (job->state != JOB_WAITING);
	nih_assert (job->process_state == PROCESS_NONE);

	/* Run the process, repeat until fork() works */
	while ((pid = process_spawn (job, argv)) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (! error)
			nih_error ("%s: %s", _("Failed to spawn process"),
				   err->message);
		nih_free (err);
	}

	/* Update the job details */
	job->pid = pid;
	if (job->daemon && (job->state == JOB_RUNNING)) {
		/* FIXME should probably set timer or something?
		 *
		 * need to cope with daemons not being, after all */
		nih_info (_("Spawned %s process (%d)"), job->name, job->pid);
		job->process_state = PROCESS_SPAWNED;
	} else {
		nih_info (_("Active %s process (%d)"), job->name, job->pid);
		job->process_state = PROCESS_ACTIVE;
	}
}


/**
 * job_kill_process:
 * @job: job to kill active process of.
 *
 * This function forces a @job to leave its current state by killing
 * its active process, thus forcing the state to be changed once the
 * process has terminated.
 *
 * The state change is not immediate unless the kill syscall fails.
 *
 * The only state that this may be called in is %JOB_RUNNING with an
 * active process; all other states are transient, and are expected to
 * change within a relatively short space of time anyway.  For those it
 * is sufficient to simply change the goal and have the appropriate
 * state selected once the running script terminates.
 **/
void
job_kill_process (Job *job)
{
	nih_assert (job != NULL);

	nih_assert (job->state == JOB_RUNNING);
	nih_assert (job->process_state == PROCESS_ACTIVE);

	nih_debug ("Sending TERM signal to %s process (%d)",
		   job->name, job->pid);

	if (process_kill (job, job->pid, FALSE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_error (_("Failed to send TERM signal to %s process (%d): %s"),
				   job->name, job->pid, err->message);
		nih_free (err);

		/* Carry on regardless; probably went away of its own
		 * accord while we were dawdling
		 */
		job->pid = 0;
		job->process_state = PROCESS_NONE;

		job_change_state (job, JOB_STOPPING);
		return;
	}

	job->process_state = PROCESS_KILLED;
	NIH_MUST (job->kill_timer = nih_timer_add_timeout (
			  job, job->kill_timeout,
			  (NihTimerCb)job_kill_timer, job));
}

/**
 * job_kill_timer:
 * @job: job to kill active process of,
 * @timer: timer that caused us to be called.
 *
 * This callback is called if the process failed to terminate within
 * a particular time of being sent the TERM signal.  The process is killed
 * more forcibly by sending the KILL signal and is assumed to have died
 * whatever happens.
 **/
static void
job_kill_timer (Job      *job,
		NihTimer *timer)
{
	nih_assert (job != NULL);

	nih_assert (job->state == JOB_RUNNING);
	nih_assert (job->process_state == PROCESS_KILLED);

	nih_debug ("Sending KILL signal to %s process (%d)",
		   job->name, job->pid);

	if (process_kill (job, job->pid, TRUE) < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ESRCH)
			nih_error (_("Failed to send KILL signal to %s process (%d): %s"),
				   job->name, job->pid, err->message);
		nih_free (err);
	}

	/* No point waiting around, if it's ignoring the KILL signal
	 * then it's wedged in the kernel somewhere; either that or it died
	 * while we were faffing
	 */

	job->pid = 0;
	job->process_state = PROCESS_NONE;
	job->kill_timer = NULL;

	job_change_state (job, JOB_STOPPING);
}


/**
 * job_handle_child:
 * @data: unused,
 * @pid: process that died,
 * @killed: whether @pid was killed,
 * @status: exit status of @pid or signal that killed it.
 *
 * This callback should be registered with #nih_child_add_watch so that
 * when processes associated with jobs die, the structure is updated and
 * the next appropriate state chosen.
 *
 * Normally this is registered so it is called for all processes, and it
 * safe to do as it only acts if the process is linked to a job.
 **/
void
job_handle_child (void  *data,
		  pid_t  pid,
		  int    killed,
		  int    status)
{
	Job *job;

	nih_assert (data == NULL);
	nih_assert (pid > 0);

	/* Find the job that died; if it's not one of ours, just let it
	 * be reaped normally
	 */
	job = job_find_by_pid (pid);
	if (! job)
		return;

	/* Report the death */
	if (killed) {
		nih_info (_("%s process (%d) killed by signal %d"),
			  job->name, pid, status);
	} else {
		nih_info (_("%s process (%d) terminated with status %d"),
			  job->name, pid, status);
	}

	/* FIXME we may be in SPAWNED here, in which case we don't want
	 * to do all this!
	 */

	job->pid = 0;
	job->process_state = PROCESS_NONE;

	/* Cancel any timer trying to kill the job */
	if (job->kill_timer) {
		nih_free (job->kill_timer);
		job->kill_timer = NULL;
	}

	switch (job->state) {
	case JOB_RUNNING:
		/* FIXME check daemon; if true, check exit status
		 * and maybe don't change the goal */
		job->goal = JOB_STOP;
		break;
	default:
		if (killed || status)
			job->goal = JOB_STOP;

		break;
	}

	job_change_state (job, job_next_state (job));
}
