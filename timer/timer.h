/* NOTE: this code copied from:
 * https://qnaplus.com/implement-periodic-timer-linux/
 */

#ifndef TIMER_H_
#define TIMER_H_

/* ----------------------------------------------------------------
 * GENERAL TYPES
 * -------------------------------------------------------------- */

/** The types of timer that can be created.
 */
typedef enum {
    TIMER_SINGLE_SHOT,
    TIMER_PERIODIC
} TimerType;
 
/** Timer callback function type.
 */
typedef void(*TimerCallback)(size_t timerId, void *pUserData);
 
/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise this code.
 ** @return true on success, otherwise false.
 */
bool initTimers();

/** Deinitialise this code.
 */
void deinitTimers();

/** Get the difference between two times in microseconds.
 * @param pStart start time.
 * @param pEnd end time; if this is NULL the current time is used.
 * @return  time difference between pStart and pEnd in microseconds.
 */
unsigned long long timeDifference(struct timeval *pStart, struct timeval *pEnd);

/** Create and start a timer.
 * @param timeMicroseconds  the timeout in microseconds.
 * @param type              the type of timer to start.  Note that one-shot
 *                          timers must be removed from the list once they have
 *                          expired.
 * @param callback          the function to be called when the timer expires.
 * @param pUserData         the user data to pass to the callback function (may be NULL).
 * @return                  the ID of the timer.
 */
size_t startTimer(unsigned long timeMicroseconds, TimerType type, TimerCallback callback, void *pUserData);

/** Read a timer.
 * @param timerId  the ID of the timer to read.
 * @return  the current value of the timer in milliseconds.
 */
unsigned long readTimer(size_t timerId);

/** Stop a timer.
 * @param timerId  the ID of the timer to stop.
 * @return  the time for which the timer ran.
 */
unsigned long stopTimer(size_t timerId);

#endif /* TIMER_H_ */
