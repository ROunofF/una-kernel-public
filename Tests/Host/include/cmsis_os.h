/**
 * @file cmsis_os.h
 * @brief Host stand-in. Some kernel sources include the RTOS v1 header name.
 *
 * On target it pulls in the v2 API; here it does the same, forwarding to the
 * host stub.
 */
#ifndef CMSIS_OS_H
#define CMSIS_OS_H

#include "cmsis_os2.h"

#endif // CMSIS_OS_H
