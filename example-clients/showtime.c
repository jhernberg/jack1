#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <jack/jack.h>
#include <jack/transport.h>

jack_client_t *client;


void
showtime ()
{
	jack_position_t current;
	jack_transport_state_t transport_state;

	transport_state = jack_transport_query (client, &current);

	printf ("frame: %7lu\t", current.frame);

	switch (transport_state) {
	case JackTransportStopped:
		printf ("state: Stopped\t");
		break;
	case JackTransportRolling:
		printf ("state: Rolling\t");
		break;
	case JackTransportStarting:
		printf ("state: Starting\t");
		break;
	default:
		printf ("state: [unknown]\t");
	}

	if (current.valid & JackPositionBBT)
		printf ("BBT: %3d|%d|%04d\n",
			current.bar, current.beat, current.tick);
	else
		printf ("BBT: [-]\n");
}

void
jack_shutdown (void *arg)
{
	exit (1);
}

void
signal_handler (int sig)
{
	jack_client_close (client);
	fprintf (stderr, "signal received, exiting ...\n");
	exit (0);
}

int
main (int argc, char *argv[])

{
	/* try to become a client of the JACK server */

	if ((client = jack_client_new ("showtime")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	signal (SIGQUIT, signal_handler);
	signal (SIGTERM, signal_handler);
	signal (SIGHUP, signal_handler);
	signal (SIGINT, signal_handler);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* tell the JACK server that we are ready to roll */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		return 1;
	}
	
	while (1) {
		usleep (100000);
		showtime ();
	}

	jack_client_close (client);
	exit (0);
}
