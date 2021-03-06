/*
 * Copyright 2019 The Android Open Source Project
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

#include "LayerInfo.h"

#include <cinttypes>
#include <cstdint>
#include <numeric>
#include <string>

namespace android {
namespace scheduler {

LayerInfo::LayerInfo(const std::string name, float maxRefreshRate)
      : mName(name),
        mMinRefreshDuration(1e9f / maxRefreshRate),
        mRefreshRateHistory(mMinRefreshDuration) {}

LayerInfo::~LayerInfo() = default;

void LayerInfo::setLastPresentTime(nsecs_t lastPresentTime) {
    std::lock_guard lock(mLock);

    // Buffers can come with a present time far in the future. That keeps them relevant.
    mLastUpdatedTime = std::max(lastPresentTime, systemTime());
    mPresentTimeHistory.insertPresentTime(mLastUpdatedTime);

    const nsecs_t timeDiff = lastPresentTime - mLastPresentTime;
    mLastPresentTime = lastPresentTime;
    // Ignore time diff that are too high - those are stale values
    if (timeDiff > TIME_EPSILON_NS.count()) return;
    const nsecs_t refreshDuration = (timeDiff > 0) ? timeDiff : mMinRefreshDuration;
    mRefreshRateHistory.insertRefreshRate(refreshDuration);
}

} // namespace scheduler
} // namespace android
