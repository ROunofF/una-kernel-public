#ifndef __PXIALG_SPO2_H__
#define __PXIALG_SPO2_H__


#include <stdint.h>


#if defined(WIN32) && !defined(PXIALG_STATIC_LIB)
#   ifdef PXIALG_EXPORTS
#       define PXIALG_API   __declspec(dllexport)
#   else
#       define PXIALG_API   __declspec(dllimport)
#   endif
#else
#   define PXIALG_API
#endif	// WIN32

#pragma pack(1)
typedef struct SpO2_mems {
	int16_t data[3];
} SpO2_mems_t;

typedef struct SpO2_info {
	float IR_DC;
	float R_DC;
	float G_DC;
	float IR_AC;
	float R_AC;
	float G_AC;
	float R_IR_Ratio;
	float R_G_Ratio;
	float G_IR_Ratio;
	float Noise;
	float SpO2_ref;
	float PI;
	float PVI;
	float RMS;
	float MEMS;
	float Output[5];
	float xyz_mean[3];
	float SpO2;
} SpO2_info_t;
#pragma pack()

typedef enum {
	SPO2_MSG_SUCCESS = 0,
	SPO2_MSG_ALG_NOT_OPEN,
	SPO2_MSG_ALG_REOPEN,
	SPO2_MSG_ALG_NOT_SUPPORT,
    SPO2_MSG_INVALID_ARGUMENT,
	SPO2_MSG_MOVING_SPO2_READY = 0x100,
	SPO2_MSG_MOVING_SPO2_NOT_READY,
	SPO2_MSG_MOVING_SPO2_STOP,
}spo2_msg_code_t;


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


PXIALG_API uint32_t SpO2_OpenSize(void);
PXIALG_API int SpO2_Open(void *mem);
PXIALG_API int SpO2_Close(void);
PXIALG_API int SpO2_GetSpO2(float *spo2);
PXIALG_API int SpO2_GetReadyFlag(uint8_t *ready_flag);
PXIALG_API int SpO2_GetPeriodicity(float *periodicity);
PXIALG_API int SpO2_GetPeriodicityByCh(int ch, float* periodicity);
PXIALG_API int SpO2_Version(void);
PXIALG_API int SpO2_Process(int touch_flag, int total_channel, int *input, int samples_per_ch, float time_in);
PXIALG_API int SpO2_ProcessD(int touch_flag, int total_channel, int *input, int samples_per_ch, float time_in);
PXIALG_API int SpO2_GetFilteredData(int touch_flag, int total_channel, int expo_R,  int expo_IR, int DAC_R, int DAC_IR, float **filter_R, float **filter_IR, int *FilteredDataLength);

//parameters
PXIALG_API int SpO2_SetIIRFilterRatio(float IIRFilterRatio);	//0 ~ 0.5. bigger value more smooth.
PXIALG_API int SpO2_SetMotionDetectionLevel(float level);
PXIALG_API int SpO2_SetInputModelScale(float scale);
PXIALG_API int SpO2_SetInputCorrelationTh(float th);
PXIALG_API int SpO2_SetInputRatioLow(float ratio);
PXIALG_API int SpO2_SetInputCoefLow0(float coef);
PXIALG_API int SpO2_SetInputCoefLow1(float coef);
PXIALG_API int SpO2_SetInputCoefLow2(float coef);
PXIALG_API int SpO2_SetInputCoefLow3(float coef);
PXIALG_API int SpO2_SetInputCoefMid0(float coef);
PXIALG_API int SpO2_SetInputCoefMid1(float coef);
PXIALG_API int SpO2_SetInputCoefMid2(float coef);
PXIALG_API int SpO2_SetInputCoefMid3(float coef);
PXIALG_API int SpO2_SetInputCoefHigh0(float coef);
PXIALG_API int SpO2_SetInputCoefHigh1(float coef);
PXIALG_API int SpO2_SetInputCoefHigh2(float coef);
PXIALG_API int SpO2_SetInputCoefHigh3(float coef);
PXIALG_API int SpO2_SetInputRatioHighBound(float bound);
PXIALG_API int SpO2_SetInputRatioLowBound(float bound);
PXIALG_API int SpO2_SetInputFlagCheckHr(int value);
PXIALG_API int SpO2_SetOutputHighBound(float value);
PXIALG_API int SpO2_SetOutputLowBound(float value);
PXIALG_API int SpO2_Set_Init_Low_SpO2_Constrain(int value);
PXIALG_API int SpO2_Set_Init_Low_SpO2_Th(float value);

