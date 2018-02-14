/* Copyright (c) 2017 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <utils.h>
#include <log.h>

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum length of a path (including trailing slash).
#define LOGGING_MAX_LEN_PATH 56

// The maximum length of a file name (including extension).
#define LOGGING_MAX_LEN_FILE_NAME 8

#define LOGGING_MAX_LEN_FILE_PATH (LOGGING_MAX_LEN_PATH + LOGGING_MAX_LEN_FILE_NAME)

// The maximum length of the URL of the logging server (including port).
#define LOGGING_MAX_LEN_SERVER_URL 128

// The TCP buffer size for log file uploads.
// Note: chose a small value here since the logs are small
// and it avoids a large malloc().
// Note: must be a multiple of a LogEntry size, otherwise
// the overhang can be lost
#define LOGGING_TCP_BUFFER_SIZE (20 * sizeof (LogEntry))

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Type used to pass parameters to the log file upload callback.
typedef struct {
    const char *pCurrentLogFile;
} LogFileUploadData;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// To obtain file system errors.
extern int errno;

// The strings associated with the enum values.
extern const char *gLogStrings[];
extern const int gNumLogStrings;

// Mutex to arbitrate logging.
// The callback which writes logging to disk
// will attempt to lock this mutex while the
// function that prints out the log owns the
// mutex. Note that the logging functions
// themselves shouldn't wait on it (they have
// no reason to as the buffering should
// handle any overlap); they MUST return quickly.
static std::mutex gLogMutex;

// The number of calls to writeLog().
static int gNumWrites = 0;

// A logging buffer.
static LogEntry *gpLog = NULL;
static LogEntry *gpLogNextEmpty = NULL;
static LogEntry const *gpLogFirstFull = NULL;

// A file to write logs to.
static FILE *gpFile = NULL;

// The path where log files are kept.
static char gLogPath[LOGGING_MAX_LEN_PATH + 1];

// The name of the current log file.
static char gCurrentLogFileName[LOGGING_MAX_LEN_FILE_PATH + 1];

// The address of the logging server.
static struct sockaddr_in *gpLoggingServer = NULL;

// A thread to run the log upload process.
static std::thread *gpLogUploadThread = NULL;

// A buffer to hold some data that is required by the
// log file upload thread.
static LogFileUploadData *gpLogFileUploadData = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print a single item from a log.
static void printLogItem(const LogEntry *pItem, unsigned int itemIndex)
{
    if (pItem->event > gNumLogStrings) {
        printf("%.3f: out of range event at entry %d (%d when max is %d)\n",
               (float) pItem->timestamp / 1000, itemIndex, pItem->event, gNumLogStrings);
    } else {
        printf ("%6.3f: %s [%d] %d (%#x)\n", (float) pItem->timestamp / 1000,
                gLogStrings[pItem->event], pItem->event, pItem->parameter, pItem->parameter);
    }
}

// Open a log file, storing its name in gCurrentLogFileName
// and returning a handle to it.
static FILE *newLogFile()
{
    FILE *pFile = NULL;

    for (unsigned int x = 0; (x < 1000) && (pFile == NULL); x++) {
        sprintf(gCurrentLogFileName, "%s/%04d.log", gLogPath, x);
        // Try to open the file to see if it exists
        pFile = fopen(gCurrentLogFileName, "r");
        // If it doesn't exist, use it, otherwise close
        // it and go around again
        if (pFile == NULL) {
            printf("Log file will be \"%s\".\n", gCurrentLogFileName);
            pFile = fopen (gCurrentLogFileName, "wb+");
            if (pFile != NULL) {
                LOG(EVENT_LOG_FILE_OPEN, 0);
            } else {
                LOG(EVENT_LOG_FILE_OPEN_FAILURE, errno);
                perror ("Error initialising log file");
            }
        } else {
            fclose(pFile);
            pFile = NULL;
        }
    }

    return pFile;
}

// Function to sit in a thread and upload log files.
static void logFileUploadCallback()
{
    DIR *pDir;
    int x = 0;
    int y;
    int z;
    struct dirent *pDirEnt;
    FILE *pFile = NULL;
    int sock;
    struct timeval tv;
    int sendCount;
    int sendTotalThisFile;
    int size;
    char *pReadBuffer = new char[LOGGING_TCP_BUFFER_SIZE];
    char fileNameBuffer[LOGGING_MAX_LEN_FILE_PATH];

    assert(gpLogFileUploadData != NULL);

    tv.tv_sec = 10;  /* 10 second timeout */

    LOG(EVENT_DIR_OPEN, 0);
    pDir = opendir(gLogPath);
    if (pDir != NULL) {
        // Send those log files, using a different TCP
        // connection for each one so that the logging server
        // stores them in separate files
        while ((pDirEnt = readdir(pDir)) != NULL) {
            // Open the file, provided it's not the one we're currently logging to
            if ((!strcmp(pDirEnt->d_name, ".") && !strcmp(pDirEnt->d_name, "..")) &&
                (pDirEnt->d_type == DT_REG) &&
                ((gpLogFileUploadData->pCurrentLogFile == NULL) ||
                 (strcmp(pDirEnt->d_name, gpLogFileUploadData->pCurrentLogFile) != 0))) {
                x++;
                LOG(EVENT_SOCKET_OPENING, y);
                sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock >= 0) {
                    LOG(EVENT_SOCKET_OPENED, x);
                    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(tv));
                    LOG(EVENT_TCP_CONNECTING, x);
                    y = connect(sock, (struct sockaddr *) gpLoggingServer, sizeof(struct sockaddr));
                    if (y >= 0) {
                        LOG(EVENT_TCP_CONNECTED, x);
                        LOG(EVENT_LOG_UPLOAD_STARTING, x);
                        sprintf(fileNameBuffer, "%s/%s", gLogPath, pDirEnt->d_name);
                        pFile = fopen(fileNameBuffer, "r");
                        if (pFile != NULL) {
                            LOG(EVENT_LOG_FILE_OPEN, 0);
                            sendTotalThisFile = 0;
                            do {
                                // Read the file and send it
                                size = fread(pReadBuffer, 1, LOGGING_TCP_BUFFER_SIZE, pFile);
                                sendCount = 0;
                                while (sendCount < size) {
                                    z = send(sock, pReadBuffer + sendCount, size - sendCount, 0);
                                    if (z > 0) {
                                        sendCount += z;
                                        sendTotalThisFile += z;
                                        LOG(EVENT_LOG_FILE_BYTE_COUNT, sendTotalThisFile);
                                    }
                                }
                            } while (size > 0);
                            LOG(EVENT_LOG_FILE_UPLOAD_COMPLETED, x);

                            // The file has now been sent, so close the socket
                            close(sock);

                            // If the upload succeeded, delete the file
                            if (feof(pFile)) {
                                if (remove(fileNameBuffer) == 0) {
                                    LOG(EVENT_FILE_DELETED, 0);
                                } else {
                                    LOG(EVENT_FILE_DELETE_FAILURE, 0);
                                }
                            }
                            LOG(EVENT_LOG_FILE_CLOSE, 0);
                            fclose(pFile);
                        } else {
                            LOG(EVENT_LOG_FILE_OPEN_FAILURE, errno);
                        }
                    } else {
                        LOG(EVENT_TCP_CONNECT_FAILURE, y);
                    }
                } else {
                    LOG(EVENT_SOCKET_OPENING_FAILURE, errno);
                }
            }
        }
    } else {
        LOG(EVENT_DIR_OPEN_FAILURE, (int) pDir);
    }

    LOG(EVENT_LOG_UPLOAD_TASK_COMPLETED, 0);
    printf("[Log file upload background task has completed]\n");

    // Clear up locals
    delete[] pReadBuffer;

    // Clear up globals
    delete gpLogFileUploadData;
    gpLogFileUploadData = NULL;
    delete gpLoggingServer;
    gpLoggingServer = NULL;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging.
