#pragma once

#include <raddebug.hpp>   // para rDebugPrintf()
#include <debug/gamelog.h>

#if defined(RAD_ANDROID)
    #include <android/log.h>
    #define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "SimpsonsHitAndRun", __VA_ARGS__); GameLog_Recordf("SimpsonsHitAndRun", __VA_ARGS__); } while(0)
    #define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, "SimpsonsHitAndRun", __VA_ARGS__); GameLog_Recordf("ERROR", __VA_ARGS__); } while(0)
#elif defined(RAD_VITA)
    #include <psp2/kernel/clib.h>
    #define LOGI(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); GameLog_Recordf("SimpsonsHitAndRun", __VA_ARGS__); } while(0)
    #define LOGE(...) do { sceClibPrintf(__VA_ARGS__); sceClibPrintf("\n"); GameLog_Recordf("ERROR", __VA_ARGS__); } while(0)
#else
    #include <cstdio>
    #define LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); GameLog_Recordf("SimpsonsHitAndRun", __VA_ARGS__); } while(0)
    #define LOGE(...) do { std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); GameLog_Recordf("ERROR", __VA_ARGS__); } while(0)
#endif