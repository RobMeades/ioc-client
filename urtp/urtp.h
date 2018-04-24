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

/* This library implements coding of a stream of I2S Philips
 * format audio samples into a mono (left channel) real-time
 * protocol like format suitable for transmission as datagrams
 * over an IP link.  The coding used is NICAM-like and hence
 * offers close to 50% compression.
 *
 * Speed and efficiency of memory usage are really important here,
 * hence the heavy use of #defines rather than variables and
 * run-time calculations.
 */

#ifndef _URTP_
#define _URTP_

/** Urtp class.
 *
 * This class takes in block of Philips I2S protocol samples
 * and encodes them into URTP datagrams.
 *
 * URTP: u-blox real time protocol; encode blocks of
 * audio into simple RTP-like mono audio datagrams.
 *
 * The header looks like this:
 *
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 * --------------------------------------------------------
 *  0    |               Sync byte = 0x5A                |
 *  1    |              Audio coding scheme              |
 *  2    |              Sequence number MSB              |
 *  3    |              Sequence number LSB              |
 *  4    |                Timestamp MSB                  |
 *  5    |                Timestamp byte                 |
 *  6    |                Timestamp byte                 |
 *  7    |                Timestamp byte                 |
 *  8    |                Timestamp byte                 |
 *  9    |                Timestamp byte                 |
 *  10   |                Timestamp byte                 |
 *  11   |                Timestamp LSB                  |
 *  12   |       Number of samples in datagram MSB       |
 *  13   |       Number of samples in datagram LSB       |
 *
 * ...where:
 *
 * - Sync byte is always 0x5A, used to sync a frame over a
 *   streamed connection (e.g. TCP).
 * - Audio coding scheme is one of:
 *   - PCM_SIGNED_16_BIT (0)
 *   - UNICAM_COMPRESSED_8_BIT (1)
 *   - UNICAM_COMPRESSED_10_BIT (2)
 * - Sequence number is a 16 bit sequence number, incremented
 *   on sending of each datagram.
 * - Timestamp is a uSecond timestamp representing the moment
 *   of the start of the audio in this datagram.
 * - Number of bytes to follow is the size of the audio payload
 *   the follows in this datagram.
 *
 * There are three audio coding schemes.  The default, and most
 * efficient, is 8 bit UNICAM compression.  If UNICAM is not
 * used, 16 bit RAW PCM is used.
 *
 * When the audio coding scheme is PCM_SIGNED_16_BIT,
 * the payload is as follows:
 *
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 *--------------------------------------------------------
 *  14   |                 Sample 0 MSB                  |
 *  15   |                 Sample 0 LSB                  |
 *  16   |                 Sample 1 MSB                  |
 *  17   |                 Sample 1 LSB                  |
 *       |                     ...                       |
 *  N    |                 Sample M MSB                  |
 *  N+1  |                 Sample M LSB                  |
 *
 * ...where the number of [big-endian] signed 16-bit samples is between
 * 0 and 320, so 5120 bits, plus 112 bits of header, gives
 * an overall data rate of 261.6 kbits/s.
 *
 * When the audio coding scheme is UNICAM_COMPRESSED_8_BIT,
 * the payload is as follows:
 *
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 *--------------------------------------------------------
 *  14   |              Block 0, Sample 0                |
 *  15   |              Block 0, Sample 1                |
 *       |                   ...                         |
 *  28   |              Block 0, Sample 14               |
 *  29   |              Block 0, Sample 15               |
 *  30   |     Block 0 shift     |     Block 1 shift     |
 *  31   |              Block 1, Sample 0                |
 *  32   |              Block 1, Sample 1                |
 *       |                     ...                       |
 *  45   |              Block 1, Sample 14               |
 *  46   |              Block 1, Sample 15               |
 *  47   |     Block 1 shift     |     Block 2 shift     |
 *       |                     ...                       |
 *  N    |     Block M-1 shift   |     Block M shift     |
 *  N+1  |              Block M, Sample 0                |
 *  N+2  |              Block M, Sample 1                |
 *       |                     ...                       |
 *  N+15 |              Block M, Sample 14               |
 *  N+16 |              Block M, Sample 15               |
 *
 * ...where the number of blocks is between 0 and 20, so 330
 * bytes in total, plus a 14 byte header gives an overall data
 * rate of 132 kbits/s.
 *
 * If the audio coding scheme is UNICAM_COMPRESSED_10_BIT,
 * the payload follows the pattern above but with 10 bit
 * samples packed as follows:
 *
 * Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
 *--------------------------------------------------------
 *  14   |  Block 0, Sample 0                            |
 *  15   |           |  Block 0, Sample 1                |
 *  16   |                       |  Block 0, Sample 2    |
 *  17   |                                   |  Block 0, |
 *  18   | Sample 3                                      |
 *  19   |  Block 0, Sample 4                            |
 *  20   |           |  Block 0, Sample 5                |
 *       |                     ...                       |
 *  29   |  Block 0, Sample 12                           |
 *  30   |           |  Block 0, Sample 13               |
 *  31   |                       |  Block 0, Sample 14   |
 *  32   |                                   |  Block 0, |
 *  33   | Sample 15                                     |
 *  34   |     Block 0 shift     |     Block 1 shift     |
 *  35   |  Block 1, Sample 0                            |
 *  36   |           |  Block 1, Sample 1                |
 *  37   |                       |  Block 1, Sample 2    |
 *  38   |                                   |  Block 1, |
 *  39   | Sample 3                                      |
 *       |                     ...                       |
 *
 * The number of blocks is still between 0 and 20 but now each
 * block is 20 bytes plus half a byte of shift value, so 410
 * bytes in total, plus a 14 byte header gives an overall data
 * rate of 169.6 kbits/s.
 *
 * The receiving end should be able to reconstruct an audio
 * stream from this.
 */