void initLog(void *pBuffer)
{
    gpLog = (LogEntry *) pBuffer;
    gpLogNextEmpty = gpLog;
    gpLogFirstFull = gpLog;
    LOG(EVENT_LOG_START, LOG_VERSION);
}

// Initialise the log file.
bool initLogFile(const char *pPath)
{
    bool goodPath = true;
    int x;

    // Save the path
    if (pPath == NULL) {
        gLogPath[0] = 0;
    } else {
        if (strlen(pPath) < sizeof (gCurrentLogFileName) - LOGGING_MAX_LEN_FILE_NAME) {
            strcpy(gLogPath, pPath);
            x = strlen(gLogPath);
            // Remove any trailing slash
            if (gLogPath[x - 1] == '/') {
                gLogPath[x - 1] = 0;
            }
        } else {
            goodPath = false;
        }
    }

    if (goodPath) {
        gpFile = newLogFile();
    }

    return (gpFile != NULL);
}

// Upload previous log files.
bool beginLogFileUpload(const char *pLoggingServerUrl)
{
    bool success = false;
    char *pBuf = new char[LOGGING_MAX_LEN_SERVER_URL];
    DIR *pDir;
    struct dirent *pDirEnt;
    int port;
    int x;
    int y;
    int z = 0;
    const char * pCurrentLogFile = NULL;

    if (gpLogUploadThread == NULL) {
        // First, determine if there are any log files to be uploaded.
        LOG(EVENT_DIR_OPEN, 0);
        pDir = opendir(gLogPath);
        if (pDir != NULL) {
            // Point to the name portion of the current log file
            // (format "*/xxxx.log")
            pCurrentLogFile = strstr(gCurrentLogFileName, ".log");
            if (pCurrentLogFile != NULL) {
                pCurrentLogFile -= 4; // Point to the start of the file name
            }
            while ((pDirEnt = readdir(pDir)) != NULL) {
                // Open the file, provided it's not the one we're currently logging to
                if((!strcmp(pDirEnt->d_name, ".") && !strcmp(pDirEnt->d_name, "..")) &&
                    (pDirEnt->d_type == DT_REG) &&
                    ((pCurrentLogFile == NULL) || (strcmp(pDirEnt->d_name, pCurrentLogFile) != 0))) {
                    z++;
                }
            }

            LOG(EVENT_LOG_FILES_TO_UPLOAD, z);
            printf("[%d log file(s) to upload]\n", z);

            if (z > 0) {
                gpLoggingServer = new struct sockaddr_in;
                getAddressFromUrl(pLoggingServerUrl, pBuf, LOGGING_MAX_LEN_SERVER_URL);
                LOG(EVENT_DNS_LOOKUP, 0);
                printf("[Looking for logging server URL \"%s\"...]\n", pBuf);
                if (inet_pton(AF_INET, pBuf, gpLoggingServer) > 0) {
                    printf("[Found it at IP address %s]\n", inet_ntoa(gpLoggingServer->sin_addr));
                    if (getPortFromUrl(pLoggingServerUrl, &port)) {
                        gpLoggingServer->sin_port = port;
                        printf("[Logging server port is %d]\n", gpLoggingServer->sin_port);
                    } else {
                        printf("[WARNING: no port number was specified in the logging server URL (\"%s\")]\n",
                                pLoggingServerUrl);
                    }
                } else {
                    LOG(EVENT_DNS_LOOKUP_FAILURE, h_errno);
                    printf("[Unable to locate logging server \"%s\"]\n", pLoggingServerUrl);
                }

                gpLogFileUploadData = new LogFileUploadData();
                gpLogFileUploadData->pCurrentLogFile = pCurrentLogFile;
                // Note: this will be destroyed by the log file upload thread when it finishes
                gpLogUploadThread = new std::thread(logFileUploadCallback);
                if (gpLogUploadThread != NULL) {
                    printf("[Log file upload background task is now running]\n");
                    success = true;
                } else {
                    delete gpLogFileUploadData;
                    gpLogFileUploadData = NULL;
                    printf("[Unable to instantiate thread to upload files to logging server]\n");
                }
            } else {
                success = true; // Nothing to do
            }
        } else {
            LOG(EVENT_DIR_OPEN_FAILURE, x);
            printf("[Unable to open path \"%s\" (error %d)]\n", gLogPath, x);
        }
    } else {
        printf("[Log file upload task already running]\n");
    }
    delete[] pBuf;

    return success;
}

