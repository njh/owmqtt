/*
    OQMQTT - 1-wire to MQTT bridge
    Copyright (C) 2011 Nicholas J Humfrey <njh@aelius.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>

#include <owcapi.h>
#include <mosquitto.h>

#include "owmqtt.h"
#include "config.h"



/* Global Variables */
struct mosquitto *mosq = NULL;
int keep_running = TRUE;
int mqtt_connected = FALSE;
int debug = TRUE;
int exit_code = EXIT_SUCCESS;

// Configurable parameters
const char *owfs_params = DEFAULT_OWFS_PARAMS;
int polling_interval = DEFAULT_POLLING;
char *mqtt_prefix = DEFAULT_MQTT_PREFIX;
const char* mqtt_host = DEFAULT_MQTT_HOST;
int mqtt_port = DEFAULT_MQTT_PORT;
int mqtt_qos = DEFAULT_MQTT_QOS;
int mqtt_retain = DEFAULT_MQTT_RETAIN;
int mqtt_keepalive = DEFAULT_MQTT_KEEPALIVE;




static void owmqtt_log(int level, const char *fmt, ...)
{
  va_list args;

  if (level == OWMQTT_DEBUG && !debug)
      return;

  // Display the message
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);

  if (level == OWMQTT_ERROR) {
    // Exit with a non-zero exit code if there was a fatal error
    exit_code++;
    if (keep_running) {
      // Quit gracefully
      keep_running = FALSE;
    } else {
      fprintf(stderr, "Error while quiting; exiting immediately.\n");
      exit(-1);
    }
  }
}

static void termination_handler(int signum)
{
    switch(signum) {
        case SIGHUP:  owmqtt_info(PACKAGE_NAME ": Received SIGHUP, exiting."); break;
        case SIGTERM: owmqtt_info(PACKAGE_NAME ": Received SIGTERM, exiting."); break;
        case SIGINT:  owmqtt_info(PACKAGE_NAME ": Received SIGINT, exiting."); break;
    }

    keep_running = FALSE;
}


/*
static void usage()
{
    printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Usage: %s [options]\n", PACKAGE_NAME);
    printf("   -h            Display this message\n");
    printf("   -D            Enable debug mode\n");
    exit(-1);
}
*/


static void publish_property(const char* node, const char* property)
{
    char path[MAX_PATH_LEN];
    char mqtt_path[MAX_PATH_LEN];
    size_t buf_len = 0;
    char *buf = NULL, *value = NULL;
    int res = -1;

    // Fetch the value
    snprintf(path, MAX_PATH_LEN, "/%s%s", node, property);
    res = OW_get(path, &buf, &buf_len);
    if (res <= 0) {
        // FIXME: report error
        return;
    }

    // Trim whitespace
    value = buf;
    while(*value && isspace(*value)) {
        value++;
    }

    snprintf(mqtt_path, MAX_PATH_LEN, "%s/%s%s", mqtt_prefix, node, property);
    owmqtt_debug("%s = %s", mqtt_path, value);

    // Publish
    res = mosquitto_publish(mosq, NULL, mqtt_path, strlen(value), (uint8_t *)value, mqtt_qos, mqtt_retain);
    if(res){
        // FIXME: deal with this better
        owmqtt_error("Error while publishing, disconnecting.", res);
        mosquitto_disconnect(mosq);
    }

    free(buf);
}

static void process_node(const char* node)
{
    char *ptr, *property, *buf = NULL;
    size_t buf_len = 0;
    int res = -1;

    res = OW_get(node, &buf, &buf_len);
    if (res <= 0) {
        // FIXME: report error
        return;
    }

    ptr = buf;
    while ((property = strsep(&ptr, ",")) != NULL) {
        // FIXME: add support for other properties
        if (strcmp(property, "temperature")==0) {
            publish_property(node, property);
        }
    }

    free(buf);
}

static void scan_bus()
{
    char *ptr, *node, *buf = NULL;
    size_t buf_len = 0;
    int res = -1;

    res = OW_get("/", &buf, &buf_len);
    if (res <= 0) {
        // FIXME: report error
        return;
    }

    ptr = buf;
    while ((node = strsep(&ptr, ",")) != NULL) {
        if (strlen(node) > 13) {
            process_node(node);
        }
    }

    free(buf);
}

