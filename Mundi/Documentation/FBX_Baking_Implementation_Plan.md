# FBX Baking System Implementation Plan

**Document Version:** 1.0
**Date:** 2025-11-11
**Purpose:** Detailed implementation plan for FBX baking system in Mundi Engine

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Implementation Phases](#implementation-phases)
4. [File Structure](#file-structure)
5. [Data Structures](#data-structures)
6. [Serialization Strategy](#serialization-strategy)
7. [Texture Pipeline Integration](#texture-pipeline-integration)
8. [Testing Strategy](#testing-strategy)
9. [Performance Targets](#performance-targets)
10. [Risk Mitigation](#risk-mitigation)

---

## Executive Summary

### Objectives

Implement a FBX baking system that:
1. **Eliminates redundant FBX SDK parsing** by caching processed mesh data to binary files
2. **Follows existing Mundi patterns** established by OBJ and Texture caching systems
3. **Achieves 6-15× load time improvement** (70-120ms → 7-18ms)
4. **Integrates seamlessly** with existing ResourceManager and texture DDS conversion pipeline
5. **Supports skeletal mesh import** with full skeleton and skinning data

### Success Criteria

| Metric | Current | Target |
|--------|---------|--------|
| Load Time (Character.fbx) | 70-120 ms | 7-18 ms |
| Cache Generation Time | N/A | < 150 ms (one-time) |
| Cache File Size | 0 (no cache) | ~150 KB (for 1794 vert mesh) |
| Memory Overhead | 0 | < 5% |
| Cache Hit Rate | 0% | > 95% after first load |

### Key Design Decisions

1. **Cache Storage Strategy:** Alongside source files (`.fbx` → `.fbx.bin`) matching OBJ system
2. **Validation Method:** Timestamp-based comparison (simple, robust)
3. **Serialization Format:** Binary with `FWindowsBinReader/Writer` (consistent with OBJ)
4. **Manager Pattern:** `FFbxManager` class similar to `FObjManager`
5. **Texture Handling:** Automatic DDS conversion using existing `FTextureConverter`

---

## Architecture Overview

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer                                           │
│ - Loads FBX via ResourceManager->Load<USkeletalMesh>()     │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ UResourceManager (Existing)                                 │
│ - In-memory cache (Tier 1)                                  │
│ - Type-safe resource loading                                │
│ - Path normalization                                        │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ USkeletalMesh::Load() (Modified)                            │
│ - Delegates to FFbxManager                                  │
│ - Creates GPU buffers from loaded data                      │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ FFbxManager (NEW)                                           │
│ - Static memory cache (like FObjManager)                    │
│ - Cache path management                                     │
│ - Timestamp validation                                      │
│ - Binary cache serialization                                │
└───────────────────────────┬─────────────────────────────────┘
                            │
        ┌───────────────────┴───────────────────┐
        │                                       │
        ▼ (Cache Valid)                        ▼ (Cache Invalid)
┌─────────────────────────┐       ┌─────────────────────────────┐
│ Load from .fbx.bin      │       │ Parse FBX with SDK          │
│ - FWindowsBinReader     │       │ - FFbxImporter (Existing)   │
│ - Deserialize mesh      │       │ - Extract mesh/skeleton     │
│ - Deserialize skeleton  │       │ - Process textures to DDS   │
│ - Return USkeletalMesh* │       │ - Serialize to .fbx.bin     │
└─────────────────────────┘       └─────────────────────────────┘
```

### Three-Tier Caching Strategy

Following Mundi's established pattern:

```
Tier 1: UResourceManager in-memory cache (fastest)
  └─> TMap<FString, USkeletalMesh*> (per-type resource map)

Tier 2: FFbxManager static cache + Binary disk cache (fast)
  └─> TMap<FString, USkeletalMesh*> FbxSkeletalMeshCache
  └─> .fbx.bin files (binary serialized data)

Tier 3: FBX SDK parsing (slowest)
  └─> FFbxImporter::ImportSkeletalMesh() (existing)
```

### Cache File Strategy

**Pattern:** Alongside source files (matching OBJ system)

```
Data/Model/Fbx/
├── Character.fbx          ← Source FBX file
├── Character.fbx.bin      ← Binary cache (mesh + skeleton + materials)
├── Enemy.fbx
├── Enemy.fbx.bin
└── Prop.fbx
    └── Prop.fbx.bin
```

**Cache File Contents:**
```cpp
Character.fbx.bin:
├── Magic Number (0x46425843 = "FBXC")
├── Version (uint32)
├── Mesh Data
│   ├── Vertices (TArray<FSkinnedVertex>)
│   ├── Indices (TArray<uint32>)
│   └── Bounds (FVector Min/Max)
├── Skeleton Data
│   ├── Bones (TArray<FBoneInfo>)
│   └── InverseBindPoseMatrices (TArray<FMatrix>)
├── Material Data
│   ├── Materials (TArray<FMaterialInfo>)
│   └── Material Groups (TArray<FStaticMeshGroup>)
└── Texture References (TArray<FString> - paths to DDS files)
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1)

**Goal:** Establish FFbxManager and basic caching infrastructure

#### Tasks

**1.1. Create FFbxManager Class**

Location: `Mundi/Source/Editor/FbxManager.h`

```cpp
#pragma once
#include "UEContainer.h"
#include "String.h"

// Forward declarations
class USkeletalMesh;

/**
 * Manages FBX skeletal mesh loading and caching
 * Similar to FObjManager for OBJ files
 */
class FFbxManager
{
private:
    // Static memory cache (lifetime of application)
    static TMap<FString, USkeletalMesh*> FbxSkeletalMeshCache;

public:
    /**
     * Preload all FBX files from Data/Model/Fbx/ directory
     * Called during engine initialization
     */
    static void Preload();

    /**
     * Clear all cached FBX data
     * Called during engine shutdown
     */
    static void Clear();

    /**
     * Load skeletal mesh from FBX file with caching
     * @param PathFileName - Path to .fbx file
     * @return Loaded skeletal mesh or nullptr on failure
     */
    static USkeletalMesh* LoadFbxSkeletalMesh(const FString& PathFileName);

private:
    /**
     * Get cache file path for FBX file
     * @param FbxPath - Source FBX path (e.g., "Data/Model/Character.fbx")
     * @return Cache path (e.g., "Data/Model/Character.fbx.bin")
     */
    static FString GetFbxCachePath(const FString& FbxPath);

    /**
     * Check if cache needs regeneration
     * @param FbxPath - Source FBX path
     * @param CachePath - Binary cache path
     * @return True if cache is missing or stale
     */
    static bool ShouldRegenerateCache(const FString& FbxPath, const FString& CachePath);

    /**
     * Load skeletal mesh from binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param OutMesh - Output skeletal mesh
     * @return True if successfully loaded
     */
    static bool LoadFromCache(const FString& CachePath, USkeletalMesh* OutMesh);

    /**
     * Save skeletal mesh to binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param Mesh - Skeletal mesh to save
     * @return True if successfully saved
     */
    static bool SaveToCache(const FString& CachePath, const USkeletalMesh* Mesh);
};
```

**1.2. Implement Cache Path and Validation Logic**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
#include "pch.h"
#include "FbxManager.h"
#include "SkeletalMesh.h"
#include "GlobalConsole.h"
#include <filesystem>

// Static member initialization
TMap<FString, USkeletalMesh*> FFbxManager::FbxSkeletalMeshCache;

FString FFbxManager::GetFbxCachePath(const FString& FbxPath)
{
    // Simple pattern: append ".bin" to FBX filename
    // "Data/Model/Character.fbx" → "Data/Model/Character.fbx.bin"
    return FbxPath + ".bin";
}

bool FFbxManager::ShouldRegenerateCache(const FString& FbxPath, const FString& CachePath)
{
    namespace fs = std::filesystem;

    // 1. Check if cache file exists
    if (!fs::exists(CachePath))
    {
        UE_LOG("FBX cache not found: %s", CachePath.c_str());
        return true;
    }

    // 2. Check if source FBX exists
    if (!fs::exists(FbxPath))
    {
        UE_LOG("[error] Source FBX not found: %s", FbxPath.c_str());
        return false;  // Can't regenerate without source
    }

    // 3. Compare timestamps
    auto FbxTime = fs::last_write_time(FbxPath);
    auto CacheTime = fs::last_write_time(CachePath);

    if (FbxTime > CacheTime)
    {
        UE_LOG("FBX source modified, cache is stale: %s", FbxPath.c_str());
        return true;
    }

    // Cache is valid
    return false;
}

void FFbxManager::Clear()
{
    // Clear static cache map
    // Note: USkeletalMesh objects are managed by ObjectFactory/ResourceManager
    FbxSkeletalMeshCache.clear();

    UE_LOG("FFbxManager: Cleared cache (%d entries)", FbxSkeletalMeshCache.size());
}

void FFbxManager::Preload()
{
    namespace fs = std::filesystem;

    // Find all .fbx files in Data/Model/Fbx/
    FString FbxDirectory = "Data/Model/Fbx/";

    if (!fs::exists(FbxDirectory))
    {
        UE_LOG("[warning] FBX directory not found: %s", FbxDirectory.c_str());
        return;
    }

    int32 LoadedCount = 0;
    for (const auto& Entry : fs::recursive_directory_iterator(FbxDirectory))
    {
        if (Entry.is_regular_file() && Entry.path().extension() == ".fbx")
        {
            FString FbxPath = Entry.path().string();

            USkeletalMesh* Mesh = LoadFbxSkeletalMesh(FbxPath);
            if (Mesh)
            {
                LoadedCount++;
            }
        }
    }

    UE_LOG("FFbxManager: Preloaded %d FBX files", LoadedCount);
}
```

**1.3. Testing Checklist**

- [ ] `GetFbxCachePath()` correctly appends `.bin` extension
- [ ] `ShouldRegenerateCache()` returns true for missing cache
- [ ] `ShouldRegenerateCache()` returns false for valid cache
- [ ] `ShouldRegenerateCache()` returns true when FBX is newer
- [ ] `Clear()` empties static cache map
- [ ] `Preload()` finds all .fbx files in directory

---

### Phase 2: Serialization Support (Week 1-2)

**Goal:** Implement binary serialization for all FBX-related data structures

#### Tasks

**2.1. Add Serialization to FSkinnedVertex**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h`

```cpp
struct FSkinnedVertex
{
    FVector Position;
    FVector Normal;
    FVector4 Tangent;
    FVector2D UV;
    uint32 BoneIndices[4];
    float BoneWeights[4];

    // NEW: Binary serialization operators
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkinnedVertex& Vertex);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkinnedVertex& Vertex);
};
```

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp`

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkinnedVertex& Vertex)
{
    // Write POD data in bulk
    Writer.Write(&Vertex.Position, sizeof(FVector));
    Writer.Write(&Vertex.Normal, sizeof(FVector));
    Writer.Write(&Vertex.Tangent, sizeof(FVector4));
    Writer.Write(&Vertex.UV, sizeof(FVector2D));
    Writer.Write(Vertex.BoneIndices, sizeof(uint32) * 4);
    Writer.Write(Vertex.BoneWeights, sizeof(float) * 4);

    return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkinnedVertex& Vertex)
{
    Reader.Read(&Vertex.Position, sizeof(FVector));
    Reader.Read(&Vertex.Normal, sizeof(FVector));
    Reader.Read(&Vertex.Tangent, sizeof(FVector4));
    Reader.Read(&Vertex.UV, sizeof(FVector2D));
    Reader.Read(Vertex.BoneIndices, sizeof(uint32) * 4);
    Reader.Read(Vertex.BoneWeights, sizeof(float) * 4);

    return Reader;
}
```

**2.2. Add Serialization to FBoneInfo**

Location: `Mundi/Source/Runtime/AssetManagement/Skeleton.h`

```cpp
struct FBoneInfo
{
    FString Name;
    int32 ParentIndex;
    FTransform LocalTransform;      // Relative to parent
    FTransform GlobalTransform;     // World space
    FMatrix InverseBindPoseMatrix;  // For skinning

    // NEW: Binary serialization operators
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FBoneInfo& Bone);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FBoneInfo& Bone);
};
```

Location: `Mundi/Source/Runtime/AssetManagement/Skeleton.cpp`

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FBoneInfo& Bone)
{
    // Write bone name
    Writer.WriteString(Bone.Name);

    // Write parent index
    Writer.Write(&Bone.ParentIndex, sizeof(int32));

    // Write transforms (POD structures)
    Writer.Write(&Bone.LocalTransform, sizeof(FTransform));
    Writer.Write(&Bone.GlobalTransform, sizeof(FTransform));

    // Write inverse bind pose matrix
    Writer.Write(&Bone.InverseBindPoseMatrix, sizeof(FMatrix));

    return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FBoneInfo& Bone)
{
    Bone.Name = Reader.ReadString();
    Reader.Read(&Bone.ParentIndex, sizeof(int32));
    Reader.Read(&Bone.LocalTransform, sizeof(FTransform));
    Reader.Read(&Bone.GlobalTransform, sizeof(FTransform));
    Reader.Read(&Bone.InverseBindPoseMatrix, sizeof(FMatrix));

    return Reader;
}
```

**2.3. Add Serialization to USkeleton**

Location: `Mundi/Source/Runtime/AssetManagement/Skeleton.h`

```cpp
class USkeleton : public UResourceBase
{
public:
    DECLARE_CLASS(USkeleton, UResourceBase)

    UPROPERTY()
    TArray<FBoneInfo> Bones;

    UPROPERTY()
    TMap<FString, int32> BoneNameToIndexMap;  // For fast lookup

    // NEW: Binary serialization operators
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const USkeleton& Skeleton);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, USkeleton& Skeleton);

    // ... existing methods
};
```

Location: `Mundi/Source/Runtime/AssetManagement/Skeleton.cpp`

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const USkeleton& Skeleton)
{
    // Write bone count
    uint32 BoneCount = Skeleton.Bones.size();
    Writer.Write(&BoneCount, sizeof(uint32));

    // Write bone array
    for (const FBoneInfo& Bone : Skeleton.Bones)
    {
        Writer << Bone;  // Uses FBoneInfo's operator<<
    }

    // Note: BoneNameToIndexMap can be rebuilt from Bones array on load
    // No need to serialize it

    return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, USkeleton& Skeleton)
{
    // Read bone count
    uint32 BoneCount;
    Reader.Read(&BoneCount, sizeof(uint32));

    // Read bone array
    Skeleton.Bones.clear();
    Skeleton.Bones.reserve(BoneCount);

    for (uint32 i = 0; i < BoneCount; ++i)
    {
        FBoneInfo Bone;
        Reader >> Bone;
        Skeleton.Bones.push_back(Bone);
    }

    // Rebuild bone name map
    Skeleton.BoneNameToIndexMap.clear();
    for (size_t i = 0; i < Skeleton.Bones.size(); ++i)
    {
        Skeleton.BoneNameToIndexMap[Skeleton.Bones[i].Name] = static_cast<int32>(i);
    }

    return Reader;
}
```

**2.4. Add Serialization to USkeletalMesh**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h`

```cpp
class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    UPROPERTY()
    TArray<FSkinnedVertex> Vertices;

    UPROPERTY()
    TArray<uint32> Indices;

    UPROPERTY()
    USkeleton* Skeleton = nullptr;

    UPROPERTY()
    TArray<FMaterialInfo> Materials;

    UPROPERTY()
    TArray<FStaticMeshGroup> MaterialGroups;

    UPROPERTY()
    FVector BoundsMin;

    UPROPERTY()
    FVector BoundsMax;

    // NEW: Binary serialization operators
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const USkeletalMesh& Mesh);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, USkeletalMesh& Mesh);

    // ... existing methods
};
```

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp`

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const USkeletalMesh& Mesh)
{
    // Write magic number and version
    uint32 MagicNumber = 0x46425843;  // "FBXC" in hex
    uint32 Version = 1;
    Writer.Write(&MagicNumber, sizeof(uint32));
    Writer.Write(&Version, sizeof(uint32));

    // Write vertex count and data
    uint32 VertexCount = Mesh.Vertices.size();
    Writer.Write(&VertexCount, sizeof(uint32));
    for (const FSkinnedVertex& Vertex : Mesh.Vertices)
    {
        Writer << Vertex;
    }

    // Write index count and data
    uint32 IndexCount = Mesh.Indices.size();
    Writer.Write(&IndexCount, sizeof(uint32));
    Writer.Write(Mesh.Indices.data(), IndexCount * sizeof(uint32));

    // Write skeleton (if exists)
    bool bHasSkeleton = (Mesh.Skeleton != nullptr);
    Writer.Write(&bHasSkeleton, sizeof(bool));
    if (bHasSkeleton)
    {
        Writer << *Mesh.Skeleton;
    }

    // Write materials
    uint32 MaterialCount = Mesh.Materials.size();
    Writer.Write(&MaterialCount, sizeof(uint32));
    for (const FMaterialInfo& Material : Mesh.Materials)
    {
        Writer << Material;  // Uses FMaterialInfo's operator<< (already exists)
    }

    // Write material groups
    uint32 GroupCount = Mesh.MaterialGroups.size();
    Writer.Write(&GroupCount, sizeof(uint32));
    Writer.Write(Mesh.MaterialGroups.data(), GroupCount * sizeof(FStaticMeshGroup));

    // Write bounds
    Writer.Write(&Mesh.BoundsMin, sizeof(FVector));
    Writer.Write(&Mesh.BoundsMax, sizeof(FVector));

    return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, USkeletalMesh& Mesh)
{
    // Read and validate magic number
    uint32 MagicNumber, Version;
    Reader.Read(&MagicNumber, sizeof(uint32));
    Reader.Read(&Version, sizeof(uint32));

    if (MagicNumber != 0x46425843)
    {
        UE_LOG("[error] Invalid FBX cache file (bad magic number)");
        return Reader;
    }

    if (Version != 1)
    {
        UE_LOG("[error] Unsupported FBX cache version: %d", Version);
        return Reader;
    }

    // Read vertices
    uint32 VertexCount;
    Reader.Read(&VertexCount, sizeof(uint32));
    Mesh.Vertices.clear();
    Mesh.Vertices.reserve(VertexCount);
    for (uint32 i = 0; i < VertexCount; ++i)
    {
        FSkinnedVertex Vertex;
        Reader >> Vertex;
        Mesh.Vertices.push_back(Vertex);
    }

    // Read indices
    uint32 IndexCount;
    Reader.Read(&IndexCount, sizeof(uint32));
    Mesh.Indices.resize(IndexCount);
    Reader.Read(Mesh.Indices.data(), IndexCount * sizeof(uint32));

    // Read skeleton
    bool bHasSkeleton;
    Reader.Read(&bHasSkeleton, sizeof(bool));
    if (bHasSkeleton)
    {
        Mesh.Skeleton = ObjectFactory::NewObject<USkeleton>();
        Reader >> *Mesh.Skeleton;
    }

    // Read materials
    uint32 MaterialCount;
    Reader.Read(&MaterialCount, sizeof(uint32));
    Mesh.Materials.clear();
    Mesh.Materials.reserve(MaterialCount);
    for (uint32 i = 0; i < MaterialCount; ++i)
    {
        FMaterialInfo Material;
        Reader >> Material;
        Mesh.Materials.push_back(Material);
    }

    // Read material groups
    uint32 GroupCount;
    Reader.Read(&GroupCount, sizeof(uint32));
    Mesh.MaterialGroups.resize(GroupCount);
    Reader.Read(Mesh.MaterialGroups.data(), GroupCount * sizeof(FStaticMeshGroup));

    // Read bounds
    Reader.Read(&Mesh.BoundsMin, sizeof(FVector));
    Reader.Read(&Mesh.BoundsMax, sizeof(FVector));

    return Reader;
}
```

**2.5. Testing Checklist**

- [ ] Serialize and deserialize FSkinnedVertex (verify data integrity)
- [ ] Serialize and deserialize FBoneInfo (verify strings and matrices)
- [ ] Serialize and deserialize USkeleton (verify bone hierarchy)
- [ ] Serialize and deserialize USkeletalMesh (verify all components)
- [ ] Test with empty skeleton (bHasSkeleton = false)
- [ ] Test with missing materials
- [ ] Verify magic number validation rejects invalid files

---

### Phase 3: Cache Loading/Saving (Week 2)

**Goal:** Implement complete cache load/save pipeline

#### Tasks

**3.1. Implement LoadFromCache()**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
bool FFbxManager::LoadFromCache(const FString& CachePath, USkeletalMesh* OutMesh)
{
    namespace fs = std::filesystem;

    if (!fs::exists(CachePath))
    {
        return false;
    }

    try
    {
        FWindowsBinReader Reader(CachePath);

        // Deserialize mesh
        Reader >> *OutMesh;

        UE_LOG("Loaded FBX from cache: %s (%d vertices, %d indices)",
            CachePath.c_str(),
            OutMesh->Vertices.size(),
            OutMesh->Indices.size());

        return true;
    }
    catch (const std::exception& e)
    {
        UE_LOG("[error] Failed to load FBX cache: %s - %s", CachePath.c_str(), e.what());
        return false;
    }
}
```

