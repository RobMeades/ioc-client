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

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <utils.h>
#include <time.h>
#include <urtp.h>

#ifdef ENABLE_RAMLOG
#include <log.h>
#else
#define LOG(x, y) 
#endif

// For testing only: define this to write the captured
// and gain controlled audio data to file.
// Format is little-endian (on a Pi) signed 32-bit mono
// @ 16000 Hz.
//#define URTP_TEST_AUDIO_OUTPUT_FILENAME "/home/rob/audio_processed.pcm"

// For testing only: define this to write the encoded
// URTP output to file.
//#define URTP_TEST_URTP_OUTPUT_FILENAME "/home/rob/audio_encoded.urtp"

#if defined (URTP_TEST_AUDIO_OUTPUT_FILENAME) || defined (URTP_TEST_URTP_OUTPUT_FILENAME)
#include <stdio.h>
#include <string.h>
#include <errno.h>
#endif

/**********************************************************************
 * STATIC VARIABLES
 **********************************************************************/

#ifdef ENABLE_STREAM_FIXED_TONE
/** Diagnostics: a 400 Hz sine wave as a little-endian 24 bit signed PCM stream sampled at 16 kHz, generated by Audacity and
 * then sign extended to 32 bits per sample.  Be careful when using this: since it exactly fits into a single audio block any
 * missing audio blocks will go unnoticed.
 */
static const int pcm400HzSigned24Bit[] = {(int) 0x00000000L, (int) 0x001004d5L, (int) 0x001fa4b2L, (int) 0x002e7d16L, (int) 0x003c3070L, (int) 0x00486861L, (int) 0x0052d7e5L, (int) 0x005b3d33L, (int) 0x00616360L, (int) 0x006523a8L,
                                          (int) 0x00666666L, (int) 0x006523a8L, (int) 0x00616360L, (int) 0x005b3d33L, (int) 0x0052d7e5L, (int) 0x00486861L, (int) 0x003c3070L, (int) 0x002e7d16L, (int) 0x001fa4b2L, (int) 0x001004d5L,
                                          (int) 0x00000000L, (int) 0xffeffb2aL, (int) 0xffe05b4eL, (int) 0xffd182e9L, (int) 0xffc3cf90L, (int) 0xffb7979eL, (int) 0xffad281bL, (int) 0xffa4c2ccL, (int) 0xff9e9ca0L, (int) 0xff9adc57L,
                                          (int) 0xff999999L, (int) 0xff9acd57L, (int) 0xff9e9ca0L, (int) 0xffa4c2ccL, (int) 0xffad281bL, (int) 0xffb7979eL, (int) 0xffc3cf90L, (int) 0xffd182e9L, (int) 0xffe05beeL, (int) 0xffeffb2aL};

/** Diagnostics: index into pcm400HzSigned24Bit.
 */
static unsigned int toneIndex = 0;
#endif

#ifdef ENABLE_RAMP_TEST
/** Diagnostics: use this to generate a triangle wave that goes from minimum to
 * maximum amplitude in order to check for discontinuities in the NICAM codec.
 * Each sample is replaced with one increment of testValue, so a 32 bit modulo
 * with an increment of 10000 makes for a full wave in about a minute at 16 kHz.
 * However, when encoding unicam such an unrealistically large jump between
 * adjacent numbers doesn't work so the increment is only performed for
 * each unicam block and hence the increment at that point must be larger if
 * the test isn't going to take ages.
 * When running a ramp test automatic gain control is switched off as it makes
 * a mess of the ramp.
 * You can switch on URTP_TEST_AUDIO_OUTPUT_FILENAME with this in place, do the same
 * capture of the decoded data at the server and then check that the two
 * waveforms track each other reasonably well.
 */
#define TEST_MODULO 0x7FFFFFFFL
# ifdef DISABLE_UNICAM
#  define TEST_INCREMENT 10000
# else
#  define TEST_INCREMENT 20000 // larger for Unicam as we only increment once per block
# endif
static long long testValue = 0;
static int testIncrement = TEST_INCREMENT;
#endif

