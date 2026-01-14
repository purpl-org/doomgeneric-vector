//
// Created by ekeleze on 1/14/26.
//

#ifndef DOOMGENERIC_PRBOOM_COMPAT_H
#define DOOMGENERIC_PRBOOM_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include "../memio.h"
#include "../doomtype.h"


#define LO_INFO  0
#define LO_WARN  1
#define LO_ERROR 2

#define lprintf(level, ...) printf(__VA_ARGS__)

#define M_fopen fopen

extern int mus_opl_gain;

#define dboolean boolean

#define doom_htows(x) (x)

#endif //DOOMGENERIC_PRBOOM_COMPAT_H