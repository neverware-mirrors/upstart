/* upstart
 *
 * test_control.c - test suite for init/control.c
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

#include <config.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/io.h>

#include <upstart/control.h>

#include "job.h"
#include "control.h"


extern int upstart_disable_safeties;


int
test_open (void)
{
	NihIoWatch         *watch;
	UpstartMsg         *message;
	ControlMsg         *msg;
	struct sockaddr_un  addr;
	int                 ret = 0, val;
	char                name[26];
	socklen_t           len;

	printf ("Testing control_open()\n");

	printf ("...with empty send queue\n");
	watch = control_open ();

	/* Should be looking for NIH_IO_READ */
	if (watch->events != NIH_IO_READ) {
		printf ("BAD: watch events weren't what we expected.\n");
		ret = 1;
	}

	/* Socket should be in AF_UNIX space */
	len = sizeof (addr);
	assert (getsockname (watch->fd, (struct sockaddr *)&addr, &len) == 0);
	if (addr.sun_family != AF_UNIX) {
		printf ("BAD: address family wasn't what we expected.\n");
		ret = 1;
	}

	/* Socket should be in abstract namespace */
	if (addr.sun_path[0] != '\0') {
		printf ("BAD: address type wasn't what we expected.\n");
		ret = 1;
	}

	/* Name should be /com/ubuntu/upstart/$PID */
	sprintf (name, "/com/ubuntu/upstart/%d", getpid ());
	if (strncmp (addr.sun_path + 1, name, strlen (name))) {
		printf ("BAD: address wasn't what we expected.\n");
		ret = 1;
	}

	/* Should work on datagrams */
	val = 0;
	len = sizeof (val);
	assert (getsockopt (watch->fd, SOL_SOCKET, SO_TYPE, &val, &len) == 0);
	if (val != SOCK_DGRAM) {
		printf ("BAD: socket type wasn't what we expected.\n");
		ret = 1;
	}

	/* Credentials should be passed with any received message */
	val = 0;
	len = sizeof (val);
	assert (getsockopt (watch->fd, SOL_SOCKET, SO_PASSCRED,
			    &val, &len) == 0);
	if (val == 0) {
		printf ("BAD: socket will not receive credentials.\n");
		ret = 1;
	}

	/* Should be non-blocking */
	assert ((val = fcntl (watch->fd, F_GETFL)) >= 0);
	if (! (val & O_NONBLOCK)) {
		printf ("BAD: socket wasn't set non-blocking.\n");
		ret = 1;
	}

	/* Should be closed on exec */
	assert ((val = fcntl (watch->fd, F_GETFD)) >= 0);
	if (! (val & FD_CLOEXEC)) {
		printf ("BAD: socket wasn't set non-blocking.\n");
		ret = 1;
	}

	control_close ();


	printf ("...with non-empty send queue\n");
	message = nih_new (NULL, UpstartMsg);
	msg = control_send (123, message);

	watch = control_open ();

	/* Should be looking for NIH_IO_READ and WRITE */
	if (watch->events != (NIH_IO_READ | NIH_IO_WRITE)) {
		printf ("BAD: watch events weren't what we expected.\n");
		ret = 1;
	}

	control_close ();

	nih_list_free (&msg->entry);
	nih_free (message);

	return ret;
}

static int was_called = 0;

static int
my_destructor (void *ptr)
{
	was_called++;

	return 0;
}

int
test_close (void)
{
	NihIoWatch *watch;
	int         ret = 0, fd;

	printf ("Testing control_close()\n");
	watch = control_open ();
	fd = watch->fd;
	was_called = 0;
	nih_alloc_set_destructor (watch, my_destructor);
	control_close ();

	/* Watch should be freed */
	if (! was_called) {
		printf ("BAD: watch was not freed.\n");
		ret = 1;
	}

	/* Socket should be closed */
	if ((fcntl (fd, F_GETFD) >= 0) || (errno != EBADF)) {
		printf ("BAD: socket was not closed.\n");
		ret = 1;
	}

	return ret;
}


