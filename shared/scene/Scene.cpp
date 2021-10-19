#include "Scene.h"
#include "shared/Utils.h"

#include <algorithm>
#include <numeric>

void SaveStringList(FILE* f, const std::vector<std::string>& lines);
void LoadStringList(FILE* f, std::vector<std::string>& lines);

int AddNode(Scene &scene, int parent, int level) {
    int node = (int)scene.mHierarchy.size();
    {
        // TODO: resize aux arrays (local/global etc.)
        scene.mLocalTransform.push_back(glm::mat4(1.0f));
        scene.mGlobalTransform.push_back(glm::mat4(1.0f));
    }
    scene.mHierarchy.push_back({ .mParent = parent, .mLastSibling = -1 });
    if (parent > -1)
    {
        // find first item (sibling)
        int s = scene.mHierarchy[parent].mFirstChild;
        if (s == -1)
        {
            scene.mHierarchy[parent].mFirstChild = node;
            scene.mHierarchy[node].mLastSibling = node;
        } else
        {
            int dest = scene.mHierarchy[s].mLastSibling;
            if (dest <= -1)
            {
                // no cached lastSibling, iterate nextSibling indices
                for (dest = s; scene.mHierarchy[dest].mNextSibling != -1; dest = scene.mHierarchy[dest].mNextSibling);
            }
            scene.mHierarchy[dest].mNextSibling = node;
            scene.mHierarchy[s].mLastSibling = node;
        }
    }
    scene.mHierarchy[node].mLevel = level;
    scene.mHierarchy[node].mNextSibling = -1;
    scene.mHierarchy[node].mFirstChild  = -1;
    return node;
}

void MarkAsChanged(Scene &scene, int node) {
    int level = scene.mHierarchy[node].mLevel;
    scene.mChangedAtThisFrame[level].push_back(node);

    // TODO: use non-recursive iteration with aux stack
    for (int s = scene.mHierarchy[node].mFirstChild; s != - 1 ; s = scene.mHierarchy[s].mNextSibling)
        MarkAsChanged(scene, s);
}

int FindNodeByName(const Scene &scene, const std::string &name) {
    // Extremely simple linear search without any hierarchy reference
    // To support DFS/BFS searches separate traversal routines are needed

    for (size_t i = 0 ; i < scene.mLocalTransform.size() ; i++)
        if (scene.mNameForNode.contains(i))
        {
            int strID = scene.mNameForNode.at(i);
            if (strID > -1)
                if (scene.mNames[strID] == name)
                    return (int)i;
        }

    return -1;
}

int GetNodeLevel(const Scene &scene, int n) {
    int level = -1;
    for (int p = 0 ; p != -1 ; p = scene.mHierarchy[p].mParent, level++);
    return level;
}

bool mat4IsIdentity(const glm::mat4& m);
void fprintfMat4(FILE* f, const glm::mat4& m);

// CPU version of global transform update []
void RecalculateGlobalTransforms(Scene &scene) {
    if (!scene.mChangedAtThisFrame[0].empty()) {
        int c = scene.mChangedAtThisFrame[0][0];
        scene.mGlobalTransform[c] = scene.mLocalTransform[c];
        scene.mChangedAtThisFrame[0].clear();
    }

    for (int i = 1 ; i < MAX_NODE_LEVEL && (!scene.mChangedAtThisFrame[i].empty()); i++ ) {
        for (const int& c: scene.mChangedAtThisFrame[i]) {
            int p = scene.mHierarchy[c].mParent;
            scene.mGlobalTransform[c] = scene.mGlobalTransform[p] * scene.mLocalTransform[c];
        }
        scene.mChangedAtThisFrame[i].clear();
    }
}

void LoadMap(FILE* f, std::unordered_map<uint32_t, uint32_t>& map) {
    std::vector<uint32_t> ms;

    uint32_t sz = 0;
    fread(&sz, 1, sizeof(sz), f);

    ms.resize(sz);
    fread(ms.data(), sizeof(int), sz, f);
    for (size_t i = 0; i < (sz / 2) ; i++)
        map[ms[i * 2 + 0]] = ms[i * 2 + 1];
}

void LoadScene(const char *fileName, Scene &scene) {
    FILE* f = fopen(fileName, "rb");

    if (!f)
    {
        printf("Cannot open scene file '%s'. Please run SceneConverter and/or MergeMeshes", fileName);
        return;
    }

    uint32_t sz = 0;
    fread(&sz, sizeof(sz), 1, f);

    scene.mHierarchy.resize(sz);
    scene.mGlobalTransform.resize(sz);
    scene.mLocalTransform.resize(sz);
    // TODO: check > -1
    // TODO: recalculate changedAtThisLevel() - find max depth of a node [or save scene.maxLevel]
    fread(scene.mLocalTransform.data(), sizeof(glm::mat4), sz, f);
    fread(scene.mGlobalTransform.data(), sizeof(glm::mat4), sz, f);
    fread(scene.mHierarchy.data(), sizeof(Hierarchy), sz, f);

    // Mesh for node [index to some list of buffers]
    LoadMap(f, scene.mMaterialForNode);
    LoadMap(f, scene.mMeshes);

    if (!feof(f))
    {
        LoadMap(f, scene.mNameForNode);
        LoadStringList(f, scene.mNames);

        LoadStringList(f, scene.mMaterialNames);
    }

    fclose(f);
}