class Urtp {

public:

    /** The audio sampling frequency in Hz.
     * This is the frequency of the WS signal on the I2S interface
     */
#   ifndef SAMPLING_FREQUENCY
#    define SAMPLING_FREQUENCY 16000
#   endif

    /** The amount of audio encoded into one URTP block in milliseconds.
     */
#   ifndef BLOCK_DURATION_MS
#    define BLOCK_DURATION_MS 20
#   endif

    /** The number of bits that a sample is coded into for UNICAM.
     * Only 8 and 10 are supported, 8 is the default.
     */
#   ifndef UNICAM_CODED_SAMPLE_SIZE_BITS
#    define UNICAM_CODED_SAMPLE_SIZE_BITS 8
#   endif

    /** The maximum number of URTP datagrams that will be stored
     * (old ones will be overwritten).  With a block duration of
     * 20 ms a value of 100 represents around 2 seconds.
     */
#   ifndef MAX_NUM_DATAGRAMS
#    define MAX_NUM_DATAGRAMS 250
#   endif

    /** The desired number of unused bits to keep in the audio processing
     * to avoid clipping when we can't move fast enough due to averaging.
     */
#   ifndef AUDIO_DESIRED_UNUSED_BITS
#    define AUDIO_DESIRED_UNUSED_BITS 4
#   endif

    /** The maximum audio shift to use (established by experiment).
     */
#   ifndef AUDIO_MAX_SHIFT_BITS
#    define AUDIO_MAX_SHIFT_BITS 12
#   endif

    // Thresholding: audio levels that are within +/- this value
    // are not shifteed.  Set to 0 for no thresholding.
#   ifndef AUDIO_SHIFT_THRESHOLD
#    define AUDIO_SHIFT_THRESHOLD 0
#   endif

    // The number of consecutive up-shifts that have to be indicated
    // before a real increase in gain is applied.
#   ifndef AUDIO_NUM_UP_SHIFTS_FOR_A_SHIFT
#    define AUDIO_NUM_UP_SHIFTS_FOR_A_SHIFT 10
#   endif
    
    /** The number of samples in BLOCK_DURATION_MS.  Note that a
     * sample is stereo when the audio is in raw form but is reduced
     * to mono when we organise it into URTP packets, hence the size
     * of a sample is different in each case (64 bits for stereo,
     * 32 bits for mono).
     */
#   define SAMPLES_PER_BLOCK (SAMPLING_FREQUENCY * BLOCK_DURATION_MS / 1000)

    /** UNICAM parameters: number of samples in a UNICAM block.
     */
#   define SAMPLES_PER_UNICAM_BLOCK      (SAMPLING_FREQUENCY / 1000)

    /** UNICAM parameters: number of UNICAM blocks per block.
     */
#  define UNICAM_BLOCKS_PER_BLOCK       (SAMPLES_PER_BLOCK / SAMPLES_PER_UNICAM_BLOCK)

