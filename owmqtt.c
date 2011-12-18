/*

	owmqtt.c
	
	1-wire to MQTT bridge

	Copyright (C) 2011 Nicholas J. Humfrey

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/fcntl.h>
#include <getopt.h>
#include <signal.h>

#include <owcapi.h>
#include <mosquitto.h>

#include "config.h"



/* Global Variables */
int keep_running = 1;
int verbose = 0;



static
void termination_handler(int signum)
{
    switch(signum) {
      case SIGHUP:  fprintf(stderr, PACKAGE_NAME ": Received SIGHUP, exiting.\n"); break;
      case SIGTERM: fprintf(stderr, PACKAGE_NAME ": Received SIGTERM, exiting.\n"); break;
      case SIGINT:  fprintf(stderr, PACKAGE_NAME ": Received SIGINT, exiting.\n"); break;
    }

    keep_running = 0;
}


/*
static
void usage()
{
    printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Usage: %s [options]\n", PACKAGE_NAME);
    printf("   -h            Display this message\n");
    printf("   -v            Enable verbose mode\n");
    exit(-1);
}
*/

int main(int argc, char *argv[])
{
    int opt;

    // Make stdout unbuffered for logging/debugging
    setbuf(stdout, NULL);

    // Parse Switches
    /*
    while ((opt = getopt(argc, argv, "d:p:vh")) != -1) {
      switch (opt) {
        case 'v':  verbose = 1; break;
        default:   usage(); break;
      }
    }
    */


    // Setup signal handlers - so we exit cleanly
    signal(SIGTERM, termination_handler);
    signal(SIGINT, termination_handler);
    signal(SIGHUP, termination_handler);

    /* Poll 1-wire */
    while (keep_running) {

      sleep(1);

    }

    /* Clean up */


    return 0;
}
