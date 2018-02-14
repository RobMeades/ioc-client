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

/** Initialise audio.
 *
 * @return  a pointer to the IocM2mAudio object.
 *          IMPORTANT: it is up to the caller to delete this
 *          object when done.
 */
bool initAudio();

/** Shut down audio.
 */
void deinitAudio();

/** Determing if audio streaming is enabled.
 * @return true if audio streaming is enabled else false.
 */
bool isAudioStreamingEnabled();

/** Get the minimum number of URTP datagrams that are free.
 * @return the low water mark of free datagrams.
 */
int getUrtpDatagramsFreeMin();

#endif // _AUDIO_

// End of file