#ifdef URTP_TEST_AUDIO_OUTPUT_FILENAME
static FILE *urtpTestAudioOutputFile = NULL;
#endif

#ifdef URTP_TEST_URTP_OUTPUT_FILENAME
static FILE *urtpTestUrtpOutputFile = NULL;
#endif

/**********************************************************************
 * PRIVATE METHODS
 **********************************************************************/

// Take an audio sample and from it produce a signed
// output that uses the maximum number of bits
// in a 32 bit word.
int Urtp::processAudio(int monoSample)
{
    int unusedBits = 0;
    int absSample = monoSample;

    //LOG(EVENT_STREAM_MONO_SAMPLE_DATA, monoSample);

    // First, determine the number of unused bits
    // (avoiding testing the top bit since that is
    // never unused)
    if (absSample < 0) {
        absSample = -absSample;
    }

    for (int x = 30; x >= 0; x--) {
        if (absSample & (1 << x)) {
            break;
        } else {
            unusedBits++;
        }
    }

    //LOG(EVENT_MONO_SAMPLE_UNUSED_BITS, unusedBits);

    if (absSample > AUDIO_SHIFT_THRESHOLD) {
        monoSample <<= _audioShift;
    }

    // Update the minimum number of unused bits
    if (unusedBits < _audioUnusedBitsMin) {
        _audioUnusedBitsMin = unusedBits;
    }
    _audioShiftSampleCount++;
    // If we've had a block's worth of data, work out how much gain we may be
    // able to apply for the next period
    if (_audioShiftSampleCount >= SAMPLING_FREQUENCY / (1000 / BLOCK_DURATION_MS)) {
        _audioShiftSampleCount = 0;
        //LOG(EVENT_MONO_SAMPLE_UNUSED_BITS_MIN, _audioUnusedBitsMin);
        if (_audioShift > _audioUnusedBitsMin) {
            _audioShift = _audioUnusedBitsMin;
        }
        if ((_audioUnusedBitsMin - _audioShift > (AUDIO_DESIRED_UNUSED_BITS + AUDIO_SHIFT_HYSTERESIS_BITS)) && (_audioShift < _audioShiftMax)) {
            // An increase in gain is noted here but not applied immediately in order to do
            // some smoothing.  Instead a note is kept of the last N audio shifts and
            // only if it persists is the gain increased.
            _audioUpShiftCount++;
            if (_audioUpShiftCount > AUDIO_NUM_UP_SHIFTS_FOR_A_SHIFT) {
                _audioShift++;
                _audioUpShiftCount = 0;
                LOG(EVENT_MONO_SAMPLE_AUDIO_SHIFT, _audioShift);
            }
        } else if ((_audioUnusedBitsMin - _audioShift < AUDIO_DESIRED_UNUSED_BITS) && (_audioShift > 0)) {
            // A reduction in gain must happen immediately to avoid clipping
            _audioShift--;
            _audioUpShiftCount = 0;
            LOG(EVENT_MONO_SAMPLE_AUDIO_SHIFT, _audioShift);
        }

        // Increment the minimum number of unused bits in the period
        // to let the number "relax"
        _audioUnusedBitsMin++;
    }

    //LOG(EVENT_STREAM_MONO_SAMPLE_PROCESSED_DATA, monoSample);

    return monoSample;
}

// Take a stereo sample in our usual form
// and return an int containing a sample
// that is MONO_INPUT_SAMPLE_SIZE
// but sign extended so that it can be
// treated as an int for maths purposes.
inline int Urtp::getMonoSample(const uint32_t *stereoSample)
{
    const char * pByte = (const char *) stereoSample;
    unsigned int retValue = 0;

    // LSB
    retValue = (unsigned int) *(pByte + 1);
    // Middle byte
    retValue += ((unsigned int) *(pByte + 2)) << 8;
    // MSB
    retValue += ((unsigned int) *(pByte + 3)) << 16;
    // Sign extend
    if (retValue & 0x800000) {
        retValue |= 0xFF000000;
    }

#ifdef ENABLE_STREAM_FIXED_TONE
    retValue = pcm400HzSigned24Bit[toneIndex];
    toneIndex++;
    if (toneIndex >= sizeof (pcm400HzSigned24Bit) / sizeof (pcm400HzSigned24Bit[0])) {
        toneIndex = 0;
    }
#endif

    return (int) retValue;
}

