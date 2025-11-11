# FBX Baking System Implementation Plan

**Document Version:** 3.0
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
5. **Supports both Static Mesh and Skeletal Mesh import** with automatic type detection
6. **Caches both mesh types separately** using dual-path caching strategy

### Success Criteria

| Metric | Current | Target |
|--------|---------|--------|
| Load Time (Skeletal Mesh Character.fbx) | 70-120 ms | 7-18 ms |
| Load Time (Static Mesh Prop.fbx) | 30-60 ms | 3-8 ms |
| Cache Generation Time | N/A | < 150 ms (one-time) |
| Cache File Size | 0 (no cache) | ~150 KB (skeletal), ~50 KB (static) |
| Memory Overhead | 0 | < 5% |
| Cache Hit Rate | 0% | > 95% after first load |

### Key Design Decisions

1. **Cache Storage Strategy:** Alongside source files (`.fbx` → `.fbx.bin`) matching OBJ system
2. **Validation Method:** Timestamp-based comparison (simple, robust)
3. **Serialization Format:** Binary with `FWindowsBinReader/Writer` (consistent with OBJ)
4. **Manager Pattern:** `FFbxManager` class similar to `FObjManager` with dual caching for both mesh types
5. **Type Detection:** Automatic FBX type detection via `FFbxImporter::DetectFbxType()` (StaticMesh vs SkeletalMesh)
6. **Dual Caching Strategy:** Separate cache maps and load functions for Static Mesh and Skeletal Mesh
7. **Texture Handling:** Automatic DDS conversion using existing `FTextureConverter`

---

## Architecture Overview

### System Components

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Application Layer                                                        │
│ - ResourceManager->Load<UStaticMesh>() for Static Mesh FBX              │
│ - ResourceManager->Load<USkeletalMesh>() for Skeletal Mesh FBX          │
└────────────────────────────────┬─────────────────────────────────────────┘
                                 │
                                 ▼
┌──────────────────────────────────────────────────────────────────────────┐
│ UResourceManager (Existing)                                              │
│ - In-memory cache (Tier 1) for both UStaticMesh and USkeletalMesh       │
│ - Type-safe resource loading                                             │
│ - Path normalization                                                     │
└────────────────────────────────┬─────────────────────────────────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 │                               │
                 ▼ Static Mesh                   ▼ Skeletal Mesh
┌────────────────────────────────┐   ┌────────────────────────────────────┐
│ UStaticMesh::Load() (Modified) │   │ USkeletalMesh::Load() (Modified)   │
│ - Delegates to FFbxManager     │   │ - Delegates to FFbxManager         │
│ - Creates GPU buffers          │   │ - Creates GPU buffers              │
└────────────────┬───────────────┘   └────────────────┬───────────────────┘
                 │                                    │
                 ▼                                    ▼
┌──────────────────────────────────────────────────────────────────────────┐
│ FFbxManager (NEW)                                                        │
│ - Dual static caches: FbxStaticMeshCache, FbxSkeletalMeshCache          │
│ - LoadFbxStaticMeshAsset() → FStaticMesh* (like FObjManager)            │
│ - LoadFbxSkeletalMeshAsset() → FSkeletalMesh* (like FObjManager)        │
│ - DetectFbxType() to route to correct load function                     │
│ - Cache path management, timestamp validation, serialization            │
└────────────────┬─────────────────────────────────┬───────────────────────┘
                 │                                 │
     ┌───────────┴────────┐           ┌────────────┴───────────┐
     ▼ Cache Valid        ▼ Invalid   ▼ Cache Valid           ▼ Invalid
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ Load .fbx.bin    │  │ Parse FBX SDK    │  │ Load .fbx.bin    │  │ Parse FBX SDK    │
│ - Deserialize    │  │ - ExtractStatic  │  │ - Deserialize    │  │ - ExtractSkeletal│
│   FStaticMesh    │  │   MeshData       │  │   FSkeletalMesh  │  │   MeshData       │
│ - Return         │  │ - Serialize      │  │ - Deserialize    │  │ - Extract        │
│   FStaticMesh*   │  │   to cache       │  │   skeleton       │  │   skeleton       │
└──────────────────┘  └──────────────────┘  │ - Return         │  │ - Serialize      │
                                            │   FSkeletalMesh* │  │   to cache       │
                                            └──────────────────┘  └──────────────────┘
```

### Three-Tier Caching Strategy

Following Mundi's established pattern (mirroring OBJ system architecture):

```
Tier 1: UResourceManager in-memory cache (fastest)
  ├─> TMap<FString, UStaticMesh*> (for Static Mesh FBX)
  └─> TMap<FString, USkeletalMesh*> (for Skeletal Mesh FBX)

Tier 2: FFbxManager static cache + Binary disk cache (fast)
  ├─> TMap<FString, FStaticMesh*> FbxStaticMeshCache (like FObjManager)
  ├─> TMap<FString, FSkeletalMesh*> FbxSkeletalMeshCache (like FObjManager)
  └─> .fbx.bin files (binary serialized data for both types)

Tier 3: FBX SDK parsing (slowest)
  ├─> FFbxImporter::ImportStaticMesh() (existing - with transform fixes)
  └─> FFbxImporter::ImportSkeletalMesh() (existing)