    /** UNICAM parameters: the size of two UNICAM blocks (has to be a two since
     * the shift nibble for two blocks are encoded into one byte).
     */
#   define TWO_UNICAM_BLOCKS_SIZE        (((SAMPLES_PER_UNICAM_BLOCK * UNICAM_CODED_SAMPLE_SIZE_BITS) / 8) * 2 + 1)

    /** URTP parameters: the size of the header
     */
#   define URTP_HEADER_SIZE        14

    /** URTP parameters: the size of one input sample, which
     * is the size of one PCM sample.
     */
#   define URTP_SAMPLE_SIZE        2

#   ifndef DISABLE_UNICAM
    /** URTP parameters: the maximum size of the payload.
     */
#    define URTP_BODY_SIZE         ((UNICAM_BLOCKS_PER_BLOCK / 2) * TWO_UNICAM_BLOCKS_SIZE)
#   else
    /** URTP parameters: the maximum size of the payload.
     */
#    define URTP_BODY_SIZE         (URTP_SAMPLE_SIZE * SAMPLES_PER_BLOCK)
#   endif

    /** URTP parameters: the size of a URTP datagram.
     */
#   define URTP_DATAGRAM_SIZE       (URTP_HEADER_SIZE + URTP_BODY_SIZE)

    /** The amount of datagram memory which must be supplied for URTP
     * to operate.
     */
#   define URTP_DATAGRAM_STORE_SIZE (URTP_DATAGRAM_SIZE * MAX_NUM_DATAGRAMS)

    /** URTP parameters: the sync byte.
     */
#   define SYNC_BYTE               0x5a

    /* Constructor.
     *
     * @param datagramReadyCb          Callback to be invoked once a URTP datagram
     *                                 has been encoded.  IMPORTANT: don't do much
     *                                 in this callback, simply flag that something
     *                                 is ready or send a signal saying so to a task.
     * @param datagramOverflowStartCb  Callback to be invoked once the URTP datagram
     *                                 buffer has begun to overflow.  Please don't do
     *                                 much in this function either, maybe toggle
     *                                 an LED or set a flag.
     * @param datagramOverflowStopCb   Callback to be invoked once the URTP datagram
     *                                 buffer is no longer overflowing.  Please don't do
     *                                 much in this function either, maybe toggle
     *                                 an LED or set a flag.
     */
    Urtp(void(*datagramReadyCb)(const char *),
         void(*datagramOverflowStartCb)(void) = NULL,
         void(*datagramOverflowStopCb)(int) = NULL);

    /* Destructor.
     */
    ~Urtp();

    /** Initialise URTP.
     *
     * @param datagramStorage  a pointer to URTP_DATAGRAM_STORE_SIZE of
     *                         memory for datagram buffers.
     * @param audioShiftFixed  the fixed gain, usually not used (-1).
     * @return                 true if successful, otherwise false.
     */
    bool init(void *datagramStorage, int audioShiftFixed = -1);

    /** URTP encode an audio block.
     * Only the samples from the LEFT CHANNEL (i.e. the even uint32_t's) are
     * used.
     *
     * The Philips I2S protocol (24-bit frame with CPOL = 0 to read
     * the data on the rising edge) looks like this.  Each data bit
     * is valid on the rising edge of SCK and the MSB of the data word is
     * clocked out on the second clock edge after WS changes.  WS is low
     * for the left channel and high for the right channel.
     *      ___                                 ______________________   ___
     * WS      \____________...________..._____/                      ...   \______
     *          0   1   2       23  24      31  32  33  34     55  56     63
     * SCK  ___   _   _   _       _   _      _   _   _   _       _   _      _   _
     *         \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_
     *
     * SD   ________--- ---     --- --- ___________--- ---     --- ---_____________
     *              --- --- ... --- ---            --- --- ... --- ---
     *              23  22       1   0             23  22       1   0
     *              Left channel data              Right channel data
     *
     * @param rawAudio  a pointer to a buffer of SAMPLES_PER_BLOCK * 2 uint32_t's
     *                  (i.e. stereo) of Philips I2S protocol data: 24-bit frame
     *                  with CPOL = 0.
     */
    void codeAudioBlock(const uint32_t *rawAudio);