// Encode UNICAM_COMPRESSED_x_BIT.
int Urtp::codeUnicam(const uint32_t *rawAudio, char *dest)
{
    int monoSample;
    int absSample;
    int maxSample = 0;
    int numBytes = 0;
    int numBlocks = 0;
    unsigned int i = 0;
    int usedBits;
    int shiftValueCoded;
    bool isEvenBlock = false;
    char *pDestOriginal = dest;

    for (const uint32_t *stereoSample = rawAudio; stereoSample < rawAudio + (SAMPLES_PER_BLOCK * 2); stereoSample += 2) {

        //LOG(EVENT_RAW_AUDIO_DATA_0, *stereoSample);
        //LOG(EVENT_RAW_AUDIO_DATA_1, *(stereoSample + 1));

        monoSample = getMonoSample(stereoSample);
        monoSample = processAudio(monoSample);

#ifdef ENABLE_RAMP_TEST
        monoSample = (int) testValue;
#endif
        // Scale the sample down to the maximum size we want the
        // decoder to derive
        monoSample >>= (32 - UNICAM_MAX_DECODED_SAMPLE_SIZE_BITS);

        // Add the _preemphasis
        firPut(&_preemphasis, (double) monoSample);
        monoSample = (int) firGet(&_preemphasis);
        
#ifdef URTP_TEST_AUDIO_OUTPUT_FILENAME
        if (urtpTestAudioOutputFile != NULL) {
            fwrite(&monoSample, sizeof(monoSample), 1, urtpTestAudioOutputFile);
        }
#endif
        // Track the max abs value
        absSample = monoSample;
        if (absSample < 0) {
            absSample = -absSample;
        }
        if (absSample > maxSample) {
            maxSample = absSample;
        }

        // Put the sample into the unicam buffer
        _unicamBuffer[i] = monoSample;
        i++;

        // Check if we have a unicam block's worth ready to go
        if (i >= sizeof (_unicamBuffer) / sizeof (_unicamBuffer[0])) {
            i = 0;
            shiftValueCoded = 0;
            usedBits = 32;

#ifdef ENABLE_RAMP_TEST
            // Only increment once per unicam block during
            // ramp testing as the increment value can be too
            // large for it to cope
            testValue += testIncrement;
            if (testValue >= TEST_MODULO) {
                testIncrement = -TEST_INCREMENT;
                testValue += testIncrement;
                testValue--;    // Add a little wiggle to avoid repeats
            } else if (testValue <= -TEST_MODULO) {
                testIncrement = TEST_INCREMENT;
                testValue += testIncrement;
                testValue++;    // Add a little wiggle to avoid repeats
            }
#endif

            //LOG(EVENT_UNICAM_MAX_ABS_VALUE, maxSample);

            // Once we have a buffer full, work out the shift value
            // to just fit the maximum value into 8 bits.  First
            // find the number of bits used (avoid testing the top
            // bit since that is always used)
            for (int x = 30; x >= 0; x--) {
                if ((maxSample & (1 << x)) != 0) {
                    break;
                } else {
                    usedBits--;
                }
            }
            maxSample = 0;

           //LOG(EVENT_UNICAM_MAX_VALUE_USED_BITS, usedBits);

            // We have a block of 32 bit samples (scaled down to
            // UNICAM_MAX_DECODED_SAMPLE_SIZE_BITS) and we know what the 
            // maximum number of used bits per sample are in the block.  If
            // the number of used bits is bigger than 8 bits then add the
            // shift value.  With a UNICAM_MAX_DECODED_SAMPLE_SIZE_BITS of
            // 16 the shifValueCoded will never be greater than 8.
            if (usedBits > UNICAM_CODED_SAMPLE_SIZE_BITS) {
                shiftValueCoded = usedBits - UNICAM_CODED_SAMPLE_SIZE_BITS;
            }
            //LOG(EVENT_UNICAM_CODED_SHIFT_VALUE, shiftValueCoded);

            isEvenBlock = false;
            if ((numBlocks & 1) == 0) {
                isEvenBlock = true;
            }

            // If we're on an odd block, the shift value goes into the
            // upper nibble of the shift byte, which is where the dest
            // pointer will already be pointed at, with nibble
            // already zeroed for us
            if (!isEvenBlock) {
                *dest |= shiftValueCoded << 4;
                //LOG(EVENT_UNICAM_CODED_SHIFTS_BYTE, *dest);
                // Now move the dest pointer on to the start of the
                // unicam data
                dest++;
            }

            // Write into the output all the values in the buffer shifted down by this amount
            for (unsigned int x = 0; x < sizeof (_unicamBuffer) / sizeof (_unicamBuffer[0]); x++) {
                //LOG(EVENT_UNICAM_SAMPLE, _unicamBuffer[x]);
                *dest = _unicamBuffer[x] >> shiftValueCoded;
                //LOG(EVENT_UNICAM_COMPRESSED_SAMPLE, *dest);
                dest++;
            }

            // If we're on an even block number the shift value goes into
            // the lower nibble of the shift byte that follows the unicam block
            // and we don't increment the dest pointer so that the shift value
            // for the next block can be written in the upper nibble
            if (isEvenBlock) {
                *dest = shiftValueCoded & 0x0F;
            }

            numBlocks++;
        }
    }

    numBytes = dest - pDestOriginal;
    if (isEvenBlock) {
        numBytes++;
    }

    //LOG(EVENT_UNICAM_BLOCKS_CODED, numBlocks);
    //LOG(EVENT_UNICAM_BYTES_CODED, numBytes);

    return numBytes;
}