// Stop uploading previous log files, returning memory.
void stopLogFileUpload()
{
    if (gpLogUploadThread != NULL) {
        gpLogUploadThread->~thread();
        gpLogUploadThread->join();
        delete gpLogUploadThread;
        gpLogUploadThread = NULL;
    }

    if (gpLogFileUploadData != NULL) {
        delete gpLogFileUploadData;
        gpLogFileUploadData = NULL;
    }

    if (gpLoggingServer != NULL) {
        delete gpLoggingServer;
        gpLoggingServer = NULL;
    }
}

// Log an event plus parameter.
// Note: ideally we'd mutex in here but I don't
// want any overheads or any cause for delay
// so please just cope with any very occasional
// logging corruption which may occur
void LOG(LogEvent event, int parameter)
{
    if (gpLogNextEmpty) {
        gpLogNextEmpty->timestamp = getUSeconds();
        gpLogNextEmpty->event = (int) event;
        gpLogNextEmpty->parameter = parameter;
        if (gpLogNextEmpty < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
            gpLogNextEmpty++;
        } else {
            gpLogNextEmpty = gpLog;
        }

        if (gpLogNextEmpty == gpLogFirstFull) {
            // Logging has wrapped, so move the
            // first pointer on to reflect the
            // overwrite
            if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                gpLogFirstFull++;
            } else {
                gpLogFirstFull = gpLog;
            }
        }
    }
}

