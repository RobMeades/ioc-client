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

#ifndef _UTILS_
#define _UTILS_

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Get the uSecond system time (UTC).
 * @return the uSecond system time (UTC).
 */
long long int getUSeconds(void);

/** Get the address portion of a URL, leaving off the port number etc.
 * @param pUrl         the URL.
 * @param pAddressBuf  the output buffer.
 * @param lenBuf       the length of the output buffer.
 */
void getAddressFromUrl(const char *pUrl, char * pAddressBuf, int lenBuf);

/** Get the port number from the end of a URL.
 * @param pUrl      the URL.
 * @param pPort     a place to put the port number.
 * @return          true if something was written to pPort, else false.
 */
bool getPortFromUrl(const char *pUrl, int *pPort);

#endif // _UTILS_

// End of file
