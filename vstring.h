#ifndef VSTRING_H_TFCSVBEE
#define VSTRING_H_TFCSVBEE

#include "datatypes.h"

#define VStringRaw(s) ((uint32_t *)(s) - 2)
#define VStringSize(v) (VStringRaw(v)[0])
#define VStringHash(v) (VStringRaw(v)[1])

#endif /* end of include guard: VSTRING_H_TFCSVBEE */
