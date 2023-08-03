/*
 * Copyright (C) 2023 Xiaomi Corporation
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

#define LOG_TAG "WindowState"

#include "WindowState.h"

#include <sys/mman.h>
#include <utils/RefBase.h>

#include "../system_server/BaseProfiler.h"
#include "RootContainer.h"
#include "WindowManagerService.h"
#include "wm/LayerState.h"
#include "wm/VsyncRequestOps.h"

namespace os {
namespace wm {

WindowState::WindowState(WindowManagerService* service, const sp<IWindow>& window,
                         WindowToken* token, const LayoutParams& params, int32_t visibility,
                         bool enableInput)
      : mClient(window),
        mToken(token),
        mService(service),
        mInputChannel(nullptr),
        mVsyncRequest(VsyncRequest::VSYNC_REQ_NONE),
        mFrameReq(0),
        mHasSurface(false) {
    mAttrs = params;
    mVisibility = visibility != 0 ? true : false;
    mRequestedWidth = 0;
    mRequestedHeight = 0;

    Rect rect(params.mX, params.mY, params.mX + params.mWidth, params.mY + params.mHeight);
    // TODO: config layer by type
    mNode = new WindowNode(this, mService->getRootContainer()->getDefLayer(), rect, enableInput);
}

WindowState::~WindowState() {
    // TODO: clear members
    mClient = nullptr;
    mToken = nullptr;
    delete mNode;
}

std::shared_ptr<BufferConsumer> WindowState::getBufferConsumer() {
    if (mSurfaceControl != nullptr && mSurfaceControl->isValid()) {
        return std::static_pointer_cast<BufferConsumer>(mSurfaceControl->bufferQueue());
    }
    return nullptr;
}

std::shared_ptr<InputChannel> WindowState::createInputChannel(const std::string name) {
    if (mInputChannel != nullptr) {
        ALOGE("mInputChannel is existed, create failed");
        return nullptr;
    }
    WM_PROFILER_BEGIN();
    mInputChannel = std::make_shared<InputChannel>();
    if (!mInputChannel->create(name)) {
        mInputChannel = nullptr;
    }
    WM_PROFILER_END();
    return mInputChannel;
}

bool WindowState::sendInputMessage(const InputMessage* ie) {
    if (mInputChannel != nullptr) return mInputChannel->sendMessage(ie);
    ALOGW("input message: invalid input channel!");
    return false;
}

void WindowState::setViewVisibility(bool visibility) {
    mVisibility = visibility;
    // TODO: NOTIFY WIN NODE
}

void WindowState::sendAppVisibilityToClients() {
    WM_PROFILER_BEGIN();

    mVisibility = mToken->isClientVisible();
    if (!mVisibility) {
        scheduleVsync(VsyncRequest::VSYNC_REQ_NONE);
    }
    mClient->dispatchAppVisibility(mVisibility);
    WM_PROFILER_END();
}

std::shared_ptr<SurfaceControl> WindowState::createSurfaceControl(vector<BufferId> ids) {
    WM_PROFILER_BEGIN();
    setHasSurface(false);

    sp<IBinder> handle = new BBinder();
    mSurfaceControl =
            std::make_shared<SurfaceControl>(IInterface::asBinder(mClient), handle, mAttrs.mWidth,
                                             mAttrs.mHeight, mAttrs.mFormat);

    mSurfaceControl->initBufferIds(ids);
    std::shared_ptr<BufferConsumer> buffConsumer =
            std::make_shared<BufferConsumer>(mSurfaceControl);
    mSurfaceControl->setBufferQueue(buffConsumer);

    setHasSurface(true);
    WM_PROFILER_END();

    return mSurfaceControl;
}

void WindowState::destorySurfaceControl() {
    mNode->updateBuffer(nullptr, nullptr);
    scheduleVsync(VsyncRequest::VSYNC_REQ_NONE);
    mSurfaceControl.reset();
}

void WindowState::destoryInputChannel() {
    mInputChannel->release();
    mInputChannel.reset();
}

void WindowState::applyTransaction(LayerState layerState) {
    ALOGI("applyTransaction(%p)", this);
    WM_PROFILER_BEGIN();

    BufferItem* buffItem = nullptr;
    Rect* rect = nullptr;
    if (layerState.mFlags & LayerState::LAYER_POSITION_CHANGED) {
        // TODO
    }

    if (layerState.mFlags & LayerState::LAYER_ALPHA_CHANGED) {
        // TODO
    }

    if (layerState.mFlags & LayerState::LAYER_BUFFER_CHANGED) {
        std::shared_ptr<BufferConsumer> consumer = getBufferConsumer();
        if (consumer == nullptr) {
            return;
        }
        buffItem = consumer->syncQueuedState(layerState.mBufferKey);
    }

    if (layerState.mFlags & LayerState::LAYER_BUFFER_CROP_CHANGED) {
        rect = &layerState.mBufferCrop;
    }

    mNode->updateBuffer(buffItem, rect);
    WM_PROFILER_END();
}

bool WindowState::scheduleVsync(VsyncRequest vsyncReq) {
    if (mVsyncRequest == vsyncReq) return false;

    mVsyncRequest = vsyncReq;
    return true;
}

bool WindowState::onVsync() {
    if (mVsyncRequest == VsyncRequest::VSYNC_REQ_NONE || !mVisibility) return false;
    WM_PROFILER_BEGIN();

    ALOGI("(%p) send vsync to client", this);
    mVsyncRequest = nextVsyncState(mVsyncRequest);
    mClient->onFrame(++mFrameReq);
    WM_PROFILER_END();

    return true;
}

void WindowState::removeIfPossible() {
    destorySurfaceControl();
    destoryInputChannel();
    mToken.reset();
    mNode = nullptr;

    ALOGI("called");
}

BufferItem* WindowState::acquireBuffer() {
    std::shared_ptr<BufferConsumer> consumer = getBufferConsumer();
    if (consumer == nullptr) {
        return nullptr;
    }
    return consumer->acquireBuffer();
}

bool WindowState::releaseBuffer(BufferItem* buffer) {
    std::shared_ptr<BufferConsumer> consumer = getBufferConsumer();
    if (consumer == nullptr) {
        return false;
    }
    if (consumer && consumer->releaseBuffer(buffer) && mClient) {
        WM_PROFILER_BEGIN();
        mClient->bufferReleased(buffer->mKey);
        WM_PROFILER_END();
        return true;
    }
    return false;
}

void WindowState::setRequestedSize(int32_t requestedWidth, int32_t requestedHeight) {
    if ((mRequestedWidth != requestedWidth || mRequestedHeight != requestedHeight)) {
        mRequestedWidth = requestedWidth;
        mRequestedHeight = requestedHeight;
    }
}

} // namespace wm
} // namespace os
