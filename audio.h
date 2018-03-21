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
