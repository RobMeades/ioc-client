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

// The name of the PCM device (the I2S microphone)
#define AUDIO_PCM_DEVICE_NAME "mic_sv"

// The maximum amount of time allowed to send a
// datagram of audio over TCP.
#define AUDIO_TCP_SEND_TIMEOUT_MS 1500

// If we've had consecutive socket errors on the
// audio streaming socket for this long, it's gone bad.
#define AUDIO_MAX_DURATION_SOCKET_ERRORS_MS 1000

// The maximum length of an audio server URL (including
// terminator).
#define AUDIO_MAX_LEN_SERVER_URL 128

// The default audio setup data.
#define AUDIO_DEFAULT_FIXED_GAIN false
#define AUDIO_DEFAULT_SERVER_URL "ciot.it-sgn.u-blox.com:5065"

/* ----------------------------------------------------------------
 * CALLBACK FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

static void datagramReadyCb(const char * datagram);
static void datagramOverflowStartCb();
static void datagramOverflowStopCb(int numOverflows);

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

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
static struct sockaddr_in *gpAudioServerUrl = NULL;

// Task to read and encode audio data.
static std::thread *gpEncodeTask = NULL;

// Task to send data off to the audio streaming server.
static std::thread *gpSendTask = NULL;

// Semaphore to communicate between tasks.
static sem_t gUrtpDatagramReady;

// The URTP codec.
static Urtp gUrtp(&datagramReadyCb, &datagramOverflowStartCb, &datagramOverflowStopCb);

// The audio send socket.
static int gSocket = -1;

// Flag to indicate that the audio comms channel is up.
static volatile bool gAudioCommsConnected = false;

// Keep track of stats.
static unsigned long gNumAudioSendFailures = 0;
static unsigned long gNumAudioBytesSent = 0;
static unsigned long gAverageAudioDatagramSendDuration = 0;
static unsigned long gNumAudioDatagrams = 0;
static unsigned long gNumAudioDatagramsSendTookTooLong = 0;
static unsigned long gWorstCaseAudioDatagramSendDuration = 0;

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
// This is a ticker call-back, so nothing heavy please.
static void audioMonitor(size_t timerId, void *pUserData)
{
    // Monitor throughput
    if (gNumAudioBytesSent > 0) {
        LOG(EVENT_THROUGHPUT_BITS_S, gNumAudioBytesSent << 3);
        gNumAudioBytesSent = 0;
        LOG(EVENT_NUM_DATAGRAMS_QUEUED, gUrtp.getUrtpDatagramsAvailable());
    }
}

// Start the audio streaming connection.
// This will set up the pAudio structure.
// Note: here be multiple return statements.
static bool startAudioStreamingConnection()
{
    char *pBuf = new char[AUDIO_MAX_LEN_SERVER_URL];
    int port;
    const int setOption = 1;
    struct timeval tv;
    int x;
    
    tv.tv_sec = 1; /* 1 second timeout */

    LOG(EVENT_AUDIO_STREAMING_CONNECTION_START, 0);
    printf("Resolving IP address of the audio streaming server...\n");
    if (gpAudioServerUrl == NULL) {
        gpAudioServerUrl = new struct sockaddr_in;
        getAddressFromUrl(AUDIO_DEFAULT_SERVER_URL, pBuf, AUDIO_MAX_LEN_SERVER_URL);
        printf("[Looking for audio server URL \"%s\"...]\n", pBuf);
        LOG(EVENT_DNS_LOOKUP, 0);
        if (inet_pton(AF_INET, pBuf, gpAudioServerUrl) > 0) {
            printf("[Found it at IP address %s]\n", inet_ntoa(gpAudioServerUrl->sin_addr));
            if (getPortFromUrl(AUDIO_DEFAULT_SERVER_URL, &port)) {
                gpAudioServerUrl->sin_port = port;
                printf("[Logging server port is %d]\n", gpAudioServerUrl->sin_port);
            } else {
                printf("[WARNING: no port number was specified in the audio server URL (\"%s\")]\n",
                       AUDIO_DEFAULT_SERVER_URL);
            }
        } else {
            LOG(EVENT_DNS_LOOKUP_FAILURE, h_errno);
            LOG(EVENT_AUDIO_STREAMING_CONNECTION_START_FAILURE, 1);
            printf("Error, couldn't resolve IP address of audio streaming server.\n");
            return false;
        }
    }
    
    printf("Opening socket to server for audio comms...\n");
    LOG(EVENT_SOCKET_OPENING, 0);
    gSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (gSocket < 0) {
        LOG(EVENT_SOCKET_OPENING_FAILURE, errno);
        printf("Could not open TCP socket to audio streaming server (error %d).\n", errno);
        return false;
    }
    LOG(EVENT_SOCKET_OPENED, gSocket);
    
    printf("Setting timeout in TCP socket options...\n");
    x = setsockopt(gSocket, SOL_SOCKET, SO_SNDTIMEO, (void *) &tv, sizeof(tv));
    if (x < 0) {
        LOG(EVENT_TCP_CONFIGURATION_FAILURE, errno);
        printf("Could not set timeout in TCP socket options (error %d).\n", errno);
        return false;
    }
    printf("Setting TCP_NODELAY in TCP socket options...\n");
    // Set TCP_NODELAY (1) in level IPPROTO_TCP (6) to 1
    x = setsockopt(gSocket, IPPROTO_TCP, TCP_NODELAY, (void *) &setOption, sizeof(setOption));
    if (x < 0) {
        LOG(EVENT_TCP_CONFIGURATION_FAILURE, errno);
        printf("Could not set TCP_NODELAY in socket options (error %d).\n", errno);
        return false;
    }
    LOG(EVENT_TCP_CONFIGURED, 0);
    
    LOG(EVENT_TCP_CONNECTING, x);
    printf("Connecting TCP...\n");
    x = connect(gSocket, (struct sockaddr *) gpAudioServerUrl, sizeof(struct sockaddr));
    if (x < 0) {
        LOG(EVENT_TCP_CONNECT_FAILURE, errno);
        printf("Could not connect TCP socket (error %d).\n", errno);
        return false;
    }
    
    gAudioCommsConnected = true;

    return true;
}