// Encode PCM_SIGNED_16_BIT.
int Urtp::codePcm(const uint32_t *rawAudio, char *dest)
{
    int monoSample;
    int numSamples = 0;

    for (const uint32_t *stereoSample = rawAudio; stereoSample < rawAudio + (SAMPLES_PER_BLOCK * 2); stereoSample += 2) {

        //LOG(EVENT_RAW_AUDIO_DATA_0, *stereoSample);
        //LOG(EVENT_RAW_AUDIO_DATA_1, *(stereoSample + 1));

        monoSample = getMonoSample(stereoSample);
        monoSample = processAudio(monoSample);
        numSamples++;

#ifdef ENABLE_RAMP_TEST
        monoSample = (int) testValue;
        testValue += testIncrement;
        if (testValue >= TEST_MODULO) {
            testIncrement = -TEST_INCREMENT;
            testValue += testIncrement;
            testValue--;   // Add a little wiggle to avoid repeats
        } else if (testValue <= -TEST_MODULO) {
            testIncrement = TEST_INCREMENT;
            testValue += testIncrement;
            testValue++;    // Add a little wiggle to avoid repeats
        }
#endif

#ifdef URTP_TEST_AUDIO_OUTPUT_FILENAME
        if (urtpTestAudioOutputFile != NULL) {
            fwrite(&monoSample, sizeof(monoSample), 1, urtpTestAudioOutputFile);
        }
#endif

        *dest = (char) (monoSample >> 24);
        dest++;
#if URTP_SAMPLE_SIZE > 1
        *dest = (char) (monoSample >> 16);
        dest++;
#endif
#if URTP_SAMPLE_SIZE > 2
        *dest = (char) (monoSample >> 8);
        dest++;
#endif
#if URTP_SAMPLE_SIZE > 3
        *dest = (char) monoSample;
        dest++;
#endif
    }

    //LOG(EVENT_DATAGRAM_NUM_SAMPLES, numSamples);

    return numSamples * URTP_SAMPLE_SIZE;
}

