#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

using glm::mat4;

constexpr int MAX_NODE_LEVEL = 16;

struct Hierarchy {
    // parent for this node (or -1 for root)
    int mParent;

    // first child for a node (or -1)
    int mFirstChild;

    // next sibling for a node (or -1)
    int mNextSibling;

    // last added node (or -1)
    int mLastSibling;

    // cached node level
    int mLevel;
};

/* This scene is converted into a descriptorSet(s) in MultiRenderer class
   This structure is also used as a storage type in SceneExporter tool
 */
struct Scene {
    // local transformations for each node and global transforms
    // + an array of 'dirty/changed' local transforms
    std::vector<mat4> mLocalTransform;
    std::vector<mat4> mGlobalTransform;

    // list of nodes whose global transform must be recalculated
    std::vector<int> mChangedAtThisFrame[MAX_NODE_LEVEL];

    // Hierarchy component
    std::vector<Hierarchy> mHierarchy;

    // Mesh component: Which node corresponds to which node
    std::unordered_map<uint32_t, uint32_t> mMeshes;

    // Material component: Which material belongs to which node
    std::unordered_map<uint32_t, uint32_t> mMaterialForNode;

    // Node name component: Which name is assigned to the node
    std::unordered_map<uint32_t, uint32_t> mNameForNode;

    // List of scene node names
    std::vector<std::string> mNames;

    // Debug list of material names
    std::vector<std::string> mMaterialNames;
};

int AddNode(Scene& scene, int parent, int level);

void MarkAsChanged(Scene& scene, int node);

int FindNodeByName(const Scene& scene, const std::string& name);

inline std::string GetNodeName(const Scene& scene, int node) {
    int strID = scene.mNameForNode.contains(node) ? scene.mNameForNode.at(node) : -1;
    return (strID > -1) ? scene.mNames[strID] : std::string();
}

inline void SetNodeName(Scene& scene, int node, const std::string& name) {
    uint32_t stringID = (uint32_t)scene.mNames.size();
    scene.mNames.push_back(name);
    scene.mNameForNode[node] = stringID;
}

int GetNodeLevel(const Scene& scene, int n);

void RecalculateGlobalTransforms(Scene& scene);

void LoadScene(const char* fileName, Scene& scene);
void SaveScene(const char* fileName, const Scene& scene);

void DumpTransforms(const char* fileName, const Scene& scene);
void PrintChangedNodes(const Scene& scene);

void DumpSceneToDot(const char* fileName, const Scene& scene, const int* visited = nullptr);

void MergeScenes(Scene& scene, const std::vector<Scene*>& scenes, const std::vector<glm::mat4>& rootTransforms, const std::vector<uint32_t>& meshCounts,
                 bool mergeMeshes = true, bool mergeMaterials = true);

// Delete a collection of nodes from a scenegraph
void DeleteSceneNodes(Scene& scene, const std::vector<uint32_t>& nodesToDelete);
