/* upstart
 *
 * Copyright © 2010 Canonical Ltd.
 * Author: Scott James Remnant <scott@netsplit.com>.
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
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/mount.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifdef ADD_DIRCRYPTO_RING
#include <keyutils.h>
#endif

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/restorecon.h>
#endif

#include <linux/kd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/option.h>
#include <nih/main.h>
#include <nih/error.h>
#include <nih/logging.h>

#include "paths.h"
#include "errors.h"
#include "events.h"
#include "system.h"
#include "job_class.h"
#include "job_process.h"
#include "event.h"
#include "conf.h"
#include "control.h"


/* Prototypes for static functions */
#ifndef DEBUG
static int  logger_kmsg     (NihLogLevel priority, const char *message);
static void crash_handler   (int signum);
static void cad_handler     (void *data, NihSignal *signal);
static void kbd_handler     (void *data, NihSignal *signal);
static void pwr_handler     (void *data, NihSignal *signal);
static void hup_handler     (void *data, NihSignal *signal);
static void usr1_handler    (void *data, NihSignal *signal);
#endif /* DEBUG */

#ifdef HAVE_SELINUX
static int initialize_selinux (void);
#endif

/**
 * argv0:
 *
 * Path to program executed, used for re-executing the init binary from the
 * same location we were executed from.
 **/
static const char *argv0 = NULL;

/**
 * restart:
 *
 * This is set to TRUE if we're being re-exec'd by an existing init
 * process.
 **/
static int restart = FALSE;


/**
 * options:
 *
 * Command-line options we accept.
 **/
static NihOption options[] = {
	{ 0, "restart", NULL, NULL, NULL, &restart, NULL },

	/* Ignore invalid options */
	{ '-', "--", NULL, NULL, NULL, NULL, NULL },

	NIH_OPTION_LAST
};


int
main (int   argc,
      char *argv[])
{
	char **args;
	char  *arg_end = NULL;
	int    ret;

	argv0 = argv[0];
	nih_main_init (argv0);

	nih_option_set_synopsis (_("Process management daemon."));
	nih_option_set_help (
		_("This daemon is normally executed by the kernel and given "
		  "process id 1 to denote its special status.  When executed "
		  "by a user process, it will actually run /sbin/telinit."));

	args = nih_option_parser (NULL, argc, argv, options, FALSE);
	if (! args)
		exit (1);

#ifndef DEBUG
	/* Check we're root */
	if (getuid ()) {
		nih_fatal (_("Need to be root"));
		exit (1);
	}

	/* Check we're process #1 */
	if (getpid () > 1) {
		execv (TELINIT, argv);
		/* Ignore failure, probably just that telinit doesn't exist */

		nih_fatal (_("Not being executed as init"));
		exit (1);
	}

	/* Clear our arguments from the command-line, so that we show up in
	 * ps or top output as /sbin/init, with no extra flags.
	 *
	 * This is a very Linux-specific trick; by deleting the NULL
	 * terminator at the end of the last argument, we fool the kernel
	 * into believing we used a setproctitle()-a-like to extend the
	 * argument space into the environment space, and thus make it use
	 * strlen() instead of its own assumed length.  In fact, we've done
	 * the exact opposite, and shrunk the command line length to just that
	 * of whatever is in argv[0].
	 *
	 * If we don't do this, and just write \0 over the rest of argv, for
	 * example; the command-line length still includes those \0s, and ps
	 * will show whitespace in their place.
	 */
	if (argc > 1) {
		arg_end = argv[argc-1] + strlen (argv[argc-1]);
		*arg_end = ' ';
	}


	/* Become the leader of a new session and process group, shedding
	 * any controlling tty (which we shouldn't have had anyway - but
	 * you never know what initramfs did).
	 */
	setsid ();

	/* Set the standard file descriptors to the ordinary console device,
	 * resetting it to sane defaults unless we're inheriting from another
	 * init process which we know left it in a sane state.
	 */
	if (system_setup_console (CONSOLE_OUTPUT, (! restart)) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to initialize console, will try /dev/null"),
			  err->message);
		nih_free (err);

		if (system_setup_console (CONSOLE_NONE, FALSE) < 0) {
			err = nih_error_get ();
			nih_fatal ("%s: %s", _("Unable to initialize console as /dev/null"),
				   err->message);
			nih_free (err);

			exit (1);
		}
	}

	/* Set the PATH environment variable */
	setenv ("PATH", PATH, TRUE);

	/* Switch to the root directory in case we were started from some
	 * strange place, or worse, some directory in the initramfs that's
	 * going to go away soon.
	 */
	if (chdir ("/"))
		nih_warn ("%s: %s", _("Unable to set root directory"),
			  strerror (errno));

	/* Mount the /proc and /sys filesystems, which are pretty much
	 * essential for any Linux system; not to mention used by
	 * ourselves.
	 */
	if (system_mount ("proc", "/proc",
			  MS_NODEV | MS_NOEXEC | MS_NOSUID, NULL) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to mount /proc filesystem"),
			  err->message);
		nih_free (err);
	}

	if (system_mount ("sysfs", "/sys",
			  MS_NODEV | MS_NOEXEC | MS_NOSUID, NULL) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to mount /sys filesystem"),
			  err->message);
		nih_free (err);
	}

	if (system_mount ("tmpfs", "/tmp", MS_NOSUID | MS_NODEV | MS_NOEXEC,
			  NULL) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to mount /tmp filesystem"),
			  err->message);
		nih_free (err);
	}

	if (system_mount ("tmpfs", "/run", MS_NOSUID | MS_NODEV | MS_NOEXEC,
			  "mode=0755") < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to mount /run filesystem"),
			  err->message);
		nih_free (err);
	}

	if ((mkdir ("/run/lock", 01777) < 0 && errno != EEXIST) ||
	    chmod ("/run/lock", 01777) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_warn ("%s: %s", _("Unable to mkdir /run/lock"),
			  err->message);
		nih_free (err);
	}