// Fill a datagram with the audio from one block.
void Urtp::fillMonoDatagramFromBlock(const uint32_t *rawAudio)
{
    Container * container = getContainerForWriting();
    char * datagram = (char *) container->contents;
    long long int timestamp = getUSeconds();
    int numBytesAudio = 0;

    // Copy in the body ASAP in case we're called from
    // DMA, which might catch up with us
#ifndef DISABLE_UNICAM
    numBytesAudio = codeUnicam (rawAudio, datagram + URTP_HEADER_SIZE);
#else
    numBytesAudio = codePcm (rawAudio, datagram + URTP_HEADER_SIZE);
#endif
    // Fill in the header
    *datagram = SYNC_BYTE;
    datagram++;
#ifndef DISABLE_UNICAM
# if UNICAM_CODED_SAMPLE_SIZE_BITS == 8
    *datagram = UNICAM_COMPRESSED_8_BIT;
# else
# error "Only 8 bit unicam is supported"
# endif
#else
    *datagram = PCM_SIGNED_16_BIT;
#endif
    datagram++;
    *datagram = (char) (_sequenceNumber >> 8);
    datagram++;
    *datagram = (char) _sequenceNumber;
    datagram++;
    _sequenceNumber++;
    *datagram = (char) (timestamp >> 56);
    datagram++;
    *datagram = (char) (timestamp >> 48);
    datagram++;
    *datagram = (char) (timestamp >> 40);
    datagram++;
    *datagram = (char) (timestamp >> 32);
    datagram++;
    *datagram = (char) (timestamp >> 24);
    datagram++;
    *datagram = (char) (timestamp >> 16);
    datagram++;
    *datagram = (char) (timestamp >> 8);
    datagram++;
    *datagram = (char) timestamp;
    datagram++;
    *datagram = (char) (numBytesAudio >> 8);
    datagram++;
    *datagram = (char) numBytesAudio;
    datagram++;

    //LOG(EVENT_DATAGRAM_SIZE, datagram - (char *)container->contents + numBytesAudio);

#ifdef URTP_TEST_URTP_OUTPUT_FILENAME
    if (urtpTestUrtpOutputFile != NULL) {
        fwrite(container->contents, datagram - (char *)container->contents + numBytesAudio, 1, urtpTestUrtpOutputFile);
    }
#endif

    // The container is now ready to read
    setContainerAsReadyToRead(container);
}

// Test that right shift is an arithmetic operation
bool Urtp::unicamTest()
{
    int negative = -1;
    return (negative >> 1) < 0;
}

// Get the next container for writing.
inline Urtp::Container * Urtp::getContainerForWriting()
{
    Container * container = _containerNextForWriting;

    // In normal circumstances one would hope that the next container
    // for writing is empty.  However, it may be that the read
    // routine is running behind, in which case it could be in state
    // READY_TO_READ or, worse, state READING.  If it is in state
    // READY_TO_READ, then it can simply be overwritten with new data,
    // but if it is in state READING we must leave it alone and move
    // on or we will could corrupt the callers work.  There should
    // only ever be one container in state READING, so the for() loop
    // isn't strictly necessary, it is simply there to be safe.
    for (unsigned int x = 0; (container->state == CONTAINER_STATE_READING) &&
                             (x < sizeof (_container) / sizeof (_container[0])); x++) {
        _containerNextForWriting = container->next;
    }

    // Move the write pointer on
    _containerNextForWriting = container->next;

    if (container->state == CONTAINER_STATE_EMPTY) {
        _numDatagramsFree--;
        //LOG(EVENT_NUM_DATAGRAMS_FREE, _numDatagramsFree);
        if (_numDatagramsFree < _minNumDatagramsFree) {
            _minNumDatagramsFree = _numDatagramsFree;
        }
        if (_numDatagramOverflows > 0) {
            LOG(EVENT_DATAGRAM_NUM_OVERFLOWS, _numDatagramOverflows);
            _numDatagramOverflows = 0;
            if (_datagramOverflowStopCb) {
                _datagramOverflowStopCb(_numDatagramOverflows);
            }
        }
    } else {
        // If the container we're about to use is not empty, we're overwriting
        // old data.  To avoid the read pointer wrapping the write pointer,
        // nudge the read pointer on by one
        _containerNextForReading = _containerNextForReading->next;
        if (_numDatagramOverflows == 0) {
            LOG(EVENT_DATAGRAM_OVERFLOW_BEGINS, (int) container);
            if (_datagramOverflowStartCb) {
                _datagramOverflowStartCb();
            }
        }
        _numDatagramOverflows++;
    }
    container->state = CONTAINER_STATE_WRITING;
    //LOG(EVENT_CONTAINER_STATE_WRITING, (int) container);

    return container;
}

