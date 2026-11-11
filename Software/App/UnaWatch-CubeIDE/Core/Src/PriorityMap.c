#include "PriorityMap.h"

#include <assert.h>

///////////////////////////////////////////////////////////////////////////////
//// Data types
///////////////////////////////////////////////////////////////////////////////

typedef struct {
    IRQn_Type irqIndex;
    uint8_t   priority;
    uint8_t   subPriority;
} PriorityMapItem_t;

///////////////////////////////////////////////////////////////////////////////
//// Private data
///////////////////////////////////////////////////////////////////////////////

static const PriorityMapItem_t mMap[] = {
        { USART1_IRQn,           0, 0 },     // Logger

        { GPDMA1_Channel6_IRQn, 15, 0 },    // Logger DMA TX
        { GPDMA1_Channel7_IRQn, 15, 0 },    // Logger DMA RX

        { SysTick_IRQn,         15, 0 },
        { PendSV_IRQn,          15, 0 }
};

///////////////////////////////////////////////////////////////////////////////
//// Public code
///////////////////////////////////////////////////////////////////////////////

void PriorityMap_init(void)
{
//    PriorityMap_setFor(SysTick_IRQn);
//    PriorityMap_setFor(PendSV_IRQn);
}

void PriorityMap_setFor(IRQn_Type irqIndex)
{
    uint32_t index;

    for (index = 0; index < ARRAY_SIZE(mMap); index++) {
        if (mMap[index].irqIndex != irqIndex) {
            continue;
        }

        HAL_NVIC_SetPriority(mMap[index].irqIndex,
                             mMap[index].priority,
                             mMap[index].subPriority);
        
        return;
    }

    assert(false);
}

void PriorityMap_enable(IRQn_Type irqIndex)
{
    PriorityMap_setFor(irqIndex);
    HAL_NVIC_EnableIRQ(irqIndex);
}