#ifdef HAVE_SELINUX
	if (!getenv ("SELINUX_INIT")) {
		/*
		 * We mount selinuxfs ourselves instead of letting
		 * libselinux do it so that our standard mount options
		 * (nosuid and noexec) will be applied. Note that
		 * we leave devices on since there is null device in
		 * selinuxfs.
		 */
		if (system_mount ("selinuxfs", "/sys/fs/selinux",
				  MS_NOEXEC | MS_NOSUID, NULL) < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s",
				   _("Unable to mount /sys/fs/selinux filesystem"),
				   err->message);
			nih_free (err);

			exit (1);
		}

		if (initialize_selinux () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_fatal ("%s: %s",
				   _("Failed to initialize SELinux"),
				   err->message);
			nih_free (err);

			exit (1);
		}

               const char *restore_paths[] = RESTORE_PATHS;
               for (size_t i = 0;
		     i < sizeof(restore_paths) / sizeof(const char *);
		     i++) {
			const int restorecon_args = SELINUX_RESTORECON_RECURSE |
						    SELINUX_RESTORECON_REALPATH;
       	        if (selinux_restorecon(restore_paths[i],
					       restorecon_args) != 0) {
				nih_warn ("%s: %d",
					  _("Failed to restorecon"), errno);
				// ignore error for now until policy are combined. exit(1);
			}
               }

		putenv ("SELINUX_INIT=YES");
		nih_info (_("SELinux policy loaded, doing self-exec"));

		/* Unmangle argv and re-execute */
		if (arg_end)
			*arg_end = '\0';
		execv (argv0, argv);

		nih_fatal ("%s: %s",
			   _("Failed to re-exec init"),
			   strerror (errno));
		exit (1);
	}
#endif

#else /* DEBUG */
	nih_log_set_priority (NIH_LOG_DEBUG);
	nih_debug ("Running as PID %d (PPID %d)",
		(int)getpid (), (int)getppid ());
#endif /* DEBUG */

#ifdef ADD_DIRCRYPTO_RING
	/*
	 * Set a keyring for the session to hold ext4 crypto keys.
	 * The session is at the root of all processes, so any users who wish
	 * to access a directory protected by ext4 crypto can access the key.
	 *
	 * Only set a session keyring if the kernel supports ext4 encryption.
	 */
	if (!access("/sys/fs/ext4/features/encryption", F_OK)) {
		key_serial_t keyring_id;

		keyring_id = add_key ("keyring", "dircrypt", 0, 0,
				KEY_SPEC_SESSION_KEYRING);
		if (keyring_id == -1) {
			nih_warn ("%s: %s",
				  _("Unable to create dircrypt keyring: %s"),
				  strerror (errno));
		} else {
			keyctl_setperm(keyring_id,
				       KEY_POS_VIEW | KEY_POS_SEARCH |
				       KEY_POS_LINK | KEY_POS_READ |
				       KEY_USR_ALL);
			keyctl_setperm(KEY_SPEC_SESSION_KEYRING,
				       KEY_POS_VIEW | KEY_POS_SEARCH |
				       KEY_POS_LINK | KEY_POS_READ |
				       KEY_USR_ALL);
		}
	}