// Set the given container as ready to read.
// Must have been writing to this container for it to be now ready to read.
inline void Urtp::setContainerAsReadyToRead(Urtp::Container * container)
{
    assert(container->state == CONTAINER_STATE_WRITING);
    container->state = CONTAINER_STATE_READY_TO_READ;
    //LOG(EVENT_CONTAINER_STATE_READY_TO_READ, (int) container);

    // Tell the callback that the contents are ready for reading
    if (_datagramReadyCb) {
        _datagramReadyCb((const char *) container->contents);
    }
}

// Get the next container for reading.
// Only if the next container for reading is ready to read, or
// is already being read, can it be returned, otherwise
// NULL is returned
inline Urtp::Container * Urtp::getContainerForReading()
{
    Container * container = _containerNextForReading;

    if ((container->state == CONTAINER_STATE_READY_TO_READ) ||
        (container->state == CONTAINER_STATE_READING)) {
        container->state = CONTAINER_STATE_READING;
        //LOG(EVENT_CONTAINER_STATE_READING, (int) container);
    } else {
        container = NULL;
    }

    return container;
}

// Set the given container as read.
// Must have been reading this container to mark it as read.  Once
// this container is marked as read the read pointer can be moved on
// and the container freed up.
inline void Urtp::setContainerAsRead(Urtp::Container * container)
{
    assert(container->state == CONTAINER_STATE_READING);
    _containerNextForReading = container->next;
    //LOG(EVENT_CONTAINER_STATE_READ, (int) container);
    setContainerAsEmpty(container);
}

// Set the given container as empty.
inline void Urtp::setContainerAsEmpty(Urtp::Container * container)
{
    container->state = CONTAINER_STATE_EMPTY;
    _numDatagramsFree++;
    //LOG(EVENT_CONTAINER_STATE_EMPTY, (int) container);
    //LOG(EVENT_NUM_DATAGRAMS_FREE, _numDatagramsFree);
}

/**********************************************************************
 * PUBLIC METHODS
 **********************************************************************/

// Constructor.
Urtp::Urtp(void(*datagramReadyCb)(const char *),
           void(*datagramOverflowStartCb)(void),
           void(*datagramOverflowStopCb)(int))
{
    _datagramReadyCb = datagramReadyCb;
    _datagramOverflowStartCb = datagramOverflowStartCb;
    _datagramOverflowStopCb = datagramOverflowStopCb;
    _datagramMemory = NULL;
    _containerNextForWriting = _container;
    _containerNextForReading = _container;
    _audioShiftSampleCount = 0;
    _audioUnusedBitsMin = 0x7FFFFFFF;
    _audioShift = AUIDIO_SHIFT_DEFAULT;
    _audioUpShiftCount = 0;
    _audioShiftMax = AUDIO_MAX_SHIFT_BITS;
    _sequenceNumber = 0;
    _numDatagramOverflows = 0;
    _numDatagramsFree = 0;
    _minNumDatagramsFree = 0;
}

// Destructor
Urtp::~Urtp()
{
#ifdef URTP_TEST_AUDIO_OUTPUT_FILENAME
    if (urtpTestAudioOutputFile != NULL) {
        fclose(urtpTestAudioOutputFile);
        urtpTestAudioOutputFile = NULL;
    }
#endif
#ifdef URTP_TEST_URTP_OUTPUT_FILENAME
    if (urtpTestUrtpOutputFile != NULL) {
        fclose(urtpTestUrtpOutputFile);
        urtpTestUrtpOutputFile = NULL;
    }
#endif
}

