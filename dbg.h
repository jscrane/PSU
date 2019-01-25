#ifndef __DBG_H__
#define __DBG_H__

extern bool debug;
#define OUT(x) Serial.x
#define DBG(x) if (debug) { OUT(x); }
#define ERR(x) OUT(x)

#endif
