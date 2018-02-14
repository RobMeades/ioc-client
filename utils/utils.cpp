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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utils.h>

/* This file contains some general utility functions.
 */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Get the uSecond system time.
long long int getUSeconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((long long int) ts.tv_sec * 1000000000L + ts.tv_nsec) / 1000;
}

// Get the address portion of a URL, leaving off the port number etc.
void getAddressFromUrl(const char *pUrl, char * pAddressBuf, int lenBuf)
{
    const char *pPortPos;
    int lenUrl;

    if (lenBuf > 0) {
        // Check for the presence of a port number
        pPortPos = strchr(pUrl, ':');
        if (pPortPos != NULL) {
            // Length wanted is up to and including the ':'
            // (which will be overwritten with the terminator)
            if (lenBuf > pPortPos - pUrl + 1) {
                lenBuf = pPortPos - pUrl + 1;
            }
        } else {
            // No port number, take the whole thing
            // including the terminator
            lenUrl = strlen (pUrl);
            if (lenBuf > lenUrl + 1) {
                lenBuf = lenUrl + 1;
            }
        }
        memcpy(pAddressBuf, pUrl, lenBuf);
        *(pAddressBuf + lenBuf - 1) = 0;
    }
}

// Get the port number from the end of a URL.
bool getPortFromUrl(const char *pUrl, int *pPort)
{
    bool success = false;
    const char *pPortPos = strchr(pUrl, ':');

    if (pPortPos != NULL) {
        *pPort = atoi(pPortPos + 1);
        success = true;
    }

    return success;
}

// End of file
