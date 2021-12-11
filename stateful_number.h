/*
  This class defines a numerical data type with internal state.
  The value stores the previous (old) value together with a time stamp and a status. As parameter, the 
  constructor requires a tolerance value with the following intention: 
  If the new value is inside the interval [old-value - tolerance, old-value + tolerance], then the new
  value is considered as valid, otherwise as invalid. 

  New values are given using the set() method, the current (valid) value is requested with the get() 
  method (get() always returns the last valid value).

  There are some exceptions from this behavior:
  (1) in the initialisation phase (first call of set()), each value is accepted as valid. This phase 
      is defined as a time period from the first call of set(), the time span is defined in the 
      constructor (may be 0, default is 300 seconds)
  (2) in case that for a longer period only invalid values are given by set(), then the object status
      will be reset to the initialisation phase (see above). This time period is defined in the 
      constructor (value 0 disables this behavior)

*/

#ifndef _STATEFUL_NUMBER_H_
#define _STATEFUL_NUMBER_H_

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h> 

#define _STATEFUL_NUMBER_DEBUG_
#ifdef _STATEFUL_NUMBER_DEBUG_
#include <syslog.h>
#endif


template <class numerical> 
class value_check {

private:
    bool init;          // status of the object
    numerical xc, xo;   // current and old value
    numerical xDiff;    // boundary for invalid value
    time_t tc, to;      // timestamp of current and old value
    time_t t_init;      // timestamp of object initialisation
    time_t t_initDuration;  // duration of object initialisation in seconds
    time_t t_limit;     // time boundary for invalid value
#ifdef _STATEFUL_NUMBER_DEBUG_
    bool logging;
    int log_prio;
#endif

    numerical my_abs(const numerical &x) {
        return (x > 0) ? x : -x;
    }

public:
    // constructor...
    value_check(numerical xlimit      /* tolerance for valid value towards the last valid value */, 
                time_t tlimit = 0     /* seconds: timeout for invalid value: if exceeded, then init state is set */, 
                time_t duration = 300 /* seconds: duration of initialisation time */ ) {
        init = true;
        t_init = 0;
        t_initDuration = duration;
        tc = to = 0;
        xc = xo = 0;
        xDiff = xlimit;
        t_limit = tlimit;
#ifdef _STATEFUL_NUMBER_DEBUG_
        logging = false;
        log_prio = LOG_INFO;
#endif
    }

    // default constructor...
    value_check() {
        value_check(0);
    }

    void set(numerical x) {
        time_t now = time(NULL);

        // first check whether the object is still in initialisation state
        if (init) {
            if (t_init > 0) {
                if ((uint32_t)difftime(now, t_init) >= t_initDuration) {
                    init = false;
#ifdef _STATEFUL_NUMBER_DEBUG_
                    if (logging) {
                        syslog(log_prio, "INIT->0: xc/xo->%.0lf/%.0lf, tc/to->%lu/%lu",
                        (double)xc, (double)xo, tc, to);
                    }
#endif
                }
            } else {
                t_init = now;    // very first call
#ifdef _STATEFUL_NUMBER_DEBUG_
                if (logging) {
                    syslog(log_prio, "STARTUP: xc/xo->%.0lf/%.0lf, tc/to->%lu/%lu, xDiff->%.0lf",
                    (double)xc, (double)xo, tc, to, (double)xDiff);
                }
#endif
            }
        } else if ((t_limit > 0) && ((uint32_t)difftime(now, tc) > t_limit)) {
            // in case that last valid value is too long ago, enter initialisation state again
            init = true;
           t_init = now;
#ifdef _STATEFUL_NUMBER_DEBUG_
            if (logging) {
                syslog(log_prio, "INIT->1: xc/xo->%.0lf/%.0lf, tc/to->%lu/%lu",
                (double)xc, (double)xo, tc, to);
            }
#endif
        }

#ifdef _STATEFUL_NUMBER_DEBUG_
        if (logging) {
            syslog(log_prio, "SET: xc/xo/x->%.0lf/%.0lf/%.0f, tc/to/now->%lu/%lu/%lu, xDiff->%.0lf",
            (double)xc, (double)xo, (double)x, tc, to, now, (double)xDiff);
        }
#endif

        // now store the given value...
        if (init) {
            xc = xo = x;
            tc = to = now;
        } else {
            xo = x;
            to = now;
            if ((xDiff == 0) || ((my_abs(x - xc) <= xDiff))) {
                xc = x;
                tc = now;
            }
        }
    }

    numerical get() {
        return xc;      // return the current value (last valid)
    }
    
    void reset() {
        init = true;
        t_init = time(NULL);
    }
    
#ifdef _STATEFUL_NUMBER_DEBUG_    
    void enable_debug() {
        openlog(NULL, LOG_PID, LOG_USER);
        syslog(log_prio, "Start debug log");
        logging = true;
    }

    void disable_debug() {
        closelog();
        logging = false;
    }
#endif    
};

#endif