**3.2. Implement SaveToCache()**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
bool FFbxManager::SaveToCache(const FString& CachePath, const USkeletalMesh* Mesh)
{
    try
    {
        FWindowsBinWriter Writer(CachePath);

        // Serialize mesh
        Writer << *Mesh;

        UE_LOG("Saved FBX cache: %s (%d vertices, %d indices)",
            CachePath.c_str(),
            Mesh->Vertices.size(),
            Mesh->Indices.size());

        return true;
    }
    catch (const std::exception& e)
    {
        UE_LOG("[error] Failed to save FBX cache: %s - %s", CachePath.c_str(), e.what());
        return false;
    }
}
```

**3.3. Implement LoadFbxSkeletalMesh() Main Logic**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
USkeletalMesh* FFbxManager::LoadFbxSkeletalMesh(const FString& PathFileName)
{
    // ──────────────────────────────────────────────────────
    // 1. Check static memory cache
    // ──────────────────────────────────────────────────────
    auto Iter = FbxSkeletalMeshCache.find(PathFileName);
    if (Iter != FbxSkeletalMeshCache.end())
    {
        UE_LOG("FBX already in memory cache: %s", PathFileName.c_str());
        return Iter->second;
    }

    // ──────────────────────────────────────────────────────
    // 2. Get cache path and validate
    // ──────────────────────────────────────────────────────
    FString CachePath = GetFbxCachePath(PathFileName);
    bool bShouldRegenerate = ShouldRegenerateCache(PathFileName, CachePath);

    USkeletalMesh* NewMesh = ObjectFactory::NewObject<USkeletalMesh>();

    if (!bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 3. Load from binary cache (FAST PATH)
        // ──────────────────────────────────────────────────────
        if (LoadFromCache(CachePath, NewMesh))
        {
            // Cache loaded successfully
            FbxSkeletalMeshCache[PathFileName] = NewMesh;
            return NewMesh;
        }
        else
        {
            // Cache load failed, fall through to regenerate
            UE_LOG("[warning] Cache load failed, regenerating: %s", CachePath.c_str());
            bShouldRegenerate = true;
        }
    }

    if (bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 4. Parse FBX with SDK (SLOW PATH)
        // ──────────────────────────────────────────────────────
        FFbxImporter Importer;

        if (!Importer.ImportSkeletalMesh(PathFileName, NewMesh))
        {
            UE_LOG("[error] Failed to import FBX: %s", PathFileName.c_str());
            ObjectFactory::Destroy(NewMesh);
            return nullptr;
        }

        UE_LOG("Imported FBX: %s (%d vertices, %d bones)",
            PathFileName.c_str(),
            NewMesh->Vertices.size(),
            NewMesh->Skeleton ? NewMesh->Skeleton->Bones.size() : 0);

        // ──────────────────────────────────────────────────────
        // 5. Serialize to binary cache
        // ──────────────────────────────────────────────────────
        if (!SaveToCache(CachePath, NewMesh))
        {
            UE_LOG("[warning] Failed to save FBX cache (will still use loaded data): %s", CachePath.c_str());
        }
    }

    // ──────────────────────────────────────────────────────
    // 6. Store in static memory cache
    // ──────────────────────────────────────────────────────
    FbxSkeletalMeshCache[PathFileName] = NewMesh;

    return NewMesh;
}
```