void SaveMap(FILE* f, const std::unordered_map<uint32_t, uint32_t>& map) {
    std::vector<uint32_t> ms;
    ms.reserve(map.size() * 2);
    for (const auto& [first, second] : map) {
        ms.push_back(first);
        ms.push_back(second);
    }
    const auto sz = static_cast<uint32_t>(ms.size());
    fwrite(&sz, sizeof(sz), 1, f);
    fwrite(ms.data(), sizeof(int), ms.size(), f);
}

void SaveScene(const char *fileName, const Scene &scene) {
    FILE* f = fopen(fileName, "wb");

    const uint32_t sz = (uint32_t)scene.mHierarchy.size();
    fwrite(&sz, sizeof(sz), 1, f);

    fwrite(scene.mLocalTransform.data(), sizeof(glm::mat4), sz, f);
    fwrite(scene.mGlobalTransform.data(), sizeof(glm::mat4), sz, f);
    fwrite(scene.mHierarchy.data(), sizeof(Hierarchy), sz, f);

    // Mesh for node [index to some list of buffers]
    SaveMap(f, scene.mMaterialForNode);
    SaveMap(f, scene.mMeshes);

    if (!scene.mNames.empty() && !scene.mNameForNode.empty()) {
        SaveMap(f, scene.mNameForNode);
        SaveStringList(f, scene.mNames);

        SaveStringList(f, scene.mMaterialNames);
    }
    fclose(f);
}

bool mat4IsIdentity(const glm::mat4& m) {
    return (m[0][0] == 1 && m[0][1] == 0 && m[0][2] == 0 && m[0][3] == 0 &&
            m[1][0] == 0 && m[1][1] == 1 && m[1][2] == 0 && m[1][3] == 0 &&
            m[2][0] == 0 && m[2][1] == 0 && m[2][2] == 1 && m[2][3] == 0 &&
            m[3][0] == 0 && m[3][1] == 0 && m[3][2] == 0 && m[3][3] == 1);
}

void fprintfMat4(FILE* f, const glm::mat4& m) {
    if (mat4IsIdentity(m)) {
        fprintf(f, "Identity\n");
    }
    else {
        fprintf(f, "\n");
        for (int i = 0 ; i < 4 ; i++) {
            for (int j = 0 ; j < 4 ; j++)
                fprintf(f, "%f ;", m[i][j]);
            fprintf(f, "\n");
        }
    }
}


void DumpTransforms(const char *fileName, const Scene &scene) {
    FILE* f = fopen(fileName, "a+");
    for (size_t i = 0 ; i < scene.mLocalTransform.size() ; i++) {
        fprintf(f, "Node[%d].localTransform: ", (int)i);
        fprintfMat4(f, scene.mLocalTransform[i]);
        fprintf(f, "Node[%d].globalTransform: ", (int)i);
        fprintfMat4(f, scene.mGlobalTransform[i]);
        fprintf(f, "Node[%d].globalDet = %f; localDet = %f\n", (int)i, glm::determinant(scene.mGlobalTransform[i]), glm::determinant(scene.mLocalTransform[i]));
    }
    fclose(f);
}

void PrintChangedNodes(const Scene &scene) {
    for (int i = 0 ; i < MAX_NODE_LEVEL && (!scene.mChangedAtThisFrame[i].empty()); i++ ) {
        printf("Changed at level(%d):\n", i);

        for (const int& c: scene.mChangedAtThisFrame[i]) {
            int p = scene.mHierarchy[c].mParent;
            //scene.globalTransform_[c] = scene.globalTransform_[p] * scene.localTransform_[c];
            printf(" Node %d. Parent = %d; LocalTransform: ", c, p);
            fprintfMat4(stdout, scene.mLocalTransform[i]);
            if (p > -1) {
                printf(" ParentGlobalTransform: ");
                fprintfMat4(stdout, scene.mGlobalTransform[p]);
            }
        }
    }
}

