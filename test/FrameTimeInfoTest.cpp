/*
 * Copyright (C) 2024 Xiaomi Corporation
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

#include <gtest/gtest.h>

#include "../common/FrameTimeInfo.h"

namespace os {
namespace wm {

class FrameTimeInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        frameTimeInfo = new FrameTimeInfo();
    }

    void TearDown() override {
        delete frameTimeInfo;
    }

    FrameTimeInfo* frameTimeInfo;
};

TEST_F(FrameTimeInfoTest, Time_NullInfo_LogPerSecond) {
    FrameMetaInfo* info = nullptr;
    frameTimeInfo->time(info);
}

TEST_F(FrameTimeInfoTest, Time_SkipReason_MSkipFrameSamplesIncremented) {
    FrameMetaInfo* info = new FrameMetaInfo();
    info->setSkipReason(FrameMetaSkipReason::NothingToDraw);

    frameTimeInfo->time(info);

    delete info;
}

extern "C" int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

} // namespace wm
} // namespace os
