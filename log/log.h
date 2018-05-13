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

/* This logging utility allows events to be logged to RAM and, optionally
 * to file at minimal run-time cost.
 *
 * Each entry includes an event, a 32 bit parameter (which is printed with
 * the event) and a microsecond time-stamp.
 */

#include "stdbool.h"
#include "stdint.h"
#include "log_enum.h"

#ifndef _LOG_
#define _LOG_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The number of log entries (must be 1 or greater).
 */
#ifndef MAX_NUM_LOG_ENTRIES
# define MAX_NUM_LOG_ENTRIES 100000
#endif

// Increase this from 1 to skip flushing on file writes if the
// processor load of writing the log file is too high.
#ifndef LOGGING_NUM_WRITES_BEFORE_FLUSH
# define LOGGING_NUM_WRITES_BEFORE_FLUSH 1
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** An entry in the log.
 */
typedef struct {
    uint64_t timestamp;
    uint32_t event;  // This will be LogEvent but it is stored as an int
                     // so that we are guaranteed to get a 32-bit value,
                     // making it easier to decode logs on another platform
    uint32_t parameter;
} LogEntry;


/** The size of the log store, given the number of entries requested.
 */
#define LOG_STORE_SIZE (sizeof (LogEntry) * MAX_NUM_LOG_ENTRIES)

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/** Log an event plus parameter.
 *
 * @param event     the event.
 * @param parameter the parameter.
 */
void LOG(LogEvent event, int parameter);

/** Initialise logging.
 *
 * @param pBuffer        must point to LOG_STORE_SIZE bytes of storage.
 * @param pUSecondTimeCb callback to obtain usecond time.
 */
void initLog(void *pBuffer);

/** Start logging to file.
 *
 * @param pPath the path at which to create the log files.
 * @return      true if successful, otherwise false.
 */
bool initLogFile(const char *pPath);

/** Begin upload of log files to a logging server.
 *
 * @param pLoggingServerUrl the logging server to connect to
 *                          (including port number).
 * @return                  true if log uploading begins successfully,
 *                          otherwise false.
 */
bool beginLogFileUpload(const char *pLoggingServerUrl);

/** Stop uploading log files to the logging server and free resources.
 */
void stopLogFileUpload();

/** Close down logging.
 */
void deinitLog();

/** Write the logging buffer to the log file.
 * @param the ID of the timer calling this callback.
 * @param the user data pointer (passed in by timer, not actually used).
 */
void writeLogCallback(size_t timerId, void *pUserData);

/** Print out the logged items.
 */
void printLog();

#ifdef __cplusplus
}
#endif

#endif

// End of file
