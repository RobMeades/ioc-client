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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <semaphore.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>
#include <utils.h>
#include <urtp.h>
#include <timer.h>
#include <log.h>
#include <audio.h>

/* This file contains the audio sample acquisition and audio streaming
 * functionality.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// For testing only: define this to also write incoming audio PCM
// stream to file.
//#define AUDIO_TEST_OUTPUT_FILENAME "/rw/audio_raw.pcm"

// The maximum amount of time allowed to send a
// datagram of audio over TCP.
#define AUDIO_TCP_SEND_TIMEOUT_MS 1500

// If we've had consecutive socket errors on the
// audio streaming socket for this long, it's gone bad.
#define AUDIO_MAX_DURATION_SOCKET_ERRORS_MS 3000

// The audio send data task will run anyway this interval,
// necessary in order to terminate it in an orderly fashion.
#define AUDIO_SEND_DATA_RUN_ANYWAY_TIME_S 2

// The maximum length of an audio server URL (including
// terminator).
#define AUDIO_MAX_LEN_SERVER_URL 128

// The default audio setup data.
#define AUDIO_DEFAULT_FIXED_GAIN -1

// The TCP buffer size for audio streaming:
// keep it small as we don't want audio to build
// up in the buffers, resulting in non real-timeness
#define AUDIO_TCP_BUFFER_SIZE 25000

/* ----------------------------------------------------------------
 * CALLBACK FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

static void datagramReadyCb(const char * datagram);
static void datagramOverflowStartCb();
static void datagramOverflowStopCb(int numOverflows);

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The ALSA input device name.
static const char *gpAlsaPcmDeviceName = NULL;

// The Internet of Chuffs server URL.
static const char *gpAudioServerUrl = NULL;

// For monitoring progress.
static size_t gSecondTicker;

// ALSA handle for the PCM input device.
static snd_pcm_t *gpPcmHandle = NULL;

// ALSA parameters for the PCM input device.
static snd_pcm_hw_params_t *gpPcmHwParams = NULL;

// ALSA frame size.
static snd_pcm_uframes_t gPcmFrames = SAMPLES_PER_BLOCK;

// Audio buffer, enough for one block of stereo audio,
// where each sample takes up 64 bits (32 bits for L channel
// and 32 bits for R channel).
static uint32_t gRawAudio[SAMPLES_PER_BLOCK * 2];

// Datagram storage for URTP.
static char gDatagramStorage[URTP_DATAGRAM_STORE_SIZE];

// The address of the audio server.
static struct sockaddr_in *gpAudioServerAddress = NULL;

// Task to read and encode audio data.
static std::thread *gpEncodeTask = NULL;

// Task to send data off to the audio streaming server.
static std::thread *gpSendTask = NULL;

// Task to check on the audio streaming server status.
static std::thread *gpServerStatusTask = NULL;

// Semaphore to communicate data transfer between tasks.
static sem_t gUrtpDatagramReady;

// Semaphore to stop the encode task.
static sem_t gStopEncodeTask;

// Semaphore to stop the send task.
static sem_t gStopSendTask;

// Semaphore to stop the server status task.
static sem_t gStopServerStatusTask;

// The URTP codec.
static Urtp *gpUrtp = NULL;

// The audio send socket.
static int gStreamingSocket = -1;

// Flag to indicate that the TCP connection is up.
static volatile bool gTcpConnected = false;

// Flag to indicate that the audio comms channel is up.
static volatile bool gAudioCommsConnected = false;

// Pointer to watchdog handler.
static void(*gpWatchdogHandler)(void) = NULL;

// Pointer to "I'm streaming" handler.
static void(*gpNowStreamingHandler)(void) = NULL;

// Keep track of stats.
static unsigned long gNumAudioSendFailures = 0;
static unsigned long gNumAudioBytesSent = 0;
static unsigned long gAverageAudioDatagramSendDuration = 0;
static unsigned long gNumAudioDatagrams = 0;
static unsigned long gNumAudioDatagramsSendTookTooLong = 0;
static unsigned long gWorstCaseAudioDatagramSendDuration = 0;

// For testing.
#ifdef AUDIO_TEST_OUTPUT_FILENAME
static FILE *gpAudioOutputFile = NULL;
#endif

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URTP CODEC AND ITS CALLBACK FUNCTIONS
 * -------------------------------------------------------------- */

