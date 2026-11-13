#include "core.h"
void voice_event(const Event*,int,double);
int voice_usage(const uint8_t*,uint32_t);
void voice_slot(const uint8_t*,uint32_t,int,int,int,int,double);

/* Channel-code quality only: does not advance any speech decoder. */
int voice_quality(const uint8_t*,uint32_t,int);
