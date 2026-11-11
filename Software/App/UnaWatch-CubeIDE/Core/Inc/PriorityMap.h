#ifndef __PriorityMap_h__
#define __PriorityMap_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"

void PriorityMap_init(void);
void PriorityMap_setFor(IRQn_Type IRQn);
void PriorityMap_enable(IRQn_Type irqIndex);

#ifdef __cplusplus
 }
#endif

#endif