// Callback for when an audio datagram is ready for sending.
static void datagramReadyCb(const char *pDatagram)
{
    if (gpSendTask != NULL) {
        // Send the signal to the sending task
        sem_post(&gUrtpDatagramReady);
    }
}

// Callback for when the audio datagram list starts to overflow.
static void datagramOverflowStartCb()
{
    // TODO
}

// Callback for when the audio datagram list stops overflowing.
static void datagramOverflowStopCb(int numOverflows)
{
    // TODO
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONNECTION
 * -------------------------------------------------------------- */

// Monitor on a 1 second tick.
static void audioMonitor(size_t timerId, void *pUserData)
{
    // Monitor throughput
    if (gNumAudioBytesSent > 0) {
        LOG(EVENT_THROUGHPUT_BITS_S, gNumAudioBytesSent << 3);
        gNumAudioBytesSent = 0;
        if (gpUrtp != NULL) {
            LOG(EVENT_NUM_DATAGRAMS_QUEUED, gpUrtp->getUrtpDatagramsAvailable());
        }
    }
}

// Start the audio streaming connection.
// This will set up the pAudio structure.
// Note: here be multiple return statements.
static bool startAudioStreamingConnection()
{
    char *pBuf = new char[AUDIO_MAX_LEN_SERVER_URL];
    struct hostent *pHostEntries = NULL;
    int port;
    int setOption;
    struct timeval tv = {0};
    int x;

    tv.tv_sec = 1; /* 1 second timeout */

    LOG(EVENT_AUDIO_STREAMING_CONNECTION_START, 0);
    printf("Resolving IP address of the audio streaming server...\n");
    if (gpAudioServerAddress == NULL) {
        gpAudioServerAddress = new struct sockaddr_in;
        memset(gpAudioServerAddress, 0, sizeof(*gpAudioServerAddress));
        getAddressFromUrl(gpAudioServerUrl, pBuf, AUDIO_MAX_LEN_SERVER_URL);
        printf("[Looking for audio server URL \"%s\"...]\n", pBuf);
        LOG(EVENT_DNS_LOOKUP, 0);
        pHostEntries = gethostbyname(pBuf);
        if (pHostEntries != NULL) {
            // Copy the network address to sockaddr_in structure
            memcpy(&(gpAudioServerAddress->sin_addr), pHostEntries->h_addr_list[0], pHostEntries->h_length) ;
            gpAudioServerAddress->sin_family = AF_INET;
            printf("[Found it at IP address %s]\n", inet_ntoa(gpAudioServerAddress->sin_addr));
            if (getPortFromUrl(gpAudioServerUrl, &port)) {
                gpAudioServerAddress->sin_port = htons(port);
                printf("[Audio server port is %d]\n", port);
            } else {
                printf("[WARNING: no port number was specified in the audio server URL (\"%s\")]\n",
                       gpAudioServerUrl);
            }
        } else {
            LOG(EVENT_DNS_LOOKUP_FAILURE, h_errno);
            LOG(EVENT_AUDIO_STREAMING_CONNECTION_START_FAILURE, 1);
            printf("Error, couldn't resolve IP address of audio streaming server.\n");
            return false;
        }
    }

    printf("Opening TCP socket to server for audio comms...\n");
    LOG(EVENT_SOCKET_OPENING, 0);
    gStreamingSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (gStreamingSocket < 0) {
        LOG(EVENT_SOCKET_OPENING_FAILURE, errno);
        printf("Could not open TCP socket to audio streaming server (%s).\n", strerror(errno));
        return false;
    }
    LOG(EVENT_SOCKET_OPENED, gStreamingSocket);
    
    printf("Setting socket to non-blocking (for the downlink timing datagram)...\n");
    x = fcntl(gStreamingSocket, F_SETFL, O_NONBLOCK);
    if (x < 0) {
        LOG(EVENT_SOCKET_CONFIGURATION_FAILURE, errno);
        printf("Could not set TCP socket to be non-blocking (%s).\n", strerror(errno));
        return false;
    }
    printf("Setting timeout in TCP socket options...\n");
    x = setsockopt(gStreamingSocket, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(tv));
    if (x < 0) {
        LOG(EVENT_SOCKET_CONFIGURATION_FAILURE, errno);
        printf("Could not set timeout in TCP socket options (%s).\n", strerror(errno));
        return false;
    }
    printf("Setting TCP_NODELAY in TCP socket options...\n");
    // Set TCP_NODELAY (1) in level IPPROTO_TCP (6) to 1
    setOption = 1;
    x = setsockopt(gStreamingSocket, IPPROTO_TCP, TCP_NODELAY, (void *) &setOption, sizeof(setOption));
    if (x < 0) {
        LOG(EVENT_SOCKET_CONFIGURATION_FAILURE, errno);
        printf("Could not set TCP_NODELAY in socket options (%s).\n", strerror(errno));
        return false;
    }
    printf("Setting SO_SNDBUF in TCP socket options...\n");
    // Set SO_SNDBUF (0x1001) in level SOL_SOCKET (0xffff) to AUDIO_TCP_BUFFER_SIZE
    setOption = AUDIO_TCP_BUFFER_SIZE;
    x = setsockopt(gStreamingSocket, SOL_SOCKET, SO_SNDBUF, (void *) &setOption, sizeof(setOption));
    if (x < 0) {
        LOG(EVENT_SOCKET_CONFIGURATION_FAILURE, errno);
        printf("Could not set SO_SNDBUF to %d in socket options (%s).\n", AUDIO_TCP_BUFFER_SIZE, strerror(errno));
        return false;
    }
    LOG(EVENT_SOCKET_CONFIGURED, 0);
    
    LOG(EVENT_SOCKET_CONNECTING, 0);
    printf("Connecting TCP...\n");
    x = connect(gStreamingSocket, (struct sockaddr *) gpAudioServerAddress, sizeof(struct sockaddr));
    if ((x < 0) && (errno != EINPROGRESS)) {  // Socket will return EINPROGRESS if it is non-blocking
        LOG(EVENT_SOCKET_CONNECT_FAILURE, errno);
        printf("Could not connect TCP socket (%s).\n", strerror(errno));
        return false;
    }
    gTcpConnected = true;
    LOG(EVENT_SOCKET_CONNECTED, 0);

    return true;
}

// Stop the audio streaming connection.
static void stopAudioStreamingConnection()
{
    LOG(EVENT_AUDIO_STREAMING_CONNECTION_STOP, 0);
    printf("Closing streaming audio server socket...\n");
    LOG(EVENT_SOCKET_CLOSING, 0);
    gTcpConnected = false;
    close(gStreamingSocket);
    gStreamingSocket = -1;
    LOG(EVENT_SOCKET_CLOSED, 0);
    gAudioCommsConnected = false;
}

// Read and encode audio from the PCM device.
static void encodeAudioData()
{
    int retValue;

    while (sem_trywait(&gStopEncodeTask) != 0) {
        // Get a buffer full of audio data
        retValue = snd_pcm_readi(gpPcmHandle, gRawAudio, gPcmFrames);
        if (retValue == -EPIPE) {
            LOG(EVENT_PCM_OVERRUN, retValue);
            snd_pcm_prepare(gpPcmHandle);
        } else if (retValue < 0) {
            LOG(EVENT_PCM_ERROR, retValue);
        } else if (retValue != (int) gPcmFrames) {
            LOG(EVENT_PCM_UNDERRUN, retValue);
        } else {
            // Encode the data
        if (gpUrtp != NULL) {
            gpUrtp->codeAudioBlock(gRawAudio);
        }
#ifdef AUDIO_TEST_OUTPUT_FILENAME
            if (gpAudioOutputFile != NULL) {
                fwrite(gRawAudio, sizeof(gRawAudio), 1, gpAudioOutputFile);
            }
#endif
        }
    }
}

// Send a buffer of data over a TCP socket
static int tcpSend(const char *pData, int size)
{
    int x = 0;
    int count = 0;
    struct timeval start;

    if (gTcpConnected) {
        gettimeofday(&start, NULL); 
        while ((count < size) && (((unsigned long) timeDifference(&start, NULL) / 1000) < AUDIO_TCP_SEND_TIMEOUT_MS)) {
            x = send(gStreamingSocket, pData + count, size - count, MSG_NOSIGNAL); //  MSG_NOSIGNAL prevents send from throwing exceptions like EPIPE
            if (x > 0) {
                count += x;
            }
        }

        if (count < size) {
            LOG(EVENT_TCP_SEND_TIMEOUT, size - count);
        }

        if (x < 0) {
            count = errno;
        }
    }

    return count;
}

// The send function that forms the body of the send task.
// This task runs whenever there is an audio datagram ready
// to send.
static void sendAudioData()
{
    const char *pUrtpDatagram = NULL;
    struct timeval start;
    struct timeval end;
    struct timeval badStart;
    bool badStarted = false;
    struct timespec runAnywayTime;
    unsigned long durationMs;
    int retValue;
    bool okToDelete = false;

    runAnywayTime.tv_sec = AUDIO_SEND_DATA_RUN_ANYWAY_TIME_S;
    runAnywayTime.tv_nsec = 0;

    while (sem_trywait(&gStopSendTask) != 0) {
        // Always try to send if the socket is connected so that
        // the server can return a timing datagram which confirms
        // that a proper connection has been made; the server check
        // task will set gAudioCommsConnected to true or false
        if (gTcpConnected) {
            // Wait for at least one datagram to be ready to send
            sem_timedwait(&gUrtpDatagramReady, &runAnywayTime);
            while (gTcpConnected && (gpUrtp != NULL) && (pUrtpDatagram = gpUrtp->getUrtpDatagram()) != NULL) {
                okToDelete = false;
                gettimeofday(&start, NULL);
                // Send the datagram
                //LOG(EVENT_SEND_START, (int) pUrtpDatagram);
                retValue = tcpSend(pUrtpDatagram, URTP_DATAGRAM_SIZE);

                if (retValue != URTP_DATAGRAM_SIZE) {
                    if (!badStarted) {
                        badStarted = true;
                        gettimeofday(&badStart, NULL);
                    }
                    LOG(EVENT_SEND_FAILURE, retValue);
                    gNumAudioSendFailures++;
                } else {
                    badStarted = false;
                    gNumAudioBytesSent += retValue;
                    okToDelete = true;
                    //  If we really are streaming then call the callback having sent something
                    if (gAudioCommsConnected && (gpNowStreamingHandler != NULL)) {
                        gpNowStreamingHandler();
                    }
                }
                //LOG(EVENT_SEND_STOP, (int) pUrtpDatagram);

                if (badStarted) {
                    // If the connection has gone, set a flag that will be picked up outside this function and
                    // cause us to shut down cleanly
                    gettimeofday(&end, NULL);
                    durationMs = (unsigned long) timeDifference(&badStart, &end) / 1000;
                    if (durationMs > AUDIO_MAX_DURATION_SOCKET_ERRORS_MS) {
                        LOG(EVENT_SOCKET_ERRORS_FOR_TOO_LONG, durationMs);
                    }
                    if ((retValue == ENOTCONN) ||
                        (retValue == ECONNRESET) ||
                        (retValue == ENOBUFS) ||
                        (retValue == EPIPE)) {
                        LOG(EVENT_SOCKET_BAD, retValue);
                    }
                }
                gettimeofday(&end, NULL);
                durationMs = (unsigned long) timeDifference(&start, &end) / 1000;
                gAverageAudioDatagramSendDuration += durationMs;
                gNumAudioDatagrams++;

                if (durationMs > BLOCK_DURATION_MS) {
                    gNumAudioDatagramsSendTookTooLong++;
                } else {
                    //LOG(EVENT_SEND_DURATION, duration);
                }
                if (durationMs > gWorstCaseAudioDatagramSendDuration) {
                    gWorstCaseAudioDatagramSendDuration = durationMs;
                    LOG(EVENT_NEW_PEAK_SEND_DURATION, durationMs);
                }

                if (okToDelete) {
                    gpUrtp->setUrtpDatagramAsRead(pUrtpDatagram);
                }

                // Make sure the watchdog is fed
                if (gpWatchdogHandler != NULL) {
                    gpWatchdogHandler();
                }
            } // while() wait on URTP datagram
        } else { // if() audio comms is connected
            // Make sure the watchdog is fed
            if (gpWatchdogHandler != NULL) {
                gpWatchdogHandler();
            }
            sleep(1);
        }
    } // while() wait on gStopSendTask semaphore
}

// Check the status of the audio streaming server
// This task should be run in the background.  It will
// check that we get a timing datagram within the expected
// interval
static void checkServerStatus()
{
    char timingDatagram[AUDIO_TIMING_DATAGRAM_LENGTH];
    char *pBuffer;
    long long int timestamp;
    long long unsigned int datagramSendTime;
    uint16_t lastUrtpSequenceNumber;
    uint16_t sequenceNumber;
    int x;
    int noValidTimingDatagramCount = 0;
    struct timeval start;

    while (sem_trywait(&gStopServerStatusTask) != 0) {
        if (gTcpConnected && (gpUrtp != NULL)) {
            lastUrtpSequenceNumber = (uint16_t) gpUrtp->getUrtpSequenceNumber();
            // Wait for up to 1 second for a timing datagram (of the right length) on the non-blocking socket
            gettimeofday(&start, NULL);
            for (pBuffer = timingDatagram; (pBuffer < timingDatagram + sizeof(timingDatagram)) && (timeDifference(&start, NULL) < 1000000);) {
                //LOG(EVENT_RECEIVE_START, 0);
                x = recv(gStreamingSocket, pBuffer, 1, 0);
                if (x > 0) {
                    //LOG(EVENT_RECEIVE_STOP, x); 
                    if (*pBuffer == SYNC_BYTE) {
                        for (pBuffer += 1; (pBuffer < timingDatagram + sizeof(timingDatagram)) && (timeDifference(&start, NULL) < 1000000);) {
                            //LOG(EVENT_RECEIVE_START, 0);
                            x = recv(gStreamingSocket, pBuffer, timingDatagram + sizeof(timingDatagram) - pBuffer, 0);
                            if (x > 0) {
                                pBuffer += x;
                                //LOG(EVENT_RECEIVE_STOP, x);
                            } else {
                                if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                                    LOG(EVENT_RECEIVE_FAILURE, errno);
                                } else {
                                    //LOG(EVENT_RECEIVE_STOP, 0);
                                }
                            }
                            if (pBuffer < timingDatagram + sizeof(timingDatagram)) {
                                usleep(100000); // Sleep only if we've not got the lot
                            }
                        }
                    }
                } else {
                    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                        LOG(EVENT_RECEIVE_FAILURE, errno);
                    } else {
                        //LOG(EVENT_RECEIVE_STOP, 0);
                    }
                }
                if (pBuffer < timingDatagram + sizeof(timingDatagram)) {
                    usleep(100000); // Sleep only if we've not got the lot
                }
            }

            if (pBuffer == timingDatagram + sizeof(timingDatagram)) {
                timestamp = getUSeconds();
                // Is the sequence number in the right range?
                sequenceNumber = (((int) timingDatagram[1]) << 8) + timingDatagram[2];
                LOG(EVENT_TIMING_DATAGRAM_RECEIVED, sequenceNumber);
                if (sequenceNumber > lastUrtpSequenceNumber - (AUDIO_TIMING_DATAGRAM_AGE_S * 1000 / BLOCK_DURATION_MS)) {
                    // Yup, it's a usable timing datagram
                    noValidTimingDatagramCount = 0;
                    if (!gAudioCommsConnected) {
                        LOG(EVENT_AUDIO_SERVER_CONNECTED, lastUrtpSequenceNumber);
                        printf("Now connected to audio streaming server.\n");
                        gAudioCommsConnected = true;
                    }
                    // Get the send time of the audio datagram
                    datagramSendTime = ((((long long unsigned int) timingDatagram[3]) << 54) + (((long long unsigned int) timingDatagram[4]) << 48) +
                                        (((long long unsigned int) timingDatagram[5]) << 40) + (((long long unsigned int) timingDatagram[6]) << 32) + 
                                        (((long long unsigned int) timingDatagram[7]) << 24) + (((long long unsigned int) timingDatagram[8]) << 16) +
                                        (((long long unsigned int) timingDatagram[9]) << 8)  + (((long long unsigned int) timingDatagram[10])));
                    LOG(EVENT_ROUNDTRIP_DELAY_MICROSECONDS, (int)((long long unsigned int) timestamp - datagramSendTime));
                } else {
                    // If we're receiving very old timings then it is better to close the link
                    // and re-establish to flush out any delay
                    LOG(EVENT_TIMING_DATAGRAM_TIMEOUT, lastUrtpSequenceNumber);
                    gAudioCommsConnected = false;
                    noValidTimingDatagramCount = 0;
                }
            } else {
                noValidTimingDatagramCount++;
                LOG(EVENT_NO_TIMING_DATAGRAM_RECEIVED, noValidTimingDatagramCount);
                if (noValidTimingDatagramCount > AUDIO_TIMING_DATAGRAM_WAIT_S) {
                    LOG(EVENT_TIMING_DATAGRAM_TIMEOUT, lastUrtpSequenceNumber);
                    gAudioCommsConnected = false;
                    noValidTimingDatagramCount = 0;
                }
            }

            usleep(100000);
        }
    }
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONTROL
 * -------------------------------------------------------------- */

