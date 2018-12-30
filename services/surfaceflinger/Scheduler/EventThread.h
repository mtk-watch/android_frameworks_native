/*
 * Copyright (C) 2011 The Android Open Source Project
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

#pragma once

#include <sys/types.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <android-base/thread_annotations.h>

#include <gui/DisplayEventReceiver.h>
#include <gui/IDisplayEventConnection.h>
#include <private/gui/BitTube.h>

#include <utils/Errors.h>

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

class EventThread;
class EventThreadTest;
class SurfaceFlinger;

// ---------------------------------------------------------------------------

class VSyncSource {
public:
    class Callback {
    public:
        virtual ~Callback() {}
        virtual void onVSyncEvent(nsecs_t when) = 0;
    };

    virtual ~VSyncSource() {}
    virtual void setVSyncEnabled(bool enable) = 0;
    virtual void setCallback(Callback* callback) = 0;
    virtual void setPhaseOffset(nsecs_t phaseOffset) = 0;
};

class EventThreadConnection : public BnDisplayEventConnection {
public:
    explicit EventThreadConnection(EventThread* eventThread);
    virtual ~EventThreadConnection();

    virtual status_t postEvent(const DisplayEventReceiver::Event& event);

    status_t stealReceiveChannel(gui::BitTube* outChannel) override;
    status_t setVsyncRate(uint32_t count) override;
    void requestNextVsync() override; // asynchronous

    // count >= 1 : continuous event. count is the vsync rate
    // count == 0 : one-shot event that has not fired
    // count ==-1 : one-shot event that fired this round / disabled
    int32_t count;

private:
    virtual void onFirstRef();
    EventThread* const mEventThread;
    gui::BitTube mChannel;
};

class EventThread {
public:
    // TODO: Remove once stable display IDs are plumbed through SF/WM interface.
    enum class DisplayType { Primary, External };

    virtual ~EventThread();

    virtual sp<EventThreadConnection> createEventConnection() const = 0;

    // called before the screen is turned off from main thread
    virtual void onScreenReleased() = 0;

    // called after the screen is turned on from main thread
    virtual void onScreenAcquired() = 0;

    // called when receiving a hotplug event
    virtual void onHotplugReceived(DisplayType displayType, bool connected) = 0;

    virtual void dump(std::string& result) const = 0;

    virtual void setPhaseOffset(nsecs_t phaseOffset) = 0;

    virtual status_t registerDisplayEventConnection(
            const sp<EventThreadConnection>& connection) = 0;
    virtual void setVsyncRate(uint32_t count, const sp<EventThreadConnection>& connection) = 0;
    virtual void requestNextVsync(const sp<EventThreadConnection>& connection) = 0;
};

namespace impl {

class EventThread : public android::EventThread, private VSyncSource::Callback {
public:
    using ResyncWithRateLimitCallback = std::function<void()>;
    using InterceptVSyncsCallback = std::function<void(nsecs_t)>;
    using ResetIdleTimerCallback = std::function<void()>;

    // TODO(b/113612090): Once the Scheduler is complete this constructor will become obsolete.
    EventThread(VSyncSource* src, ResyncWithRateLimitCallback resyncWithRateLimitCallback,
                InterceptVSyncsCallback interceptVSyncsCallback, const char* threadName);
    EventThread(std::unique_ptr<VSyncSource> src,
                const ResyncWithRateLimitCallback& resyncWithRateLimitCallback,
                const InterceptVSyncsCallback& interceptVSyncsCallback,
                const ResetIdleTimerCallback& resetIdleTimerCallback, const char* threadName);
    ~EventThread();

    sp<EventThreadConnection> createEventConnection() const override;

    status_t registerDisplayEventConnection(const sp<EventThreadConnection>& connection) override;
    void setVsyncRate(uint32_t count, const sp<EventThreadConnection>& connection) override;
    void requestNextVsync(const sp<EventThreadConnection>& connection) override;

    // called before the screen is turned off from main thread
    void onScreenReleased() override;

    // called after the screen is turned on from main thread
    void onScreenAcquired() override;

    // called when receiving a hotplug event
    void onHotplugReceived(DisplayType displayType, bool connected) override;

    void dump(std::string& result) const override;

    void setPhaseOffset(nsecs_t phaseOffset) override;

private:
    friend EventThreadTest;

    // TODO(b/113612090): Once the Scheduler is complete this constructor will become obsolete.
    EventThread(VSyncSource* src, std::unique_ptr<VSyncSource> uniqueSrc,
                ResyncWithRateLimitCallback resyncWithRateLimitCallback,
                InterceptVSyncsCallback interceptVSyncsCallback, const char* threadName);


    void threadMain();
    std::vector<sp<EventThreadConnection>> waitForEventLocked(std::unique_lock<std::mutex>* lock,
                                                              DisplayEventReceiver::Event* event)
            REQUIRES(mMutex);

    void removeDisplayEventConnectionLocked(const wp<EventThreadConnection>& connection)
            REQUIRES(mMutex);
    void enableVSyncLocked() REQUIRES(mMutex);
    void disableVSyncLocked() REQUIRES(mMutex);

    // Implements VSyncSource::Callback
    void onVSyncEvent(nsecs_t timestamp) override;

    // TODO(b/113612090): Once the Scheduler is complete this pointer will become obsolete.
    VSyncSource* mVSyncSource GUARDED_BY(mMutex) = nullptr;
    std::unique_ptr<VSyncSource> mVSyncSourceUnique GUARDED_BY(mMutex) = nullptr;
    // constants
    const ResyncWithRateLimitCallback mResyncWithRateLimitCallback;
    const InterceptVSyncsCallback mInterceptVSyncsCallback;

    std::thread mThread;
    mutable std::mutex mMutex;
    mutable std::condition_variable mCondition;

    // protected by mLock
    std::vector<wp<EventThreadConnection>> mDisplayEventConnections GUARDED_BY(mMutex);
    std::queue<DisplayEventReceiver::Event> mPendingEvents GUARDED_BY(mMutex);
    std::array<DisplayEventReceiver::Event, 2> mVSyncEvent GUARDED_BY(mMutex);
    bool mUseSoftwareVSync GUARDED_BY(mMutex) = false;
    bool mVsyncEnabled GUARDED_BY(mMutex) = false;
    bool mKeepRunning GUARDED_BY(mMutex) = true;

    // for debugging
    bool mDebugVsyncEnabled GUARDED_BY(mMutex) = false;

    // Callback that resets the idle timer when the next vsync is received.
    ResetIdleTimerCallback mResetIdleTimer;
};

// ---------------------------------------------------------------------------

} // namespace impl
} // namespace android