```

**Architectural Pattern Notes:**
- **Static Mesh FBX:** Follows `FObjManager` pattern exactly
  - `FFbxManager` owns `FStaticMesh*` (cached data structure)
  - `UStaticMesh` references `FStaticMesh*` (UObject resource wrapper)
  - `ResourceManager` manages `UStaticMesh*` lifetime
- **Skeletal Mesh FBX:** Follows `FObjManager` pattern exactly
  - `FFbxManager` owns `FSkeletalMesh*` (cached data structure)
  - `USkeletalMesh` references `FSkeletalMesh*` (UObject resource wrapper)
  - `ResourceManager` manages `USkeletalMesh*` lifetime

### Cache File Strategy

**Pattern:** Alongside source files (matching OBJ system)

```
Data/Model/Fbx/
├── Character.fbx          ← Source FBX file (Skeletal Mesh)
├── Character.fbx.bin      ← Binary cache (mesh + skeleton + materials)
├── Enemy.fbx              ← Source FBX file (Skeletal Mesh)
├── Enemy.fbx.bin          ← Binary cache
├── Prop.fbx               ← Source FBX file (Static Mesh)
└── Prop.fbx.bin           ← Binary cache (mesh + materials only, no skeleton)
```

**Cache File Contents (Skeletal Mesh):**
```cpp
Character.fbx.bin (Skeletal Mesh):
├── Magic Number (0x46425843 = "FBXC")
├── Version (uint32)
├── Type Flag (uint8: 0 = StaticMesh, 1 = SkeletalMesh)
├── Mesh Data
│   ├── Vertices (TArray<FSkinnedVertex>)
│   ├── Indices (TArray<uint32>)
│   └── Bounds (FVector Min/Max)
├── Skeleton Data (if SkeletalMesh)
│   ├── Bones (TArray<FBoneInfo>)
│   └── InverseBindPoseMatrices (TArray<FMatrix>)
├── Material Data
│   ├── Materials (TArray<FMaterialInfo>)
│   └── Material Groups (TArray<FStaticMeshGroup>)
└── Texture References (TArray<FString> - paths to DDS files)
```

**Cache File Contents (Static Mesh):**
```cpp
Prop.fbx.bin (Static Mesh):
├── Magic Number (0x46425843 = "FBXC")
├── Version (uint32)
├── Type Flag (uint8: 0 = StaticMesh)
├── Mesh Data
│   ├── Vertices (TArray<FNormalVertex>)  ← Different vertex type
│   ├── Indices (TArray<uint32>)
│   └── Bounds (FVector Min/Max)
├── Material Data
│   ├── Materials (TArray<FMaterialInfo>)
│   └── Material Groups (TArray<FStaticMeshGroup>)
└── Texture References (TArray<FString> - paths to DDS files)
```

**Note on Cache Format:**
- Cache files include a type flag to distinguish Static vs Skeletal Mesh
- Static Mesh FBX cache mirrors OBJ binary format (`FStaticMesh` structure)
- Skeletal Mesh FBX cache includes skeleton data
- Both share material and texture reference serialization patterns

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
#include "FbxImportOptions.h"  // For EFbxImportType

// Forward declarations
class USkeletalMesh;
class UStaticMesh;
struct FStaticMesh;
struct FSkeletalMesh;

/**
 * Manages FBX mesh loading and caching (both Static and Skeletal)
 * Similar to FObjManager for OBJ files
 *
 * Architecture:
 * - Static Mesh: Caches FStaticMesh* (data structure), UStaticMesh references it
 * - Skeletal Mesh: Caches FSkeletalMesh* (data structure), USkeletalMesh references it
 */
class FFbxManager
{
private:
    // ═══════════════════════════════════════════════════════════
    // Static Mesh Cache (FStaticMesh* - mirrors FObjManager)
    // ═══════════════════════════════════════════════════════════
    static TMap<FString, FStaticMesh*> FbxStaticMeshCache;

    // ═══════════════════════════════════════════════════════════
    // Skeletal Mesh Cache (FSkeletalMesh* - mirrors FObjManager)
    // ═══════════════════════════════════════════════════════════
    static TMap<FString, FSkeletalMesh*> FbxSkeletalMeshCache;

public:
    /**
     * Preload all FBX files from Data/Model/Fbx/ directory
     * Called during engine initialization
     * Automatically detects type and loads to appropriate cache
     */
    static void Preload();

    /**
     * Clear all cached FBX data (both Static and Skeletal)
     * Called during engine shutdown
     */
    static void Clear();

    // ═══════════════════════════════════════════════════════════
    // Static Mesh Loading (follows FObjManager pattern)
    // ═══════════════════════════════════════════════════════════

    /**
     * Load static mesh from FBX file with caching
     * @param PathFileName - Path to .fbx file
     * @return Loaded static mesh data or nullptr on failure
     *
     * Pattern: Identical to FObjManager::LoadObjStaticMeshAsset()
     */
    static FStaticMesh* LoadFbxStaticMeshAsset(const FString& PathFileName);

    // ═══════════════════════════════════════════════════════════
    // Skeletal Mesh Loading (follows FObjManager pattern)
    // ═══════════════════════════════════════════════════════════

    /**
     * Load skeletal mesh from FBX file with caching
     * @param PathFileName - Path to .fbx file
     * @return Loaded skeletal mesh data or nullptr on failure
     *
     * Pattern: Identical to FObjManager::LoadObjStaticMeshAsset()
     */
    static FSkeletalMesh* LoadFbxSkeletalMeshAsset(const FString& PathFileName);

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

    // ═══════════════════════════════════════════════════════════
    // Static Mesh Cache I/O
    // ═══════════════════════════════════════════════════════════

    /**
     * Load static mesh from binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param OutMesh - Output static mesh data
     * @return True if successfully loaded
     */
    static bool LoadStaticMeshFromCache(const FString& CachePath, FStaticMesh* OutMesh);

    /**
     * Save static mesh to binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param Mesh - Static mesh data to save
     * @return True if successfully saved
     */
    static bool SaveStaticMeshToCache(const FString& CachePath, const FStaticMesh* Mesh);

    // ═══════════════════════════════════════════════════════════
    // Skeletal Mesh Cache I/O
    // ═══════════════════════════════════════════════════════════

    /**
     * Load skeletal mesh from binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param OutMesh - Output skeletal mesh data
     * @return True if successfully loaded
     */
    static bool LoadSkeletalMeshFromCache(const FString& CachePath, FSkeletalMesh* OutMesh);

    /**
     * Save skeletal mesh to binary cache
     * @param CachePath - Path to .fbx.bin file
     * @param Mesh - Skeletal mesh data to save
     * @return True if successfully saved
     */
    static bool SaveSkeletalMeshToCache(const FString& CachePath, const FSkeletalMesh* Mesh);
};
```

**1.2. Implement Cache Path and Validation Logic**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
#include "pch.h"
#include "FbxManager.h"
#include "StaticMesh.h"
#include "SkeletalMesh.h"
#include "FbxImporter.h"
#include "GlobalConsole.h"
#include <filesystem>

// Static member initialization
TMap<FString, FStaticMesh*> FFbxManager::FbxStaticMeshCache;
TMap<FString, FSkeletalMesh*> FFbxManager::FbxSkeletalMeshCache;