// Start up PCM audio.
// Note: here be multiple return statements.
static bool startPcm()
{
    int rc;
    int size;
    unsigned int val;
    int dir;
    snd_pcm_uframes_t pcmFrames;    

    LOG(EVENT_PCM_START, 0);

    // Open PCM device for recording (capture)
    rc = snd_pcm_open(&gpPcmHandle, gpAlsaPcmDeviceName, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        LOG(EVENT_PCM_START_FAILURE, 1);
        printf("Unable to open pcm device: %s.\n", snd_strerror(rc));
        return false;
    }

    // Allocate a hardware parameters object
    snd_pcm_hw_params_alloca(&gpPcmHwParams);

    // Fill it in with default values
    snd_pcm_hw_params_any(gpPcmHandle, gpPcmHwParams);

    // Set the desired hardware parameters...
    // Interleaved mode
    snd_pcm_hw_params_set_access(gpPcmHandle, gpPcmHwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    // Signed 32-bit little-endian format
    snd_pcm_hw_params_set_format(gpPcmHandle, gpPcmHwParams, SND_PCM_FORMAT_S32_LE);
    // Stereo
    snd_pcm_hw_params_set_channels(gpPcmHandle, gpPcmHwParams, 2);
    // Sampling rate
    val = SAMPLING_FREQUENCY;
    snd_pcm_hw_params_set_rate_near(gpPcmHandle, gpPcmHwParams, &val, &dir);
    // Set period size in frames
    snd_pcm_hw_params_set_period_size_near(gpPcmHandle, gpPcmHwParams, &gPcmFrames, &dir);

    // Write the parameters to the driver
    rc = snd_pcm_hw_params(gpPcmHandle, gpPcmHwParams);
    if (rc < 0) {
        LOG(EVENT_PCM_START_FAILURE, 2);
        printf("Unable to set HW parameters: %s.\n", snd_strerror(rc));
        return false;
    }
    
    // Check that the buffer size we've ended up with is correct
    snd_pcm_hw_params_get_period_size(gpPcmHwParams, &pcmFrames, &dir);
    assert(pcmFrames == gPcmFrames);
    
#ifdef AUDIO_TEST_OUTPUT_FILENAME
    gpAudioOutputFile = fopen(AUDIO_TEST_OUTPUT_FILENAME, "wb+");
    if (gpAudioOutputFile == NULL) {
        printf("Cannot open audio test output file %s (%s).\n", AUDIO_TEST_OUTPUT_FILENAME, strerror(errno));
    }
#endif
    
    return true;
}

// Stop PCM audio.
static void stopPcm()
{
    LOG(EVENT_PCM_STOP, 0);
    if (gpPcmHandle != NULL) {
        snd_pcm_drop(gpPcmHandle);
        snd_pcm_close(gpPcmHandle);
        gpPcmHandle = NULL;
    }

#ifdef AUDIO_TEST_OUTPUT_FILENAME
    if (gpAudioOutputFile != NULL) {
        fclose(gpAudioOutputFile);
        gpAudioOutputFile = NULL;
    }
#endif
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Start audio streaming.
// Note: here be multiple return statements.
bool startAudioStreaming(const char *pAlsaPcmDeviceName,
                         const char *pAudioServerUrl,
                         int maxShift,
                         void(*pWatchdogHandler)(void),
                         void(*pNowStreamingHandler)(void))
{
    gpAlsaPcmDeviceName = pAlsaPcmDeviceName;
    gpAudioServerUrl = pAudioServerUrl;
    gpWatchdogHandler = pWatchdogHandler;
    gpNowStreamingHandler = pNowStreamingHandler;

    printf("Initialising semaphores...\n");
    if (sem_init(&gUrtpDatagramReady, false, 0) != 0) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 0);
        printf("Error initialising gUrtpDatagramReady semaphore (%s).\n", strerror(errno));
        return false;
    }
    if (sem_init(&gStopEncodeTask, false, 0) != 0) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 1);
        printf("Error initialising gStopEncodeTask semaphore (%s).\n", strerror(errno));
        return false;
    }
    if (sem_init(&gStopSendTask, false, 0) != 0) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 2);
        printf("Error initialising gStopSendTask semaphore (%s).\n", strerror(errno));
        return false;
    }
    if (sem_init(&gStopServerStatusTask, false, 0) != 0) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 3);
        printf("Error initialising gStopServerStatusTask semaphore (%s).\n", strerror(errno));
        return false;
    }

    // Start the per-second monitor tick and reset the diagnostics
    LOG(EVENT_AUDIO_STREAMING_START, 0);
    gSecondTicker = startTimer(1000000L, TIMER_PERIODIC, audioMonitor, NULL);

    if (!startAudioStreamingConnection()) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 4);
        return false;
    }

    printf("Starting task to check that the audio streaming server is there...\n");
    if (gpServerStatusTask == NULL) {
        gpServerStatusTask = new std::thread(checkServerStatus);
        if (gpServerStatusTask == NULL) {
            LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 5);
            printf("Error starting task (%s).\n", strerror(errno));
            return false;
        }
    }

    printf("Setting up URTP...\n");
    gpUrtp = new Urtp(&datagramReadyCb, &datagramOverflowStartCb, &datagramOverflowStopCb);
    if (!gpUrtp->init((void *) &gDatagramStorage, maxShift)) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 6);
        printf("Unable to start URTP.\n");
        return false;
    }

    printf("Starting PCM...\n");
    if (!startPcm()) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 7);
        return false;
    }

    printf("Starting task to send audio data...\n");
    if (gpSendTask == NULL) {
        gpSendTask = new std::thread(sendAudioData);
        if (gpSendTask == NULL) {
            LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 8);
            printf("Error starting task (%s).\n", strerror(errno));
            return false;
        }
    }

    printf("Starting task to encode audio data...\n");
    if (gpEncodeTask == NULL) {
        gpEncodeTask = new std::thread(encodeAudioData);
        if (gpEncodeTask == NULL) {
            LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 9);
            printf("Error starting task (%s).\n", strerror(errno));
            return false;
        }
    }

    printf("Now, hopefully, streaming audio.\n");

    // Wait a few second for the link to the server to really establish
    for (int x = 0; !gAudioCommsConnected && (x < AUDIO_SERVER_LINK_ESTABLISHMENT_WAIT_S); x++) {
        // Make sure the watchdog is fed
        if (gpWatchdogHandler != NULL) {
            gpWatchdogHandler();
        }
        sleep(1);
    }

    return true;
}