// Shift all hierarchy components in the nodes
void ShiftNodes(Scene& scene, int startOffset, int nodeCount, int shiftAmount)
{
    auto shiftNode = [shiftAmount](Hierarchy& node)
    {
        if (node.mParent > -1)
            node.mParent += shiftAmount;
        if (node.mFirstChild > -1)
            node.mFirstChild += shiftAmount;
        if (node.mNextSibling > -1)
            node.mNextSibling += shiftAmount;
        if (node.mLastSibling > -1)
            node.mLastSibling += shiftAmount;
        // node->mLevel does not have to be shifted
    };


//	std::transform(scene.hierarchy_.begin() + startOffset, scene.hierarchy_.begin() + nodeCount, scene.hierarchy_.begin() + startOffset, shiftNode);

//	for (auto i = scene.hierarchy_.begin() + startOffset ; i != scene.hierarchy_.begin() + nodeCount ; i++)
//		shiftNode(*i);

    for (int i = 0 ; i < nodeCount ; i++)
        shiftNode(scene.mHierarchy[i + startOffset]);
}

using ItemMap = std::unordered_map<uint32_t, uint32_t>;

// Add the items from otherMap shifting indices and values along the way
void MergeMaps(ItemMap& m, const ItemMap& otherMap, int indexOffset, int itemOffset)
{
    for (const auto& i: otherMap)
        m[i.first + indexOffset] = i.second + itemOffset;
}


void DumpSceneToDot(const char *fileName, const Scene &scene, const int *visited) {
    FILE* f = fopen(fileName, "w");
    fprintf(f, "digraph G\n{\n");
    for (size_t i = 0; i < scene.mGlobalTransform.size(); i++) {
        std::string name;
        std::string extra;
        if (scene.mNameForNode.contains(i))
        {
            int strID = scene.mNameForNode.at(i);
            name = scene.mNames[strID];
        }
        if (visited)
        {
            if (visited[i])
                extra = ", color = red";
        }
        fprintf(f, "n%d [label=\"%s\" %s]\n", (int)i, name.c_str(), extra.c_str());
    }

    for (size_t i = 0; i < scene.mHierarchy.size(); i++) {
        int p = scene.mHierarchy[i].mParent;
        if (p > -1)
            fprintf(f, "\t n%d -> n%d\n", p, (int)i);
    }
    fprintf(f, "}\n");
    fclose(f);
}

void MergeScenes(Scene &scene, const std::vector<Scene *> &scenes, const std::vector<glm::mat4> &rootTransforms,
                 const std::vector<uint32_t> &meshCounts, bool mergeMeshes, bool mergeMaterials) {
// Create new root node
    scene.mHierarchy = {
            {
                    .mParent = -1,
                    .mFirstChild = 1,
                    .mNextSibling = -1,
                    .mLastSibling = -1,
                    .mLevel = 0
            }
    };

    scene.mNameForNode[0] = 0;
    scene.mNames = { "NewRoot" };

    scene.mLocalTransform.emplace_back(1.f);
    scene.mGlobalTransform.emplace_back(1.f);

    if (scenes.empty())
        return;

    int offs = 1;
    int meshOffs = 0;
    int nameOffs = (int)scene.mNames.size();
    int materialOfs = 0;
    auto meshCount = meshCounts.begin();

    if (!mergeMaterials)
        scene.mMaterialNames = scenes[0]->mMaterialNames;

    // FIXME: too much logic (for all the components in a scene, though mesh data and materials go separately - there are dedicated data lists)
    for (const Scene* s: scenes) {
        mergeVectors(scene.mLocalTransform, s->mLocalTransform);
        mergeVectors(scene.mGlobalTransform, s->mGlobalTransform);

        mergeVectors(scene.mHierarchy, s->mHierarchy);

        mergeVectors(scene.mNames, s->mNames);
        if (mergeMaterials)
            mergeVectors(scene.mMaterialNames, s->mMaterialNames);

        int nodeCount = (int)s->mHierarchy.size();

        ShiftNodes(scene, offs, nodeCount, offs);

        MergeMaps(scene.mMeshes,          s->mMeshes,          offs, mergeMeshes ? meshOffs : 0);
        MergeMaps(scene.mMaterialForNode, s->mMaterialForNode, offs, mergeMaterials ? materialOfs : 0);
        MergeMaps(scene.mNameForNode,     s->mNameForNode,     offs, nameOffs);

        offs += nodeCount;

        materialOfs += (int)s->mMaterialNames.size();
        nameOffs += (int)s->mNames.size();

        if (mergeMeshes) {
            meshOffs += *meshCount;
            meshCount++;
        }
    }

    // fixing 'nextSibling' fields in the old roots (zero-index in all the scenes)
    offs = 1;
    int idx = 0;
    for (const Scene* s: scenes) {
        int nodeCount = (int)s->mHierarchy.size();
        bool isLast = (idx == scenes.size() - 1);
        // calculate new next sibling for the old scene roots
        int next = isLast ? -1 : offs + nodeCount;
        scene.mHierarchy[offs].mNextSibling = next;
        // attach to new root
        scene.mHierarchy[offs].mParent = 0;

        // transform old root nodes, if the transforms are given
        if (!rootTransforms.empty())
            scene.mLocalTransform[offs] = rootTransforms[idx] * scene.mLocalTransform[offs];

        offs += nodeCount;
        idx++;
    }

    // now shift levels of all nodes below the root
    for (auto i = scene.mHierarchy.begin() + 1 ; i != scene.mHierarchy.end() ; i++)
        i->mLevel++;
}