**3.4. Testing Checklist**

- [ ] First load triggers FBX import and creates cache
- [ ] Second load uses cache (verify performance improvement)
- [ ] Cache regenerates when FBX is modified
- [ ] Graceful fallback if cache is corrupted
- [ ] Memory cache prevents redundant disk reads
- [ ] Multiple meshes can be loaded simultaneously

---

### Phase 4: ResourceManager Integration (Week 2-3)

**Goal:** Integrate FFbxManager with existing ResourceManager pipeline

#### Tasks

**4.1. Modify USkeletalMesh::Load()**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp`

```cpp
void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    // ──────────────────────────────────────────────────────
    // Delegate to FFbxManager for loading/caching
    // ──────────────────────────────────────────────────────
    USkeletalMesh* CachedMesh = FFbxManager::LoadFbxSkeletalMesh(InFilePath);

    if (!CachedMesh)
    {
        UE_LOG("[error] Failed to load skeletal mesh: %s", InFilePath.c_str());
        return;
    }

    // ──────────────────────────────────────────────────────
    // Copy data from cached mesh
    // ──────────────────────────────────────────────────────
    this->Vertices = CachedMesh->Vertices;
    this->Indices = CachedMesh->Indices;
    this->Skeleton = CachedMesh->Skeleton;
    this->Materials = CachedMesh->Materials;
    this->MaterialGroups = CachedMesh->MaterialGroups;
    this->BoundsMin = CachedMesh->BoundsMin;
    this->BoundsMax = CachedMesh->BoundsMax;

    // ──────────────────────────────────────────────────────
    // Create GPU buffers
    // ──────────────────────────────────────────────────────
    if (Vertices.size() > 0 && Indices.size() > 0)
    {
        CreateVertexBuffer(InDevice, InVertexType);
        CreateIndexBuffer(InDevice);

        UE_LOG("Created GPU buffers for skeletal mesh: %s", InFilePath.c_str());
    }

    // ──────────────────────────────────────────────────────
    // Store file path
    // ──────────────────────────────────────────────────────
    this->SetFilePath(InFilePath);
}
```

**4.2. Ensure ResourceManager Type Registration**

Location: `Mundi/Source/Runtime/AssetManagement/ResourceManager.h`

Verify that `USkeletalMesh` is registered in `GetResourceType<T>()`:

```cpp
template<typename T>
ResourceType UResourceManager::GetResourceType()
{
    if (T::StaticClass() == UStaticMesh::StaticClass())
        return ResourceType::StaticMesh;
    if (T::StaticClass() == USkeletalMesh::StaticClass())  // Ensure this exists
        return ResourceType::SkeletalMesh;
    if (T::StaticClass() == UTexture::StaticClass())
        return ResourceType::Texture;
    // ... other types

    return ResourceType::None;
}
```

**4.3. Add ResourceType Enum Value (If Missing)**

Location: `Mundi/Source/Runtime/AssetManagement/ResourceBase.h`

```cpp
enum class ResourceType : uint8
{
    None = 0,
    StaticMesh,
    SkeletalMesh,  // Ensure this exists
    Quad,
    DynamicMesh,
    Shader,
    Texture,
    Material,
    Sound,
    // ...
};
```

**4.4. Testing Checklist**

- [ ] `ResourceManager->Load<USkeletalMesh>("path.fbx")` works correctly
- [ ] ResourceManager in-memory cache prevents redundant loads
- [ ] Multiple different skeletal meshes can coexist
- [ ] GPU buffers are created correctly
- [ ] Material references are preserved

---

### Phase 5: Texture Pipeline Integration (Week 3)

**Goal:** Ensure FBX-referenced textures use DDS conversion pipeline

#### Tasks

**5.1. Modify FFbxImporter to Track Texture References**

Location: `Mundi/Source/Editor/FbxImporter.cpp`

In `ImportSkeletalMesh()`, after material extraction:

```cpp
void FFbxImporter::ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh)
{
    // ... existing FBX parsing code ...

    // Extract materials and textures
    ExtractMaterials(FbxScene, OutMesh);

    // NEW: Process texture references
    ProcessTextureReferences(OutMesh, FilePath);
}