// Stop audio streaming.
void stopAudioStreaming()
{
    LOG(EVENT_AUDIO_STREAMING_STOP, 0);
    
    gpAlsaPcmDeviceName = NULL;
    gpAudioServerUrl = NULL;
    gpWatchdogHandler = NULL;
    gpNowStreamingHandler = NULL;

    if (gpEncodeTask != NULL) {
        LOG(EVENT_AUDIO_STREAMING_STOP, 1);
        printf("Stopping audio encode task...\n");
        sem_post(&gStopEncodeTask);
        gpEncodeTask->join();
        delete gpEncodeTask;
        gpEncodeTask = NULL;
        printf("Audio encode task stopped.\n");
        LOG(EVENT_AUDIO_STREAMING_STOP, 2);
    }
    
    if (gpSendTask != NULL) {
        LOG(EVENT_AUDIO_STREAMING_STOP, 3);
        printf("Stopping audio send task...\n");
        sem_post(&gStopSendTask);
        gpSendTask->join();
        delete gpSendTask;
        gpSendTask = NULL;
        printf("Audio send task stopped.\n");
        LOG(EVENT_AUDIO_STREAMING_STOP, 4);
    }
    
    if (gpServerStatusTask != NULL) {
        LOG(EVENT_AUDIO_STREAMING_STOP, 5);
        printf("Stopping audio server status task...\n");
        sem_post(&gStopServerStatusTask);
        gpServerStatusTask->join();
        delete gpServerStatusTask;
        gpServerStatusTask = NULL;
        printf("Audio server status task stopped.\n");
        LOG(EVENT_AUDIO_STREAMING_STOP, 6);
    }
    
    LOG(EVENT_AUDIO_STREAMING_STOP, 7);
    stopPcm();
    stopAudioStreamingConnection();
    stopTimer(gSecondTicker);
    sem_destroy(&gUrtpDatagramReady);
    sem_destroy(&gStopEncodeTask);
    sem_destroy(&gStopSendTask);
    sem_destroy(&gStopServerStatusTask);
    if (gpUrtp != NULL) {
        delete gpUrtp;
        gpUrtp = NULL;
    }

    printf("Audio streaming stopped.\n");
}

// Return whether audio is streaming or not.
bool audioIsStreaming()
{
    return gAudioCommsConnected;
}

// End of file