    /** Call this to obtain a pointer to a URTP datagram that has been
     * prepared.
     *
     * @return  a pointer to the next URTP datagram that has been
     *          prepared, NULL if there is none.
     */
    const char * getUrtpDatagram();

    /** Call this to free a URTP datagram and move the read pointer on.
     *
     * @param datagram  a pointer to a member of the datagram array
     *                  that should be freed.
     */
    void setUrtpDatagramAsRead(const char *datagram);

    /** Call this to get the number of URTP datagrams available.
     *
     * @return   the number of datagrams available.
     */
    int getUrtpDatagramsAvailable();

    /** Call this to get the number of URTP datagrams free.
     *
     * @return   the number of datagrams free.
     */
    int getUrtpDatagramsFree();

    /** Call this to get the low water mark of the number of
     * URTP datagrams free.
     *
     * @return   the minimum number of datagrams free.
     */
    int getUrtpDatagramsFreeMin();

    /** Call this to get the last URTP sequence number.
     *
     * @return   the lsat URTP sequence number.
     */
    int getUrtpSequenceNumber();

protected:
    /** The number of valid bytes in each mono sample of audio received
     * on the I2S stream (the number of bytes received may be larger
     * but some are discarded along the way).
     */
#   define MONO_INPUT_SAMPLE_SIZE  3

    /** The audio coding schemes.
     */
    typedef enum {
        PCM_SIGNED_16_BIT = 0,
        UNICAM_COMPRESSED_8_BIT = 1,
        UNICAM_COMPRESSED_10_BIT = 2
    } AudioCoding;

    /** The possible states for a container.
     *
     * The normal life cycle for a container is:
     *
     * EMPTY
     * WRITING
     * READY_TO_READ
     * READING
     * EMPTY
     */
    typedef enum {
        CONTAINER_STATE_EMPTY,
        CONTAINER_STATE_WRITING,
        CONTAINER_STATE_READY_TO_READ,
        CONTAINER_STATE_READING,
        MAX_NUM_CONTAINER_STATES
    } ContainerState;

    /** A linked list container, used to manage datagrams
     * as a FIFO.
     */
    typedef struct ContainerTag {
        ContainerState state;
        void * contents;
        ContainerTag *next;
    } Container;

    /** Callback to be called when a datagram has been populated.
     * The parameter is a pointer to the datagram.
     */
    void(*_datagramReadyCb)(const char *);

    /** Callback to be called when the datagram buffer begins overflowing.
     */
    void(*_datagramOverflowStartCb)(void);

    /** Callback to be called when the datagram buffer stops overflow.
     * The parameter indicates hte number of datagram overflows that
     * occurred.
     */
    void(*_datagramOverflowStopCb)(int);

    /** The number of samples that have been used so far in
     * evaluation the audio bit-shift
     */
    int _audioShiftSampleCount;

    /** The minimum value of the number of unused bits
     */
    int _audioUnusedBitsMin;

    /** The current audio shift value.
     */
    int _audioShift;

    /** A count of the number of times that an
     * increase in shift has been suggested.
     */
    int _audioUpShiftCount;

    /** The fixed gain audio shift value.
     */
    int _audioShiftFixed;

    /** Buffer for UNICAM coding.
     */
    int _unicamBuffer[SAMPLES_PER_UNICAM_BLOCK];

    /** Pointer to the URTP datagrams.
     */
    char * _datagramMemory;

    /** A linked list to manage the datagrams, must have the same
     * number of elements as gDatagram.
     */
    Container _container[MAX_NUM_DATAGRAMS];

    /** A sequence number for the URTP datagrams.
     */
    int _sequenceNumber;

    /** Pointer to the next container to write to.
     */
    Container *_containerNextForWriting;

    /** Pointer to the first filled container for reading.
     */
    Container *_containerNextForReading;

    /** Diagnostics: a count of the number of consecutive datagram
     * overflows that have occurred.
     */
    int _numDatagramOverflows;

    /** Diagnostics: The current number of datagrams free.
     */
    unsigned int _numDatagramsFree;

    /** Diagnostics: The minimum number of datagrams free.
     */
    unsigned int _minNumDatagramsFree;