int
test_send (void)
{
	ControlMsg *msg;
	UpstartMsg *message;
	NihIoWatch *watch;
	int         ret = 0;

	printf ("Testing control_send()\n");
	message = nih_new (NULL, UpstartMsg);
	watch = control_open ();


	printf ("...with simple message\n");
	message->type = UPSTART_NO_OP;
	msg = control_send (123, message);

	/* Destination process should be 123 */
	if (msg->pid != 123) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Message type should be what we gave */
	if (msg->message.type != UPSTART_NO_OP) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be in the send queue */
	if (NIH_LIST_EMPTY (&msg->entry)) {
		printf ("BAD: was not placed in the send queue.\n");
		ret = 1;
	}

	/* Should have been allocated with NihAlloc */
	if (nih_alloc_size (msg) != sizeof (ControlMsg)) {
		printf ("BAD: nih_alloc was not used.\n");
		ret = 1;
	}

	/* Watch should be looking for NIH_IO_WRITE */
	if (! (watch->events & NIH_IO_WRITE)) {
		printf ("BAD: watch not looking for write.\n");
		ret = 1;
	}

	nih_list_free (&msg->entry);


	printf ("...with complex message\n");
	message->type = UPSTART_JOB_START;
	message->job_start.name = "wibble";
	msg = control_send (123, message);

	/* Destination process should be 123 */
	if (msg->pid != 123) {
		printf ("BAD: process id wasn't what we expected.\n");
		ret = 1;
	}

	/* Message type should be what we gave */
	if (msg->message.type != UPSTART_JOB_START) {
		printf ("BAD: message type wasn't what we expected.\n");
		ret = 1;
	}

	/* Message name should have been copied */
	if (strcmp (msg->message.job_start.name, "wibble")) {
		printf ("BAD: job name wasn't what we expected.\n");
		ret = 1;
	}

	/* Should be in the send queue */
	if (NIH_LIST_EMPTY (&msg->entry)) {
		printf ("BAD: was not placed in the send queue.\n");
		ret = 1;
	}

	/* Should have been allocated with NihAlloc */
	if (nih_alloc_size (msg) != sizeof (ControlMsg)) {
		printf ("BAD: nih_alloc was not used.\n");
		ret = 1;
	}

	/* Name should be nih_alloc child of msg */
	if (nih_alloc_parent (msg->message.job_start.name) != msg) {
		printf ("BAD: job name wasn't nih_alloc child of msg.\n");
		ret = 1;
	}

	nih_list_free (&msg->entry);


	nih_free (message);
	control_close ();

	return ret;
}


enum {
	TEST_SILLY,
	TEST_NO_OP,
	TEST_JOB_UNKNOWN,
	TEST_JOB_START,
	TEST_JOB_STOP,
	TEST_JOB_QUERY
};

static pid_t
test_cb_child (int test)
{
	UpstartMsg *s_msg, *r_msg;
	pid_t       pid;
	int         sock, ret = 0;

	if ((pid = fork ()) != 0)
	    return pid;

	sock = upstart_open ();
	s_msg = nih_new (NULL, UpstartMsg);

	switch (test) {
	case TEST_SILLY:
		s_msg->type = UPSTART_JOB_UNKNOWN;
		s_msg->job_unknown.name = "eh";
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		break;
	case TEST_NO_OP:
		s_msg->type = UPSTART_NO_OP;
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		break;
	case TEST_JOB_UNKNOWN:
		s_msg->type = UPSTART_JOB_START;
		s_msg->job_start.name = "wibble";
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		assert (r_msg = upstart_recv_msg (NULL, sock, NULL));

		/* Should receive a UPSTART_JOB_UNKNOWN message */
		if (r_msg->type != UPSTART_JOB_UNKNOWN) {
			printf ("BAD: response wasn't what we expected.\n");
			ret = 1;
		}

		/* Job should be the one we tried */
		if (strcmp (r_msg->job_status.name, "wibble")) {
			printf ("BAD: name wasn't what we expected.\n");
			ret = 1;
		}

		break;
	case TEST_JOB_START:
		s_msg->type = UPSTART_JOB_START;
		s_msg->job_start.name = "test";
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		assert (r_msg = upstart_recv_msg (NULL, sock, NULL));

		/* Should receive a UPSTART_JOB_STATUS message */
		if (r_msg->type != UPSTART_JOB_STATUS) {
			printf ("BAD: response wasn't what we expected.\n");
			ret = 1;
		}

		/* Job should be the one we started */
		if (strcmp (r_msg->job_status.name, "test")) {
			printf ("BAD: name wasn't what we expected.\n");
			ret = 1;
		}

		/* Goal should now be JOB_START */
		if (r_msg->job_status.goal != JOB_START) {
			printf ("BAD: start wasn't what we expected.\n");
			ret = 1;
		}

		/* State should now be JOB_RUNNING */
		if (r_msg->job_status.state != JOB_RUNNING) {
			printf ("BAD: process wasn't what we expected.\n");
			ret = 1;
		}

		/* State should now be PROCESS_ACTIVE */
		if (r_msg->job_status.process_state != PROCESS_ACTIVE) {
			printf ("BAD: goal wasn't what we expected.\n");
			ret = 1;
		}

		break;
	case TEST_JOB_STOP:
		s_msg->type = UPSTART_JOB_STOP;
		s_msg->job_stop.name = "test";
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		assert (r_msg = upstart_recv_msg (NULL, sock, NULL));

		/* Should receive a UPSTART_JOB_STATUS message */
		if (r_msg->type != UPSTART_JOB_STATUS) {
			printf ("BAD: response wasn't what we expected.\n");
			ret = 1;
		}

		/* Job should be the one we stopped */
		if (strcmp (r_msg->job_status.name, "test")) {
			printf ("BAD: name wasn't what we expected.\n");
			ret = 1;
		}

		/* Goal should now be JOB_STOP */
		if (r_msg->job_status.goal != JOB_STOP) {
			printf ("BAD: goal wasn't what we expected.\n");
			ret = 1;
		}

		/* State should still be JOB_RUNNING */
		if (r_msg->job_status.state != JOB_RUNNING) {
			printf ("BAD: state wasn't what we expected.\n");
			ret = 1;
		}

		/* State should now be PROCESS_KILLED */
		if (r_msg->job_status.process_state != PROCESS_KILLED) {
			printf ("BAD: process wasn't what we expected.\n");
			ret = 1;
		}

		break;
	case TEST_JOB_QUERY:
		s_msg->type = UPSTART_JOB_QUERY;
		s_msg->job_stop.name = "test";
		assert (upstart_send_msg_to (getppid (), sock, s_msg) == 0);
		assert (r_msg = upstart_recv_msg (NULL, sock, NULL));

		/* Should receive a UPSTART_JOB_STATUS message */
		if (r_msg->type != UPSTART_JOB_STATUS) {
			printf ("BAD: response wasn't what we expected.\n");
			ret = 1;
		}

		/* Job should be the one we asked for */
		if (strcmp (r_msg->job_status.name, "test")) {
			printf ("BAD: name wasn't what we expected.\n");
			ret = 1;
		}

		/* Goal should be JOB_START */
		if (r_msg->job_status.goal != JOB_START) {
			printf ("BAD: goal wasn't what we expected.\n");
			ret = 1;
		}

		/* State should be JOB_STOPPING */
		if (r_msg->job_status.state != JOB_STOPPING) {
			printf ("BAD: state wasn't what we expected.\n");
			ret = 1;
		}

		/* State should be PROCESS_ACTIVE */
		if (r_msg->job_status.process_state != PROCESS_ACTIVE) {
			printf ("BAD: process wasn't what we expected.\n");
			ret = 1;
		}

		break;
	}

	exit (ret);
}