// Stop the audio streaming connection.
static void stopAudioStreamingConnection()
{
    LOG(EVENT_AUDIO_STREAMING_CONNECTION_STOP, 0);
    printf("Closing audio server socket...\n");
    close(gSocket);
    gSocket = -1;
    gAudioCommsConnected = false;
}

// Read and encode audio from the PCM device.
static void encodeAudioData()
{
    int retValue;

    while (1) {
        // Get a buffer full of audio data
        retValue = snd_pcm_readi(gpPcmHandle, gRawAudio, gPcmFrames);
        if (retValue == -EPIPE) {
            LOG(EVENT_PCM_OVERRUN, retValue);
            snd_pcm_prepare(gpPcmHandle);
        }
        else if (retValue < 0) {
            LOG(EVENT_PCM_ERROR, retValue);
        }
        else if (retValue != (int) gPcmFrames) {
            LOG(EVENT_PCM_UNDERRUN, retValue);
        } else {
            // Encode the data
            gUrtp.codeAudioBlock(gRawAudio);
        }
    }
}

// Send a buffer of data over a TCP socket
static int tcpSend(const char *pData, int size)
{
    int x = 0;
    int count = 0;
    struct timeval start;

    if (gAudioCommsConnected) {
        gettimeofday(&start, NULL); 
        while ((count < size) && (((unsigned long) timeDifference(&start, NULL) / 1000) < AUDIO_TCP_SEND_TIMEOUT_MS)) {
            x = send(gSocket, pData + count, size - count, 0);
            if (x > 0) {
                count += x;
            }
        }

        if (count < size) {
            LOG(EVENT_TCP_SEND_TIMEOUT, size - count);
        }

        if (x < 0) {
            count = x;
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
    unsigned long durationMs;
    int retValue;
    bool okToDelete = false;

    while (gAudioCommsConnected) {
        // Wait for at least one datagram to be ready to send
        sem_wait(&gUrtpDatagramReady);
        
        while ((pUrtpDatagram = gUrtp.getUrtpDatagram()) != NULL) {
            okToDelete = false;
            gettimeofday(&start, NULL);
            // Send the datagram
            if (gAudioCommsConnected) {
                //LOG(EVENT_SEND_START, (int) pUrtpDatagram);
                retValue = tcpSend(pUrtpDatagram, URTP_DATAGRAM_SIZE);

                if (retValue != URTP_DATAGRAM_SIZE) {
                    gettimeofday(&badStart, NULL);
                    LOG(EVENT_SEND_FAILURE, retValue);
                    gNumAudioSendFailures++;
                } else {
                    gNumAudioBytesSent += retValue;
                    okToDelete = true;
                }
                //LOG(EVENT_SEND_STOP, (int) pUrtpDatagram);

                if (retValue < 0) {
                    // If the connection has gone, set a flag that will be picked up outside this function and
                    // cause us to start again
                    gettimeofday(&end, NULL);
                    durationMs = (unsigned long) timeDifference(&badStart, &end) / 1000;
                    if (durationMs > AUDIO_MAX_DURATION_SOCKET_ERRORS_MS) {
                        LOG(EVENT_SOCKET_ERRORS_FOR_TOO_LONG, durationMs);
                        gAudioCommsConnected = false;
                    }
                    if ((retValue == ENOTCONN) ||
                        (retValue == ECONNRESET) ||
                        (retValue == ENOBUFS) ||
                        (retValue == EPIPE)) {
                        LOG(EVENT_SOCKET_BAD, retValue);
                        gAudioCommsConnected = false;
                    }
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
                gUrtp.setUrtpDatagramAsRead(pUrtpDatagram);
            }
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
    rc = snd_pcm_open(&gpPcmHandle, AUDIO_PCM_DEVICE_NAME, SND_PCM_STREAM_CAPTURE, 0);
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
    
    return true;
}

// Stop PCM audio.
static void stopPcm()
{
    LOG(EVENT_PCM_STOP, 0);
    snd_pcm_drain(gpPcmHandle);
    snd_pcm_close(gpPcmHandle);
}

// Stop audio streaming.
static void stopStreaming()
{
    LOG(EVENT_AUDIO_STREAMING_STOP, 0);
    
    if (gpEncodeTask != NULL) {
        printf("Stopping audio encode task...\n");
        gpEncodeTask->~thread();
        gpEncodeTask->join();
        delete gpEncodeTask;
        gpEncodeTask = NULL;
        printf("Audio encode task stopped.\n");
    }
    
    if (gpSendTask != NULL) {
        // Wait for any on-going transmissions to complete
        sleep(2);

        printf("Stopping audio send task...\n");
        gpSendTask->~thread();
        gpSendTask->join();
        delete gpSendTask;
        gpSendTask = NULL;
        printf("Audio send task stopped.\n");
    }
    
    sem_destroy(&gUrtpDatagramReady);
    stopPcm();
    stopAudioStreamingConnection();
    stopTimer(gSecondTicker);

    printf("Audio streaming stopped.\n");
}

// Start audio streaming.
// Note: here be multiple return statements.
static bool startStreaming()
{
    // Start the per-second monitor tick and reset the diagnostics
    LOG(EVENT_AUDIO_STREAMING_START, 0);
    gSecondTicker = startTimer(1000000L, TIMER_PERIODIC, audioMonitor, NULL);

    if (!startAudioStreamingConnection()) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 0);
        return false;
    }

    printf("Setting up URTP...\n");
    if (!gUrtp.init((void *) &gDatagramStorage, AUDIO_DEFAULT_FIXED_GAIN)) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 1);
        printf("Unable to start URTP.\n");
        return false;
    }

    printf("Starting PCM...\n");
    if (!startPcm()) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 2);
        return false;
    }

    printf("Initialising sempahore...\n");
    if (sem_init(&gUrtpDatagramReady, false, 0) != 0) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 3);
        printf("Error initialising semaphore (%d).\n", errno);
        return false;
    }

    printf("Starting task to send audio data...\n");
    if (gpSendTask == NULL) {
        gpSendTask = new std::thread(sendAudioData);
        if (gpSendTask == NULL) {
            LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 4);
            printf("Error starting task (%d).\n", errno);
            return false;
        }
    }

    printf("Starting task to encode audio data...\n");
    if (gpEncodeTask == NULL) {
        gpEncodeTask = new std::thread(encodeAudioData);
        if (gpEncodeTask == NULL) {
            LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 5);
            printf("Error starting task (%d).\n", errno);
            return false;
        }
    }

    printf("Now streaming audio.\n");

    return true;
}

// End of file
