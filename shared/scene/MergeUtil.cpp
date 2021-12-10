#include "MergeUtil.h"

#include "shared/scene/Material.h"

#include <map>

static uint32_t shiftMeshIndices(MeshData& meshData, const std::vector<uint32_t>& meshesToMerge)
{
    auto minVtxOffset = std::numeric_limits<uint32_t>::max();
    for (auto i: meshesToMerge)
        minVtxOffset = std::min(meshData.mMeshes[i].vertexOffset, minVtxOffset);

    auto mergeCount = 0u; // calculated by summing index counts in meshesToMerge

    // now shift all the indices in individual index blocks [use minVtxOffset]
    for (auto i: meshesToMerge)
    {
        auto& m = meshData.mMeshes[i];
        // for how much should we shift the indices in mesh [m]
        const uint32_t delta = m.vertexOffset - minVtxOffset;

        const auto idxCount = m.GetLODIndicesCount(0);
        for (auto ii = 0u ; ii < idxCount ; ii++)
            meshData.mIndexData[m.indexOffset + ii] += delta;

        m.vertexOffset = minVtxOffset;

        // sum all the deleted meshes' indices
        mergeCount += idxCount;
    }

    return meshData.mIndexData.size() - mergeCount;
}

// All the meshesToMerge now have the same vertexOffset and individual index values are shifted by appropriate amount
// Here we move all the indices to appropriate places in the new index array
static void mergeIndexArray(MeshData& md, const std::vector<uint32_t>& meshesToMerge, std::map<uint32_t, uint32_t>& oldToNew)
{
    std::vector<uint32_t> newIndices(md.mIndexData.size());
    // Two offsets in the new indices array (one begins at the start, the second one after all the copied indices)
    uint32_t copyOffset = 0,
            mergeOffset = shiftMeshIndices(md, meshesToMerge);

    const auto mergedMeshIndex = md.mMeshes.size() - meshesToMerge.size();
    auto newIndex = 0u;
    for (auto midx = 0u ; midx < md.mMeshes.size() ; midx++)
    {
        const bool shouldMerge = std::binary_search( meshesToMerge.begin(), meshesToMerge.end(), midx);

        oldToNew[midx] = shouldMerge ? mergedMeshIndex : newIndex;
        newIndex += shouldMerge ? 0 : 1;

        auto& mesh = md.mMeshes[midx];
        auto idxCount = mesh.GetLODIndicesCount(0);
        // move all indices to the new array at mergeOffset
        const auto start = md.mIndexData.begin() + mesh.indexOffset;
        mesh.indexOffset = copyOffset;
        const auto offsetPtr = shouldMerge ? &mergeOffset : &copyOffset;
        std::copy(start, start + idxCount, newIndices.begin() + *offsetPtr);
        *offsetPtr += idxCount;
    }

    md.mIndexData = newIndices;

    // all the merged indices are now in lastMesh
    Mesh lastMesh = md.mMeshes[meshesToMerge[0]];
    lastMesh.indexOffset = copyOffset;
    lastMesh.lodOffset[0] = copyOffset;
    lastMesh.lodOffset[1] = mergeOffset;
    lastMesh.lodCount = 1;
    md.mMeshes.push_back(lastMesh);
}

void MergeScene(Scene &scene, MeshData &meshData, const std::string &materialName) {
// Find material index
    int oldMaterial = (int)std::distance(std::begin(scene.mMaterialNames), std::find(std::begin(scene.mMaterialNames), std::end(scene.mMaterialNames), materialName));

    std::vector<uint32_t> toDelete;

    for (auto i = 0u ; i < scene.mHierarchy.size() ; i++)
        if (scene.mMeshes.contains(i) && scene.mMaterialForNode.contains(i) && (scene.mMaterialForNode.at(i) == oldMaterial))
            toDelete.push_back(i);

    std::vector<uint32_t> meshesToMerge(toDelete.size());

    // Convert toDelete indices to mesh indices
    std::transform(toDelete.begin(), toDelete.end(), meshesToMerge.begin(), [&scene](uint32_t i) { return scene.mMeshes.at(i); });

    // TODO: if merged mesh transforms are non-zero, then we should pre-transform individual mesh vertices in meshData using local transform

    // old-to-new mesh indices
    std::map<uint32_t, uint32_t> oldToNew;

    // now move all the meshesToMerge to the end of array
    mergeIndexArray(meshData, meshesToMerge, oldToNew);

    // cutoff all but one of the merged meshes (insert the last saved mesh from meshesToMerge - they are all the same)
    EraseSelected(meshData.mMeshes, meshesToMerge);

    for (auto& n: scene.mMeshes)
        n.second = oldToNew[n.second];

    // reattach the node with merged meshes [identity transforms are assumed]
    int newNode = AddNode(scene, 0, 1);
    scene.mMeshes[newNode] = meshData.mMeshes.size() - 1;
    scene.mMaterialForNode[newNode] = (uint32_t)oldMaterial;

    DeleteSceneNodes(scene, toDelete);
}