void FFbxImporter::ProcessTextureReferences(USkeletalMesh* Mesh, const FString& FbxPath)
{
    // Get directory of FBX file (textures are usually relative to FBX)
    FString FbxDirectory = GetDirectoryPath(FbxPath);

    for (FMaterialInfo& Material : Mesh->Materials)
    {
        // Process diffuse map
        if (!Material.DiffuseMap.empty())
        {
            FString TexturePath = ResolveTexturePath(Material.DiffuseMap, FbxDirectory);

            // Convert to DDS if needed
            FString DDSPath = ConvertTextureToDDS(TexturePath);

            if (!DDSPath.empty())
            {
                // Update material to reference DDS file
                Material.DiffuseMap = DDSPath;
                UE_LOG("Converted texture to DDS: %s -> %s", TexturePath.c_str(), DDSPath.c_str());
            }
        }

        // Process normal map
        if (!Material.NormalMap.empty())
        {
            FString TexturePath = ResolveTexturePath(Material.NormalMap, FbxDirectory);
            FString DDSPath = ConvertTextureToDDS(TexturePath);
            if (!DDSPath.empty())
            {
                Material.NormalMap = DDSPath;
            }
        }

        // Process other texture maps...
        // (Specular, Emissive, Roughness, Metallic, etc.)
    }
}