FString FFbxManager::GetFbxCachePath(const FString& FbxPath)
{
    // Simple pattern: append ".bin" to FBX filename (same as OBJ system)
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
    // Clear Static Mesh cache
    // Note: FStaticMesh objects are owned by FFbxManager (like FObjManager)
    for (auto& Pair : FbxStaticMeshCache)
    {
        delete Pair.second;  // Delete FStaticMesh*
    }
    FbxStaticMeshCache.clear();

    // Clear Skeletal Mesh cache
    // Note: FSkeletalMesh objects are owned by FFbxManager (like FObjManager)
    for (auto& Pair : FbxSkeletalMeshCache)
    {
        delete Pair.second;  // Delete FSkeletalMesh*
    }
    FbxSkeletalMeshCache.clear();

    UE_LOG("FFbxManager: Cleared caches (Static: %d, Skeletal: %d entries)",
        FbxStaticMeshCache.size(), FbxSkeletalMeshCache.size());
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

    int32 StaticMeshCount = 0;
    int32 SkeletalMeshCount = 0;

    for (const auto& Entry : fs::recursive_directory_iterator(FbxDirectory))
    {
        if (Entry.is_regular_file() && Entry.path().extension() == ".fbx")
        {
            FString FbxPath = Entry.path().string();

            // Detect FBX type (Static vs Skeletal)
            FFbxImporter Importer;
            EFbxImportType FbxType = Importer.DetectFbxType(FbxPath);

            if (FbxType == EFbxImportType::StaticMesh)
            {
                // Load as Static Mesh
                FStaticMesh* Mesh = LoadFbxStaticMeshAsset(FbxPath);
                if (Mesh)
                {
                    StaticMeshCount++;
                }
            }
            else if (FbxType == EFbxImportType::SkeletalMesh)
            {
                // Load as Skeletal Mesh
                FSkeletalMesh* Mesh = LoadFbxSkeletalMeshAsset(FbxPath);
                if (Mesh)
                {
                    SkeletalMeshCount++;
                }
            }
            else
            {
                UE_LOG("[warning] Unsupported FBX type: %s (Animation not yet supported)", FbxPath.c_str());
            }
        }
    }

    UE_LOG("FFbxManager: Preloaded %d Static Meshes, %d Skeletal Meshes",
        StaticMeshCount, SkeletalMeshCount);
}
```

**1.3. Testing Checklist**

- [ ] `GetFbxCachePath()` correctly appends `.bin` extension
- [ ] `ShouldRegenerateCache()` returns true for missing cache
- [ ] `ShouldRegenerateCache()` returns false for valid cache
- [ ] `ShouldRegenerateCache()` returns true when FBX is newer
- [ ] `Clear()` empties both static cache maps (Static + Skeletal)
- [ ] `Clear()` correctly deletes FStaticMesh* objects
- [ ] `Preload()` finds all .fbx files in directory
- [ ] `Preload()` correctly detects Static Mesh FBX via `DetectFbxType()`
- [ ] `Preload()` correctly detects Skeletal Mesh FBX via `DetectFbxType()`
- [ ] `Preload()` loads both mesh types to correct caches

---

### Phase 2: Serialization Support (Week 1-2)

**Goal:** Implement binary serialization for all FBX-related data structures

**Note on Static Mesh Serialization:**
- `FStaticMesh` serialization already exists (from OBJ system) → **No additional work needed**
- Static Mesh FBX will reuse existing `FStaticMesh` serialization operators
- Only Skeletal Mesh structures require new serialization implementation

#### Tasks

**Note on Serialization Scope:**
- **FStaticMesh serialization:** Already exists (from OBJ system) → No work needed for Static Mesh FBX
- **FSkeletalMesh serialization:** Needs implementation → This Phase 2 focuses on this

**2.1. Add Serialization to FSkinnedVertex**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h`

**Note:** This structure is used by USkeletalMesh (UObject wrapper). FSkeletalMesh uses the same vertex type.

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

**Note:** FBoneInfo is stored directly in FSkeletalMesh structure (no separate USkeleton needed for serialization).

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

**2.3. Add Serialization to FSkeletalMesh**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h` (or separate `FSkeletalMesh.h`)

**Note:** FSkeletalMesh is the data structure (not UObject) that gets serialized to cache.

```cpp
struct FSkeletalMesh
{
    TArray<FSkinnedVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FBoneInfo> Bones;  // Skeleton data inline
    TMap<FString, int32> BoneNameToIndexMap;
    TArray<FMaterialInfo> Materials;
    TArray<FStaticMeshGroup> MaterialGroups;
    FVector BoundsMin;
    FVector BoundsMax;
    FString PathFileName;
    FString CacheFilePath;
    bool bHasMaterial = false;

    // Binary serialization operators
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkeletalMesh& Mesh);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkeletalMesh& Mesh);
};
```

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp` (or separate `FSkeletalMesh.cpp`)

