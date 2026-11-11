/**
 ******************************************************************************
 * @file    TimeUtils.h
 * @brief   Time utilities.
 *          Convert epoch (UTC) -> calendar date (UTC) and vice versa.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __TIME_UTILS_H
#define __TIME_UTILS_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <time.h>
#include <stdbool.h>

struct tm* TimeUtils_epoch2tm(time_t t, struct tm *out);
time_t     TimeUtils_tm2epoch(const struct tm *tm);

#ifdef __cplusplus
 }
#endif

#endif /* __TIME_UTILS_H */