#endif

	/* Reset the signal state and install the signal handler for those
	 * signals we actually want to catch; this also sets those that
	 * can be sent to us, because we're special
	 */
	if (! restart)
		nih_signal_reset ();

#ifndef DEBUG
	/* Catch fatal errors immediately rather than waiting for a new
	 * iteration through the main loop.
	 */
	nih_signal_set_handler (SIGSEGV, crash_handler);
	nih_signal_set_handler (SIGABRT, crash_handler);
#endif /* DEBUG */

	/* Don't ignore SIGCHLD or SIGALRM, but don't respond to them
	 * directly; it's enough that they interrupt the main loop and
	 * get dealt with during it.
	 */
	nih_signal_set_handler (SIGCHLD, nih_signal_handler);
	nih_signal_set_handler (SIGALRM, nih_signal_handler);

#ifndef DEBUG
	/* Ask the kernel to send us SIGINT when control-alt-delete is
	 * pressed; generate an event with the same name.
	 */
	reboot (RB_DISABLE_CAD);
	nih_signal_set_handler (SIGINT, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGINT, cad_handler, NULL));

	/* Ask the kernel to send us SIGWINCH when alt-uparrow is pressed;
	 * generate a keyboard-request event.
	 */
	if (ioctl (0, KDSIGACCEPT, SIGWINCH) == 0) {
		nih_signal_set_handler (SIGWINCH, nih_signal_handler);
		NIH_MUST (nih_signal_add_handler (NULL, SIGWINCH,
						  kbd_handler, NULL));
	}

	/* powstatd sends us SIGPWR when it changes /etc/powerstatus */
	nih_signal_set_handler (SIGPWR, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGPWR, pwr_handler, NULL));

	/* SIGHUP instructs us to re-load our configuration */
	nih_signal_set_handler (SIGHUP, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGHUP, hup_handler, NULL));

	/* SIGUSR1 instructs us to reconnect to D-Bus */
	nih_signal_set_handler (SIGUSR1, nih_signal_handler);
	NIH_MUST (nih_signal_add_handler (NULL, SIGUSR1, usr1_handler, NULL));
#endif /* DEBUG */


	/* Watch children for events */
	NIH_MUST (nih_child_add_watch (NULL, -1, NIH_CHILD_ALL,
				       job_process_handler, NULL));

	/* Process the event queue each time through the main loop */
	NIH_MUST (nih_main_loop_add_func (NULL, (NihMainLoopCb)event_poll,
					  NULL));


	/* Adjust our OOM priority to the default, which will be inherited
	 * by all jobs.
	 */
	if (JOB_DEFAULT_OOM_SCORE_ADJ) {
		char  filename[PATH_MAX];
		int   oom_value;
		FILE *fd;

		snprintf (filename, sizeof (filename),
			  "/proc/%d/oom_score_adj", getpid ());
		oom_value = JOB_DEFAULT_OOM_SCORE_ADJ;
		fd = fopen (filename, "w");
		if ((! fd) && (errno == ENOENT)) {
			snprintf (filename, sizeof (filename),
				  "/proc/%d/oom_adj", getpid ());
			oom_value = (JOB_DEFAULT_OOM_SCORE_ADJ
				     * ((JOB_DEFAULT_OOM_SCORE_ADJ < 0) ? 17 : 15)) / 1000;
			fd = fopen (filename, "w");
		}
		if (! fd) {
			nih_warn ("%s: %s", _("Unable to set default oom score"),
				  strerror (errno));
		} else {
			fprintf (fd, "%d\n", oom_value);

			if (fclose (fd))
				nih_warn ("%s: %s", _("Unable to set default oom score"),
					  strerror (errno));
		}
	}


	/* Read configuration */
	NIH_MUST (conf_source_new (NULL, CONFFILE, CONF_FILE));
	NIH_MUST (conf_source_new (NULL, CONFDIR, CONF_JOB_DIR));

	conf_reload ();

	/* Create a listening server for private connections. */
	while (control_server_open () < 0) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ENOMEM) {
			nih_warn ("%s: %s", _("Unable to listen for private connections"),
				  err->message);
			nih_free (err);
			break;
		}
		nih_free (err);
	}

	/* Open connection to the system bus; we normally expect this to
	 * fail and will try again later - don't let ENOMEM stop us though.
	 */
	while (control_bus_open () < 0) {
		NihError *err;
		int       number;

		err = nih_error_get ();
		number = err->number;
		nih_free (err);

		if (number != ENOMEM)
			break;
	}

