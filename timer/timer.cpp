/* NOTE: this code copied from:
 * https://qnaplus.com/implement-periodic-timer-linux/
 */

#include <stdint.h>
#include <string.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <timer.h>

#define MAX_TIMER_COUNT 100

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Definition of a timer linked list.
typedef struct TimerNodeTag {
    int fd;
    TimerCallback callback;
    void *pUserData;
    unsigned long timeMicroseconds;
    TimerType type;
    struct timeval startTime;
    struct timeval expiryTime;
    bool hasExpired;
    struct TimerNodeTag *pNext;
} TimerNode;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The ID of the timer processing thread.
static pthread_t gThreadId;

// Anchor for the timer linked list.
static TimerNode *gpHead = NULL;

// Keep track of the number of timers running.
static int gNumTimers = 0;

// A bool to know we're running.
static bool gInited = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * --------------------------------------------------------------*/

// Get a timer ID from a file descriptor.
static TimerNode *pGetTimerFromFd(int fd)
{
    bool foundIt = false;
    TimerNode *pTmp = gpHead;

    while (pTmp && !foundIt) {
        if (pTmp->fd == fd) {
            foundIt = true;
        } else {
            pTmp = pTmp->pNext;
        }
    }
    
    return pTmp;
}

// The timer thread.
static void *pTimerThread(void *pData /* not used */)
{
    struct pollfd ufds[MAX_TIMER_COUNT];
    int iMaxCount = 0;
    TimerNode *pTmp = NULL;
    int readFds = 0;
    int i;
    int s;
    long long int exp;

    while (1) {
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        iMaxCount = 0;
        pTmp = gpHead;
        memset(ufds, 0, sizeof(struct pollfd) *MAX_TIMER_COUNT);
        while (pTmp) {
            ufds[iMaxCount].fd = pTmp->fd;
            ufds[iMaxCount].events = POLLIN;
            iMaxCount++;
            pTmp = pTmp->pNext;
        }

        readFds = poll(ufds, iMaxCount, 100);
        if (readFds > 0) {
            for (i = 0; i < iMaxCount; i++) {
                if (ufds[i].revents & POLLIN) {
                    s = read(ufds[i].fd, &exp, sizeof(exp));
                    if (s != sizeof(exp)) {
                        i = iMaxCount; // to force an exit
                    } else {
                        pTmp = pGetTimerFromFd(ufds[i].fd);
                        if (pTmp) {
                            gettimeofday(&(pTmp->expiryTime), NULL);
                            pTmp->hasExpired = true;
                            if (pTmp->callback) {
                                pTmp->callback((size_t) pTmp, pTmp->pUserData);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return NULL;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise this code.
bool initTimers()
{
    if (pthread_create(&gThreadId, NULL, pTimerThread, NULL) == 0) {
        gInited = true;
    }
    return gInited;
}

// Shut down this code.
void deinitTimers()
{
    if (gInited) {
        while (gpHead) {
            stopTimer((size_t) gThreadId);
        }

        pthread_cancel(gThreadId);
        pthread_join(gThreadId, NULL);
    }
    
    gInited = false;
    gNumTimers = 0;
}

// Get the difference between two times in microseconds
unsigned long long timeDifference(struct timeval *pStart, struct timeval *pEnd)
{
    unsigned long long startMicroseconds = (unsigned long long) pStart->tv_sec * 1000000L + (unsigned long long) pStart->tv_usec;
    unsigned long long endMicroseconds;
    
    if (pEnd == NULL) {
        struct timeval endTime;
        gettimeofday(&(endTime), NULL);
        endMicroseconds = (unsigned long long) endTime.tv_sec * 1000000L + (unsigned long long) endTime.tv_usec;
    } else {
        endMicroseconds = (unsigned long long) pEnd->tv_sec * 1000000L + (unsigned long long) pEnd->tv_usec;
    }
    
    return endMicroseconds - startMicroseconds;
}

// Start a timer.
size_t startTimer(unsigned long timeMicroseconds, TimerType type, TimerCallback callback, void *pUserData)
{
    TimerNode *pNewNode = NULL;
    struct itimerspec newValue;

    if (gNumTimers < MAX_TIMER_COUNT) {
        pNewNode = new TimerNode;
        pNewNode->callback  = callback;
        pNewNode->pUserData = pUserData;
        pNewNode->timeMicroseconds  = timeMicroseconds;
        pNewNode->type = type;
        memset(&(pNewNode->expiryTime), 0, sizeof(pNewNode->expiryTime));
        memset(&(pNewNode->startTime), 0, sizeof(pNewNode->startTime));
        pNewNode->hasExpired = false;
        pNewNode->fd = timerfd_create(CLOCK_REALTIME, 0);

        if (pNewNode->fd == -1) {
            delete pNewNode;
            pNewNode = NULL;
        } else {
            newValue.it_value.tv_sec  = timeMicroseconds / 1000000UL;
            newValue.it_value.tv_nsec = (timeMicroseconds * 1000) % 1000000UL;

            if (type == TIMER_PERIODIC) {
                newValue.it_interval.tv_sec  = timeMicroseconds / 1000000UL;
                newValue.it_interval.tv_nsec = (timeMicroseconds * 1000) % 1000000UL;
            } else {
                newValue.it_interval.tv_sec = 0;
                newValue.it_interval.tv_nsec = 0;
            }

            gettimeofday(&(pNewNode->startTime), NULL);
            timerfd_settime(pNewNode->fd, 0, &newValue, NULL);

            /* Inserting the timer node into the list */
            pNewNode->pNext = gpHead;
            gpHead = pNewNode;
            gNumTimers++;
        }
    }
    
    return (size_t) pNewNode;
}

// Read a timer.
unsigned long readTimer(size_t timerId)
{
    TimerNode *pTmp = NULL;
    TimerNode *pNode = (TimerNode *) timerId;
    unsigned long durationMicroseconds = 0;

    if (pNode != NULL) {
        
        if (pNode == gpHead) {
            gpHead = gpHead->pNext;
        }

        pTmp = gpHead;
        while (pTmp && (pTmp->pNext != pNode)) {
            pTmp = pTmp->pNext;
        }

        if (pNode) {
            if (pNode->hasExpired) {
                durationMicroseconds = (unsigned long) timeDifference(&(pTmp->startTime), &(pTmp->expiryTime));
            } else {
                struct timeval timeNow;
                gettimeofday(&(timeNow), NULL);
                durationMicroseconds = (unsigned long) timeDifference(&(pTmp->startTime), &(timeNow));
            }
        }
    }

    return durationMicroseconds;
}

// Stop a timer.
unsigned long stopTimer(size_t timerId)
{
    TimerNode *pTmp = NULL;
    TimerNode *pNode = (TimerNode *) timerId;
    unsigned long durationMicroseconds = 0;

    if (pNode != NULL) {
        close(pNode->fd);
        
        if (pNode == gpHead) {
            gpHead = gpHead->pNext;
        }

        pTmp = gpHead;
        while (pTmp && (pTmp->pNext != pNode)) {
            pTmp = pTmp->pNext;
        }

        if (pTmp && (pTmp->pNext)) {
            pTmp->pNext = pTmp->pNext->pNext;
        }

        if (pNode) {
            if (pNode->hasExpired) {
                durationMicroseconds = (unsigned long) timeDifference(&(pNode->startTime), &(pNode->expiryTime));
            } else {
                struct timeval timeNow;
                gettimeofday(&(timeNow), NULL);
                durationMicroseconds = (unsigned long) timeDifference(&(pNode->startTime), &(timeNow));
            }
            delete pNode;
        }
        
        if (gNumTimers > 0) {
            gNumTimers--;
        }
    }
    
    return durationMicroseconds;
}

// End of file