// Add an index to a sorted index array
static void AddUniqueIdx(std::vector<uint32_t>& v, uint32_t index) {
    if (!std::binary_search(v.begin(), v.end(), index))
        v.push_back(index);
}

// Recurse down from a node and collect all nodes which are already marked for deletion
static void CollectNodesToDelete(const Scene& scene, int node, std::vector<uint32_t>& nodes) {
    for (int n = scene.mHierarchy[node].mFirstChild; n != - 1 ; n = scene.mHierarchy[n].mNextSibling) {
        AddUniqueIdx(nodes, n);
        CollectNodesToDelete(scene, n, nodes);
    }
}

int FindLastNonDeletedItem(const Scene& scene, const std::vector<int>& newIndices, int node) {
    // we have to be more subtle:
    //   if the (newIndices[firstChild_] == -1), we should follow the link and extract the last non-removed item
    //   ..
    if (node == -1)
        return -1;

    return (newIndices[node] == -1) ?
           FindLastNonDeletedItem(scene, newIndices, scene.mHierarchy[node].mNextSibling) :
           newIndices[node];
}

void ShiftMapIndices(std::unordered_map<uint32_t, uint32_t>& items, const std::vector<int>& newIndices) {
    std::unordered_map<uint32_t, uint32_t> newItems;
    for (const auto& m: items) {
        int newIndex = newIndices[m.first];
        if (newIndex != -1)
            newItems[newIndex] = m.second;
    }
    items = newItems;
}

// Approximately an O ( N * Log(N) * Log(M)) algorithm (N = scene.size, M = nodesToDelete.size) to delete a collection of nodes from scene graph
void DeleteSceneNodes(Scene &scene, const std::vector<uint32_t> &nodesToDelete) {
// 0) Add all the nodes down below in the hierarchy
    auto indicesToDelete = nodesToDelete;
    for (auto i: indicesToDelete)
        CollectNodesToDelete(scene, i, indicesToDelete);

    // aux array with node indices to keep track of the moved ones [moved = [](node) { return (node != nodes[node]); ]
    std::vector<int> nodes(scene.mHierarchy.size());
    std::iota(nodes.begin(), nodes.end(), 0);

    // 1.a) Move all the indicesToDelete to the end of 'nodes' array (and cut them off, a variation of swap'n'pop for multiple elements)
    auto oldSize = nodes.size();
    EraseSelected(nodes, indicesToDelete);

    // 1.b) Make a newIndices[oldIndex] mapping table
    std::vector<int> newIndices(oldSize, -1);
    for(int i = 0 ; i < nodes.size() ; i++)
        newIndices[nodes[i]] = i;

    // 2) Replace all non-null parent/firstChild/nextSibling pointers in all the nodes by new positions
    auto nodeMover = [&scene, &newIndices](Hierarchy& h) {
        return Hierarchy {
                .mParent = (h.mParent != -1) ? newIndices[h.mParent] : -1,
                .mFirstChild = FindLastNonDeletedItem(scene, newIndices, h.mFirstChild),
                .mNextSibling = FindLastNonDeletedItem(scene, newIndices, h.mNextSibling),
                .mLastSibling = FindLastNonDeletedItem(scene, newIndices, h.mLastSibling)
        };
    };
    std::transform(scene.mHierarchy.begin(), scene.mHierarchy.end(), scene.mHierarchy.begin(), nodeMover);

    // 3) Finally throw away the hierarchy items
    EraseSelected(scene.mHierarchy, indicesToDelete);

    // 4) As in mergeScenes() routine we also have to adjust all the "components" (i.e., meshes, materials, names and transformations)

    // 4a) Transformations are stored in arrays, so we just erase the items as we did with the scene.hierarchy_
    EraseSelected(scene.mLocalTransform, indicesToDelete);
    EraseSelected(scene.mGlobalTransform, indicesToDelete);

    // 4b) All the maps should change the key values with the newIndices[] array
    ShiftMapIndices(scene.mMeshes, newIndices);
    ShiftMapIndices(scene.mMaterialForNode, newIndices);
    ShiftMapIndices(scene.mNameForNode, newIndices);

    // 5) scene node names list is not modified, but in principle it can be (remove all non-used items and adjust the nameForNode_ map)
    // 6) Material names list is not modified also, but if some materials fell out of use
}
