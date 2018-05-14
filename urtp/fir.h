#ifndef _FIR_H_
#define _FIR_H_

/* FIR filter designed with http://t-filter.appspot.com

sampling frequency: 16000 Hz

fixed point precision: 32 bits

* 0 Hz - 150 Hz
  gain = 0
  desired attenuation = -20 dB
  actual attenuation = n/a

* 1000 Hz - 2000 Hz
  gain = 0.5
  desired ripple = 5 dB
  actual ripple = n/a

* 3000 Hz - 5000 Hz
  gain = 0.9
  desired ripple = 5 dB
  actual ripple = n/a

* 6000 Hz - 8000 Hz
  gain = 1
  desired ripple = 5 dB
  actual ripple = n/a

*/

#define FIR_TAP_NUM 13

typedef struct {
    double history[FIR_TAP_NUM];
    unsigned int lastIndex;
} Fir;

#ifdef __cplusplus
extern "C" {
#endif

void firInit(Fir *f);
void firPut(Fir *f, double input);
double firGet(Fir *f);

#ifdef __cplusplus
}
#endif

#endif