#ifndef DEBUG
	/* Now that the startup is complete, send all further logging output
	 * to kmsg instead of to the console.
	 */
	if (system_setup_console (CONSOLE_NONE, FALSE) < 0) {
		NihError *err;

		err = nih_error_get ();
		nih_fatal ("%s: %s", _("Unable to setup standard file descriptors"),
			   err->message);
		nih_free (err);

		exit (1);
	}

	nih_log_set_logger (logger_kmsg);
#endif /* DEBUG */


	/* Generate and run the startup event or read the state from the
	 * init daemon that exec'd us
	 */
	if (! restart) {
		NIH_MUST (event_new (NULL, STARTUP_EVENT, NULL));
	} else {
		sigset_t mask;

		/* We're ok to receive signals again */
		sigemptyset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);
	}

	/* Run through the loop at least once to deal with signals that were
	 * delivered to the previous process while the mask was set or to
	 * process the startup event we emitted.
	 */
	nih_main_loop_interrupt ();
	ret = nih_main_loop ();

	return ret;
}


#ifndef DEBUG
/**
 * logger_kmsg:
 * @priority: priority of message being logged,
 * @message: message to log.
 *
 * Outputs the @message to the kernel log message socket prefixed with an
 * appropriate tag based on @priority, the program name and terminated with
 * a new line.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
logger_kmsg (NihLogLevel priority,
	     const char *message)
{
	int   tag;
	FILE *kmsg;

	nih_assert (message != NULL);

	switch (priority) {
	case NIH_LOG_DEBUG:
		tag = '7';
		break;
	case NIH_LOG_INFO:
		tag = '6';
		break;
	case NIH_LOG_MESSAGE:
		tag = '5';
		break;
	case NIH_LOG_WARN:
		tag = '4';
		break;
	case NIH_LOG_ERROR:
		tag = '3';
		break;
	case NIH_LOG_FATAL:
		tag = '2';
		break;
	default:
		tag = 'd';
	}

	kmsg = fopen ("/dev/kmsg", "w");
	if (! kmsg)
		return -1;

	if (fprintf (kmsg, "<%c>%s: %s\n", tag, program_name, message) < 0) {
		int saved_errno = errno;
		fclose (kmsg);
		errno = saved_errno;
		return -1;
	}

	if (fclose (kmsg) < 0)
		return -1;

	return 0;
}


/**
 * crash_handler:
 * @signum: signal number received.
 *
 * Handle receiving the SEGV or ABRT signal, usually caused by one of
 * our own mistakes.  We deal with it by dumping core in a child process
 * and then killing the parent.
 *
 * Sadly there's no real alternative to the ensuing kernel panic.  Our
 * state is likely in tatters, so we can't sigjmp() anywhere "safe" or
 * re-exec since the system will be suddenly lobotomised.  We definitely
 * don't want to start a root shell or anything like that.  Best thing is
 * to just stop the whole thing and hope that bug report comes quickly.
 **/
