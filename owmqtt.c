/*
    OQMQTT - 1-wire to MQTT bridge
    Copyright (C) 2011-2012 Nicholas J Humfrey <njh@aelius.com>

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

#include <sys/types.h>
#include <regex.h>

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


// Paths on OWFS to ignore (not publish)
regex_t *ignore_regexs;
size_t ignore_count;
const char* ignore_paths[] = {
  "^/alarm/",
  "^/bus\\.",
  "^/settings/",
  "^/simultaneous/",
  "^/statistics/",
  "^/structure/",
  "^/system/",
  "^/uncached/",
  NULL
};

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


static void owmqtt_publish(const char* path)
{
    char mqtt_path[MAX_PATH_LEN];
    size_t buf_len = 0;
    char *buf = NULL, *value = NULL;
    int res = -1;

    // Fetch the value
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

    snprintf(mqtt_path, MAX_PATH_LEN, "%s%s", mqtt_prefix, path);
    owmqtt_debug("%s = %s", mqtt_path, value);

    // Publish
    res = mosquitto_publish(mosq, NULL, mqtt_path, strlen(value), value, mqtt_qos, mqtt_retain);
    if(res){
        // FIXME: deal with this better
        owmqtt_error("Error while publishing, disconnecting.", res);
        mosquitto_disconnect(mosq);
    }

    free(buf);
}


static char owmqtt_check_ignore(const char *path)
{
  int i;
  
  for(i=0; i< ignore_count; i++) {
    int res = regexec(&ignore_regexs[i], path, 0, NULL, 0);
    if (res==0) return TRUE;
  }

  return FALSE;
}

static void owmqtt_process_node(const char *path)
{
    char *ptr, *node, *buf = NULL;
    size_t buf_len = 0;
    size_t path_len = strlen(path);
    int res = -1;

    res = OW_get(path, &buf, &buf_len);
    if (res <= 0) {
        // FIXME: report error
        return;
    }

    for(ptr = buf; (node = strsep(&ptr, ",")) != NULL; ) {
        size_t newlen = strlen(node) + path_len + 1;
        char *newpath = malloc(newlen);
        snprintf(newpath, newlen, "%s%s", path, node);

        if (owmqtt_check_ignore(newpath) == FALSE) {
          if (newpath[newlen-2] == '/') {
            owmqtt_process_node(newpath);
          } else {
            owmqtt_publish(newpath);
          }
        }
        
        free(newpath);
    }

    free(buf);
}



static void owmqtt_connect_callback(struct mosquitto *mosq, void *obj, int rc)
{
  if(!rc){
    owmqtt_info("Connected to MQTT server.\n");
    mqtt_connected = 1;
  } else {
    const char *str = mosquitto_connack_string(rc);
    owmqtt_error("Connection Refused: %s\n", str);
    mqtt_connected = 0;
  }
}


static void owmqtt_disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    mqtt_connected = FALSE;

    // FIXME: re-establish the connection
    // FIXME: keep count of re-connects
}

static void owmqtt_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
    // FIXME: use the log level
    printf("LOG: %s\n", str);
}

static struct mosquitto * owmqtt_initialise_mqtt(const char* id)
{
    struct mosquitto *mosq = NULL;
    int res = 0;

    mosq = mosquitto_new(id, true, NULL);
    if (!mosq) {
        owmqtt_error("Failed to initialise MQTT client.");
        return NULL;
    }

    // FIXME: add support for username and password

    mosquitto_log_callback_set(mosq, owmqtt_log_callback);
    mosquitto_connect_callback_set(mosq, owmqtt_connect_callback);
    mosquitto_disconnect_callback_set(mosq, owmqtt_disconnect_callback);

    owmqtt_info("Connecting to %s:%d...", mqtt_host, mqtt_port);
    res = mosquitto_connect(mosq, mqtt_host, mqtt_port, mqtt_keepalive);
    if (res) {
        owmqtt_error("Unable to connect (%d).", res);
        mosquitto_destroy(mosq);
        return NULL;
    }

    return mosq;
}

static void owmqtt_regex_init()
{
    int i;
    
    for(i=0; ignore_paths[i]; i++);
    ignore_count = i;
    owmqtt_debug("ignore_count=%d", ignore_count);
    
    // Allocate memory for the regular experssions
    ignore_regexs = (regex_t*)malloc(sizeof(regex_t) * ignore_count);
    // FIXME: check for error
    
    for(i=0; i<ignore_count; i++) {
      int res = regcomp(&ignore_regexs[i], ignore_paths[i], REG_EXTENDED | REG_NOSUB);
      // FIXME: check for error
      owmqtt_debug("Compiling %s=%d", ignore_paths[i], res);
    }
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
    
    // Compile the regular expressions
    owmqtt_regex_init();

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
    mosq = owmqtt_initialise_mqtt(PACKAGE_NAME);
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
            owmqtt_process_node("/");
            next_scan += polling_interval;
        }

        // FIXME: check that we can keep-up

        // Wait for network packets for a maximum of 0.5s
        res = mosquitto_loop(mosq, 500, 1);
        // FIXME: check for errors
    }

cleanup:
    owmqtt_debug("Cleaning up.");

    // Disconnect from MQTT server
    if (mosq) mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    OW_finish();
    
    // FIXME: call regfree() on each of the regular expressions

    // exit_code is non-zero if something went wrong
    return exit_code;
}
