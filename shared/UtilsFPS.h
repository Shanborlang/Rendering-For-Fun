#pragma once

#include <cassert>
#include <cstdio>

class FramePerSecondCounter{
public:
    explicit FramePerSecondCounter(float avgInterval = 0.5f)
    : mAvgInterval(avgInterval) {
        assert(avgInterval > 0.0f);
    }

    bool Tick(float deltaSeconds, bool frameRendered = true) {
        if(frameRendered)
            mNumFrames++;

        mAccumulatedTime += deltaSeconds;

        if(mAccumulatedTime > mAvgInterval) {
            mCurrentFPS = static_cast<float>(mNumFrames / mAccumulatedTime);
            if(mPrintFPS)
                printf("FPS: %.1f\n", mCurrentFPS);
            mNumFrames = 0;
            mAccumulatedTime = 0;
            return true;
        }

        return false;
    }

    inline float GetFPS() const { return mCurrentFPS; }

    bool mPrintFPS = true;

private:
    const float mAvgInterval = 0.5f;
    unsigned int mNumFrames = 0;
    double mAccumulatedTime = 0;
    float mCurrentFPS = 0.0f;
};