FString FFbxImporter::ResolveTexturePath(const FString& TextureName, const FString& FbxDirectory)
{
    namespace fs = std::filesystem;

    // Try various path combinations
    TArray<FString> SearchPaths = {
        TextureName,                                      // Absolute path
        FbxDirectory + "/" + TextureName,                // Relative to FBX
        "Data/Textures/" + TextureName,                  // Standard texture directory
        FbxDirectory + "/../Textures/" + TextureName     // Sibling Textures folder
    };

    for (const FString& Path : SearchPaths)
    {
        if (fs::exists(Path))
        {
            return Path;
        }
    }

    UE_LOG("[warning] Texture not found: %s", TextureName.c_str());
    return "";
}

FString FFbxImporter::ConvertTextureToDDS(const FString& SourceTexturePath)
{
    if (SourceTexturePath.empty())
    {
        return "";
    }

    namespace fs = std::filesystem;
    FString Extension = fs::path(SourceTexturePath).extension().string();

    // Already DDS, no conversion needed
    if (Extension == ".dds")
    {
        return SourceTexturePath;
    }

    // Use existing FTextureConverter system
    FString DDSCachePath = FTextureConverter::GetDDSCachePath(SourceTexturePath);

    // Check if conversion is needed
    if (FTextureConverter::ShouldRegenerateDDS(SourceTexturePath, DDSCachePath))
    {
        // Determine format based on texture type
        bool bIsSRGB = true;  // Diffuse maps use sRGB
        DXGI_FORMAT Format = FTextureConverter::GetRecommendedFormat(true, bIsSRGB);

        if (!FTextureConverter::ConvertToDDS(SourceTexturePath, DDSCachePath, Format))
        {
            UE_LOG("[error] Failed to convert texture: %s", SourceTexturePath.c_str());
            return SourceTexturePath;  // Fall back to source
        }
    }

    return DDSCachePath;
}
```

**5.2. Texture Path Serialization**

Ensure material serialization includes updated DDS paths:

Location: `Mundi/Source/Runtime/AssetManagement/Material.cpp`

FMaterialInfo serialization already exists - verify it handles texture paths correctly:

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FMaterialInfo& Mat)
{
    Writer.WriteString(Mat.MaterialName);
    Writer.Write(&Mat.Ambient, sizeof(FVector));
    Writer.Write(&Mat.Diffuse, sizeof(FVector));
    Writer.Write(&Mat.Specular, sizeof(FVector));
    Writer.Write(&Mat.Shininess, sizeof(float));
    Writer.Write(&Mat.Opacity, sizeof(float));

    // Texture paths (already DDS if converted)
    Writer.WriteString(Mat.DiffuseMap);
    Writer.WriteString(Mat.NormalMap);
    // ... other maps

    return Writer;
}
```

**5.3. Testing Checklist**

- [ ] FBX with PNG diffuse map converts to DDS
- [ ] FBX with JPG normal map converts to DDS
- [ ] DDS cache is reused on subsequent loads
- [ ] Material references point to DDS files after import
- [ ] Texture paths are correctly serialized to .fbx.bin
- [ ] Textures load correctly when rendering

---

### Phase 6: Preloading and Initialization (Week 3)

**Goal:** Integrate with engine initialization for preloading

#### Tasks

**6.1. Add Preload Call to Engine Initialization**

Location: `Mundi/Source/Runtime/Engine/EditorEngine.cpp`

```cpp
void EditorEngine::Initialize()
{
    // ... existing initialization ...

    // Preload OBJ meshes (existing)
    FObjManager::Preload();

    // NEW: Preload FBX meshes
    FFbxManager::Preload();

    UE_LOG("Engine initialization complete");
}
```

Location: `Mundi/Source/Runtime/Engine/GameEngine.cpp`

```cpp
void GameEngine::Initialize()
{
    // ... existing initialization ...

    // Preload resources (if needed for standalone game)
    FObjManager::Preload();

    // NEW: Preload FBX meshes
    FFbxManager::Preload();

    UE_LOG("Game engine initialization complete");
}
```

**6.2. Add Cleanup to Engine Shutdown**

Location: `Mundi/Source/Runtime/Engine/EditorEngine.cpp`

```cpp
void EditorEngine::Shutdown()
{
    UE_LOG("Shutting down EditorEngine...");

    // Clear caches
    FObjManager::Clear();
    FFbxManager::Clear();  // NEW

    // ... existing shutdown code ...
}
```

**6.3. Testing Checklist**

- [ ] Engine startup preloads all FBX files
- [ ] Preloaded meshes are immediately available
- [ ] No duplicate loading occurs after preload
- [ ] Engine shutdown clears FBX cache properly
- [ ] Memory is released correctly on shutdown

---

## File Structure

### New Files to Create

```
Mundi/Source/Editor/
├── FbxManager.h              (NEW)
└── FbxManager.cpp            (NEW)
```

### Files to Modify

