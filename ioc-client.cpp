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
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <compile_time.h>
#include <utils.h>
#include <audio.h>
#include <log.h>

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Things to help with parsing filenames.
#define DIR_SEPARATORS "\\/"
#define EXT_SEPARATOR "."

// The default location for log files
#define DEFAULT_LOG_FILE_PATH "~/logtmp"

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// For logging.
static char gLogBuffer[LOG_STORE_SIZE];

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print the usage text
static void printUsage(char * pExeName) {
    printf("\n%s: run the Internet of Chuffs client.  Usage:\n", pExeName);
    printf("    %s audio_source server_url <-p local_port> <-l log_directory>\n\n", pExeName);
    printf("where:\n");
    printf("    audio_source is the name of the ALSA PCM audio capture device (must be 32 bits per channel, stereo, 16 kHz sample rate),\n");
    printf("    server_url is the URL of the Internet of Chuffs server,\n");
    printf("    -p optionally specifies the local port to bind to,\n");
    printf("    -l optionally specifies the temporary directory to use for log files (default %s); the directory will be created if it does not exist,\n", DEFAULT_LOG_FILE_PATH);
    printf("For example:\n");
    printf("    %s mic io-server.co.uk:1297 -p 5065 -l /var/log\n\n", pExeName);
}

// Signal handler for CTRL-C
static void exitHandler(int signal)
{
    printf("Stopping.\n");
    stopAudioStreaming();
    exit(0); 
}

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

// Main.
int main(int argc, char *argv[])
{
    int retValue = -1;
    bool success = true;
    int x = 0;
    char *pExeName = NULL;
    char *pPcmAudio = NULL;
    char *pUrl = NULL;
    int localPort = -1;
    const char *pLogFilePath = NULL;
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
    while (success && (x < argc)) {
        // Test for PCM audio device
        if (x == 1) {
            pPcmAudio = argv[x];
        // Test for server URL
        } else if (x == 2) {
            pUrl = argv[x];
        // Test for port option
        } else if (strcmp(argv[x], "-p") == 0) {
            x++;
            if (x < argc) {
                localPort = atoi(argv[x]);
            }
        // Test for log directory option
        } else if (strcmp(argv[x], "-l") == 0) {
            x++;
            if (x < argc) {
                pLogFilePath = argv[x];
                // Check if the directory exists
                if (stat(pLogFilePath, &st) == -1) {
                    // If it doesn't exist create it
                    if (mkdir(pLogFilePath, 0700) != 0) {
                        success = false;
                        printf("Unable to create directory temporary log file directory %s.", pLogFilePath);
                    }
                }
            }
        }
        x++;
    }
    
    // Must have no errors and the two mandatory command-line parameters
    if (success && (pPcmAudio != NULL) && (pUrl != NULL)) {
        if (pLogFilePath == NULL) {
            pLogFilePath = DEFAULT_LOG_FILE_PATH;
        } 
        printf("Internet of Chuffs client starting. Audio PCM capture device is %s, server is %s", pPcmAudio, pUrl);
        if (localPort >= 0) {
            printf(", binding to local port %d", localPort);
        }
        if (pLogFilePath != NULL) {
            printf(", temporarily storing log files in %s", pLogFilePath);
        }
        printf(".\n");
        
        // Set up the CTRL-C handler
        sigIntHandler.sa_handler = exitHandler;
        sigemptyset(&sigIntHandler.sa_mask);
        sigIntHandler.sa_flags = 0;
        sigaction(SIGINT, &sigIntHandler, NULL);

        // Initialise logging
        initLog(gLogBuffer);
        initLogFile(pLogFilePath);
        
        LOG(EVENT_SYSTEM_START, getUSeconds() / 1000000);
        LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);
        
        // Start
        if (startAudioStreaming(pPcmAudio, pUrl)) {
            printf("Audio streaming is running, press CTRL-C to stop.\n");
        }
    } else {
        printUsage(pExeName);
        success = false;
    }

    if (success) {
        retValue = 0;
    }
    
    return retValue;
}

// End of file