// Initialise ourselves.
bool Urtp::init(void *datagramStorage, int audioShiftMax)
{
    bool success = false;
    int x = 0;
    Container *tmp = NULL;

    _audioShiftMax = audioShiftMax;

    firInit(&_preemphasis);

#ifdef DISABLE_UNICAM
    {
#else
    if (unicamTest()) {
#endif
        _datagramMemory = (char *) datagramStorage;

        if (_datagramMemory != NULL) {
            // Initialise the linked list
            _numDatagramsFree = 0;
            // This looks peculiar but it is deliberate: container is moved
            // on in the body of the loop in order to deal with the next pointer,
            // x is advanced as the for loop variable
            for (Container *container = _container;
                 container < _container + sizeof (_container) / sizeof (_container[0]);
                 x += URTP_DATAGRAM_SIZE) {
                container->contents = (void *) (_datagramMemory + x);
                container->state = CONTAINER_STATE_EMPTY;
                _numDatagramsFree++;
                container->next = NULL;
                tmp = container;
                container++;
                tmp->next = container;
            }
            // Handle the final next, making the list circular
            tmp->next = _container;
            _minNumDatagramsFree = _numDatagramsFree;

            LOG(EVENT_NUM_DATAGRAMS_FREE, _numDatagramsFree);
        }

#ifdef ENABLE_STREAM_FIXED_TONE
        toneIndex = 0;
#endif

#ifdef URTP_TEST_AUDIO_OUTPUT_FILENAME
        urtpTestAudioOutputFile = fopen(URTP_TEST_AUDIO_OUTPUT_FILENAME, "wb+");
        if (urtpTestAudioOutputFile == NULL) {
            printf("Cannot open URTP test audio output file %s (%s).\n", URTP_TEST_AUDIO_OUTPUT_FILENAME, strerror(errno));
        }
#endif

#ifdef URTP_TEST_URTP_OUTPUT_FILENAME
        urtpTestUrtpOutputFile = fopen(URTP_TEST_URTP_OUTPUT_FILENAME, "wb+");
        if (urtpTestUrtpOutputFile == NULL) {
            printf("Cannot open URTP test URTP output file %s (%s).\n", URTP_TEST_URTP_OUTPUT_FILENAME, strerror(errno));
        }
#endif

        success = true;
    }

    return success;
}

// URTP encode an audio block.
void Urtp::codeAudioBlock(const uint32_t *rawAudio)
{
    fillMonoDatagramFromBlock(rawAudio);
}

// Return a pointer to the next filled URTP datagram.
const char * Urtp::getUrtpDatagram()
{
    const char * contents = NULL;
    Container * container = getContainerForReading();

    if (container != NULL) {
        contents = (const char *) container->contents;
    }

    return contents;
}

// Free a URTP datagram that has been read.
void Urtp::setUrtpDatagramAsRead(const char *datagram)
{
    bool freedIt = false;
    Container *container = _containerNextForReading;

    // This looks pretty inefficient but the datagram being freed will
    // always be _containerNextForReading, so it should be found immediately
    for (unsigned int x = 0; (x < sizeof (_container) / sizeof (_container[0])) && !freedIt; x++) {
        if ((const char *) container->contents == datagram) {
            setContainerAsRead(container);
            freedIt = true;
        }
    }
}

// The number of datagrams available
int Urtp::getUrtpDatagramsAvailable()
{
    return sizeof (_container) / sizeof (_container[0]) - _numDatagramsFree;
}

// The number of datagrams free
int Urtp::getUrtpDatagramsFree()
{
    return _numDatagramsFree;
}

// The minimum number of datagrams free
int Urtp::getUrtpDatagramsFreeMin()
{
    return _minNumDatagramsFree;
}

// The last URTP sequence number
int Urtp::getUrtpSequenceNumber()
{
    return _sequenceNumber;
}

// End of file