// Flush the log file.
// Note: log file mutex must be locked before calling.
void flushLog()
{
    if (gpFile != NULL) {
        fclose(gpFile);
        gpFile = fopen(gCurrentLogFileName, "ab+");
    }
}

// This should be called periodically to write the log
// to file, if a filename was provided to initLog().
void writeLog()
{
    if (gLogMutex.try_lock()) {
        if (gpFile != NULL) {
            gNumWrites++;
            while (gpLogNextEmpty != gpLogFirstFull) {
                fwrite(gpLogFirstFull, sizeof(LogEntry), 1, gpFile);
                if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                    gpLogFirstFull++;
                } else {
                    gpLogFirstFull = gpLog;
                }
            }
            if (gNumWrites > LOGGING_NUM_WRITES_BEFORE_FLUSH) {
                gNumWrites = 0;
                flushLog();
            }
        }
        gLogMutex.unlock();
    }
}

// Close down logging.
void deinitLog()
{
    stopLogFileUpload(); // Just in case

    LOG(EVENT_LOG_STOP, LOG_VERSION);
    if (gpFile != NULL) {
        writeLog();
        flushLog(); // Just in case
        LOG(EVENT_LOG_FILE_CLOSE, 0);
        fclose(gpFile);
        gpFile = NULL;
    }

    // Don't reset the variables
    // here so that printLog() still
    // works afterwards if we're just
    // logging to RAM rather than
    // to file.
}

// Print out the log.
void printLog()
{
    const LogEntry *pItem = gpLogNextEmpty;
    LogEntry fileItem;
    bool loggingToFile = false;
    FILE *pFile = gpFile;
    unsigned int x = 0;

    gLogMutex.lock();
    printf ("------------- Log starts -------------\n");
    if (pFile != NULL) {
        // If we were logging to file, read it back
        // First need to flush the file to disk
        loggingToFile = true;
        fclose(gpFile);
        gpFile = NULL;
        LOG(EVENT_LOG_FILE_CLOSE, 0);
        pFile = fopen(gCurrentLogFileName, "rb");
        if (pFile != NULL) {
            LOG(EVENT_LOG_FILE_OPEN, 0);
            while (fread(&fileItem, sizeof(fileItem), 1, pFile) == 1) {
                printLogItem(&fileItem, x);
                x++;
            }
            // If we're not at the end of the file, there must have been an error
            if (!feof(pFile)) {
                perror ("Error reading portion of log stored in file system");
            }
            fclose(pFile);
            LOG(EVENT_LOG_FILE_CLOSE, 0);
        } else {
            perror ("Error opening portion of log stored in file system");
        }
    }

    // Print the log items remaining in RAM
    pItem = gpLogFirstFull;
    x = 0;
    while (pItem != gpLogNextEmpty) {
        printLogItem(pItem, x);
        x++;
        pItem++;
        if (pItem >= gpLog + MAX_NUM_LOG_ENTRIES) {
            pItem = gpLog;
        }
    }

    // Allow writeLog() to resume with the same file name
    if (loggingToFile) {
        gpFile = fopen(gCurrentLogFileName, "ab+");
        if (gpFile) {
            LOG(EVENT_LOG_FILE_OPEN, 0);
        } else {
            LOG(EVENT_LOG_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    }

    printf ("-------------- Log ends --------------\n");
    gLogMutex.unlock();
}

// End of file
