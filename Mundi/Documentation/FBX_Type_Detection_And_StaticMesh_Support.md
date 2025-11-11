# FBX Type Detection and StaticMesh Support Implementation Guide

**Document Version:** 1.0
**Date:** 2025-11-11
**Phase:** Phase 0 (Pre-Baking System)
**Priority:** High - Must complete before FBX Baking implementation

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Current Problem Analysis](#current-problem-analysis)
3. [Goals and Scope](#goals-and-scope)
4. [Architecture Design](#architecture-design)
5. [Implementation Steps](#implementation-steps)
6. [Testing Strategy](#testing-strategy)
7. [Integration with Future Baking System](#integration-with-future-baking-system)
8. [Timeline and Deliverables](#timeline-and-deliverables)

---

## Executive Summary

### Problem Statement

Currently, Mundi's FBX importer assumes **all FBX files are skeletal meshes**. This causes issues:

1. **Cannot import static props** (buildings, rocks, trees) from FBX
2. **Wastes memory** storing empty skeleton data for static meshes
3. **Incompatible with future baking system** - cache format would need redesign later
4. **User confusion** - Why does a rock have bone data?

### Solution

Implement **automatic FBX type detection** that distinguishes between:
- **StaticMesh** (no skeleton, no skinning)
- **SkeletalMesh** (has skeleton and/or skinning data)

Then route to appropriate import path.

### Why Before Baking?

| Metric | If Done Before Baking | If Done After Baking |
|--------|----------------------|---------------------|
| Implementation Time | 2-3 days | 5-9 days (requires refactoring) |
| Cache Format Changes | None (designed correctly from start) | Major (version 2 needed) |
| Code Complexity | Low (clean design) | High (retrofitting) |
| User Impact | None (no existing caches) | High (all caches invalidated) |

**Decision: Implement NOW before baking system.**

---

## Current Problem Analysis

### Existing Code Flow

```cpp
// Current implementation (SkeletalMesh only)
ResourceManager->Load<USkeletalMesh>("Data/Model/Rock.fbx")
    ↓
USkeletalMesh::Load()
    ↓
FFbxImporter::ImportSkeletalMesh()
    ↓
Extracts skeleton (even if none exists!)
    ↓
Creates empty skeleton with 0 bones
    ↓
Wastes memory, confuses users
```

### Issues with Current Approach

**Issue 1: Type Assumption**
```cpp
// FFbxImporter.cpp - CURRENT CODE
bool FFbxImporter::ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh)
{
    // ... FBX loading ...

    // Assumes skeleton exists!
    ExtractSkeleton(FbxScene, OutMesh->Skeleton);

    // What if FBX is a static prop with no skeleton?
    // → Creates empty skeleton (waste)
}
```

**Issue 2: No StaticMesh Import**
```cpp
// Trying to import a static building FBX:
UStaticMesh* Building = ResourceManager->Load<UStaticMesh>("Building.fbx");
// ❌ ERROR: No import path exists for StaticMesh FBX!
```

**Issue 3: User Confusion**
```
Artist exports "Rock.fbx" (static mesh)
   ↓
Import as USkeletalMesh
   ↓
Rock has skeleton with 0 bones?
   ↓
Artist confused: "Why does a rock need bones?"
```

---

## Goals and Scope

### Primary Goals

1. ✅ **Automatic Type Detection**: Inspect FBX and determine StaticMesh vs SkeletalMesh
2. ✅ **StaticMesh FBX Import**: Add import path for static FBX files
3. ✅ **Unified Interface**: ResourceManager routes to correct type automatically
4. ✅ **Future-Proof Design**: Architecture supports future baking system

### Scope

**In Scope:**
- Type detection (StaticMesh vs SkeletalMesh)
- StaticMesh FBX import implementation
- Integration with existing ResourceManager
- Unit tests for type detection

**Out of Scope (Future Work):**
- Animation FBX import
- Multiple mesh nodes in single FBX
- FBX → .fbx.bin caching (Phase 1-3)
- Vertex optimization

### Success Criteria

| Criterion | Target |
|-----------|--------|
| Type Detection Accuracy | 100% for test assets |
| StaticMesh Import Time | < 50ms for typical prop |
| Memory Usage | No skeleton for StaticMesh |
| API Compatibility | Existing code continues to work |

---

## Architecture Design

### Type Detection Strategy

**Detection Logic:**

```
FBX File
    ↓
Check for FbxSkeleton nodes
    ↓ Yes
SkeletalMesh
    ↓ No
Check for FbxSkin deformers
    ↓ Yes
SkeletalMesh (rigged without explicit skeleton)
    ↓ No
Check for FbxAnimStack (animation)
    ↓ Yes
SkeletalMesh (animated mesh)
    ↓ No
StaticMesh (default)
```

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer                                           │
│ ResourceManager->Load<UStaticMesh>("Prop.fbx")             │
│ ResourceManager->Load<USkeletalMesh>("Character.fbx")      │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ UResourceManager (Modified)                                 │
│ - Detects resource type from file extension                 │
│ - Routes .fbx to appropriate mesh type                      │
└───────────────────────────┬─────────────────────────────────┘
                            │
        ┌───────────────────┴───────────────────┐
        │                                       │
        ▼ UStaticMesh                          ▼ USkeletalMesh
┌─────────────────────────┐       ┌─────────────────────────────┐
│ UStaticMesh::Load()     │       │ USkeletalMesh::Load()       │
│ - Calls FFbxImporter    │       │ - Calls FFbxImporter        │
│ - Creates GPU buffers   │       │ - Creates GPU buffers       │
└───────────┬─────────────┘       └───────────┬─────────────────┘
            │                                 │
            └────────────┬────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│ FFbxImporter (Enhanced)                                     │
│ - NEW: DetectFbxType(path) → StaticMesh/SkeletalMesh      │
│ - NEW: ImportStaticMesh(path, UStaticMesh*)                │
│ - EXISTING: ImportSkeletalMesh(path, USkeletalMesh*)       │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

**StaticMesh Import Flow:**
```
"Building.fbx"
    ↓
ResourceManager->Load<UStaticMesh>()
    ↓
UStaticMesh::Load()
    ↓
FFbxImporter::DetectFbxType() → StaticMesh
    ↓
FFbxImporter::ImportStaticMesh()
    ↓
Extract vertices (FStaticMeshVertex)
Extract indices
Extract materials
NO skeleton extraction
    ↓
UStaticMesh populated
    ↓
Create GPU buffers
```

**SkeletalMesh Import Flow (Unchanged):**
```
"Character.fbx"
    ↓
ResourceManager->Load<USkeletalMesh>()
    ↓
USkeletalMesh::Load()
    ↓
FFbxImporter::DetectFbxType() → SkeletalMesh
    ↓
FFbxImporter::ImportSkeletalMesh()
    ↓
Extract vertices (FSkinnedVertex)
Extract indices
Extract skeleton
Extract skinning weights
Extract materials
    ↓
USkeletalMesh populated
```

---

## Implementation Steps

### Step 1: Define FBX Type Enum

**Location:** `Mundi/Source/Editor/FbxImporter.h`

```cpp
#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "String.h"

// Forward declarations
class USkeletalMesh;
class UStaticMesh;

/**
 * FBX file type classification
 */
enum class EFbxImportType : uint8
{
    StaticMesh = 0,   // No skeleton, no skinning, no animation
    SkeletalMesh = 1, // Has skeleton and/or skinning
    Animation = 2     // Animation data (future support)
};

/**
 * FBX SDK wrapper for importing meshes
 */
class FFbxImporter
{
public:
    FFbxImporter();
    ~FFbxImporter();

    // ──────────────────────────────────────────────────────
    // NEW: Type Detection
    // ──────────────────────────────────────────────────────

    /**
     * Detect FBX file type without full import
     * @param FilePath - Path to .fbx file
     * @return Detected type (StaticMesh, SkeletalMesh, or Animation)
     */
    static EFbxImportType DetectFbxType(const FString& FilePath);

    // ──────────────────────────────────────────────────────
    // NEW: StaticMesh Import
    // ──────────────────────────────────────────────────────

    /**
     * Import FBX as static mesh (no skeleton)
     * @param FilePath - Path to .fbx file
     * @param OutMesh - Output static mesh
     * @return True if import succeeded
     */
    bool ImportStaticMesh(const FString& FilePath, UStaticMesh* OutMesh);

    // ──────────────────────────────────────────────────────
    // EXISTING: SkeletalMesh Import
    // ──────────────────────────────────────────────────────

    /**
     * Import FBX as skeletal mesh (with skeleton)
     * @param FilePath - Path to .fbx file
     * @param OutMesh - Output skeletal mesh
     * @return True if import succeeded
     */
    bool ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh);

private:
    // FBX SDK objects
    FbxManager* SdkManager;
    FbxScene* Scene;
    FbxImporter* Importer;

    // Helper functions
    bool LoadFbxScene(const FString& FilePath);
    void ExtractStaticMeshData(FbxNode* Node, UStaticMesh* OutMesh);
    void ExtractSkeletalMeshData(FbxNode* Node, USkeletalMesh* OutMesh);

    // ... existing helper functions
};
```

---

### Step 2: Implement Type Detection

**Location:** `Mundi/Source/Editor/FbxImporter.cpp`

```cpp
#include "pch.h"
#include "FbxImporter.h"
#include "SkeletalMesh.h"
#include "StaticMesh.h"
#include "GlobalConsole.h"

EFbxImportType FFbxImporter::DetectFbxType(const FString& FilePath)
{
    // ──────────────────────────────────────────────────────
    // 1. Initialize FBX SDK (temporary instance)
    // ──────────────────────────────────────────────────────
    FbxManager* TempManager = FbxManager::Create();
    if (!TempManager)
    {
        UE_LOG("[error] Failed to create FbxManager for type detection");
        return EFbxImportType::StaticMesh;  // Default fallback
    }

    FbxScene* TempScene = FbxScene::Create(TempManager, "DetectionScene");
    FbxImporter* TempImporter = FbxImporter::Create(TempManager, "");

    // ──────────────────────────────────────────────────────
    // 2. Load FBX file
    // ──────────────────────────────────────────────────────
    if (!TempImporter->Initialize(FilePath.c_str(), -1, TempManager->GetIOSettings()))
    {
        UE_LOG("[error] Failed to initialize FbxImporter for: %s", FilePath.c_str());
        TempManager->Destroy();
        return EFbxImportType::StaticMesh;
    }

    if (!TempImporter->Import(TempScene))
    {
        UE_LOG("[error] Failed to import FBX scene for type detection: %s", FilePath.c_str());
        TempManager->Destroy();
        return EFbxImportType::StaticMesh;
    }

    // ──────────────────────────────────────────────────────
    // 3. Check for FbxSkeleton nodes
    // ──────────────────────────────────────────────────────
    int SkeletonCount = TempScene->GetSrcObjectCount<FbxSkeleton>();

    if (SkeletonCount > 0)
    {
        UE_LOG("FBX contains %d skeleton nodes → SkeletalMesh: %s", SkeletonCount, FilePath.c_str());
        TempManager->Destroy();
        return EFbxImportType::SkeletalMesh;
    }

    // ──────────────────────────────────────────────────────
    // 4. Check for FbxSkin deformers (skinning/rigging)
    // ──────────────────────────────────────────────────────
    int DeformerCount = 0;
    int NodeCount = TempScene->GetNodeCount();

    for (int i = 0; i < NodeCount; ++i)
    {
        FbxNode* Node = TempScene->GetNode(i);
        FbxMesh* Mesh = Node->GetMesh();

        if (Mesh)
        {
            int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
            DeformerCount += SkinCount;
        }
    }

    if (DeformerCount > 0)
    {
        UE_LOG("FBX contains %d skin deformers → SkeletalMesh: %s", DeformerCount, FilePath.c_str());
        TempManager->Destroy();
        return EFbxImportType::SkeletalMesh;
    }

    // ──────────────────────────────────────────────────────
    // 5. Check for FbxAnimStack (animation data)
    // ──────────────────────────────────────────────────────
    int AnimStackCount = TempScene->GetSrcObjectCount<FbxAnimStack>();

    if (AnimStackCount > 0)
    {
        UE_LOG("FBX contains %d animation stacks → SkeletalMesh: %s", AnimStackCount, FilePath.c_str());
        TempManager->Destroy();
        return EFbxImportType::SkeletalMesh;  // Animated meshes need skeleton
    }

    // ──────────────────────────────────────────────────────
    // 6. Default: StaticMesh
    // ──────────────────────────────────────────────────────
    UE_LOG("FBX is a static mesh (no skeleton/skinning): %s", FilePath.c_str());
    TempManager->Destroy();
    return EFbxImportType::StaticMesh;
}
```

---

### Step 3: Implement StaticMesh Import

**Location:** `Mundi/Source/Editor/FbxImporter.cpp`

```cpp
bool FFbxImporter::ImportStaticMesh(const FString& FilePath, UStaticMesh* OutMesh)
{
    if (!OutMesh)
    {
        UE_LOG("[error] OutMesh is null");
        return false;
    }

    // ──────────────────────────────────────────────────────
    // 1. Load FBX scene
    // ──────────────────────────────────────────────────────
    if (!LoadFbxScene(FilePath))
    {
        return false;
    }

    // ──────────────────────────────────────────────────────
    // 2. Convert scene coordinate system
    // ──────────────────────────────────────────────────────
    FbxAxisSystem SceneAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    FbxAxisSystem MundiAxisSystem(
        FbxAxisSystem::eZAxis,      // Z-Up
        FbxAxisSystem::eParityEven, // Left-handed
        FbxAxisSystem::eLeftHanded
    );

    if (SceneAxisSystem != MundiAxisSystem)
    {
        MundiAxisSystem.ConvertScene(Scene);
        UE_LOG("Converted FBX coordinate system to Mundi (Z-Up, Left-Handed)");
    }

    // ──────────────────────────────────────────────────────
    // 3. Triangulate meshes
    // ──────────────────────────────────────────────────────
    FbxGeometryConverter GeometryConverter(SdkManager);
    GeometryConverter.Triangulate(Scene, true);

    // ──────────────────────────────────────────────────────
    // 4. Extract mesh data from all nodes
    // ──────────────────────────────────────────────────────
    FbxNode* RootNode = Scene->GetRootNode();

    if (!RootNode)
    {
        UE_LOG("[error] FBX scene has no root node");
        return false;
    }

    // Temporary storage for all mesh data
    TArray<FStaticMeshVertex> AllVertices;
    TArray<uint32> AllIndices;
    TArray<FMaterialInfo> Materials;

    // Recursively process all mesh nodes
    ProcessStaticMeshNode(RootNode, AllVertices, AllIndices, Materials);

    if (AllVertices.empty())
    {
        UE_LOG("[error] No mesh data found in FBX: %s", FilePath.c_str());
        return false;
    }

    // ──────────────────────────────────────────────────────
    // 5. Populate UStaticMesh
    // ──────────────────────────────────────────────────────
    OutMesh->StaticMeshAsset = new FStaticMesh();
    OutMesh->StaticMeshAsset->Vertices = AllVertices;
    OutMesh->StaticMeshAsset->Indices = AllIndices;

    // Calculate bounds
    CalculateBounds(OutMesh->StaticMeshAsset);

    // Store materials (textures will be processed later)
    OutMesh->Materials = Materials;

    UE_LOG("Imported static mesh from FBX: %s (%d vertices, %d indices)",
        FilePath.c_str(),
        AllVertices.size(),
        AllIndices.size());

    return true;
}

// ──────────────────────────────────────────────────────
// Helper: Process mesh node recursively
// ──────────────────────────────────────────────────────
void FFbxImporter::ProcessStaticMeshNode(
    FbxNode* Node,
    TArray<FStaticMeshVertex>& OutVertices,
    TArray<uint32>& OutIndices,
    TArray<FMaterialInfo>& OutMaterials)
{
    if (!Node)
        return;

    // Process mesh at this node
    FbxMesh* Mesh = Node->GetMesh();
    if (Mesh)
    {
        ExtractStaticMeshData(Node, OutVertices, OutIndices, OutMaterials);
    }

    // Recursively process children
    int ChildCount = Node->GetChildCount();
    for (int i = 0; i < ChildCount; ++i)
    {
        ProcessStaticMeshNode(Node->GetChild(i), OutVertices, OutIndices, OutMaterials);
    }
}

// ──────────────────────────────────────────────────────
// Helper: Extract static mesh data from FbxMesh
// ──────────────────────────────────────────────────────
void FFbxImporter::ExtractStaticMeshData(
    FbxNode* Node,
    TArray<FStaticMeshVertex>& OutVertices,
    TArray<uint32>& OutIndices,
    TArray<FMaterialInfo>& OutMaterials)
{
    FbxMesh* Mesh = Node->GetMesh();
    if (!Mesh)
        return;

    // Get vertex positions
    FbxVector4* ControlPoints = Mesh->GetControlPoints();
    int ControlPointCount = Mesh->GetControlPointsCount();

    // Get normals
    FbxArray<FbxVector4> Normals;
    Mesh->GetPolygonVertexNormals(Normals);

    // Get UVs
    FbxStringList UVSetNames;
    Mesh->GetUVSetNames(UVSetNames);
    const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;

    // Process each polygon
    int PolygonCount = Mesh->GetPolygonCount();
    uint32 BaseVertexIndex = OutVertices.size();

    for (int PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
    {
        int PolyVertexCount = Mesh->GetPolygonSize(PolyIndex);

        // Only process triangles
        if (PolyVertexCount != 3)
        {
            UE_LOG("[warning] Non-triangle polygon found, skipping");
            continue;
        }

        for (int VertIndex = 0; VertIndex < 3; ++VertIndex)
        {
            int ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, VertIndex);

            FStaticMeshVertex Vertex;

            // Position
            FbxVector4 Position = ControlPoints[ControlPointIndex];
            Vertex.Position = FVector(
                static_cast<float>(Position[0]),
                static_cast<float>(Position[1]),
                static_cast<float>(Position[2])
            );

            // Normal
            FbxVector4 Normal;
            Mesh->GetPolygonVertexNormal(PolyIndex, VertIndex, Normal);
            Vertex.Normal = FVector(
                static_cast<float>(Normal[0]),
                static_cast<float>(Normal[1]),
                static_cast<float>(Normal[2])
            );

            // UV
            if (UVSetName)
            {
                FbxVector2 UV;
                bool bUnmapped;
                Mesh->GetPolygonVertexUV(PolyIndex, VertIndex, UVSetName, UV, bUnmapped);
                Vertex.UV = FVector2D(
                    static_cast<float>(UV[0]),
                    static_cast<float>(UV[1])
                );
            }

            // Tangent (calculate from normal)
            Vertex.Tangent = CalculateTangent(Vertex.Normal);

            // Color (default white)
            Vertex.Color = FColor(1.0f, 1.0f, 1.0f, 1.0f);

            OutVertices.push_back(Vertex);
            OutIndices.push_back(BaseVertexIndex + OutVertices.size() - 1);
        }
    }

    // Extract materials (similar to existing implementation)
    ExtractMaterials(Node, OutMaterials);
}
```

---

### Step 4: Update UStaticMesh::Load()

**Location:** `Mundi/Source/Runtime/AssetManagement/StaticMesh.cpp`

```cpp
void UStaticMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    namespace fs = std::filesystem;
    FString Extension = fs::path(InFilePath).extension().string();

    // ──────────────────────────────────────────────────────
    // Route based on file extension
    // ──────────────────────────────────────────────────────

    if (Extension == ".obj")
    {
        // EXISTING: OBJ import via FObjManager
        StaticMeshAsset = FObjManager::LoadObjStaticMeshAsset(InFilePath);
    }
    else if (Extension == ".fbx")
    {
        // NEW: FBX import via FFbxImporter
        FFbxImporter Importer;

        // Detect type first
        EFbxImportType FbxType = FFbxImporter::DetectFbxType(InFilePath);

        if (FbxType != EFbxImportType::StaticMesh)
        {
            UE_LOG("[error] FBX is not a StaticMesh: %s", InFilePath.c_str());
            return;
        }

        // Import as StaticMesh
        if (!Importer.ImportStaticMesh(InFilePath, this))
        {
            UE_LOG("[error] Failed to import FBX as StaticMesh: %s", InFilePath.c_str());
            return;
        }
    }
    else
    {
        UE_LOG("[error] Unsupported mesh format: %s", Extension.c_str());
        return;
    }

    // ──────────────────────────────────────────────────────
    // Create GPU buffers (common for all formats)
    // ──────────────────────────────────────────────────────

    if (StaticMeshAsset && StaticMeshAsset->Vertices.size() > 0)
    {
        CreateVertexBuffer(StaticMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(StaticMeshAsset, InDevice);

        UE_LOG("Created GPU buffers for static mesh: %s", InFilePath.c_str());
    }
}
```

---

### Step 5: Update USkeletalMesh::Load()

**Location:** `Mundi/Source/Runtime/AssetManagement/SkeletalMesh.cpp`

```cpp
void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    namespace fs = std::filesystem;
    FString Extension = fs::path(InFilePath).extension().string();

    // ──────────────────────────────────────────────────────
    // Only FBX is supported for SkeletalMesh
    // ──────────────────────────────────────────────────────

    if (Extension != ".fbx")
    {
        UE_LOG("[error] SkeletalMesh only supports .fbx format: %s", InFilePath.c_str());
        return;
    }

    // ──────────────────────────────────────────────────────
    // Detect FBX type
    // ──────────────────────────────────────────────────────

    EFbxImportType FbxType = FFbxImporter::DetectFbxType(InFilePath);

    if (FbxType != EFbxImportType::SkeletalMesh)
    {
        UE_LOG("[error] FBX is not a SkeletalMesh (use UStaticMesh instead): %s", InFilePath.c_str());
        return;
    }

    // ──────────────────────────────────────────────────────
    // Import as SkeletalMesh
    // ──────────────────────────────────────────────────────

    FFbxImporter Importer;

    if (!Importer.ImportSkeletalMesh(InFilePath, this))
    {
        UE_LOG("[error] Failed to import FBX as SkeletalMesh: %s", InFilePath.c_str());
        return;
    }

    // ──────────────────────────────────────────────────────
    // Create GPU buffers
    // ──────────────────────────────────────────────────────

    if (Vertices.size() > 0)
    {
        CreateVertexBuffer(InDevice, InVertexType);
        CreateIndexBuffer(InDevice);

        UE_LOG("Created GPU buffers for skeletal mesh: %s", InFilePath.c_str());
    }
}
```

---

## Testing Strategy

### Unit Tests

**Test 1: Type Detection - StaticMesh**
```cpp
void Test_DetectFbxType_StaticMesh()
{
    // Arrange: Static prop FBX (no skeleton, no skinning)
    FString FbxPath = "TestAssets/Rock.fbx";

    // Act
    EFbxImportType Type = FFbxImporter::DetectFbxType(FbxPath);

    // Assert
    Assert(Type == EFbxImportType::StaticMesh, "Rock.fbx should be StaticMesh");
}
```

**Test 2: Type Detection - SkeletalMesh**
```cpp
void Test_DetectFbxType_SkeletalMesh()
{
    // Arrange: Character FBX (with skeleton)
    FString FbxPath = "TestAssets/Character.fbx";

    // Act
    EFbxImportType Type = FFbxImporter::DetectFbxType(FbxPath);

    // Assert
    Assert(Type == EFbxImportType::SkeletalMesh, "Character.fbx should be SkeletalMesh");
}
```

**Test 3: StaticMesh Import**
```cpp
void Test_ImportStaticMesh()
{
    // Arrange
    FFbxImporter Importer;
    UStaticMesh* Mesh = ObjectFactory::NewObject<UStaticMesh>();

    // Act
    bool bSuccess = Importer.ImportStaticMesh("TestAssets/Building.fbx", Mesh);

    // Assert
    Assert(bSuccess, "Import should succeed");
    Assert(Mesh->StaticMeshAsset != nullptr, "Mesh asset should be created");
    Assert(Mesh->StaticMeshAsset->Vertices.size() > 0, "Should have vertices");
    Assert(Mesh->StaticMeshAsset->Indices.size() > 0, "Should have indices");

    // No skeleton should exist
    Assert(Mesh->Skeleton == nullptr, "StaticMesh should not have skeleton");
}
```

**Test 4: SkeletalMesh Import Still Works**
```cpp
void Test_ImportSkeletalMesh_StillWorks()
{
    // Arrange
    FFbxImporter Importer;
    USkeletalMesh* Mesh = ObjectFactory::NewObject<USkeletalMesh>();

    // Act
    bool bSuccess = Importer.ImportSkeletalMesh("TestAssets/Character.fbx", Mesh);

    // Assert
    Assert(bSuccess, "Import should succeed");
    Assert(Mesh->Vertices.size() > 0, "Should have vertices");
    Assert(Mesh->Skeleton != nullptr, "SkeletalMesh should have skeleton");
    Assert(Mesh->Skeleton->Bones.size() > 0, "Skeleton should have bones");
}
```

**Test 5: Wrong Type Error Handling**
```cpp
void Test_WrongType_ErrorHandling()
{
    // Arrange: Try to load StaticMesh FBX as SkeletalMesh
    USkeletalMesh* Mesh = nullptr;

    // Act
    Mesh = ResourceManager->Load<USkeletalMesh>("TestAssets/Rock.fbx");

    // Assert
    Assert(Mesh == nullptr, "Should fail to load Rock as SkeletalMesh");
    // (Or: Log error and return empty mesh, depending on error handling policy)
}
```

### Integration Tests

**Test 6: ResourceManager Routing**
```cpp
void Test_ResourceManager_Routing()
{
    // Test StaticMesh
    UStaticMesh* Building = ResourceManager->Load<UStaticMesh>("Data/Model/Building.fbx");
    Assert(Building != nullptr);
    Assert(Building->StaticMeshAsset->Vertices.size() > 0);

    // Test SkeletalMesh
    USkeletalMesh* Character = ResourceManager->Load<USkeletalMesh>("Data/Model/Character.fbx");
    Assert(Character != nullptr);
    Assert(Character->Skeleton != nullptr);
    Assert(Character->Skeleton->Bones.size() > 0);
}
```

**Test 7: Memory Verification**
```cpp
void Test_StaticMesh_NoSkeletonMemory()
{
    // Load static prop
    UStaticMesh* Rock = ResourceManager->Load<UStaticMesh>("Rock.fbx");

    // Verify no skeleton memory allocated
    size_t MemoryBefore = GetCurrentMemoryUsage();

    // Try to access skeleton (should be null)
    Assert(Rock->Skeleton == nullptr);

    size_t MemoryAfter = GetCurrentMemoryUsage();
    size_t SkeletonMemory = MemoryAfter - MemoryBefore;

    Assert(SkeletonMemory == 0, "No memory should be allocated for skeleton");
}
```

### Real-World Test Assets

Create test FBX files with known properties:

| Asset | Type | Properties | Purpose |
|-------|------|------------|---------|
| `Rock.fbx` | StaticMesh | 500 verts, no bones | Test basic static mesh |
| `Building.fbx` | StaticMesh | 2000 verts, no bones | Test larger static mesh |
| `Character.fbx` | SkeletalMesh | 1800 verts, 40 bones | Test skeletal mesh |
| `Weapon.fbx` | SkeletalMesh | 300 verts, 5 bones | Test rigged prop |
| `Tree.fbx` | StaticMesh | 1200 verts, no bones | Test vegetation |

---

## Integration with Future Baking System

### Design Considerations for Baking

**Cache File Format (Future):**

```cpp
// .fbx.bin header will include type information
struct FFbxCacheHeader
{
    uint32 MagicNumber = 0x46425843;  // "FBXC"
    uint32 Version = 1;
    EFbxImportType MeshType;  // ← NEW: StaticMesh or SkeletalMesh
};

// Based on MeshType, different data follows:
if (MeshType == StaticMesh)
{
    // Serialize FStaticMesh data (no skeleton)
}
else if (MeshType == SkeletalMesh)
{
    // Serialize USkeletalMesh data (with skeleton)
}
```

### FFbxManager Design (Future)

With type detection in place, FFbxManager can be designed correctly from the start:

```cpp
class FFbxManager
{
private:
    // Separate caches for each type
    static TMap<FString, UStaticMesh*> FbxStaticMeshCache;
    static TMap<FString, USkeletalMesh*> FbxSkeletalMeshCache;

public:
    // Type-specific loading (uses DetectFbxType internally)
    static UStaticMesh* LoadFbxStaticMesh(const FString& Path);
    static USkeletalMesh* LoadFbxSkeletalMesh(const FString& Path);

    // Or unified interface:
    static UResourceBase* LoadFbxMesh(const FString& Path, EFbxImportType* OutType = nullptr);
};
```

**Benefits of Implementing Type Detection First:**

1. ✅ Cache format includes type from day 1 (no version 2 needed)
2. ✅ FFbxManager designed for both types from the start
3. ✅ Serialization logic clean (type-specific from beginning)
4. ✅ No user cache invalidation needed
5. ✅ No code refactoring after initial implementation

---

## Timeline and Deliverables

### Phase 0 Timeline: 2-3 Days

**Day 1: Type Detection (4-6 hours)**
- [ ] Define `EFbxImportType` enum
- [ ] Implement `DetectFbxType()` function
- [ ] Unit tests for type detection
- [ ] Test with 5+ sample FBX files

**Day 2: StaticMesh Import (6-8 hours)**
- [ ] Implement `ImportStaticMesh()` function
- [ ] Implement `ProcessStaticMeshNode()` helper
- [ ] Implement `ExtractStaticMeshData()` helper
- [ ] Integration with `UStaticMesh::Load()`
- [ ] Unit tests for static mesh import

**Day 3: Integration and Testing (4-6 hours)**
- [ ] Update `USkeletalMesh::Load()` with type checking
- [ ] Integration tests (ResourceManager routing)
- [ ] Performance benchmarks
- [ ] Documentation updates
- [ ] Code review and cleanup

### Deliverables

**Code Files:**
- ✅ `FbxImporter.h` (updated with type detection)
- ✅ `FbxImporter.cpp` (DetectFbxType + ImportStaticMesh)
- ✅ `StaticMesh.cpp` (updated Load() for FBX)
- ✅ `SkeletalMesh.cpp` (updated Load() with type check)

**Tests:**
- ✅ 7 unit tests (type detection, import, error handling)
- ✅ 3 integration tests (ResourceManager, memory)
- ✅ 5 test FBX assets

**Documentation:**
- ✅ This implementation guide
- ✅ Code comments in all new functions
- ✅ Usage examples in README

---

## Success Criteria Checklist

### Functional Requirements

- [ ] `DetectFbxType()` correctly identifies StaticMesh FBX files
- [ ] `DetectFbxType()` correctly identifies SkeletalMesh FBX files
- [ ] `ImportStaticMesh()` imports static FBX files without skeleton data
- [ ] `ImportSkeletalMesh()` continues to work as before
- [ ] `UStaticMesh::Load()` can load .fbx files
- [ ] `USkeletalMesh::Load()` validates FBX type before import
- [ ] Wrong-type errors are logged clearly

### Performance Requirements

- [ ] Type detection adds < 5ms overhead (one-time per file)
- [ ] StaticMesh import takes < 50ms for typical prop
- [ ] No performance regression for SkeletalMesh import
- [ ] Memory usage correct (no skeleton for StaticMesh)

### Quality Requirements

- [ ] All unit tests pass (7/7)
- [ ] All integration tests pass (3/3)
- [ ] No memory leaks (verified with leak detector)
- [ ] No crashes with edge cases (empty FBX, malformed files)
- [ ] Code follows Mundi coding standards

### Documentation Requirements

- [ ] All public functions have doc comments
- [ ] Implementation guide complete (this document)
- [ ] Usage examples provided
- [ ] Known limitations documented

---

## Known Limitations and Future Work

### Current Limitations

1. **Single Mesh Per FBX**: Only imports first mesh found (ignores additional meshes)
2. **No LOD Support**: Ignores LOD groups in FBX
3. **No Animation Import**: `EFbxImportType::Animation` detection only (no import)
4. **No Morph Targets**: Blend shapes not imported

### Future Enhancements (Post-Baking)

1. **Multi-Mesh Import**: Import all meshes from single FBX
2. **LOD Support**: Import LOD chains
3. **Animation Import**: Support animation-only FBX files
4. **Morph Target Support**: Blend shape import
5. **Material Instance Support**: Advanced material features

---

## Appendix: Code Reference

### Key Functions Summary

| Function | Location | Purpose |
|----------|----------|---------|
| `DetectFbxType()` | FbxImporter.cpp | Auto-detect FBX type |
| `ImportStaticMesh()` | FbxImporter.cpp | Import static FBX |
| `ImportSkeletalMesh()` | FbxImporter.cpp | Import skeletal FBX (existing) |
| `ProcessStaticMeshNode()` | FbxImporter.cpp | Recursively process nodes |
| `ExtractStaticMeshData()` | FbxImporter.cpp | Extract mesh from FbxMesh |

### Dependencies

| Component | Status | Notes |
|-----------|--------|-------|
| FBX SDK | Existing | Already integrated |
| `UStaticMesh` | Existing | Will support FBX loading |
| `USkeletalMesh` | Existing | Add type validation |
| `ResourceManager` | Existing | No changes needed |
| `FObjManager` | Existing | Pattern to follow |

### Testing Assets Location

```
TestAssets/
├── Rock.fbx (StaticMesh, 500 verts)
├── Building.fbx (StaticMesh, 2000 verts)
├── Character.fbx (SkeletalMesh, 1800 verts, 40 bones)
├── Weapon.fbx (SkeletalMesh, 300 verts, 5 bones)
└── Tree.fbx (StaticMesh, 1200 verts)
```

---

## Next Steps After Completion

Once Phase 0 is complete:

1. ✅ **Verify all tests pass**
2. ✅ **Code review with team**
3. ✅ **Merge to main branch**
4. ➡️ **Begin Phase 1: FBX Baking System** (FFbxManager implementation)

The baking system can now be designed correctly from the start with type information available.

---

## Document Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-11-11 | Initial implementation guide for Phase 0 |

---

**Document End**
