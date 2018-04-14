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
 * AUDIO "HELLO" EXCHANGE
 * -------------------------------------------------------------- */

/* The audio stream checks that the audio streaming server is
 * connected by sending at a "Hello Request" UDP packet periodically.
 * A Hello Request is formed as follows
 * 
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 * --------------------------------------------------------
 *  0    |               Sync byte = 0xAE                |
 *  1    |               Sequence number                 |
 *  2    |                Timestamp MSB                  |
 *  3    |                Timestamp byte                 |
 *  4    |                Timestamp byte                 |
 *  5    |                Timestamp byte                 |
 *  6    |                Timestamp byte                 |
 *  7    |                Timestamp byte                 |
 *  8    |                Timestamp byte                 |
 *  9    |                Timestamp LSB                  |
 *
 * ...where :
 *
 * - Sync byte is always 0xAE, intended to be different from
 *   the SYNC_BYTE of the URTP protocol.
 * - Sequence number is an incremeting 8-bit counter.
 * - Timestamp is a UTC uSecond timestamp representing the moment
 *   the Hello Request was created.
 *   
 * The audio streaming server should respond by send a Hello
 * Response, which is formed as follows:
 * 
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 * --------------------------------------------------------
 *  0    |               Sync byte = 0xAE                |
 *  1    |               Sequence number                 |
 *  2    |              Req Timestamp MSB                |
 *  3    |              Req Timestamp byte               |
 *  4    |              Req Timestamp byte               |
 *  5    |              Req Timestamp byte               |
 *  6    |              Req Timestamp byte               |
 *  7    |              Req Timestamp byte               |
 *  8    |              Req Timestamp byte               |
 *  9    |              Req Timestamp LSB                |
 *  10   |                Timestamp MSB                  |
 *  11   |                Timestamp byte                 |
 *  12   |                Timestamp byte                 |
 *  13   |                Timestamp byte                 |
 *  14   |                Timestamp byte                 |
 *  15   |                Timestamp byte                 |
 *  16   |                Timestamp byte                 |
 *  17   |                Timestamp LSB                  |
 *
 * ...where:
 *
 * - Sync byte, Sequence number and Req Timestamp are copied
 *   from the Hello Request.
 * - Timestamp is the UTC uSecond timestamp representing the moment
 *   the Hello Request was received.
 *
 * If no Hello Response is received within a given time then the
 * connection to the audio streaming server can be assumed to be lost.
 */

/** The length of a Hello Request datagram. */
#define AUDIO_HELLO_REQUEST_LENGTH 10

/** The length of a Hello Response datagram. */
#define AUDIO_HELLO_RESPONSE_LENGTH (AUDIO_HELLO_REQUEST_LENGTH + 8)

/** The sync byte at the start of an audio hello message. */
#define AUDIO_HELLO_SYNC_BYTE 0xae

/** The number of seconds to wait for a response to a Hello Request. */
#define AUDIO_HELLO_RESPONSE_WAIT_S 10

/** The number of seconds to wait for the connection to the audio
 * streaming server to establish. */
#define AUDIO_SERVER_LINK_ESTABLISHMENT_WAIT_S 3

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Start audio streaming.
 * @param pAlsaPcmDeviceName   the name of the ALSA PCM device to stream
 *                             from (must be 32 bits per channel, stereo,
 *                             16 kHz sample rate).
 * @param pAudioServerUrl      the URL of the server to stream at.
 * @param pWatchdogHandler     pointer to the watchdog handler, NULL if none is active.
 * @param pNowStreamingHandler pointer to a "I'm streaming" handler which should be called
 *                             frequently (e.g. every transmit) to show activity; may be
 *                             NULL.
 * @return                     true if succesful, else false.
 */
bool startAudioStreaming(const char *pAlsaPcmDeviceName,
                         const char *pAudioServerUrl,
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
