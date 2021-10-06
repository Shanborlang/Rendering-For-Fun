#pragma once

#include <deque>
#include <limits>

#include "EasyProfilerWrapper.h"
#include "vkRenderers/VulkanCanvas.h"

class LinearGraph {
public:
    explicit LinearGraph(size_t maxGraphPoints = 256)
    : mMaxPoints(maxGraphPoints)
    {}

    void AddPoint(float value) {
        mGraph.push_back(value);
        if(mGraph.size() > mMaxPoints)
            mGraph.pop_front();
    }

    void RenderGraph(VulkanCanvas& c, const glm::vec4& color = glm::vec4(1.0f)) const {
        EASY_FUNCTION()

        float minfps = std::numeric_limits<float>::max();
        float maxfps = std::numeric_limits<float>::min();

        for(float f : mGraph) {
            if(f < minfps) minfps = f;
            if(f > maxfps) maxfps = f;
        }

        const float range = maxfps - minfps;

        float x = 0.0f;
        glm::vec3 p1 = glm::vec3(0, 0, 0);

        for(float f : mGraph) {
            const float val = (f - minfps) / range;
            const glm::vec3 p2 = glm::vec3(x, val * 0.15f, 0);
            x += 1.0f / mMaxPoints;
            c.Line(p1, p2, color);
            p1 = p2;
        }
    }

private:
    std::deque<float> mGraph;
    const size_t mMaxPoints;
};