PXIALG_API int SpO2_Set_Init_Low_SpO2_Check_Sec(int value);

PXIALG_API int SpO2_Set_Motion_Rms_Th(float value);
PXIALG_API int SpO2_Set_Motion_Rms_Th2(float value);

PXIALG_API int SpO2_Set_Correlation_Th2_Count(int value);
PXIALG_API int SpO2_Set_Input_Correlation_Th2(float value);

PXIALG_API int SpO2_Set_Flag_Hold_Breathe_Mode(char value);

PXIALG_API int SpO2_Set_Keep_Init_SpO2_Bound(float value);
PXIALG_API int SpO2_Set_Keep_Init_SpO2_Count_Th(int value);
PXIALG_API int SpO2_Set_SpO2_Bias_Value(float value);
PXIALG_API int SpO2_Set_SpO2_Outlier_Smooth_Coef(float value);
PXIALG_API int SpO2_Set_SpO2_Decrease_Coef(float value);

PXIALG_API int SpO2_Set_Motion_MEMS_Th(float value);

PXIALG_API int SpO2_Set_T1(float value);
PXIALG_API int SpO2_Set_T2(float value);
PXIALG_API int SpO2_Set_T3(float value);
PXIALG_API int SpO2_Set_T4(float value);
PXIALG_API int SpO2_Set_T5(float value);
PXIALG_API int SpO2_Set_Spo2_Force_Output_Second(int value);

PXIALG_API int SpO2_Set_Signal_Periodicity_Th(float value);
PXIALG_API int Spo2_Set_SpO2_First_Smooth_Th(float value);
PXIALG_API int Spo2_Set_SpO2_First_Smooth_Sec(int value);
PXIALG_API int Spo2_Set_SpO2_Correlation_Smooth_Th(float value);
//set HR
PXIALG_API int SpO2_SetHrValue(int value);

//get SPO2_Info
PXIALG_API int SpO2_GetInfo(SpO2_info_t *info);
PXIALG_API int SpO2_GetInfo_ByIndex(SpO2_info_t* p, int index);

PXIALG_API int SpO2_SetMems(SpO2_mems_t *mems, int samples);

PXIALG_API int SpO2_Set_Update_Sec(int spo2_update_sec);

PXIALG_API int SpO2_SetMemsRmsThr(float mems_rms_thr);
PXIALG_API int SpO2_CalAvgSpO2(float mySpO2, float trust_flag, float mems_rms, float *result); //called every one second
PXIALG_API int SpO2_SetSkipDataLen(int len);
PXIALG_API int SpO2_SetChMapping(int8_t *alg_ch, int8_t ch_nr);

PXIALG_API int SpO2_Set_Init_Low_SpO2_Check_Sec(int value);

PXIALG_API int Spo2_Set_SpO2_First_Smooth_Th(float value);
PXIALG_API int Spo2_Set_SpO2_First_Smooth_Sec(int value);
PXIALG_API int Spo2_Set_SpO2_Correlation_Smooth_Th(float value);

PXIALG_API int Spo2_Set_Force_Output_Value(float value);
PXIALG_API int Spo2_Set_Force_Output_Value2(float value);

PXIALG_API int SpO2_GetFlagOptimalSpO2Info();
#ifdef __cplusplus
}
#endif // __cplusplus


#endif
