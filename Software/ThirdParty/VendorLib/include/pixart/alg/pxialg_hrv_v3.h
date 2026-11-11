#ifndef _PXIALG_HRV_v3_
#define _PXIALG_HRV_v3_

#include "stdint.h"

#if defined(WIN32) && !defined(PXIALG_STATIC_LIB)
#   ifdef PXIALG_EXPORTS
#       define PXIALG_API   __declspec(dllexport)
#   else
#       define PXIALG_API   __declspec(dllimport)
#   endif
#else
#   define PXIALG_API
#endif	// WIN32


#ifdef __cplusplus
extern "C"
{
#endif
	PXIALG_API int HHRVReset(void);
	PXIALG_API int HHRVRecompute(void);
	PXIALG_API int HHRVGetMemorySize(int enable_lhr, int test_seconds, int sampling_rate);
	PXIALG_API int HHRVOpen(void *p_buffer);
	PXIALG_API int HHRVProcess(int data_ready, float data, float time);	
	PXIALG_API int HHRVProcessMEMS(int data_ready, float data_mx, float data_my, float data_mz, float time);
	PXIALG_API int HHRVProcessD(int data_ready, uint32_t data, float time);	
	PXIALG_API int HHRVProcessM(int data_ready, float *mems_data, float time);

	PXIALG_API int HHRVClose(void);

	PXIALG_API int HHRVGetVersion(void);
	PXIALG_API float HHRVGetSDNN(void);
	PXIALG_API int HHRVGetSDNNLevel(int m_iAge, float dSDNN);
	PXIALG_API float HHRVGetLHRatio(float *nlf, float *nhf);
	PXIALG_API float HHRVGetRMSSD(void);
	PXIALG_API int HHRVGetSleepQuality(int age);
	PXIALG_API int HHRVGetOnlineRRIReportFlag(void);
	PXIALG_API float HHRVGetOnlineRRI(void);
	PXIALG_API int HHRVGetOnlineRRIConfidenceLevel(void);
	PXIALG_API float HHRVGetOnlineRRITimePoint(void);
	PXIALG_API int HHRVGetRRINum(void);
	PXIALG_API float * HHRVGetRRIBuf(void);
	PXIALG_API float HHRVGetNoiseRate(void);
	PXIALG_API float HHRVGetRRIStability(void);
	PXIALG_API float HHRVGetRRIQuality(void);
	PXIALG_API int HHRVGetReadyFlag(void);
	PXIALG_API int HHRVGetHRAvg(void);
	PXIALG_API float HHRVGetMEMSStd(void);
    PXIALG_API int HHRVGetHR(void);
	PXIALG_API float HHRVGetMEMSgs(void);
	PXIALG_API float HHRVGetMEMSwgs(int win);

	PXIALG_API int HHRVAlignment(int ecg_time[], int ecg_rri[], int ecg_len, 
	                             int ppg_rri[], int ppg_cf[], int ppg_len,
	                             float align_ppg_time[], float align_ppg_rri[]);

	//PXIALG_API void HRVSetFreqOffset(float FreqOffset);
	//PXIALG_API void HRVSetDiffTime(float DiffTime, int SampleNum);
	PXIALG_API int HHRVSetMemsScale(int scale);
	PXIALG_API int HHRVSetRRIQualityMode(int mode);

	PXIALG_API int HHRVGetOutput(float* p1, float* p2, float* p3, float* p4, float* p5, float* p6, float* p7, float* p8);

	PXIALG_API int RESPReset(void);
	PXIALG_API int RESPGetMemorySize(int enable_lhr, int enable_rr, int test_seconds, int sampling_rate);
	PXIALG_API int RESPOpen(void *p_buffer);
	PXIALG_API int RESPProcess(int data_ready, float data, float time);
	PXIALG_API int RESPGetVersion(void);
	PXIALG_API int RESPGetRR(void);
	PXIALG_API int RESPClose(void);

	//PXIALG_API int RESPClose(void);
	PXIALG_API float HHRVGetStress(int grade); //v312
#ifdef __cplusplus
}
#endif

#endif /* _PXIALG_HRV_v3_ */

