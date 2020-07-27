/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LIBINPUT_MTK_ARC_DISPATCHER_H
#define _LIBINPUT_MTK_ARC_DISPATCHER_H


#include <input/Input.h>
#include <dlfcn.h>
#include <time.h>


/// M: MTK_ARC @{
/// M:
/// #ARC# is a feature key for GiFT (Graphics Frame-rate Tuner), which is a
/// sub-module in DFPS (Dynamic FPS).
///
/// ARC (Adaptive Rendering Choreography) is a framework to collect system
/// information for internal algorithm (ex: GiFT) and to report result(s) to
/// corresponding module(ex: sending appropriate FPS to FpsPolicyService).
///
/// GiFT (Graphics Frame-rate Tuner) is an algorithm of ARC. Its goal is to
/// suggest an appropriate FPS for games without loss too much quality. The
/// target is given by analyzing GL commands and input events.

/// M: ARC library name & function string
#define LIB_EGL_NAME                    "libEGL.so"
#define LIB_ARC_INIT_FUN_NAME_STR       "eglTouchInitializeGIFT"
#define LIB_ARC_DEST_FUN_NAME_STR       "eglDestroyEventThreadGIFT"
#define LIB_ARC_NOTI_FUN_NAME_STR       "eglTouchEventGIFT"
/// M: the maximun data per package
#define ARC_TOUCH_EVENT_PACKAGE_MAX_POINT   10


namespace android {

class MTKMotionEvent : public MotionEvent
{
public:
    inline size_t getHistorySizePlusOne() const { return mSampleEventTimes.size(); }
};

class MTKArcDispatcher
{
private:
    /// M: a touch event data package used in ARC.
    struct TouchEventPackage {
        uint32_t pointCount;
        uint32_t action;
        int64_t eventTime;
        float x[ARC_TOUCH_EVENT_PACKAGE_MAX_POINT];
        float y[ARC_TOUCH_EVENT_PACKAGE_MAX_POINT];
    };
    /// M: function pointer types
    typedef void (*EventTracerTouchEventNotifyFuncPtr)(void *packed_data);
    typedef void (*EventTracerTouchInitFuncPtr)(void);

    typedef void (*__eglMustCastToProperFunctionPointerType)(void);
    typedef __eglMustCastToProperFunctionPointerType (*EglGetProcAddressFuncPtr)(const char *procname);

public:
    MTKArcDispatcher()
    {
        /// M: Init ARC functions from libARC. @{
        mTouchEventNotifyFunPtr = NULL;
        mTouchInitializeFunPtr = mTouchDestroyFunPtr = NULL;
        mLibArcHandle = dlopen(LIB_EGL_NAME, RTLD_NOW);

        if (mLibArcHandle) {
            EglGetProcAddressFuncPtr eglGetProcAddress = reinterpret_cast<EglGetProcAddressFuncPtr>(dlsym(mLibArcHandle, "eglGetProcAddress"));
            if (eglGetProcAddress) {
                #define ARC_REGISTER_FTOR(is_debug, mFtor, func_name, func_type) \
                    if (mFtor == NULL) { \
                        mFtor = (func_type)(eglGetProcAddress(func_name)); \
                        if ((is_debug) && (mFtor == NULL)) { \
                            ALOGI("ARC Get %s::%s() fail to get symbol", LIB_EGL_NAME, func_name); \
                        } \
                    }

                ARC_REGISTER_FTOR(0, mTouchInitializeFunPtr,  LIB_ARC_INIT_FUN_NAME_STR, EventTracerTouchInitFuncPtr);
                ARC_REGISTER_FTOR(0, mTouchDestroyFunPtr,     LIB_ARC_DEST_FUN_NAME_STR, EventTracerTouchInitFuncPtr);
                ARC_REGISTER_FTOR(0, mTouchEventNotifyFunPtr, LIB_ARC_NOTI_FUN_NAME_STR, EventTracerTouchEventNotifyFuncPtr);

                #undef ARC_REGISTER_FTOR

                if (mTouchInitializeFunPtr && mTouchDestroyFunPtr && mTouchEventNotifyFunPtr) {
                    /// M: Call mTouchInitializeFunPtr if all function is ready.
                    mTouchInitializeFunPtr();
                } else {
                    /// M: Reset/close arc*FunPtr, mLibArcHandle if any thing fail.
                    dlclose(mLibArcHandle);
                    mLibArcHandle = NULL;
                    mTouchInitializeFunPtr = mTouchDestroyFunPtr = NULL;
                    mTouchEventNotifyFunPtr = NULL;
                }
            }
        } else {
            /// M: Report warning if dlopen fail.
            ALOGW("ARC dlopen LIB: %s fail with %s", LIB_EGL_NAME, dlerror());
        }
        /// M: @}
    };

    ~MTKArcDispatcher()
    {
        /// M: Clean up the ARC by invoking mTouchDestroyFunPtr() & dlclose() @{
        if (mTouchDestroyFunPtr) {
            mTouchDestroyFunPtr();
        }
        if (mLibArcHandle) {
            dlclose(mLibArcHandle);
            mLibArcHandle = NULL;
        }
        /// M: @}
    };

    inline void dispatchEvent(const MotionEvent *arcEvent)
    {
        /// M: dispatch event to ARC framework if
        ///    1. having incoming event(s) and
        ///    2. libARC existed
        if (mTouchEventNotifyFunPtr) {
            struct TouchEventPackage data;

            // getHistorySize return (mSampleEventTimes.size() - 1) acturally
            // so compiler may generate <__ubsan_handle_sub_overflow_minimal_abort> to abort if value overflow
            // from 0 to 0xffffffff for example
            size_t historySize = static_cast<const MTKMotionEvent *>(arcEvent)->getHistorySizePlusOne();

            for (uint32_t i=0; i<historySize; i++) {
                data.eventTime  = arcEvent->getHistoricalEventTime(i);
                data.action     = arcEvent->getAction();
                data.pointCount = arcEvent->getPointerCount();
                size_t pointerCount = (ARC_TOUCH_EVENT_PACKAGE_MAX_POINT < data.pointCount)
                                    ? ARC_TOUCH_EVENT_PACKAGE_MAX_POINT
                                    : data.pointCount;
                for (uint32_t j=0; j < pointerCount; j++) {
                    data.x[j] = arcEvent->getHistoricalX(j, i),
                    data.y[j] = arcEvent->getHistoricalY(j, i);
                }
                mTouchEventNotifyFunPtr((void *)&data);
            }
        }
    }

private:
    /// M: dl handle for LIB_EGL_NAME & its function pointers
    void *mLibArcHandle = NULL;
    void (*mTouchEventNotifyFunPtr)(void *packed_data) = NULL;
    void (*mTouchInitializeFunPtr)(void) = NULL;
    void (*mTouchDestroyFunPtr)(void) = NULL;
}; // class MTKArcDispatcher

} // namespace android
/// @}
#endif