int
test_cb (void)
{
	NihIoWatch *watch;
	Job        *job;
	pid_t       pid;
	int         ret = 0, status;

	printf ("Testing control_cb()\n");
	watch = control_open ();
	upstart_disable_safeties = TRUE;


	printf ("...with inappropriate command\n");
	pid = test_cb_child (TEST_SILLY);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;


	printf ("...with no-op command\n");
	pid = test_cb_child (TEST_NO_OP);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;


	printf ("...with unknown job\n");
	pid = test_cb_child (TEST_JOB_UNKNOWN);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;


	printf ("...with start job command\n");
	job = job_new (NULL, "test");
	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process_state = PROCESS_NONE;
	job->command = "echo";

	pid = test_cb_child (TEST_JOB_START);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;

	/* Job goal should have been changed in parent */
	if (job->goal != JOB_START) {
		printf ("BAD: job goal didn't change as expected.\n");
		ret = 1;
	}


	printf ("...with stop job command\n");
	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process_state = PROCESS_ACTIVE;
	job->pid = fork ();
	if (job->pid == 0) {
		select (0, NULL, NULL, NULL, NULL);
		exit (0);
	}

	pid = test_cb_child (TEST_JOB_STOP);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;

	/* Job goal should have been changed in parent */
	if (job->goal != JOB_STOP) {
		printf ("BAD: job goal didn't change as expected.\n");
		ret = 1;
	}

	waitpid (job->pid, NULL, 0);


	printf ("...with query job command\n");
	job->goal = JOB_START;
	job->state = JOB_STOPPING;
	job->process_state = PROCESS_ACTIVE;

	pid = test_cb_child (TEST_JOB_QUERY);
	watch->callback (watch->data, watch, NIH_IO_READ | NIH_IO_WRITE);
	waitpid (pid, &status, 0);
	if ((! WIFEXITED (status)) || (WEXITSTATUS (status) != 0))
		ret = 1;


	upstart_disable_safeties = FALSE;
	control_close ();

	return ret;
}



int
main (int   argc,
      char *argv[])
{
	int ret = 0;

	ret |= test_open ();
	ret |= test_close ();
	ret |= test_send ();
	ret |= test_cb ();

	return ret;
}