    /** Take an audio sample and from it produce a signed
     * output that uses the maximum number of bits
     * in a 32 bit word (hopefully) without clipping.
     * The algorithm is as follows:
     *
     * Calculate how many of bits of the input value are unused.
     * Add this to a rolling average of unused bits of length
     * AUDIO_AVERAGING_INTERVAL_MILLISECONDS, which starts off at 0.
     * Every AUDIO_AVERAGING_INTERVAL_MILLISECONDS work out whether.
     * the average number of unused is too large and, if it is,
     * increase the gain, or if it is too small, decrease the gain.
     *
     * @param monoSample a mono audio sample.
     * @return           the output mono audio sample.
     */
     int processAudio(int monoSample);

    /** Take a stereo sample and return an int
     * containing a sample that will fit within
     * MONO_INPUT_SAMPLE_SIZE but sign extended
     * so that it can be treated as an int for
     * maths purposes.
     *
     * @param stereoSample a pointer to a 2 uint32_t
     *                     stereo audio sample in
     *                     Philips I2S 24-bit format.
     * @return             the output mono audio sample.
     */
    inline int getMonoSample(const uint32_t *stereoSample);

    /** Take a buffer of rawAudio and code the samples from
     *  the left channel (i.e. the even uint32_t's) into dest.
     *
     * Here we use the principles of NICAM coding, see
     * http:www.doc.ic.ac.uk/~nd/surprise_97/journal/vol2/aps2/
     * We take 1 ms of audio data, so 16 samples (SAMPLES_PER_UNICAM_BLOCK),
     * and work out the peak.  Then we shift all the samples in the
     * block down so that they fit in just UNICAM_CODED_SAMPLE_SIZE_BITS.
     * Then we put the shift value in the lower four bits of the next
     * byte. In order to pack things neatly, the shift value for the
     * following block is encoded into the upper four bits, followed by
     * the shifted samples for that block, etc.
     *
     * This represents UNICAM_COMPRESSED_x_BIT.
     *
     * @param rawAudio  a pointer to a buffer of
     *                  SAMPLES_PER_BLOCK * 2 uint32_t's
     *                  (i.e. stereo), each in Philips I2S
     *                  24-bit format.
     * @param dest      a pointer to an empty datagram.
      */
    int codeUnicam(const uint32_t *rawAudio, char *dest);

    /** Take a buffer of rawAudio and copy the samples from
     * the left channel (i.e. the even uint32_t's) into dest.
     * Each byte is passed through processAudio() before it
     * is coded.
     *
     * This represents PCM_SIGNED_16_BIT.
     *
     * @param rawAudio  a pointer to a buffer of
     *                  SAMPLES_PER_BLOCK * 2 uint32_t's
     *                  (i.e. stereo), each in Philips I2S
     *                  24-bit format.
     * @param dest      a pointer to an empty datagram.
     */
    int codePcm(const uint32_t *rawAudio, char *dest);

    /** Fill a datagram with the audio from one block.
     * Only the samples from the left channel
     * (i.e. the even uint32_t's) are used.
     *
     * @param rawAudio  a pointer to a buffer of
     *                  SAMPLES_PER_BLOCK * 2 uint32_t's
     *                  (i.e. stereo), each in Philips I2S
     *                  24-bit format.
     */
    void fillMonoDatagramFromBlock(const uint32_t *rawAudio);

    /** For the UNICAM compression scheme, we need
     * the right shift operation to be arithmetic
     * (so preserving the sign bit) rather than
     * logical.  This function tests that this is
     * the case.
     *
     * @return true if OK for UNICAM, otherwise false.
     */
    bool unicamTest();

    /** Get the next container for writing.  Always returns
     * something, even if it is necessary to overwrite old data.
     * The container will be marked as WRITING.
     *
     * @return a pointer to the container.
     */
    inline Urtp::Container * getContainerForWriting();

    /** Set the given container as ready to read.
     *
     * @param container  a pointer to the container.
     */
    inline void setContainerAsReadyToRead(Urtp::Container * container);

    /** Get the next container for reading.  If there is a container,
     * that can be read, it will be marked READING.  If there are no
     * containers for reading, NULL will be returned.
     *
     * @return a pointer to the container, may be NULL.
     */
    inline Urtp::Container * getContainerForReading();

    /** Set the given container as read.
     *
     * @param container  a pointer to the container.
     */
    inline void setContainerAsRead(Urtp::Container * container);

    /** Set the given container as empty.
     *
     * @param container  a pointer to the container.
     */
    inline void setContainerAsEmpty(Urtp::Container * container);

};

#endif // _URTP_
