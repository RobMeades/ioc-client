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

#ifndef _AUDIO_
#define _AUDIO_

/* ----------------------------------------------------------------
 * AUDIO TIMING MONITORING
 * -------------------------------------------------------------- */

/* In addition to the URTP uplink audio stream it is a requirement
 * of this code that the audio streaming server responds with
 * a downlink timing packet once per second in order that the
 * system delay can be monitored.
 * 
 * A downlink timing packet is formed as follows:
 * 
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 * --------------------------------------------------------
 *  0    |               Sync byte = 0x5A                |
 *  1    |               Sequence number MSB             |
 *  2    |               Sequence number LSB             |
 *  3    |                Timestamp MSB                  |
 *  4    |                Timestamp byte                 |
 *  5    |                Timestamp byte                 |
 *  6    |                Timestamp byte                 |
 *  7    |                Timestamp byte                 |
 *  8    |                Timestamp byte                 |
 *  9    |                Timestamp byte                 |
 *  10   |                Timestamp LSB                  |
 *
 * ...where :
 *
 * - Sync byte is always 0x5A, the same as the  SYNC_BYTE of
 *   the URTP protocol.
 * - Sequence number is the 16-bit sequence number from an uplink
 *   URTP datagram and...
 * - Timestamp is the 64-bit usecond timestamp copied out of the
 *   same URTP datagram.
 *
 * If no downlink timing packet is received within a given time then the
 * connection to the audio streaming server can be assumed to be lost.
 */

/** The length of a timing datagram. */
#define AUDIO_TIMING_DATAGRAM_LENGTH 11

/** The maximium age of a timing datagram in seconds. */
#define AUDIO_TIMING_DATAGRAM_AGE_S 15

/** The number of seconds to wait for any one timing datagram. */
#define AUDIO_TIMING_DATAGRAM_WAIT_S 5

/** The number of seconds to wait for the connection to the audio
 * streaming server to establish. */
#define AUDIO_SERVER_LINK_ESTABLISHMENT_WAIT_S 5

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Start audio streaming.
 * @param pAlsaPcmDeviceName   the name of the ALSA PCM device to stream
 *                             from (must be 32 bits per channel, stereo,
 *                             16 kHz sample rate).
 * @param maxShift             the maximum audio shift (gain) to apply,
 *                             see urtp.h for the valid range.
 * @param pAudioServerUrl      the URL of the server to stream at.
 * @param pWatchdogHandler     pointer to the watchdog handler, NULL if none is active.
 * @param pNowStreamingHandler pointer to a "I'm streaming" handler which should be called
 *                             frequently (e.g. every transmit) to show activity; may be
 *                             NULL.
 * @return                     true if succesful, else false.
 */
bool startAudioStreaming(const char *pAlsaPcmDeviceName,
                         const char *pAudioServerUrl,
                         int maxShift,
                         void(*pWatchdogHandler)(void),
                         void(*pNowStreamingHandler)(void));

/** Shut down audio streaming.
 */
void stopAudioStreaming();

/** Return whether audio is streaming or not.
 * @return true if audio is streaming, else false.
 */
bool audioIsStreaming();

#endif // _AUDIO_

// End of file
