/* Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <systemd/sd-daemon.h> // For systemd watchdog
#include <wiringPi.h>
#include <compile_time.h>
#include <utils.h>
#include <timer.h>
#include <audio.h>
#include <urtp.h>
#include <log.h>

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Things to help with parsing filenames.
#define DIR_SEPARATORS "\\/"
#define EXT_SEPARATOR "."

// The default location for log files
#define DEFAULT_LOG_FILE_PATH "./logtmp"

/* ----------------------------------------------------------------
 * STATIC VARIABLES
 * -------------------------------------------------------------- */

// For logging.
static char gLogBuffer[LOG_STORE_SIZE];

// For writing a log to file in the background.
static size_t gLogWriteTicker;

// Watchdog timer interval.
static long long unsigned int gWatchdogIntervalSeconds;

// The GPIO pin to toggle.
static int gGpio = -1;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print the usage text
static void printUsage(char * pExeName) {
    printf("\n%s: run the Internet of Chuffs client.  Usage:\n", pExeName);
    printf("    %s audio_source audio_server_url <-g max_gain> <-ls log_server_url> <-ld log_directory> <-p gpio>\n", pExeName);
    printf("where:\n");
    printf("    audio_source is the name of the ALSA PCM audio capture device (must be 32 bits per channel, stereo, 16 kHz sample rate),\n");
    printf("    audio_server_url is the URL of the Internet of Chuffs server,\n");
    printf("    -g optionally specifies the maximum gain to apply; default is max which is %d, lower numbers mean less gain (and noise),\n", AUDIO_MAX_SHIFT_BITS);
    printf("    -ls optionally specifies the URL of a server to upload log-files to (where a logging server application must be listening),\n");
    printf("    -ld optionally specifies the directory to use for log files (default %s); the directory will be created if it does not exist,\n", DEFAULT_LOG_FILE_PATH);
    printf("    -p optionally specifies a GPIO pin to toggle to show activity (using wiringPi numbering),\n");
    printf("For example:\n");
    printf("    %s mic io-server.co.uk:1297 -ls logserver.com -ld /var/log -p 0\n\n", pExeName);
}

// Exit handler
static void exitHandler(int retValue)
{
    printf("\nStopping.\n");
    stopAudioStreaming();
    digitalWrite(gGpio, LOW);
    printLog();
    deinitLog();
    deinitTimers();
    exit(retValue); 
}

// Signal handler for CTRL-C
static void exitHandlerSignal(int signal)
{
    exitHandler(0);
}

// Watchdog handler
static void watchdogHandler()
{
    if (gWatchdogIntervalSeconds > 0) {
        /* Ping systemd */
        sd_notify(0, "WATCHDOG=1");
    }
}

// Handler to toggle the LED
static void ledToggleHandler()
{
    if (gGpio >= 0) {
        int x = 0;
        // Toggle the LED
        if (digitalRead(gGpio) == 0) {
            x = 1;
        }
        digitalWrite(gGpio, x);
    }
}

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