```
Mundi/Source/Runtime/AssetManagement/
├── SkeletalMesh.h            (Add serialization operators)
├── SkeletalMesh.cpp          (Implement serialization, modify Load())
├── Skeleton.h                (Add serialization operators)
├── Skeleton.cpp              (Implement serialization)
└── ResourceManager.h         (Verify SkeletalMesh type registration)

Mundi/Source/Editor/
└── FbxImporter.cpp           (Add texture processing)

Mundi/Source/Runtime/Engine/
├── EditorEngine.cpp          (Add preload call)
└── GameEngine.cpp            (Add preload call)
```

---

## Data Structures

### FBX Cache File Format

```
.fbx.bin Binary Layout:
┌────────────────────────────────────────────┐
│ Header                                     │
├────────────────────────────────────────────┤
│ uint32 MagicNumber = 0x46425843 ("FBXC")  │
│ uint32 Version = 1                         │
├────────────────────────────────────────────┤
│ Mesh Data                                  │
├────────────────────────────────────────────┤
│ uint32 VertexCount                         │
│ FSkinnedVertex[VertexCount]                │
│   - FVector Position (12 bytes)            │
│   - FVector Normal (12 bytes)              │
│   - FVector4 Tangent (16 bytes)            │
│   - FVector2D UV (8 bytes)                 │
│   - uint32 BoneIndices[4] (16 bytes)       │
│   - float BoneWeights[4] (16 bytes)        │
│   Total: 80 bytes per vertex               │
├────────────────────────────────────────────┤
│ uint32 IndexCount                          │
│ uint32[IndexCount] Indices                 │
├────────────────────────────────────────────┤
│ Skeleton Data                              │
├────────────────────────────────────────────┤
│ bool bHasSkeleton                          │
│ [if bHasSkeleton]                          │
│   uint32 BoneCount                         │
│   FBoneInfo[BoneCount]                     │
│     - FString Name (4 + N bytes)           │
│     - int32 ParentIndex (4 bytes)          │
│     - FTransform LocalTransform (40 bytes) │
│     - FTransform GlobalTransform (40 bytes)│
│     - FMatrix InverseBindPose (64 bytes)   │
│     Total: ~152 + N bytes per bone         │
├────────────────────────────────────────────┤
│ Material Data                              │
├────────────────────────────────────────────┤
│ uint32 MaterialCount                       │
│ FMaterialInfo[MaterialCount]               │
│   (Existing serialization format)          │
├────────────────────────────────────────────┤
│ uint32 GroupCount                          │
│ FStaticMeshGroup[GroupCount]               │
├────────────────────────────────────────────┤
│ Bounds                                     │
├────────────────────────────────────────────┤
│ FVector BoundsMin (12 bytes)               │
│ FVector BoundsMax (12 bytes)               │
└────────────────────────────────────────────┘
```

### File Size Estimation

Example: Character mesh (1794 vertices, 598 triangles, 40 bones)

```
Header:              8 bytes
Vertices:            143,520 bytes (1794 × 80)
Indices:             7,176 bytes (1794 × 4)
Skeleton:            6,080 bytes (40 × 152)
Materials:           ~500 bytes (varies)
Groups:              ~100 bytes
Bounds:              24 bytes
────────────────────────────────────
Total:               ~157 KB

vs Original FBX:     ~2 MB
Compression ratio:   13:1
```

---

## Serialization Strategy

### Design Principles

1. **Binary Format:** Compact, fast to read/write
2. **Versioning:** Magic number + version for future compatibility
3. **Validation:** Check magic number on load
4. **Error Handling:** Graceful degradation (fall back to FBX import)
5. **Platform-Specific:** Windows-optimized (struct packing assumptions)

### Versioning Strategy

```cpp
// Version 1 (Initial)
struct FFbxCacheHeader
{
    uint32 MagicNumber = 0x46425843;  // "FBXC"
    uint32 Version = 1;
};

// Future Version 2 (Example: Add LOD support)
struct FFbxCacheHeader_V2
{
    uint32 MagicNumber = 0x46425843;
    uint32 Version = 2;
    uint8 NumLODs;  // NEW FIELD
};

// Load logic handles multiple versions:
if (Version == 1)
{
    LoadV1Format(Reader, OutMesh);
}
else if (Version == 2)
{
    LoadV2Format(Reader, OutMesh);
}
```

### Error Handling Strategy

```cpp
bool FFbxManager::LoadFromCache(const FString& CachePath, USkeletalMesh* OutMesh)
{
    try
    {
        FWindowsBinReader Reader(CachePath);

        // Validate magic number
        uint32 MagicNumber;
        Reader.Read(&MagicNumber, sizeof(uint32));

        if (MagicNumber != 0x46425843)
        {
            UE_LOG("[error] Invalid cache file (bad magic): %s", CachePath.c_str());
            return false;  // Trigger regeneration
        }

        // Read version
        uint32 Version;
        Reader.Read(&Version, sizeof(uint32));

        if (Version > CURRENT_VERSION)
        {
            UE_LOG("[error] Cache version too new: %d > %d", Version, CURRENT_VERSION);
            return false;  // Trigger regeneration
        }

        // Deserialize (version-specific)
        if (Version == 1)
        {
            LoadV1Format(Reader, OutMesh);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        UE_LOG("[error] Cache load exception: %s - %s", CachePath.c_str(), e.what());
        return false;  // Trigger regeneration
    }
}
```

---

## Texture Pipeline Integration

### Texture Processing Workflow

```
FBX Import
    ↓
Extract Material Info (with texture paths)
    ↓
For each texture reference:
    ├─> Resolve absolute path (search multiple directories)
    ├─> Check if already DDS → Skip conversion
    ├─> Check DDS cache validity
    ├─> Convert to DDS if needed (FTextureConverter)
    └─> Update material reference to DDS path
    ↓
Serialize material with DDS paths to .fbx.bin
    ↓
Future loads use cached DDS references
```

### Texture Path Resolution

FBX files can reference textures in various ways:

1. **Absolute Path:** `C:/Projects/Mundi/Data/Textures/Character_Diffuse.png`
2. **Relative to FBX:** `../Textures/Character_Diffuse.png`
3. **Filename Only:** `Character_Diffuse.png`

Resolution strategy:

```cpp
FString ResolveTexturePath(const FString& TextureName, const FString& FbxDirectory)
{
    // Priority order:
    // 1. As-is (absolute path)
    // 2. Relative to FBX file
    // 3. Standard texture directory (Data/Textures/)
    // 4. Sibling Textures folder (../Textures/)

    TArray<FString> SearchPaths = {
        TextureName,
        FbxDirectory + "/" + TextureName,
        "Data/Textures/" + GetFilename(TextureName),
        FbxDirectory + "/../Textures/" + GetFilename(TextureName)
    };

    for (const FString& Path : SearchPaths)
    {
        if (FileExists(Path))
        {
            return Path;
        }
    }

    return "";  // Not found
}
```

### DDS Cache Integration