```cpp
FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkeletalMesh& Mesh)
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

    // Write skeleton data (bones)
    uint32 BoneCount = Mesh.Bones.size();
    Writer.Write(&BoneCount, sizeof(uint32));
    for (const FBoneInfo& Bone : Mesh.Bones)
    {
        Writer << Bone;  // Uses FBoneInfo's operator<<
    }
    // Note: BoneNameToIndexMap is NOT serialized (can be rebuilt on load)

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

    // Read skeleton data (bones)
    uint32 BoneCount;
    Reader.Read(&BoneCount, sizeof(uint32));
    Mesh.Bones.clear();
    Mesh.Bones.reserve(BoneCount);
    for (uint32 i = 0; i < BoneCount; ++i)
    {
        FBoneInfo Bone;
        Reader >> Bone;
        Mesh.Bones.push_back(Bone);
    }

    // Rebuild bone name map
    Mesh.BoneNameToIndexMap.clear();
    for (size_t i = 0; i < Mesh.Bones.size(); ++i)
    {
        Mesh.BoneNameToIndexMap[Mesh.Bones[i].Name] = static_cast<int32>(i);
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

**2.4. Testing Checklist**

- [ ] Serialize and deserialize FSkinnedVertex (verify data integrity)
- [ ] Serialize and deserialize FBoneInfo (verify strings and matrices)
- [ ] Serialize and deserialize FSkeletalMesh (verify all components: vertices, bones, materials)
- [ ] Test with empty bone array (BoneCount = 0)
- [ ] Test with missing materials
- [ ] Verify magic number validation rejects invalid files
- [ ] Verify BoneNameToIndexMap is correctly rebuilt on deserialization
- [ ] Test round-trip: serialize → deserialize → compare data integrity

---

### Phase 3: Cache Loading/Saving (Week 2)

**Goal:** Implement complete cache load/save pipeline for both Static and Skeletal meshes

#### Tasks

**3.1. Implement Static Mesh Cache I/O (reuses OBJ system)**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
bool FFbxManager::LoadStaticMeshFromCache(const FString& CachePath, FStaticMesh* OutMesh)
{
    namespace fs = std::filesystem;

    if (!fs::exists(CachePath))
    {
        return false;
    }

    try
    {
        // Use existing FStaticMesh serialization operators (from OBJ system)
        FWindowsBinReader Reader(CachePath);
        Reader >> *OutMesh;  // Deserialize using existing operator>>

        UE_LOG("Loaded Static Mesh FBX from cache: %s (%d vertices, %d indices)",
            CachePath.c_str(),
            OutMesh->Vertices.size(),
            OutMesh->Indices.size());

        return true;
    }
    catch (const std::exception& e)
    {
        UE_LOG("[error] Failed to load Static Mesh FBX cache: %s - %s", CachePath.c_str(), e.what());
        return false;
    }
}

bool FFbxManager::SaveStaticMeshToCache(const FString& CachePath, const FStaticMesh* Mesh)
{
    try
    {
        // Use existing FStaticMesh serialization operators (from OBJ system)
        FWindowsBinWriter Writer(CachePath);
        Writer << *Mesh;  // Serialize using existing operator<<

        UE_LOG("Saved Static Mesh FBX cache: %s (%d vertices, %d indices)",
            CachePath.c_str(),
            Mesh->Vertices.size(),
            Mesh->Indices.size());

        return true;
    }
    catch (const std::exception& e)
    {
        UE_LOG("[error] Failed to save Static Mesh FBX cache: %s - %s", CachePath.c_str(), e.what());
        return false;
    }
}
```

**3.2. Implement Skeletal Mesh Cache I/O**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
bool FFbxManager::LoadSkeletalMeshFromCache(const FString& CachePath, FSkeletalMesh* OutMesh)
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

bool FFbxManager::SaveSkeletalMeshToCache(const FString& CachePath, const FSkeletalMesh* Mesh)
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