// Main.
int main(int argc, char *argv[])
{
    int retValue = -1;
    bool success = false;
    bool logFileUploadSuccess = false;
    int x = 0;
    char *pExeName = NULL;
    int maxShift = AUDIO_MAX_SHIFT_BITS;
    char *pPcmAudio = NULL;
    char *pAudioUrl = NULL;
    char *pLogUrl = NULL;
    const char *pLogFilePath = DEFAULT_LOG_FILE_PATH;
    struct stat st = { 0 };
    char *pChar;
    struct sigaction sigIntHandler;

    // Find the exe name in the first argument
    pChar = strtok(argv[x], DIR_SEPARATORS);
    while (pChar != NULL) {
        pExeName = pChar;
        pChar = strtok(NULL, DIR_SEPARATORS);
    }
    if (pExeName != NULL) {
        // Remove the extension
        pChar = strtok(pExeName, EXT_SEPARATOR);
        if (pChar != NULL) {
            pExeName = pChar;
        }
    }
    x++;

    // Look for all the command line parameters
    while (x < argc) {
        // Test for PCM audio device
        if (x == 1) {
            pPcmAudio = argv[x];
        // Test for server URL
        } else if (x == 2) {
            pAudioUrl = argv[x];
            // Test for max gain option
        } else if (strcmp(argv[x], "-g") == 0) {
            x++;
            if (x < argc) {
                maxShift = atoi(argv[x]);
            }
            // Test for log server option
        } else if (strcmp(argv[x], "-ls") == 0) {
            x++;
            if (x < argc) {
                pLogUrl = argv[x];
            }
        // Test for log directory option
        } else if (strcmp(argv[x], "-ld") == 0) {
            x++;
            if (x < argc) {
                pLogFilePath = argv[x];
            }
        // Test for gpio option
        } else if (strcmp(argv[x], "-p") == 0) {
            x++;
            if (x < argc) {
                gGpio = atoi(argv[x]);
            }
        }
        x++;
    }

    // Must have the two mandatory command-line parameters
    if ((pPcmAudio != NULL) && (pAudioUrl != NULL)) {
        // Check that the maximum shift value, if specifed, is sensible
        if ((maxShift >= 0) && (maxShift <= AUDIO_MAX_SHIFT_BITS)) {
            // Check if the directory exists
            if (stat(pLogFilePath, &st) == -1) {
                // If it doesn't exist create it
                if (mkdir(pLogFilePath, 0700) == 0) {
                    success = true;
                } else {
                    printf("Unable to create directory temporary log file directory %s (%s).\n", pLogFilePath, strerror(errno));
                }
            } else {
                success = true;
            }
        } else {
            printf("Max gain must be between 0 and %d (not %d).\n", AUDIO_MAX_SHIFT_BITS, maxShift);
        }

        if (success) {
            printf("Internet of Chuffs client starting.\nAudio PCM capture device is \"%s\", server is \"%s\"", pPcmAudio, pAudioUrl);
            if (pLogUrl != NULL) {
                printf(", log files from previous sessions will be uploaded to \"%s\"", pLogUrl);
            }
            if (pLogFilePath != NULL) {
                printf(", temporarily storing log files in directory \"%s\"", pLogFilePath);
            }
            if (gGpio >= 0) {
                printf(", GPIO%d will be toggled to show activity", gGpio);
            }
            printf(".\n");

            // Set up the CTRL-C handler
            sigIntHandler.sa_handler = exitHandlerSignal;
            sigemptyset(&sigIntHandler.sa_mask);
            sigIntHandler.sa_flags = 0;
            sigaction(SIGINT, &sigIntHandler, NULL);

            // Initialise the timers
            initTimers();

            // Initialise logging
            initLog(gLogBuffer);
            initLogFile(pLogFilePath);
            gLogWriteTicker = startTimer(1000000L, TIMER_PERIODIC, writeLogCallback, NULL);

            LOG(EVENT_SYSTEM_START, getUSeconds() / 1000000);
            LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

            // Tell systemd we're awake and determine if the systemd watchdog is on
            sd_notify(0, "READY=1");
            if (sd_watchdog_enabled(0, &gWatchdogIntervalSeconds) <= 0) {
                gWatchdogIntervalSeconds = 0;
            }

            // Set up wiringPi and the LED pin
            if (gGpio >= 0) {
                wiringPiSetup();
                pinMode(gGpio, OUTPUT);
            }

            // Start
            while (1) {
                // Keep it up until CTRL-C
                if (!audioIsStreaming()) {
                    // if we're not streaming then either we've not started or we've dropped
                    // out of streaming.  In the latter case we need to clean up, so always
                    // do that here just in case
                    stopAudioStreaming();
                    if (startAudioStreaming(pPcmAudio, pAudioUrl, maxShift, watchdogHandler, ledToggleHandler)) {
                        printf("Audio streaming started, press CTRL-C to exit\n");
                        // Safe to upload log files now we've succeeded in making
                        // at least one connection
                        if (!logFileUploadSuccess && (pLogUrl != NULL)) {
                            logFileUploadSuccess = beginLogFileUpload(pLogUrl);
                        }
                    }
                }

                // If we weren't successful, and are going to try again,
                // make sure the watchdog is fed
                if (gWatchdogIntervalSeconds > 0) {
                    watchdogHandler();
                }

                sleep(1);
            }
        } else {
            printUsage(pExeName);
        }
    } else {
        printUsage(pExeName);
    }

    if (success) {
        retValue = 0;
    }

    return retValue;
}

// End of file
