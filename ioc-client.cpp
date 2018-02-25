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
#include <compile_time.h>
#include <utils.h>
#include <timer.h>
#include <audio.h>
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
 * VARIABLES
 * -------------------------------------------------------------- */

// For logging.
static char gLogBuffer[LOG_STORE_SIZE];

// For writing a log to file in the background.
static size_t gLogWriteTicker;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print the usage text
static void printUsage(char * pExeName) {
    printf("\n%s: run the Internet of Chuffs client.  Usage:\n", pExeName);
    printf("    %s audio_source audio_server_url <-ls log_server_url> <-ld log_directory>\n", pExeName);
    printf("where:\n");
    printf("    audio_source is the name of the ALSA PCM audio capture device (must be 32 bits per channel, stereo, 16 kHz sample rate),\n");
    printf("    audio_server_url is the URL of the Internet of Chuffs server,\n");
    printf("    -ls optionally specifies the URL of a server to upload log-files to (where a logging server application must be listening),\n");
    printf("    -ld optionally specifies the directory to use for log files (default %s); the directory will be created if it does not exist,\n", DEFAULT_LOG_FILE_PATH);
    printf("For example:\n");
    printf("    %s mic io-server.co.uk:1297 -ls logserver.com -ld /var/log\n\n", pExeName);
}

// Signal handler for CTRL-C
static void exitHandlerSignal(int signal)
{
    exitHandler(0);
}

// Exit handler
static void exitHandler(int retValue)
{
    printf("\nStopping.\n");
    stopAudioStreaming();
    printLog();
    deinitLog();
    exit(retvalue); 
}

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

// Main.
int main(int argc, char *argv[])
{
    int retValue = -1;
    bool success = false;
    int x = 0;
    char *pExeName = NULL;
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
        }
        x++;
    }

    // Must have the two mandatory command-line parameters
    if ((pPcmAudio != NULL) && (pAudioUrl != NULL)) {
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

        if (success) {
            printf("Internet of Chuffs client starting.\nAudio PCM capture device is \"%s\", server is \"%s\"", pPcmAudio, pAudioUrl);
            if (pLogUrl != NULL) {
                printf(", log files from previous sessions will be uploaded to \"%s\"", pLogUrl);
            }
            if (pLogFilePath != NULL) {
                printf(", temporarily storing log files in directory \"%s\"", pLogFilePath);
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
            if (pLogUrl != NULL) {
                beginLogFileUpload(pLogUrl);
            }

            LOG(EVENT_SYSTEM_START, getUSeconds() / 1000000);
            LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

            // Start
            success = startAudioStreaming(pPcmAudio, pAudioUrl);
            if (success) {
                printf("Audio streaming is running, press CTRL-C to stop.\n");
                pause();
            } else {
                printf("Could not start audio streaming, ending.\n");
            }
            // Call the exit handler if we get here
            exitHandler(retValue);
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