static void
crash_handler (int signum)
{
	pid_t pid;

	nih_assert (argv0 != NULL);

	pid = fork ();
	if (pid == 0) {
		struct sigaction act;
		struct rlimit    limit;
		sigset_t         mask;

		/* Mask out all signals */
		sigfillset (&mask);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Set the handler to the default so core is dumped */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset (&act.sa_mask);
		sigaction (signum, &act, NULL);

		/* Don't limit the core dump size */
		limit.rlim_cur = RLIM_INFINITY;
		limit.rlim_max = RLIM_INFINITY;
		setrlimit (RLIMIT_CORE, &limit);

		/* Dump in the root directory */
		if (chdir ("/"))
			nih_warn ("%s: %s", _("Unable to set root directory"),
				  strerror (errno));

		/* Raise the signal again */
		raise (signum);

		/* Unmask so that we receive it */
		sigdelset (&mask, signum);
		sigprocmask (SIG_SETMASK, &mask, NULL);

		/* Wait for death */
		pause ();
		exit (0);
	} else if (pid > 0) {
		/* Wait for the core to be generated */
		waitpid (pid, NULL, 0);

		nih_fatal (_("Caught %s, core dumped"),
			   (signum == SIGSEGV
			    ? "segmentation fault" : "abort"));
	} else {
		nih_fatal (_("Caught %s, unable to dump core"),
			   (signum == SIGSEGV
			    ? "segmentation fault" : "abort"));
	}

	/* Goodbye, cruel world. */
	exit (signum);
}

/**
 * cad_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGINT signal, sent to us when somebody
 * presses Ctrl-Alt-Delete on the console.  We just generate a
 * ctrlaltdel event.
 **/
static void
cad_handler (void      *data,
	     NihSignal *signal)
{
	NIH_MUST (event_new (NULL, CTRLALTDEL_EVENT, NULL));
}

/**
 * kbd_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGWINCH signal, sent to us when somebody
 * presses Alt-UpArrow on the console.  We just generate a
 * kbdrequest event.
 **/
static void
kbd_handler (void      *data,
	     NihSignal *signal)
{
	NIH_MUST (event_new (NULL, KBDREQUEST_EVENT, NULL));
}

/**
 * pwr_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGPWR signal, sent to us when powstatd
 * changes the /etc/powerstatus file.  We just generate a
 * power-status-changed event and jobs read the file.
 **/
static void
pwr_handler (void      *data,
	     NihSignal *signal)
{
	NIH_MUST (event_new (NULL, PWRSTATUS_EVENT, NULL));
}

/**
 * hup_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGHUP signal, which we use to instruct us to
 * reload our configuration.
 **/
static void
hup_handler (void      *data,
	     NihSignal *signal)
{
	nih_info (_("Reloading configuration"));
	conf_reload ();
}

/**
 * usr1_handler:
 * @data: unused,
 * @signal: signal that called this handler.
 *
 * Handle having recieved the SIGUSR signal, which we use to instruct us to
 * reconnect to D-Bus.
 **/
static void
usr1_handler (void      *data,
	      NihSignal *signal)
{
	if (! control_bus) {
		nih_info (_("Reconnecting to system bus"));

		if (control_bus_open () < 0) {
			NihError *err;

			err = nih_error_get ();
			nih_warn ("%s: %s", _("Unable to connect to the system bus"),
				  err->message);
			nih_free (err);
		}
	}
}
#endif /* DEBUG */

#ifdef HAVE_SELINUX
/**
 * selinux_set_checkreqprot:
 *
 * Forces /sys/fs/selinux/checkreqprot to 0 to ensure that
 * SELinux will check the protection for mmap and mprotect
 * calls that will be applied by the kernel and not the
 * one requested by the application.
 **/
static int selinux_set_checkreqprot (void)
{
	static const char path[] = "/sys/fs/selinux/checkreqprot";
	FILE *checkreqprot_file;

	checkreqprot_file = fopen (path, "w");
	if (!checkreqprot_file)
		nih_return_system_error (-1);

	if (fputc ('0', checkreqprot_file) == EOF)
		nih_return_system_error (-1);

	if (fclose (checkreqprot_file) != 0)
		nih_return_system_error (-1);

	return 0;
}

/**
 * initialize_selinux:
 *
 * Loads an SELinux policy.
 **/
static int initialize_selinux (void)
{
	int         enforce = 0;

	if (selinux_init_load_policy (&enforce) != 0) {
		nih_warn (_("SELinux policy failed to load"));
		if (enforce > 0) {
			/* Enforcing mode, must quit. */
			nih_return_error (-1, SELINUX_POLICY_LOAD_FAIL,
					  _(SELINUX_POLICY_LOAD_FAIL_STR));
		}
	}

	return selinux_set_checkreqprot ();
}
#endif /* HAVE_SELINUX */
