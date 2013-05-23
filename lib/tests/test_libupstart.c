/* upstart
 *
 * test_libupstart.c - test suite for libupstart.
 *
 * Copyright © 2013 Canonical Ltd.
 * Author: James Hunt <james.hunt@canonical.com>
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

#include <unistd.h>
#include <fnmatch.h>

#include <nih/test.h>
#include <nih/main.h>
#include <nih/string.h>
#include <nih/logging.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_proxy.h>
#include <nih-dbus/errors.h>
#include <nih-dbus/dbus_connection.h>

#include "dbus/upstart.h"

#include "upstart.h"

/**
 * upstart_open:
 * @parent: parent object for new proxy.
 *
 * Opens a connection to the Upstart init daemon and returns a proxy
 * to the manager object. If @dest_name is not NULL, a connection is
 * instead opened to the system bus and the proxy linked to the
 * well-known name given.
 *
 * If @parent is not NULL, it should be a pointer to another object
 * which will be used as a parent for the returned proxy.  When all
 * parents of the returned proxy are freed, the returned proxy will
 * also be freed.
 *
 * Returns: newly allocated D-Bus proxy or NULL on raised error.
 **/
NihDBusProxy *
upstart_open (const void *parent)
{
	DBusError       dbus_error;
	DBusConnection *connection;
	NihDBusProxy *  upstart;

	dbus_error_init (&dbus_error);

	connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
	if (! connection) {
		nih_dbus_error_raise (dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		return NULL;
	}

	dbus_connection_set_exit_on_disconnect (connection, FALSE);
	dbus_error_free (&dbus_error);

	upstart = nih_dbus_proxy_new (parent, connection,
				      DBUS_SERVICE_UPSTART,
				      DBUS_PATH_UPSTART,
				      NULL, NULL);
	if (! upstart) {
		dbus_connection_unref (connection);
		return NULL;
	}

	upstart->auto_start = FALSE;

	/* Drop initial reference now the proxy holds one */
	dbus_connection_unref (connection);

	return upstart;
}

void
test_libupstart (void)
{
	nih_local NihDBusProxy  *upstart = NULL;
	nih_local char          *version = NULL;
	int                      ret;

	TEST_GROUP ("libupstart");

	TEST_FEATURE ("version");

	upstart = upstart_open (NULL);
	TEST_NE_P (upstart, NULL);


	/* Basic test (that does not change the state of the system
	 * running this test) to see if we can query version of running
	 * Upstart instance.
	 */
	ret = upstart_get_version_sync (NULL, upstart, &version);
	TEST_EQ (ret, 0);

	nih_message ("Running instance version: '%s'", version);
	assert0 (fnmatch ("init (upstart*)", version, 0x0));
}

int
main (int   argc,
      char *argv[])
{
	test_libupstart ();

	return 0;
}