```
Texture Resolution Result: "Data/Textures/Character_Diffuse.png"
    ↓
FTextureConverter::GetDDSCachePath()
    ↓
"Data/Textures/Cache/Character_Diffuse.dds"
    ↓
FTextureConverter::ShouldRegenerateDDS()
    ↓
[If stale] FTextureConverter::ConvertToDDS()
    ↓
Material.DiffuseMap = "Data/Textures/Cache/Character_Diffuse.dds"
    ↓
Serialized to .fbx.bin
```

---

## Testing Strategy

### Unit Tests

**Test 1: Cache Path Generation**
```cpp
void Test_GetFbxCachePath()
{
    FString FbxPath = "Data/Model/Character.fbx";
    FString Expected = "Data/Model/Character.fbx.bin";
    FString Result = FFbxManager::GetFbxCachePath(FbxPath);

    Assert(Result == Expected, "Cache path mismatch");
}
```

**Test 2: Cache Validation**
```cpp
void Test_ShouldRegenerateCache()
{
    // Missing cache
    Assert(ShouldRegenerateCache("test.fbx", "missing.bin") == true);

    // Valid cache
    CreateDummyCache("test.bin", OlderThan("test.fbx"));
    Assert(ShouldRegenerateCache("test.fbx", "test.bin") == false);

    // Stale cache
    Touch("test.fbx");  // Make newer
    Assert(ShouldRegenerateCache("test.fbx", "test.bin") == true);
}
```

**Test 3: Serialization Round-Trip**
```cpp
void Test_SerializationRoundTrip()
{
    // Create test mesh
    USkeletalMesh* OriginalMesh = CreateTestSkeletalMesh();

    // Serialize
    FWindowsBinWriter Writer("test.bin");
    Writer << *OriginalMesh;

    // Deserialize
    USkeletalMesh* LoadedMesh = NewObject<USkeletalMesh>();
    FWindowsBinReader Reader("test.bin");
    Reader >> *LoadedMesh;

    // Verify
    AssertEqual(OriginalMesh->Vertices.size(), LoadedMesh->Vertices.size());
    AssertEqual(OriginalMesh->Indices.size(), LoadedMesh->Indices.size());
    AssertEqual(OriginalMesh->Skeleton->Bones.size(), LoadedMesh->Skeleton->Bones.size());
}
```

### Integration Tests

**Test 4: End-to-End Import**
```cpp
void Test_EndToEndImport()
{
    // First load (cold cache)
    auto Start1 = GetTime();
    USkeletalMesh* Mesh1 = ResourceManager->Load<USkeletalMesh>("Data/Model/Character.fbx");
    auto Time1 = GetTime() - Start1;

    Assert(Mesh1 != nullptr);
    Assert(FileExists("Data/Model/Character.fbx.bin"));

    // Second load (warm cache)
    auto Start2 = GetTime();
    USkeletalMesh* Mesh2 = ResourceManager->Load<USkeletalMesh>("Data/Model/Character.fbx");
    auto Time2 = GetTime() - Start2;

    Assert(Mesh2 == Mesh1);  // Same pointer (memory cache)
    Assert(Time2 < Time1 / 5);  // At least 5× faster
}
```

**Test 5: Cache Invalidation**
```cpp
void Test_CacheInvalidation()
{
    // Load and cache
    USkeletalMesh* Mesh1 = ResourceManager->Load<USkeletalMesh>("test.fbx");

    // Modify source FBX
    Touch("test.fbx");

    // Clear memory cache to force disk cache check
    FFbxManager::Clear();

    // Reload - should regenerate cache
    USkeletalMesh* Mesh2 = ResourceManager->Load<USkeletalMesh>("test.fbx");

    Assert(Mesh2 != nullptr);
    // Verify cache file timestamp is newer than before
}
```

**Test 6: Texture Integration**
```cpp
void Test_TextureConversion()
{
    // FBX with PNG texture reference
    USkeletalMesh* Mesh = ResourceManager->Load<USkeletalMesh>("CharacterWithTexture.fbx");

    Assert(Mesh->Materials.size() > 0);

    FString DiffusePath = Mesh->Materials[0].DiffuseMap;

    // Should be DDS file
    Assert(DiffusePath.ends_with(".dds"));

    // DDS cache should exist
    Assert(FileExists(DiffusePath));
}
```

### Performance Tests

**Test 7: Load Time Benchmark**
```cpp
void Benchmark_LoadTime()
{
    // Measure cold load (no cache)
    DeleteCache("Character.fbx.bin");
    auto ColdTime = MeasureLoadTime("Character.fbx");

    // Measure warm load (with cache)
    auto WarmTime = MeasureLoadTime("Character.fbx");

    // Report
    printf("Cold load: %.2f ms\n", ColdTime);
    printf("Warm load: %.2f ms\n", WarmTime);
    printf("Speedup: %.1fx\n", ColdTime / WarmTime);

    // Assert targets
    Assert(ColdTime < 150.0f);  // Max 150ms for first load
    Assert(WarmTime < 20.0f);   // Max 20ms for cached load
    Assert(ColdTime / WarmTime > 6.0f);  // At least 6× speedup
}
```

### Stress Tests

**Test 8: Multiple Meshes**
```cpp
void Test_MultipleMeshes()
{
    TArray<FString> FbxFiles = {
        "Character1.fbx",
        "Character2.fbx",
        "Enemy1.fbx",
        "Prop1.fbx",
        "Prop2.fbx"
    };

    // Load all meshes
    TArray<USkeletalMesh*> Meshes;
    for (const FString& Path : FbxFiles)
    {
        USkeletalMesh* Mesh = ResourceManager->Load<USkeletalMesh>(Path);
        Assert(Mesh != nullptr);
        Meshes.push_back(Mesh);
    }

    // Verify all caches exist
    for (const FString& Path : FbxFiles)
    {
        Assert(FileExists(Path + ".bin"));
    }

    // Verify memory cache contains all
    for (const FString& Path : FbxFiles)
    {
        USkeletalMesh* Cached = ResourceManager->Load<USkeletalMesh>(Path);
        Assert(Cached != nullptr);
    }
}
```

---

## Performance Targets

### Load Time Targets

| Mesh Complexity | Cold Load (No Cache) | Warm Load (With Cache) | Target Speedup |
|----------------|---------------------|------------------------|----------------|
| Low-poly (< 2K verts) | < 40 ms | < 5 ms | 8-10× |
| Medium (2K-5K verts) | < 80 ms | < 10 ms | 8-10× |
| High-poly (5K-10K verts) | < 150 ms | < 20 ms | 7-8× |
| Very high (> 10K verts) | < 300 ms | < 40 ms | 7-8× |

### Cache File Size Targets

| Mesh Complexity | Vertex Count | FBX Size | Cache Size | Ratio |
|----------------|--------------|----------|------------|-------|
| Low-poly | 1,794 | 2 MB | ~150 KB | 13:1 |
| Medium | 5,000 | 5 MB | ~400 KB | 12:1 |
| High-poly | 10,000 | 10 MB | ~800 KB | 12:1 |

Cache should be **10-15% of FBX size**.

### Memory Usage Targets