static void owmqtt_connect_callback(void *obj, int result)
{
    if(!result){
        owmqtt_info("Connected to MQTT server.");
        mqtt_connected = TRUE;
    } else {
        switch(result) {
            case 0x01:
                owmqtt_error("Connection Refused: unacceptable protocol version\n");
                break;
            case 0x02:
                owmqtt_error("Connection Refused: identifier rejected\n");
                break;
            case 0x03:
                // FIXME: if broker is unavailable, sleep and try connecting again
                owmqtt_error("Connection Refused: broker unavailable\n");
                break;
            case 0x04:
                owmqtt_error("Connection Refused: bad user name or password\n");
                break;
            case 0x05:
                owmqtt_error("Connection Refused: not authorised\n");
                break;
            default:
                owmqtt_error("Connection Refused: unknown reason\n");
                break;
        }

        mqtt_connected = FALSE;
    }
}


static void owmqtt_disconnect_callback(void *obj)
{
    mqtt_connected = FALSE;

    // FIXME: re-establish the connection
    // FIXME: keep count of re-connects
}


static struct mosquitto * initialise_mqtt(const char* id)
{
    struct mosquitto *mosq = NULL;
    int res = 0;

    mosq = mosquitto_new(id, NULL);
    if (!mosq) {
        owmqtt_error("Failed to initialise MQTT client.");
        return NULL;
    }

    if (debug) {
        mosquitto_log_init(mosq, MOSQ_LOG_DEBUG | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO |
                                 MOSQ_LOG_WARNING | MOSQ_LOG_ERR, MOSQ_LOG_STDOUT);
    }

    // FIXME: add support for username and password

    mosquitto_connect_callback_set(mosq, owmqtt_connect_callback);
    mosquitto_disconnect_callback_set(mosq, owmqtt_disconnect_callback);

    owmqtt_info("Connecting to %s:%d...", mqtt_host, mqtt_port);
    res = mosquitto_connect(mosq, mqtt_host, mqtt_port, mqtt_keepalive, 1);
    if (res) {
        owmqtt_error("Unable to connect (%d).", res);
        mosquitto_destroy(mosq);
        return NULL;
    }

    return mosq;
}

int main(int argc, char *argv[])
{
    //int opt;
    time_t next_scan = 0;
    int res;

    // Make stdout unbuffered for logging/debugging
    setbuf(stdout, NULL);


    // Parse Switches
    /*
    while ((opt = getopt(argc, argv, "Dh")) != -1) {
        switch (opt) {
            case 'D':  debug = TRUE; break;
            case 'h':
            default:   usage(); break;
        }
    }
    */

    // Initialise libmosquitto
    mosquitto_lib_init();
    if (debug) {
        int major, minor, revision;
        mosquitto_lib_version(&major, &minor, &revision);
        owmqtt_debug("libmosquitto version: %d.%d.%d", major, minor, revision);
    }

    // Initialise owfs
    res = OW_init(owfs_params);
    if (res) {
        owmqtt_error("Failed to initialise owfs");
        goto cleanup;
    }

    OW_set_error_print("2");
    OW_set_error_level("6");


    // Create MQTT client
    // FIXME: get the client id from OW
    mosq = initialise_mqtt(PACKAGE_NAME);
    if (!mosq) {
        owmqtt_error("Failed to initialise MQTT client");
        goto cleanup;
    }

    // Setup signal handlers - so we exit cleanly
    signal(SIGTERM, termination_handler);
    signal(SIGINT, termination_handler);
    signal(SIGHUP, termination_handler);


    next_scan = time(NULL);
    // FIXME: check for error

    while (keep_running) {

        // Is it time to scan the bus again?
        if (time(NULL) >= next_scan && mqtt_connected) {
            scan_bus();
            next_scan += polling_interval;
        }

        // FIXME: check that we can keep-up

        // Wait for network packets for a maximum of 0.5s
        res = mosquitto_loop(mosq, 500);
        // FIXME: check for errors
    }

cleanup:
    owmqtt_debug("Cleaning up.");

    // Disconnect from MQTT server
    if (mosq) mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    OW_finish();

    // exit_code is non-zero if something went wrong
    return exit_code;
}