**3.3. Implement LoadFbxStaticMeshAsset() Main Logic**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
FStaticMesh* FFbxManager::LoadFbxStaticMeshAsset(const FString& PathFileName)
{
    // ──────────────────────────────────────────────────────
    // 1. Check static memory cache
    // ──────────────────────────────────────────────────────
    auto Iter = FbxStaticMeshCache.find(PathFileName);
    if (Iter != FbxStaticMeshCache.end())
    {
        UE_LOG("FBX Static Mesh already in memory cache: %s", PathFileName.c_str());
        return Iter->second;
    }

    // ──────────────────────────────────────────────────────
    // 2. Get cache path and validate
    // ──────────────────────────────────────────────────────
    FString CachePath = GetFbxCachePath(PathFileName);
    bool bShouldRegenerate = ShouldRegenerateCache(PathFileName, CachePath);

    FStaticMesh* NewMesh = new FStaticMesh();  // FFbxManager owns this (like FObjManager)

    if (!bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 3. Load from binary cache (FAST PATH)
        // ──────────────────────────────────────────────────────
        if (LoadStaticMeshFromCache(CachePath, NewMesh))
        {
            // Cache loaded successfully
            FbxStaticMeshCache[PathFileName] = NewMesh;
            return NewMesh;
        }
        else
        {
            // Cache load failed, fall through to regenerate
            UE_LOG("[warning] Static Mesh cache load failed, regenerating: %s", CachePath.c_str());
            bShouldRegenerate = true;
        }
    }

    if (bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 4. Parse FBX with SDK (SLOW PATH)
        // ──────────────────────────────────────────────────────
        FFbxImporter Importer;
        FFbxImportOptions Options;
        Options.bConvertScene = true;
        Options.bConvertSceneUnit = true;
        Options.bRemoveDegenerates = true;
        Options.ImportScale = 1.0f;

        if (!Importer.ImportStaticMesh(PathFileName, Options, *NewMesh))
        {
            UE_LOG("[error] Failed to import Static Mesh FBX: %s", PathFileName.c_str());
            delete NewMesh;
            return nullptr;
        }

        UE_LOG("Imported Static Mesh FBX: %s (%d vertices, %d indices)",
            PathFileName.c_str(),
            NewMesh->Vertices.size(),
            NewMesh->Indices.size());

        // ──────────────────────────────────────────────────────
        // 5. Serialize to binary cache
        // ──────────────────────────────────────────────────────
        if (!SaveStaticMeshToCache(CachePath, NewMesh))
        {
            UE_LOG("[warning] Failed to save Static Mesh FBX cache (will still use loaded data): %s", CachePath.c_str());
        }
    }

    // ──────────────────────────────────────────────────────
    // 6. Store in static memory cache
    // ──────────────────────────────────────────────────────
    FbxStaticMeshCache[PathFileName] = NewMesh;

    return NewMesh;
}
```

**3.4. Implement LoadFbxSkeletalMeshAsset() Main Logic**

Location: `Mundi/Source/Editor/FbxManager.cpp`

```cpp
FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
    // ──────────────────────────────────────────────────────
    // 1. Check static memory cache
    // ──────────────────────────────────────────────────────
    auto Iter = FbxSkeletalMeshCache.find(PathFileName);
    if (Iter != FbxSkeletalMeshCache.end())
    {
        UE_LOG("FBX Skeletal Mesh already in memory cache: %s", PathFileName.c_str());
        return Iter->second;
    }

    // ──────────────────────────────────────────────────────
    // 2. Get cache path and validate
    // ──────────────────────────────────────────────────────
    FString CachePath = GetFbxCachePath(PathFileName);
    bool bShouldRegenerate = ShouldRegenerateCache(PathFileName, CachePath);

    FSkeletalMesh* NewMesh = new FSkeletalMesh();  // FFbxManager owns this (like FObjManager)

    if (!bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 3. Load from binary cache (FAST PATH)
        // ──────────────────────────────────────────────────────
        if (LoadSkeletalMeshFromCache(CachePath, NewMesh))
        {
            // Cache loaded successfully
            FbxSkeletalMeshCache[PathFileName] = NewMesh;
            return NewMesh;
        }
        else
        {
            // Cache load failed, fall through to regenerate
            UE_LOG("[warning] Skeletal Mesh cache load failed, regenerating: %s", CachePath.c_str());
            bShouldRegenerate = true;
        }
    }

    if (bShouldRegenerate)
    {
        // ──────────────────────────────────────────────────────
        // 4. Parse FBX with SDK (SLOW PATH)
        // ──────────────────────────────────────────────────────
        FFbxImporter Importer;
        FFbxImportOptions Options;
        Options.bConvertScene = true;
        Options.bConvertSceneUnit = true;
        Options.bRemoveDegenerates = true;
        Options.bImportSkeleton = true;
        Options.ImportScale = 1.0f;

        if (!Importer.ImportSkeletalMesh(PathFileName, Options, *NewMesh))
        {
            UE_LOG("[error] Failed to import Skeletal Mesh FBX: %s", PathFileName.c_str());
            delete NewMesh;
            return nullptr;
        }

        UE_LOG("Imported Skeletal Mesh FBX: %s (%d vertices, %d bones)",
            PathFileName.c_str(),
            NewMesh->Vertices.size(),
            NewMesh->Bones.size());

        // ──────────────────────────────────────────────────────
        // 5. Serialize to binary cache
        // ──────────────────────────────────────────────────────
        if (!SaveSkeletalMeshToCache(CachePath, NewMesh))
        {
            UE_LOG("[warning] Failed to save Skeletal Mesh FBX cache (will still use loaded data): %s", CachePath.c_str());
        }
    }

    // ──────────────────────────────────────────────────────
    // 6. Store in static memory cache
    // ──────────────────────────────────────────────────────
    FbxSkeletalMeshCache[PathFileName] = NewMesh;

    return NewMesh;
}
```

**3.5. Testing Checklist**

**Static Mesh FBX:**
- [ ] First load triggers FBX import and creates cache
- [ ] Second load uses cache (verify performance improvement)
- [ ] Cache regenerates when Static Mesh FBX is modified
- [ ] Graceful fallback if cache is corrupted
- [ ] Memory cache prevents redundant disk reads
- [ ] Multiple Static Mesh FBX files can be loaded simultaneously
- [ ] Static Mesh FBX cache format matches OBJ binary format

**Skeletal Mesh FBX:**
- [ ] First load triggers FBX import and creates cache (FSkeletalMesh*)
- [ ] Second load uses cache (verify performance improvement)
- [ ] Cache regenerates when Skeletal Mesh FBX is modified
- [ ] Graceful fallback if cache is corrupted
- [ ] Memory cache prevents redundant disk reads
- [ ] Multiple Skeletal Mesh FBX files can be loaded simultaneously
- [ ] Skeleton data (bones) is correctly serialized and deserialized
- [ ] BoneNameToIndexMap is correctly rebuilt on cache load
- [ ] FFbxManager correctly owns FSkeletalMesh* (deleted on Clear())

**Integration:**
- [ ] Both mesh types can coexist in caches
- [ ] Type detection correctly routes to appropriate cache

---

### Phase 4: ResourceManager Integration (Week 2-3)

**Goal:** Integrate FFbxManager with existing ResourceManager pipeline for both mesh types

#### Tasks

**4.1. Modify UStaticMesh::Load() for FBX Static Mesh**

Location: `Mundi/Source/Runtime/AssetManagement/StaticMesh.cpp`

```cpp
void UStaticMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    // Check file extension
    std::filesystem::path FilePath(InFilePath);
    FString Extension = FilePath.extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

    if (Extension == ".fbx")
    {
        // ═══════════════════════════════════════════════════════════
        // FBX Static Mesh: Delegate to FFbxManager (like OBJ pattern)
        // ═══════════════════════════════════════════════════════════
        StaticMeshAsset = FFbxManager::LoadFbxStaticMeshAsset(InFilePath);
        bOwnsStaticMeshAsset = false;  // FFbxManager owns it (no longer temporary flag!)

        // TODO Comment can be removed after FbxManager is implemented
        // Ownership: FFbxManager owns FStaticMesh* (same as FObjManager)
    }
    else if (Extension == ".obj")
    {
        // ═══════════════════════════════════════════════════════════
        // OBJ Static Mesh: Existing pattern
        // ═══════════════════════════════════════════════════════════
        StaticMeshAsset = FObjManager::LoadObjStaticMeshAsset(InFilePath);
        bOwnsStaticMeshAsset = false;  // FObjManager owns it
    }
    else
    {
        UE_LOG("[StaticMesh ERROR] Unsupported file format: %s", Extension.c_str());
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Create GPU buffers from loaded asset
    // ═══════════════════════════════════════════════════════════
    if (StaticMeshAsset && 0 < StaticMeshAsset->Vertices.size() && 0 < StaticMeshAsset->Indices.size())
    {
        CacheFilePath = StaticMeshAsset->CacheFilePath;
        CreateVertexBuffer(StaticMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(StaticMeshAsset, InDevice);
        CreateLocalBound(StaticMeshAsset);
        VertexCount = static_cast<uint32>(StaticMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(StaticMeshAsset->Indices.size());
    }
}
```

**NOTE:** With this implementation, the temporary `bOwnsStaticMeshAsset` flag and TODOs in StaticMesh.cpp can be removed. FFbxManager now manages FStaticMesh* ownership like FObjManager.

**4.2. Modify USkeletalMesh::Load() for FBX Skeletal Mesh**

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h`

First, add a reference pointer to FSkeletalMesh in USkeletalMesh:

```cpp
class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    // ... existing GPU buffer members ...

    // ═══════════════════════════════════════════════════════════
    // NEW: Reference to parsed data (owned by FFbxManager)
    // ═══════════════════════════════════════════════════════════
    FSkeletalMesh* SkeletalMeshAsset = nullptr;  // Does NOT own (FFbxManager owns this)

    // NOTE: Ownership flag not needed (unlike temporary StaticMesh implementation)
    // FFbxManager always owns FSkeletalMesh*, USkeletalMesh never owns it

    // ... rest of class ...
};
```

Location: `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp`

```cpp
void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    // ═══════════════════════════════════════════════════════════
    // FBX Skeletal Mesh: Delegate to FFbxManager (like OBJ pattern)
    // ═══════════════════════════════════════════════════════════
    SkeletalMeshAsset = FFbxManager::LoadFbxSkeletalMeshAsset(InFilePath);

    if (!SkeletalMeshAsset)
    {
        UE_LOG("[error] Failed to load skeletal mesh: %s", InFilePath.c_str());
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // Create GPU buffers from loaded asset
    // ═══════════════════════════════════════════════════════════
    if (SkeletalMeshAsset &&
        SkeletalMeshAsset->Vertices.size() > 0 &&
        SkeletalMeshAsset->Indices.size() > 0)
    {
        // Create vertex and index buffers
        CreateVertexBuffer(SkeletalMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshAsset, InDevice);
        CreateLocalBound(SkeletalMeshAsset);

        VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());

        UE_LOG("Loaded skeletal mesh: %s (%d vertices, %d bones)",
            InFilePath.c_str(),
            VertexCount,
            SkeletalMeshAsset->Bones.size());
    }
}
```

**4.3. Ensure ResourceManager Type Registration**

Location: `Mundi/Source/Runtime/AssetManagement/ResourceManager.h`

Verify that both `UStaticMesh` and `USkeletalMesh` are registered in `GetResourceType<T>()`:

```cpp
template<typename T>
ResourceType UResourceManager::GetResourceType()
{
    if (T::StaticClass() == UStaticMesh::StaticClass())
        return ResourceType::StaticMesh;  // Used for both OBJ and FBX Static Mesh
    if (T::StaticClass() == USkeletalMesh::StaticClass())  // Ensure this exists
        return ResourceType::SkeletalMesh;  // Used for FBX Skeletal Mesh
    if (T::StaticClass() == UTexture::StaticClass())
        return ResourceType::Texture;
    // ... other types

    return ResourceType::None;
}
```

**4.4. Add ResourceType Enum Value (If Missing)**

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

**4.5. Testing Checklist**

**Static Mesh FBX:**
- [ ] `ResourceManager->Load<UStaticMesh>("path.fbx")` works correctly for Static Mesh FBX
- [ ] Static Mesh FBX no longer requires `bOwnsStaticMeshAsset` flag (cleaned up)
- [ ] Static Mesh FBX follows same pattern as OBJ (FFbxManager owns FStaticMesh*)
- [ ] ResourceManager in-memory cache prevents redundant loads
- [ ] Multiple Static Mesh FBX files can coexist with OBJ files
- [ ] GPU buffers are created correctly

**Skeletal Mesh FBX:**
- [ ] `ResourceManager->Load<USkeletalMesh>("path.fbx")` works correctly
- [ ] USkeletalMesh correctly references FSkeletalMesh* (does not own it)
- [ ] ResourceManager in-memory cache prevents redundant loads
- [ ] Multiple Skeletal Mesh FBX files can coexist
- [ ] GPU buffers are created correctly from FSkeletalMesh data
- [ ] Bone data is accessible via USkeletalMesh->SkeletalMeshAsset->Bones
- [ ] No ownership flags needed (FFbxManager always owns FSkeletalMesh*)

**Integration:**
- [ ] Both mesh types work through ResourceManager
- [ ] Material references are preserved for both types
- [ ] Cache system doesn't interfere with runtime behavior

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

### FSkeletalMesh Data Structure

The `FSkeletalMesh` structure is a pure data container for parsed skeletal mesh information, following the same pattern as `FStaticMesh` for OBJ files.

**Location:** `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.h` (or separate `FSkeletalMesh.h`)

```cpp
/**
 * Skeletal Mesh data structure (pure data, no UObject overhead)
 * Owned by FFbxManager, referenced by USkeletalMesh
 * Similar to FStaticMesh for OBJ files
 */
struct FSkeletalMesh
{
    // ═══════════════════════════════════════════════════════════
    // Rendering Data
    // ═══════════════════════════════════════════════════════════

    /** Skinned vertices with bone weights */
    TArray<FSkinnedVertex> Vertices;

    /** Triangle indices */
    TArray<uint32> Indices;

    // ═══════════════════════════════════════════════════════════
    // Skeleton Data
    // ═══════════════════════════════════════════════════════════

    /** Bone hierarchy information */
    TArray<FBoneInfo> Bones;

    /** Bone name to index lookup map (can be rebuilt on load) */
    TMap<FString, int32> BoneNameToIndexMap;

    // ═══════════════════════════════════════════════════════════
    // Material Data
    // ═══════════════════════════════════════════════════════════

    /** Material definitions */
    TArray<FMaterialInfo> Materials;

    /** Material group assignments (which triangles use which material) */
    TArray<FStaticMeshGroup> MaterialGroups;

    // ═══════════════════════════════════════════════════════════
    // Bounds
    // ═══════════════════════════════════════════════════════════

    /** Bounding box minimum */
    FVector BoundsMin;

    /** Bounding box maximum */
    FVector BoundsMax;

    // ═══════════════════════════════════════════════════════════
    // Metadata
    // ═══════════════════════════════════════════════════════════

    /** Source FBX file path */
    FString PathFileName;

    /** Binary cache file path */
    FString CacheFilePath;

    /** Material flags */
    bool bHasMaterial = false;

    // ═══════════════════════════════════════════════════════════
    // Serialization
    // ═══════════════════════════════════════════════════════════

    /**
     * Binary serialization operators (for cache I/O)
     */
    friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkeletalMesh& Mesh);
    friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkeletalMesh& Mesh);
};
```

**Design Notes:**

1. **No UObject Inheritance:** `FSkeletalMesh` is a plain struct (POD-style), not a UObject
   - Faster construction/destruction
   - No reflection overhead
   - Simpler memory management (owned by FFbxManager)

2. **Mirrors FStaticMesh Pattern:** Same architecture as OBJ system
   - `FStaticMesh` for OBJ files → parsed mesh data
   - `FSkeletalMesh` for FBX skeletal meshes → parsed mesh + skeleton data
   - Both owned by their respective managers (FObjManager, FFbxManager)

3. **Skeleton Data Storage:**
   - `Bones` array stores full bone hierarchy
   - `BoneNameToIndexMap` can be rebuilt on deserialization (no need to serialize it)
   - Bone transforms stored in `FBoneInfo` structure

4. **Relationship with USkeletalMesh:**
   - `FSkeletalMesh` = parsed data (CPU-side, owned by FFbxManager)
   - `USkeletalMesh` = resource wrapper (UObject, creates GPU buffers, owned by ResourceManager)
   - `USkeletalMesh` references `FSkeletalMesh*` via pointer (does NOT own it)

**Memory Ownership:**
```
FFbxManager
  └─> TMap<FString, FSkeletalMesh*> (OWNS the parsed data)
         ↑
         │ references (does NOT own)
         │
USkeletalMesh (UObject wrapper)
  └─> FSkeletalMesh* SkeletalMeshAsset (pointer to FFbxManager's data)
  └─> ID3D11Buffer* VertexBuffer (owns GPU buffer)
  └─> ID3D11Buffer* IndexBuffer (owns GPU buffer)
```

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

**Test 3: Static Mesh Serialization Round-Trip**
```cpp
void Test_StaticMeshSerializationRoundTrip()
{
    // Create test Static Mesh
    FStaticMesh* OriginalMesh = CreateTestStaticMesh();

    // Serialize (uses existing OBJ system operators)
    FWindowsBinWriter Writer("test_static.bin");
    Writer << *OriginalMesh;

    // Deserialize
    FStaticMesh* LoadedMesh = new FStaticMesh();
    FWindowsBinReader Reader("test_static.bin");
    Reader >> *LoadedMesh;

    // Verify
    AssertEqual(OriginalMesh->Vertices.size(), LoadedMesh->Vertices.size());
    AssertEqual(OriginalMesh->Indices.size(), LoadedMesh->Indices.size());
    AssertEqual(OriginalMesh->Groups.size(), LoadedMesh->Groups.size());

    delete OriginalMesh;
    delete LoadedMesh;
}
```

**Test 4: Skeletal Mesh Serialization Round-Trip**
```cpp
void Test_SkeletalMeshSerializationRoundTrip()
{
    // Create test Skeletal Mesh (FSkeletalMesh, not USkeletalMesh)
    FSkeletalMesh* OriginalMesh = CreateTestFSkeletalMesh();

    // Serialize
    FWindowsBinWriter Writer("test_skeletal.bin");
    Writer << *OriginalMesh;

    // Deserialize
    FSkeletalMesh* LoadedMesh = new FSkeletalMesh();
    FWindowsBinReader Reader("test_skeletal.bin");
    Reader >> *LoadedMesh;

    // Verify
    AssertEqual(OriginalMesh->Vertices.size(), LoadedMesh->Vertices.size());
    AssertEqual(OriginalMesh->Indices.size(), LoadedMesh->Indices.size());
    AssertEqual(OriginalMesh->Bones.size(), LoadedMesh->Bones.size());  // Bones directly in FSkeletalMesh
    AssertEqual(OriginalMesh->Materials.size(), LoadedMesh->Materials.size());

    // Verify BoneNameToIndexMap was rebuilt
    Assert(LoadedMesh->BoneNameToIndexMap.size() == LoadedMesh->Bones.size());

    delete OriginalMesh;
    delete LoadedMesh;
}
```

### Integration Tests

**Test 5: End-to-End Static Mesh FBX Import**
```cpp
void Test_EndToEndStaticMeshImport()
{
    // First load (cold cache)
    auto Start1 = GetTime();
    UStaticMesh* Mesh1 = ResourceManager->Load<UStaticMesh>("Data/Model/Prop.fbx");
    auto Time1 = GetTime() - Start1;

    Assert(Mesh1 != nullptr);
    Assert(FileExists("Data/Model/Prop.fbx.bin"));

    // Second load (warm cache)
    auto Start2 = GetTime();
    UStaticMesh* Mesh2 = ResourceManager->Load<UStaticMesh>("Data/Model/Prop.fbx");
    auto Time2 = GetTime() - Start2;

    Assert(Mesh2 == Mesh1);  // Same pointer (memory cache)
    Assert(Time2 < Time1 / 5);  // At least 5× faster
}
```

**Test 6: End-to-End Skeletal Mesh FBX Import**
```cpp
void Test_EndToEndSkeletalMeshImport()
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

**Test 7: Cache Invalidation**
```cpp
void Test_CacheInvalidation()
{
    // Load and cache (creates FSkeletalMesh* in FFbxManager, USkeletalMesh* in ResourceManager)
    USkeletalMesh* Mesh1 = ResourceManager->Load<USkeletalMesh>("test.fbx");

    // Modify source FBX
    Touch("test.fbx");

    // Clear memory cache to force disk cache check
    FFbxManager::Clear();        // Clears FSkeletalMesh* cache (deletes FSkeletalMesh objects)
    ResourceManager->Clear();    // Clears USkeletalMesh* cache

    // Reload - should regenerate cache
    USkeletalMesh* Mesh2 = ResourceManager->Load<USkeletalMesh>("test.fbx");

    Assert(Mesh2 != nullptr);
    // Verify cache file timestamp is newer than before
    // Verify new FSkeletalMesh* was created in FFbxManager
}
```

**Test 8: Texture Integration**
```cpp
void Test_TextureConversion()
{
    // FBX with PNG texture reference (both Static and Skeletal work the same way)
    USkeletalMesh* SkeletalMesh = ResourceManager->Load<USkeletalMesh>("CharacterWithTexture.fbx");

    Assert(SkeletalMesh->Materials.size() > 0);

    FString DiffusePath = SkeletalMesh->Materials[0].DiffuseMap;

    // Should be DDS file
    Assert(DiffusePath.ends_with(".dds"));

    // DDS cache should exist
    Assert(FileExists(DiffusePath));
}
```

### Performance Tests

**Test 9: Load Time Benchmark (Static Mesh)**
```cpp
void Benchmark_StaticMeshLoadTime()
{
    // Measure cold load (no cache)
    DeleteCache("Prop.fbx.bin");
    auto ColdTime = MeasureLoadTime("Prop.fbx");

    // Measure warm load (with cache)
    auto WarmTime = MeasureLoadTime("Prop.fbx");

    // Report
    printf("Static Mesh Cold load: %.2f ms\n", ColdTime);
    printf("Static Mesh Warm load: %.2f ms\n", WarmTime);
    printf("Speedup: %.1fx\n", ColdTime / WarmTime);

    // Assert targets
    Assert(ColdTime < 80.0f);   // Max 80ms for first load (Static Mesh)
    Assert(WarmTime < 10.0f);   // Max 10ms for cached load
    Assert(ColdTime / WarmTime > 6.0f);  // At least 6× speedup
}
```

**Test 10: Load Time Benchmark (Skeletal Mesh)**
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

**Test 11: Multiple Meshes (Mixed Types)**
```cpp
void Test_MultipleMixedMeshes()
{
    TArray<FString> StaticMeshFiles = {
        "Prop1.fbx",
        "Prop2.fbx",
        "Environment.fbx"
    };

    TArray<FString> SkeletalMeshFiles = {
        "Character1.fbx",
        "Character2.fbx",
        "Enemy1.fbx"
    };

    // Load all Static Meshes
    TArray<UStaticMesh*> StaticMeshes;
    for (const FString& Path : StaticMeshFiles)
    {
        UStaticMesh* Mesh = ResourceManager->Load<UStaticMesh>(Path);
        Assert(Mesh != nullptr);
        StaticMeshes.push_back(Mesh);
    }

    // Load all Skeletal Meshes
    TArray<USkeletalMesh*> SkeletalMeshes;
    for (const FString& Path : SkeletalMeshFiles)
    {
        USkeletalMesh* Mesh = ResourceManager->Load<USkeletalMesh>(Path);
        Assert(Mesh != nullptr);
        SkeletalMeshes.push_back(Mesh);
    }

    // Verify all Static Mesh caches exist
    for (const FString& Path : StaticMeshFiles)
    {
        Assert(FileExists(Path + ".bin"));
    }

    // Verify all Skeletal Mesh caches exist
    for (const FString& Path : SkeletalMeshFiles)
    {
        Assert(FileExists(Path + ".bin"));
    }

    // Verify memory caches contain all
    for (const FString& Path : StaticMeshFiles)
    {
        UStaticMesh* Cached = ResourceManager->Load<UStaticMesh>(Path);
        Assert(Cached != nullptr);
    }

    for (const FString& Path : SkeletalMeshFiles)
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
| 1.0 | 2025-11-11 | Initial implementation plan (Skeletal Mesh only) |
| 2.0 | 2025-11-11 | **Major Update:** Added Static Mesh support<br>- Updated to reflect dual mesh type support (Static + Skeletal)<br>- Added FBX type detection via `DetectFbxType()`<br>- Split FFbxManager into dual caching system<br>- Added LoadFbxStaticMeshAsset() following FObjManager pattern<br>- Updated all phases to cover both mesh types<br>- Added comprehensive testing for both types<br>- Clarified ownership model (Static: FFbxManager owns FStaticMesh*, Skeletal: FFbxManager owns USkeletalMesh*)<br>- Noted that FStaticMesh serialization already exists from OBJ system |
| 3.0 | 2025-11-11 | **Critical Architectural Fix:** Corrected Skeletal Mesh caching pattern<br>- **BREAKING CHANGE:** Changed from `TMap<FString, USkeletalMesh*>` to `TMap<FString, FSkeletalMesh*>`<br>- Added `FSkeletalMesh` data structure (pure data container, no UObject overhead)<br>- FFbxManager now owns `FSkeletalMesh*` (parsed data), not `USkeletalMesh*`<br>- USkeletalMesh references `FSkeletalMesh*` via pointer (does not own it)<br>- **Now follows FObjManager pattern exactly for both Static and Skeletal meshes**<br>- Updated serialization to serialize `FSkeletalMesh` (not `USkeletalMesh`)<br>- Updated all cache I/O functions to use `FSkeletalMesh*`<br>- Updated all code examples throughout document<br>- Fixed ownership model: both mesh types now consistent<br>- Added comprehensive FSkeletalMesh structure definition<br>- Updated all testing sections to reflect new architecture |

---

## Summary of Architectural Changes (v3.0)

This document has been updated to fix a critical architectural error in v2.0:

### Critical Fix: Skeletal Mesh Caching Pattern

**Problem in v2.0:** FFbxManager cached `USkeletalMesh*` directly, violating the FObjManager pattern

**Solution in v3.0:** Introduced `FSkeletalMesh` data structure, matching the FObjManager architecture

### Key Changes:

1. **New FSkeletalMesh Data Structure**
   - Pure data container (no UObject overhead)
   - Stores vertices, indices, bones, materials, bounds
   - Serializable to binary cache format
   - Owned by FFbxManager (deleted on Clear())

2. **Updated FFbxManager Caching**
   - **Static Mesh:** `TMap<FString, FStaticMesh*> FbxStaticMeshCache` (unchanged)
   - **Skeletal Mesh:** `TMap<FString, FSkeletalMesh*> FbxSkeletalMeshCache` (**changed from USkeletalMesh**)
   - Both follow identical FObjManager pattern now

3. **Consistent Ownership Model (Both Mesh Types)**
   - **Static Mesh FBX:**
     - FFbxManager owns `FStaticMesh*` (parsed data)
     - UStaticMesh references `FStaticMesh*` (UObject wrapper, creates GPU buffers)
     - ResourceManager manages `UStaticMesh*` lifetime
   - **Skeletal Mesh FBX:**
     - FFbxManager owns `FSkeletalMesh*` (parsed data)
     - USkeletalMesh references `FSkeletalMesh*` (UObject wrapper, creates GPU buffers)
     - ResourceManager manages `USkeletalMesh*` lifetime

4. **Updated Serialization**
   - `FSkeletalMesh` is serialized to cache (not `USkeletalMesh`)
   - Bones stored directly in `FSkeletalMesh` structure
   - `BoneNameToIndexMap` rebuilt on deserialization

5. **Updated Integration**
   - `USkeletalMesh::Load()` delegates to `FFbxManager::LoadFbxSkeletalMeshAsset()`
   - Returns `FSkeletalMesh*` (not `USkeletalMesh*`)
   - USkeletalMesh creates GPU buffers from `FSkeletalMesh` data
   - No ownership flags needed (clean architecture)

**Architecture Reference:** Both Static and Skeletal Mesh FBX now follow the FObjManager pattern exactly.

---

**Document End**