| Component | Per-Mesh Overhead | Acceptable? |
|-----------|------------------|-------------|
| Static cache map | ~50 bytes | ✓ |
| Cached mesh data | Same as runtime | ✓ |
| Total overhead | < 5% | ✓ |

### Cache Hit Rate Targets

| Scenario | Target Hit Rate |
|----------|----------------|
| First game launch | 0% (expected) |
| Second launch | 100% |
| During gameplay | > 95% |
| After asset update | Regenerates (expected) |

---

## Risk Mitigation

### Risk 1: Cache Corruption

**Risk:** Binary cache file becomes corrupted (disk error, interrupted write)

**Mitigation:**
1. Magic number validation on load
2. Exception handling in serialization
3. Graceful fallback to FBX import
4. User can manually delete .bin files to force regeneration

**Code:**
```cpp
if (MagicNumber != 0x46425843)
{
    UE_LOG("[error] Corrupted cache file, regenerating: %s", CachePath.c_str());
    DeleteFile(CachePath);
    return false;  // Triggers regeneration
}
```

### Risk 2: Version Incompatibility

**Risk:** Future code changes break cache format

**Mitigation:**
1. Version number in header
2. Version-specific load functions
3. Old caches auto-regenerate if version mismatches

**Code:**
```cpp
if (Version > CURRENT_VERSION)
{
    UE_LOG("Cache version too new (%d > %d), regenerating", Version, CURRENT_VERSION);
    return false;
}

if (Version < MINIMUM_SUPPORTED_VERSION)
{
    UE_LOG("Cache version too old (%d < %d), regenerating", Version, MINIMUM_SUPPORTED_VERSION);
    return false;
}
```

### Risk 3: Disk Space Consumption

**Risk:** Binary caches consume significant disk space

**Mitigation:**
1. Caches are ~10-15% of FBX size (efficient)
2. User can delete all `.bin` files to reclaim space
3. Provide cache cleanup utility in editor

**Monitoring:**
```cpp
void FFbxManager::ReportCacheStats()
{
    int64 TotalCacheSize = 0;
    int32 CacheCount = 0;

    for (const auto& Entry : FbxSkeletalMeshCache)
    {
        FString CachePath = GetFbxCachePath(Entry.first);
        if (FileExists(CachePath))
        {
            TotalCacheSize += GetFileSize(CachePath);
            CacheCount++;
        }
    }

    UE_LOG("FBX Cache: %d files, %.2f MB total", CacheCount, TotalCacheSize / 1024.0f / 1024.0f);
}
```

### Risk 4: Timestamp Issues

**Risk:** Copied files have wrong timestamps, cache doesn't regenerate

**Mitigation:**
1. User can manually delete cache
2. Editor tool: "Force Re-import All FBX"
3. Build system clears caches on clean build

**Editor Tool:**
```cpp
#ifdef _EDITOR
void MainToolbar::OnForceReimportAllFBX()
{
    // Delete all .fbx.bin files
    TArray<FString> CacheFiles = FindFilesWithPattern("Data/Model/Fbx/**/*.fbx.bin");

    for (const FString& CacheFile : CacheFiles)
    {
        DeleteFile(CacheFile);
        UE_LOG("Deleted cache: %s", CacheFile.c_str());
    }

    // Clear memory cache
    FFbxManager::Clear();

    // Reload all FBX
    FFbxManager::Preload();

    MessageBox(nullptr, "All FBX files re-imported", "Complete", MB_OK);
}
#endif
```

### Risk 5: Texture Path Issues

**Risk:** Texture paths become invalid after moving files

**Mitigation:**
1. Store relative paths when possible
2. Multiple search paths for texture resolution
3. Graceful fallback to default texture if missing
4. Log warnings for missing textures

**Code:**
```cpp
UTexture* LoadTexture(const FString& Path)
{
    if (FileExists(Path))
    {
        return ResourceManager->Load<UTexture>(Path);
    }

    UE_LOG("[warning] Texture not found: %s, using default", Path.c_str());
    return ResourceManager->GetDefaultTexture();
}
```

---

## Implementation Timeline

### Week 1: Core Infrastructure
- **Days 1-2:** Create FFbxManager class, implement cache path logic
- **Days 3-4:** Implement timestamp validation
- **Day 5:** Unit tests for Phase 1

### Week 2: Serialization and Cache I/O
- **Days 1-2:** Add serialization operators to all data structures
- **Days 3-4:** Implement LoadFromCache() and SaveToCache()
- **Day 5:** Implement LoadFbxSkeletalMesh() main logic, integration tests

### Week 3: Texture Integration and Polish
- **Days 1-2:** Texture resolution and DDS conversion integration
- **Days 3-4:** ResourceManager integration, preloading
- **Day 5:** Performance benchmarks, stress tests, documentation

---

## Success Metrics

### Must-Have (Release Blockers)

✅ **Functional:**
- [ ] FBX files load correctly with caching
- [ ] Cache regenerates when source FBX is modified
- [ ] Textures convert to DDS automatically
- [ ] No crashes or data corruption

✅ **Performance:**
- [ ] 6-15× load time improvement
- [ ] Cache generation < 150ms
- [ ] Warm load < 20ms for typical mesh

✅ **Quality:**
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] No memory leaks

### Nice-to-Have (Future Enhancements)

🎯 **Features:**
- [ ] LOD support in cache format
- [ ] Vertex compression (packed normals, half-float UVs)
- [ ] Multi-threaded cache loading
- [ ] Cache statistics UI in editor

🎯 **Optimizations:**
- [ ] Vertex deduplication during import
- [ ] Index buffer compression (16-bit vs 32-bit)
- [ ] Bone influence culling (remove unused bones)

---

## Appendix: Code References

### Key Functions to Implement

| Function | Location | Purpose |
|----------|----------|---------|
| `FFbxManager::GetFbxCachePath()` | FbxManager.cpp | Generate cache file path |
| `FFbxManager::ShouldRegenerateCache()` | FbxManager.cpp | Validate cache timestamp |
| `FFbxManager::LoadFromCache()` | FbxManager.cpp | Deserialize from binary |
| `FFbxManager::SaveToCache()` | FbxManager.cpp | Serialize to binary |
| `FFbxManager::LoadFbxSkeletalMesh()` | FbxManager.cpp | Main entry point |
| `FFbxImporter::ProcessTextureReferences()` | FbxImporter.cpp | Convert textures to DDS |
| `operator<<(USkeletalMesh)` | SkeletalMesh.cpp | Mesh serialization |
| `operator>>(USkeletalMesh)` | SkeletalMesh.cpp | Mesh deserialization |

### Dependencies

| Component | Existing/New | Notes |
|-----------|-------------|-------|
| `FWindowsBinReader` | Existing | Binary deserialization |
| `FWindowsBinWriter` | Existing | Binary serialization |
| `FFbxImporter` | Existing | FBX SDK wrapper |
| `FTextureConverter` | Existing | DDS conversion |
| `UResourceManager` | Existing | Resource loading |
| `FFbxManager` | **NEW** | FBX cache manager |

---

## Document Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-11-11 | Initial implementation plan |

---

**Document End**
