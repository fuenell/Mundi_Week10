# Unreal Engine 5 FBX Import Pipeline Architecture

## Overview

This document provides a comprehensive analysis of the Unreal Engine 5 FBX import pipeline, focusing on the architecture, workflow, coordinate system conversion, and differences between StaticMesh and SkeletalMesh import paths.

**Source:** Unreal Engine 5 source code
**Location:** `Engine/Source/Editor/UnrealEd/Private/Fbx/`
**Date Analyzed:** 2025-11-10

---

## Table of Contents

1. [Overall Architecture](#1-overall-architecture)
2. [Import Pipeline Phases](#2-import-pipeline-phases)
3. [Coordinate System Conversion](#3-coordinate-system-conversion)
4. [StaticMesh Import Pipeline](#4-staticmesh-import-pipeline)
5. [SkeletalMesh Import Pipeline](#5-skeletalmesh-import-pipeline)
6. [Key Differences](#6-key-differences)
7. [Critical Classes and Their Roles](#7-critical-classes-and-their-roles)
8. [Important Code Patterns](#8-important-code-patterns)

---

## 1. Overall Architecture

### 1.1 High-Level Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    User Initiates Import                         │
│                  (Drag FBX into Content Browser)                 │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                     UFbxFactory::FactoryCreateFile               │
│  • Entry point for all FBX imports                               │
│  • Handles file validation and detection                         │
│  • Routes to appropriate import path                             │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│               FFbxImporter::GetInstance() [Singleton]            │
│  • Single instance manages all FBX operations                    │
│  • Maintains FbxManager and Scene                                │
│  • Coordinates all import phases                                 │
└────────────────────────────┬────────────────────────────────────┘
                             │
                ┌────────────┴────────────┐
                ▼                         ▼
┌───────────────────────┐   ┌──────────────────────────┐
│   StaticMesh Path     │   │   SkeletalMesh Path      │
│                       │   │                          │
│ ImportStaticMesh()    │   │ ImportSkeletalMesh()     │
│ ImportStaticMeshAsSingle() │ (handles bones + skin) │
└───────────────────────┘   └──────────────────────────┘
```

### 1.2 Key Design Patterns

1. **Singleton Pattern:** `FFbxImporter` is a singleton to manage FBX SDK resources
2. **Phase State Machine:** Import progresses through distinct phases (NOTSTARTED → FILEOPENED → IMPORTED → FIXEDANDCONVERTED)
3. **Factory Pattern:** `UFbxFactory` creates appropriate asset types
4. **Converter Pattern:** `FFbxDataConverter` handles coordinate system transformations

---

## 2. Import Pipeline Phases

### 2.1 Phase State Diagram

```
┌─────────────┐
│ NOTSTARTED  │  Initial state
└──────┬──────┘
       │ OpenFile()
       ▼
┌─────────────┐
│ FILEOPENED  │  FBX file loaded, FbxScene created
└──────┬──────┘
       │ ImportFile()
       ▼
┌─────────────┐
│  IMPORTED   │  Scene imported, ready for conversion
└──────┬──────┘
       │ ConvertScene()
       ▼
┌──────────────────┐
│ FIXEDANDCONVERTED │  Coordinate system converted, ready for mesh building
└──────────────────┘
```

### 2.2 Phase Details

#### Phase 1: OpenFile()
**Location:** `FbxMainImport.cpp` lines ~1300-1400

**Responsibilities:**
- Create `FbxImporter` and `FbxScene` objects
- Load FBX file from disk
- Parse file header and version info
- Validate file format

**Key Code:**
```cpp
bool FFbxImporter::OpenFile(FString Filename)
{
    Importer = FbxImporter::Create(SdkManager, "");

    // Initialize the importer
    bool bStatus = Importer->Initialize(TCHAR_TO_UTF8(*Filename), -1, SdkManager->GetIOSettings());

    if(bStatus)
    {
        // Create the scene
        Scene = FbxScene::Create(SdkManager, "");
        CurPhase = FILEOPENED;
    }

    return bStatus;
}
```

#### Phase 2: ImportFile()
**Location:** `FbxMainImport.cpp` lines ~1400-1497

**Responsibilities:**
- Import FBX scene content into `FbxScene` object
- Read all scene nodes, meshes, materials, textures
- Parse animation data
- Extract metadata (creator, file version, etc.)

**Key Code:**
```cpp
bool FFbxImporter::ImportFile(FString Filename, bool bPreventMaterialNameClash)
{
    bool bStatus = Importer->Import(Scene);

    if(bStatus)
    {
        // Get file version
        Importer->GetFileVersion(FileMajor, FileMinor, FileRevision);
        FbxFileVersion = FString::Printf(TEXT("%d.%d.%d"), FileMajor, FileMinor, FileRevision);

        // Get file creator info
        FbxFileCreator = UTF8_TO_TCHAR(Importer->GetFileHeaderInfo()->mCreator.Buffer());

        CurPhase = IMPORTED;
    }

    return bStatus;
}
```

#### Phase 3: ConvertScene()
**Location:** `FbxMainImport.cpp` lines 1499-1580

**Responsibilities:**
- **CRITICAL:** Convert from FBX coordinate system to Unreal's coordinate system
- Merge animation layers
- Convert scene units to centimeters
- Apply axis conversion matrices
- This is where handedness conversion happens!

**Detailed Analysis:**

```cpp
void FFbxImporter::ConvertScene()
{
    // 1. Merge animation stack layers (if multiple layers exist)
    int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
    for (int32 AnimStackIndex = 0; AnimStackIndex < AnimStackCount; AnimStackIndex++)
    {
        FbxAnimStack* CurAnimStack = Scene->GetSrcObject<FbxAnimStack>(AnimStackIndex);
        if (CurAnimStack->GetMemberCount() > 1)
        {
            int32 ResampleRate = GetGlobalAnimStackSampleRate(CurAnimStack);
            MergeAllLayerAnimation(CurAnimStack, ResampleRate);
        }
    }

    // 2. Store original file coordinate system
    FileAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    FileUnitSystem = Scene->GetGlobalSettings().GetSystemUnit();

    FbxAMatrix AxisConversionMatrix;
    AxisConversionMatrix.SetIdentity();

    FbxAMatrix JointOrientationMatrix;
    JointOrientationMatrix.SetIdentity();

    // 3. COORDINATE SYSTEM CONVERSION (if enabled)
    if (GetImportOptions()->bConvertScene)
    {
        // UE uses Z-Up, -Y Forward (when bForceFrontXAxis = false)
        // Or Z-Up, X-Forward (when bForceFrontXAxis = true)

        // Why -Y as forward? To mimic Maya/Max behavior where
        // a model facing +X in DCC appears facing +X in UE
        // The negative is needed because we're converting RH to LH

        FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
        FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
        FbxAxisSystem::EFrontVector FrontVector = (FbxAxisSystem::EFrontVector) - FbxAxisSystem::eParityOdd;

        if (GetImportOptions()->bForceFrontXAxis)
        {
            FrontVector = FbxAxisSystem::eParityEven;  // Positive X forward
        }

        // Create Unreal's target axis system
        FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);

        // Get source axis system
        FbxAxisSystem SourceSetup = Scene->GetGlobalSettings().GetAxisSystem();

        // Only convert if systems differ
        if (SourceSetup != UnrealImportAxis)
        {
            // Remove any root transformation nodes
            FbxRootNodeUtility::RemoveAllFbxRoots(Scene);

            // CRITICAL: This modifies the entire scene in-place
            UnrealImportAxis.ConvertScene(Scene);

            // Calculate conversion matrix for later use
            FbxAMatrix SourceMatrix;
            SourceSetup.GetMatrix(SourceMatrix);
            FbxAMatrix UE4Matrix;
            UnrealImportAxis.GetMatrix(UE4Matrix);
            AxisConversionMatrix = SourceMatrix.Inverse() * UE4Matrix;

            // Apply additional joint rotation if forcing X-forward
            if (GetImportOptions()->bForceFrontXAxis)
            {
                JointOrientationMatrix.SetR(FbxVector4(-90.0, -90.0, 0.0));
            }
        }
    }

    // 4. Store conversion matrices for later use by FFbxDataConverter
    FFbxDataConverter::SetJointPostConversionMatrix(JointOrientationMatrix);
    FFbxDataConverter::SetAxisConversionMatrix(AxisConversionMatrix);

    // 5. UNIT CONVERSION to centimeters
    if (GetImportOptions()->bConvertSceneUnit &&
        Scene->GetGlobalSettings().GetSystemUnit() != FbxSystemUnit::cm)
    {
        FbxSystemUnit::cm.ConvertScene(Scene);
    }

    // 6. Reset animation evaluator cache (transforms changed)
    Scene->GetAnimationEvaluator()->Reset();
}
```

**Coordinate System Mapping:**

| Aspect | FBX (Typical) | Unreal (bForceFrontXAxis=false) | Unreal (bForceFrontXAxis=true) |
|--------|---------------|----------------------------------|--------------------------------|
| Up Axis | Y or Z | Z | Z |
| Forward Axis | Z (varies) | -Y | X |
| Right Axis | X (varies) | X | Y |
| Handedness | Right-Handed | **Right-Handed** (intermediate) | **Right-Handed** (intermediate) |
| Final Handedness | - | Left-Handed (via Y-flip in ConvertPos) | Left-Handed (via Y-flip in ConvertPos) |

---

## 3. Coordinate System Conversion

### 3.1 Two-Stage Conversion Process

Unreal Engine uses a **two-stage** coordinate system conversion:

1. **Stage 1 (Scene-Level):** `FbxAxisSystem::ConvertScene()` converts the entire FBX scene to a right-handed Z-up system
2. **Stage 2 (Per-Vertex):** `FFbxDataConverter::ConvertPos()` flips the Y-axis to convert from RH to LH

#### Stage 1: Scene Conversion (ConvertScene)

```
FBX Source Coordinate System (varies by DCC tool)
    │
    │ FbxAxisSystem::ConvertScene(Scene)
    │ Converts to: RightHanded, Z-Up, -Y Forward (or X Forward)
    │
    ▼
Intermediate Right-Handed Scene
    │
    │ Scene is now in FBX SDK memory
    │ All node transforms are modified
    │
    ▼
Ready for vertex extraction
```

#### Stage 2: Vertex-Level Handedness Conversion

**Location:** `FbxUtilsImport.cpp` lines 63-84

```cpp
FVector FFbxDataConverter::ConvertPos(FbxVector4 Vector)
{
    FVector Out;
    Out[0] = Vector[0];     // X stays the same
    Out[1] = -Vector[1];    // Y is FLIPPED (RH → LH conversion)
    Out[2] = Vector[2];     // Z stays the same
    UE::Fbx::Private::VerifyFiniteValue(Out);
    return Out;
}

FVector FFbxDataConverter::ConvertDir(FbxVector4 Vector)
{
    FVector Out;
    Out[0] = Vector[0];     // Same as ConvertPos
    Out[1] = -Vector[1];    // Y flip for directions too
    Out[2] = Vector[2];
    UE::Fbx::Private::VerifyFiniteValue(Out);
    return Out;
}
```

**Why Flip Y?**
- FBX SDK's `ConvertScene()` converts to a right-handed system
- Unreal uses a left-handed system
- Flipping Y-axis converts RH coordinates to LH: `(X, Y, Z)_{RH}` → `(X, -Y, Z)_{LH}`
- This also **reverses triangle winding order** from CCW to CW

### 3.2 Matrix Conversion

**Location:** `FbxUtilsImport.cpp` lines 178-203

When converting FbxAMatrix to FMatrix, UE applies special handling to row 1 (Y-axis):

```cpp
FMatrix FFbxDataConverter::ConvertMatrix(const FbxAMatrix& Matrix)
{
    FMatrix UEMatrix;

    for (int i = 0; i < 4; ++i)
    {
        const FbxVector4 Row = Matrix.GetRow(i);
        if(i == 1)  // Y-axis row
        {
            UEMatrix.M[i][0] = -Row[0];  // Negate X component
            UEMatrix.M[i][1] =  Row[1];  // Keep Y component
            UEMatrix.M[i][2] = -Row[2];  // Negate Z component
            UEMatrix.M[i][3] = -Row[3];  // Negate W component
        }
        else  // X, Z, W rows
        {
            UEMatrix.M[i][0] =  Row[0];
            UEMatrix.M[i][1] = -Row[1];  // Negate Y component
            UEMatrix.M[i][2] =  Row[2];
            UEMatrix.M[i][3] =  Row[3];
        }
    }
    UE::Fbx::Private::VerifyFiniteValue(UEMatrix);
    return UEMatrix;
}
```

**Pattern Analysis:**
- Row 1 (Y-axis): Flip X, Z, W components (but keep Y)
- Other rows: Flip only the Y component

This ensures proper handedness conversion for transformation matrices.

### 3.3 Winding Order Implications

**Critical Insight:** The Y-flip in `ConvertPos()` automatically reverses triangle winding:

```
Right-Handed CCW Triangle (FBX):
    v0 (0, 1, 0)
    v1 (1, 0, 0)
    v2 (0, 0, 0)

After ConvertPos() - Left-Handed CW Triangle (Unreal):
    v0 (0, -1, 0)
    v1 (1,  0, 0)
    v2 (0,  0, 0)

When viewed from the same camera position, the winding order is reversed!
```

**No explicit winding order reversal code is needed** because the Y-flip naturally handles it.

---

## 4. StaticMesh Import Pipeline

### 4.1 StaticMesh Import Flow

```
UFbxFactory::FactoryCreateFile()
    │
    ├─> DetectImportType() → Determines mesh type
    │
    ├─> ShowImportDialog() → User configures options
    │
    └─> ImportANode() → Processes each FBX node
            │
            ▼
FFbxImporter::ImportStaticMesh() [Single node]
    │
    └─> ImportStaticMeshAsSingle() [Multiple nodes combined]
            │
            ├─> Create UStaticMesh object
            │
            ├─> For each LOD level:
            │   │
            │   └─> BuildStaticMeshFromGeometry()
            │           │
            │           ├─> Extract geometry from FbxMesh
            │           │
            │           ├─> Triangulate mesh (if needed)
            │           │
            │           ├─> Process materials
            │           │
            │           ├─> Extract vertex data → FMeshDescription
            │           │       │
            │           │       ├─> Positions (ConvertPos)
            │           │       ├─> Normals (ConvertDir)
            │           │       ├─> Tangents (ConvertDir)
            │           │       ├─> UVs (no conversion)
            │           │       └─> Vertex Colors
            │           │
            │           └─> Build smoothing groups
            │
            └─> PostImportStaticMesh()
                    │
                    ├─> Build render data (RawMesh → GPU buffers)
                    ├─> Reorder materials to match FBX order
                    ├─> Generate collision
                    └─> Generate lightmap UVs (if requested)
```

### 4.2 BuildStaticMeshFromGeometry() Deep Dive

**Location:** `FbxStaticMeshImport.cpp` lines 375-end

**Phase 1: Preparation**
```cpp
bool FFbxImporter::BuildStaticMeshFromGeometry(
    FbxNode* Node,
    UStaticMesh* StaticMesh,
    TArray<FFbxMaterial>& MeshMaterials,
    int32 LODIndex,
    EVertexColorImportOption::Type VertexColorImportOption,
    const TMap<FVector3f, FColor>& ExistingVertexColorData,
    const FColor& VertexOverrideColor)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FFbxImporter::BuildStaticMeshFromGeometry);

    FbxMesh* Mesh = Node->GetMesh();
    FbxLayer* BaseLayer = Mesh->GetLayer(0);

    // Get MeshDescription (UE5's internal mesh representation)
    FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);
    FStaticMeshAttributes Attributes(*MeshDescription);
```

**Phase 2: UV Extraction**
```cpp
    // Extract UV channels
    FFBXUVs FBXUVs(this, Mesh);
    int32 FBXNamedLightMapCoordinateIndex = FBXUVs.FindLightUVIndex();
    if (FBXNamedLightMapCoordinateIndex != INDEX_NONE)
    {
        StaticMesh->SetLightMapCoordinateIndex(FBXNamedLightMapCoordinateIndex);
    }
```

**Phase 3: Material Processing**
```cpp
    // Import/find materials
    TArray<UMaterialInterface*> FbxMeshMaterials;
    FindOrImportMaterialsFromNode(Node, FbxMeshMaterials, FBXUVs.UVSets, bForSkeletalMesh);

    MaterialCount = Node->GetMaterialCount();

    // Store material references
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
    {
        FFbxMaterial* NewMaterial = new(MeshMaterials) FFbxMaterial;
        FbxSurfaceMaterial* FbxMaterial = Node->GetMaterial(MaterialIndex);
        NewMaterial->FbxMaterial = FbxMaterial;
        NewMaterial->Material = FbxMeshMaterials[MaterialIndex];
    }
```

**Phase 4: Mesh Triangulation**
```cpp
    // Ensure mesh is triangulated
    if (!Mesh->IsTriangleMesh())
    {
        UE_LOG(LogFbx, Display, TEXT("Triangulating static mesh %s"), *FbxNodeName);

        const bool bReplace = true;
        FbxNodeAttribute* ConvertedNode = GeometryConverter->Triangulate(Mesh, bReplace);

        if(ConvertedNode != NULL && ConvertedNode->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            Mesh = (fbxsdk::FbxMesh*)ConvertedNode;
        }
    }
```

**Phase 5: Vertex Position Extraction**

This is where coordinate conversion happens!

```cpp
    // Get control points (vertex positions)
    int32 ControlPointsCount = Mesh->GetControlPointsCount();
    FbxVector4* ControlPoints = Mesh->GetControlPoints();

    // Compute total transform matrix for this node
    FbxAMatrix TotalMatrix = ComputeTotalMatrix(Node);

    // Extract each vertex position
    for(int32 VertexIndex = 0; VertexIndex < ControlPointsCount; VertexIndex++)
    {
        // Get position in FBX space
        FbxVector4 FbxPosition = ControlPoints[VertexIndex];

        // Transform by node's total matrix
        FbxVector4 FinalPosition = TotalMatrix.MultT(FbxPosition);

        // Convert to Unreal coordinate space (Y-flip happens here!)
        FVector3f UnrealPosition = (FVector3f)Converter.ConvertPos(FinalPosition);

        // Store in MeshDescription
        VertexPositions[VertexID] = UnrealPosition;
    }
```

**Phase 6: Triangle/Polygon Processing**

```cpp
    int32 PolygonCount = Mesh->GetPolygonCount();

    for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
    {
        // Get material ID for this polygon
        int32 MaterialIndex = 0;
        if (FbxMaterialElement)
        {
            MaterialIndex = FbxMaterialElement->GetIndexArray().GetAt(PolygonIndex);
        }

        // Extract triangle vertices (should be 3 after triangulation)
        int32 PolygonSize = Mesh->GetPolygonSize(PolygonIndex);
        check(PolygonSize == 3);  // Must be triangulated

        for (int32 CornerIndex = 0; CornerIndex < 3; CornerIndex++)
        {
            int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);

            // Extract normal
            FbxVector4 FbxNormal;
            Mesh->GetPolygonVertexNormal(PolygonIndex, CornerIndex, FbxNormal);
            FVector3f Normal = (FVector3f)Converter.ConvertDir(FbxNormal);  // Y-flip!

            // Extract tangent
            FbxVector4 FbxTangent = GetFbxTangent(Mesh, PolygonIndex, CornerIndex);
            FVector3f Tangent = (FVector3f)Converter.ConvertDir(FbxTangent);  // Y-flip!

            // Extract UVs (no coordinate conversion needed!)
            for (int32 UVIndex = 0; UVIndex < UniqueUVCount; UVIndex++)
            {
                FbxVector2 FbxUV = GetFbxUV(Mesh, UVIndex, ControlPointIndex, PolygonIndex, CornerIndex);
                FVector2f UV((float)FbxUV[0], 1.0f - (float)FbxUV[1]);  // V-flip for DX texture convention

                UVs.Set(VertexInstanceID, UVIndex, UV);
            }

            // Extract vertex color (if present)
            if (LayerElementVertexColor)
            {
                FbxColor FbxColor = GetVertexColor(Mesh, LayerElementVertexColor, ControlPointIndex, PolygonIndex, CornerIndex);
                FLinearColor Color(FbxColor.mRed, FbxColor.mGreen, FbxColor.mBlue, FbxColor.mAlpha);
                VertexColors[VertexInstanceID] = Color.ToFColor(true);
            }
        }

        // Create triangle
        // Note: No explicit winding order reversal needed!
        // The Y-flip in ConvertPos() already reversed it.
        FTriangleID TriangleID = MeshDescription->CreateTriangle(
            PolygonGroupID,  // Material section
            VertexInstanceID[0],
            VertexInstanceID[1],
            VertexInstanceID[2]
        );
    }
```

**Phase 7: Smoothing Groups**

```cpp
    // Import smoothing groups (if present)
    FbxLayerElementSmoothing const* SmoothingInfo = Mesh->GetLayer(0)->GetSmoothing();
    if (SmoothingInfo)
    {
        for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
        {
            uint32 SmoothingMask = GetSmoothingGroups(SmoothingInfo, PolygonIndex);
            // Store smoothing mask per-triangle
            // Used later to generate hard/soft edges
        }
    }
```

### 4.3 PostImportStaticMesh()

**Location:** `FbxStaticMeshImport.cpp`

```cpp
void FFbxImporter::PostImportStaticMesh(
    UStaticMesh* StaticMesh,
    TArray<FbxNode*>& MeshNodeArray,
    int32 LODIndex)
{
    // 1. Build render data from MeshDescription
    //    - Generates GPU vertex buffers
    //    - Builds index buffers
    //    - Computes adjacency for outline rendering
    StaticMesh->Build();

    // 2. Reorder materials to match FBX file order (if requested)
    if (ImportOptions->bReorderMaterialToFbxOrder)
    {
        ReorderMaterialsToMatchFbxOrder(StaticMesh, MeshNodeArray);
    }

    // 3. Import sockets (attachment points)
    ImportStaticMeshGlobalSockets(StaticMesh);
    ImportStaticMeshLocalSockets(StaticMesh, MeshNodeArray);

    // 4. Generate collision (if requested)
    if (ImportCollisionModels(StaticMesh, MeshNodeArray[0]->GetName()))
    {
        // Collision primitives imported from FBX nodes with UCX_/UBX_/USP_ prefix
    }

    // 5. Mark package dirty
    StaticMesh->MarkPackageDirty();
}
```

---

## 5. SkeletalMesh Import Pipeline

### 5.1 SkeletalMesh Import Flow

```
FFbxImporter::ImportSkeletalMesh()
    │
    ├─> FindFBXMeshesByBone() → Group meshes by skeleton
    │
    ├─> ImportSkeletalMesh(FImportSkeletalMeshArgs)
    │       │
    │       ├─> Create/Find USkeleton
    │       │
    │       ├─> Create USkeletalMesh
    │       │
    │       ├─> For each FbxNode:
    │       │   │
    │       │   ├─> BuildSkeletalMesh()
    │       │   │       │
    │       │   │       ├─> Extract skeleton hierarchy
    │       │   │       │   │
    │       │   │       │   └─> ProcessBone()
    │       │   │       │       │
    │       │   │       │       ├─> Get bone transform (bind pose)
    │       │   │       │       ├─> ConvertPos/ConvertDir
    │       │   │       │       └─> Build bone hierarchy
    │       │   │       │
    │       │   │       ├─> Extract skinning data
    │       │   │       │   │
    │       │   │       │   └─> ProcessSkinning()
    │       │   │       │       │
    │       │   │       │       ├─> Get skin clusters
    │       │   │       │       ├─> Extract bone influences
    │       │   │       │       └─> Normalize weights
    │       │   │       │
    │       │   │       └─> Extract vertices (skinned)
    │       │   │           │
    │       │   │           ├─> Positions (ConvertPos)
    │       │   │           ├─> Normals (ConvertDir)
    │       │   │           ├─> Tangents (ConvertDir)
    │       │   │           ├─> Bone indices
    │       │   │           └─> Bone weights
    │       │   │
    │       │   └─> SkinControlPointsToPose()
    │       │       │
    │       │       └─> Apply skinning deformation
    │       │           to vertex positions (optional)
    │       │
    │       ├─> ImportMorphTargets() (if requested)
    │       │
    │       └─> BuildSkeletalMesh_RenderData()
    │           │
    │           ├─> Create render sections
    │           ├─> Build GPU skin weight buffers
    │           └─> Optimize bone influences
    │
    └─> ImportAnimation() (if requested)
            │
            └─> Extract animation curves for each bone
```

### 5.2 Skeleton Extraction

**Location:** `FbxSkeletalMeshImport.cpp`

**Phase 1: Find Skeleton Root**
```cpp
FbxNode* FFbxImporter::FindSkeletonRoot(FbxMesh* Mesh)
{
    // Get first skin deformer
    FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin);

    int32 ClusterCount = Skin->GetClusterCount();
    FbxNode* Link = NULL;

    // Find root bone by traversing up from any bone
    for (int32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
    {
        FbxCluster* Cluster = Skin->GetCluster(ClusterId);
        Link = Cluster->GetLink();

        // Traverse up to find root
        while (Link && Link->GetParent() && Link->GetParent()->GetSkeleton())
        {
            Link = Link->GetParent();
        }

        if (Link != NULL)
        {
            break;  // Found skeleton root
        }
    }

    return Link;
}
```

**Phase 2: Build Bone Hierarchy**
```cpp
void FFbxImporter::RecursiveBuildSkeleton(
    FbxNode* Link,
    TArray<FbxNode*>& OutSortedLinks)
{
    // Add this bone to the list
    OutSortedLinks.Add(Link);

    // Recursively process children
    int32 ChildCount = Link->GetChildCount();
    for (int32 ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++)
    {
        FbxNode* Child = Link->GetChild(ChildIndex);

        // Only process skeleton nodes or nodes with mesh (for bone meshes)
        if (Child->GetSkeleton() || Child->GetMesh())
        {
            RecursiveBuildSkeleton(Child, OutSortedLinks);
        }
    }
}
```

**Phase 3: Extract Bone Transforms (Bind Pose)**

This is **critical** - bone transforms must be in the correct coordinate space!

```cpp
FTransform FFbxImporter::GetBoneBindPose(FbxNode* Link, FbxMesh* Mesh)
{
    FbxAMatrix GlobalBindPoseMatrix;

    // Try to get bind pose from FBX pose object
    FbxPose* BindPose = FindBindPose(Scene, Link);
    if (BindPose)
    {
        int32 PoseIndex = BindPose->Find(Link);
        if (PoseIndex >= 0)
        {
            GlobalBindPoseMatrix = BindPose->GetMatrix(PoseIndex);
        }
    }
    else
    {
        // Fallback: get from cluster's transform link matrix
        FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin);
        FbxCluster* Cluster = FindClusterForLink(Skin, Link);
        if (Cluster)
        {
            Cluster->GetTransformLinkMatrix(GlobalBindPoseMatrix);
        }
    }

    // Convert FbxAMatrix to FTransform
    // This applies coordinate system conversion!
    FTransform UnrealTransform = Converter.ConvertTransform(GlobalBindPoseMatrix);

    return UnrealTransform;
}
```

### 5.3 Skinning Data Extraction

**Phase 1: Extract Bone Influences**
```cpp
void FFbxImporter::ExtractSkinning(
    FbxMesh* Mesh,
    FSkeletalMeshImportData& ImportData,
    TArray<FbxNode*>& SortedLinks)
{
    int32 SkinDeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

    for (int32 DeformerIndex = 0; DeformerIndex < SkinDeformerCount; DeformerIndex++)
    {
        FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(DeformerIndex, FbxDeformer::eSkin);

        int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            FbxNode* Link = Cluster->GetLink();

            // Find bone index in sorted bone list
            int32 BoneIndex = SortedLinks.Find(Link);
            if (BoneIndex < 0)
            {
                continue;  // Bone not in skeleton
            }

            // Get influenced control points
            int32* Indices = Cluster->GetControlPointIndices();
            double* Weights = Cluster->GetControlPointWeights();
            int32 IndexCount = Cluster->GetControlPointIndicesCount();

            for (int32 i = 0; i < IndexCount; i++)
            {
                int32 ControlPointIndex = Indices[i];
                float Weight = (float)Weights[i];

                // Store bone influence
                SkeletalMeshImportData::FRawBoneInfluence Influence;
                Influence.VertexIndex = ControlPointIndex;
                Influence.BoneIndex = BoneIndex;
                Influence.Weight = Weight;

                ImportData.Influences.Add(Influence);
            }
        }
    }
}
```

**Phase 2: Normalize Bone Weights**

UE requires bone weights to sum to 1.0 and supports max 8 influences per vertex (typically 4).

```cpp
void NormalizeBoneWeights(FSkeletalMeshImportData& ImportData)
{
    // Group influences by vertex
    TMap<int32, TArray<FRawBoneInfluence>> InfluenceMap;
    for (const FRawBoneInfluence& Influence : ImportData.Influences)
    {
        InfluenceMap.FindOrAdd(Influence.VertexIndex).Add(Influence);
    }

    // Normalize each vertex's weights
    for (auto& Pair : InfluenceMap)
    {
        TArray<FRawBoneInfluence>& Influences = Pair.Value;

        // Sort by weight (descending)
        Influences.Sort([](const FRawBoneInfluence& A, const FRawBoneInfluence& B)
        {
            return A.Weight > B.Weight;
        });

        // Keep only top N influences (typically 4 or 8)
        const int32 MaxInfluences = 8;
        if (Influences.Num() > MaxInfluences)
        {
            Influences.SetNum(MaxInfluences);
        }

        // Normalize weights to sum to 1.0
        float TotalWeight = 0.0f;
        for (const FRawBoneInfluence& Influence : Influences)
        {
            TotalWeight += Influence.Weight;
        }

        if (TotalWeight > SMALL_NUMBER)
        {
            for (FRawBoneInfluence& Influence : Influences)
            {
                Influence.Weight /= TotalWeight;
            }
        }
    }
}
```

### 5.4 SkinControlPointsToPose()

**Location:** `FbxSkeletalMeshImport.cpp` lines 285-479

This function applies skeletal skinning deformation to move vertices from their modeled position to the bind pose (or T-pose).

**Why is this needed?**
- In some workflows, artists model the mesh in a "deformed" pose
- The bind pose (T-pose) is defined by bone transforms
- We need to "un-deform" vertices to match the bind pose
- This is optional (controlled by `bUseT0AsRefPose`)

```cpp
void FFbxImporter::SkinControlPointsToPose(
    FSkeletalMeshImportData& ImportData,
    FbxMesh* FbxMesh,
    FbxShape* FbxShape,
    bool bUseT0)
{
    FbxTime poseTime = bUseT0 ? 0 : FBXSDK_TIME_INFINITE;

    int32 VertexCount = FbxMesh->GetControlPointsCount();

    // Create copy of vertex array
    FbxVector4* VertexArray = new FbxVector4[VertexCount];

    if (FbxShape)
    {
        // Morph target vertices
        FMemory::Memcpy(VertexArray, FbxShape->GetControlPoints(), VertexCount * sizeof(FbxVector4));
    }
    else
    {
        // Base mesh vertices
        FMemory::Memcpy(VertexArray, FbxMesh->GetControlPoints(), VertexCount * sizeof(FbxVector4));
    }

    // Get mesh's global transform
    FbxAMatrix MeshMatrix = ComputeTotalMatrix(FbxMesh->GetNode());

    // Process each skin deformer
    int32 SkinCount = FbxMesh->GetDeformerCount(FbxDeformer::eSkin);

    for (int i = 0; i < SkinCount; ++i)
    {
        FbxSkin* Skin = (FbxSkin*)FbxMesh->GetDeformer(i, FbxDeformer::eSkin);
        int32 ClusterCount = Skin->GetClusterCount();

        // Get link mode (how skinning is computed)
        FbxCluster::ELinkMode lClusterMode = Skin->GetCluster(0)->GetLinkMode();

        // Per-vertex deformation matrices
        TArray<FbxAMatrix> ClusterDeformations;
        ClusterDeformations.AddZeroed(VertexCount);

        TArray<double> ClusterWeights;
        ClusterWeights.AddZeroed(VertexCount);

        if (lClusterMode == FbxCluster::eAdditive)
        {
            for (int j = 0; j < VertexCount; j++)
            {
                ClusterDeformations[j].SetIdentity();
            }
        }

        // Process each bone (cluster)
        for (int j = 0; j < ClusterCount; ++j)
        {
            FbxCluster* Cluster = Skin->GetCluster(j);
            FbxNode* Link = Cluster->GetLink();

            if (!Link)
                continue;

            // Get reference (mesh) transform
            FbxAMatrix lReferenceGlobalInitPosition;
            FbxAMatrix lReferenceGlobalCurrentPosition;
            Cluster->GetTransformMatrix(lReferenceGlobalInitPosition);
            lReferenceGlobalCurrentPosition = MeshMatrix;

            // Apply geometric transform
            FbxAMatrix lReferenceGeometry = GetGeometry(FbxMesh->GetNode());
            lReferenceGlobalInitPosition *= lReferenceGeometry;

            // Get link (bone) transform at bind pose and current pose
            FbxAMatrix lClusterGlobalInitPosition;
            FbxAMatrix lClusterGlobalCurrentPosition;
            Cluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
            lClusterGlobalCurrentPosition = Link->GetScene()->GetAnimationEvaluator()->GetNodeGlobalTransform(Link, poseTime);

            // Compute transformation from reference to link
            FbxAMatrix lClusterRelativeInitPosition = lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;
            FbxAMatrix lClusterRelativeCurrentPositionInverse = lReferenceGlobalCurrentPosition.Inverse() * lClusterGlobalCurrentPosition;

            // Final vertex transformation matrix
            FbxAMatrix lVertexTransformMatrix = lClusterRelativeCurrentPositionInverse * lClusterRelativeInitPosition;

            // Apply to influenced vertices
            int32 lVertexIndexCount = Cluster->GetControlPointIndicesCount();
            int32* lVertexIndices = Cluster->GetControlPointIndices();
            double* lWeights = Cluster->GetControlPointWeights();

            for (int k = 0; k < lVertexIndexCount; ++k)
            {
                int32 lIndex = lVertexIndices[k];

                if (lIndex >= VertexCount)
                    continue;

                double lWeight = lWeights[k];

                if (lWeight == 0.0)
                    continue;

                // Compute weighted influence
                FbxAMatrix lInfluence = lVertexTransformMatrix;
                MatrixScale(lInfluence, lWeight);

                if (lClusterMode == FbxCluster::eAdditive)
                {
                    MatrixAddToDiagonal(lInfluence, 1.0 - lWeight);
                    ClusterDeformations[lIndex] = lInfluence * ClusterDeformations[lIndex];
                    ClusterWeights[lIndex] = 1.0;
                }
                else // eNormalize or eTotalOne
                {
                    MatrixAdd(ClusterDeformations[lIndex], lInfluence);
                    ClusterWeights[lIndex] += lWeight;
                }
            }
        }

        // Apply accumulated deformations to vertices
        for (int j = 0; j < VertexCount; j++)
        {
            FbxVector4 lSrcVertex = VertexArray[j];
            FbxVector4& lDstVertex = VertexArray[j];
            double lWeight = ClusterWeights[j];

            if (lWeight != 0.0)
            {
                // Multiply vertex by deformation matrix
                lDstVertex = ClusterDeformations[j].MultT(lSrcVertex);

                if (lClusterMode == FbxCluster::eNormalize)
                {
                    lDstVertex /= lWeight;
                }
                else if (lClusterMode == FbxCluster::eTotalOne)
                {
                    lSrcVertex *= (1.0 - lWeight);
                    lDstVertex += lSrcVertex;
                }
            }
        }
    }

    // Store transformed vertices back into ImportData
    int32 ExistPointNum = ImportData.Points.Num();
    int32 StartPointIndex = ExistPointNum - VertexCount;

    for(int32 ControlPointsIndex = 0; ControlPointsIndex < VertexCount; ControlPointsIndex++)
    {
        // Apply mesh matrix and convert coordinates
        FbxVector4 FinalPosition = MeshMatrix.MultT(VertexArray[ControlPointsIndex]);
        ImportData.Points[ControlPointsIndex + StartPointIndex] = (FVector3f)Converter.ConvertPos(FinalPosition);
    }

    delete[] VertexArray;
}
```

**Key Insight:** This function demonstrates the complex skinning math that UE uses. Note the final `ConvertPos()` call that applies coordinate system conversion to the deformed vertices.

---

## 6. Key Differences Between StaticMesh and SkeletalMesh Import

### 6.1 Pipeline Comparison

| Aspect | StaticMesh | SkeletalMesh |
|--------|-----------|--------------|
| **Entry Point** | `ImportStaticMesh()` | `ImportSkeletalMesh()` |
| **Primary Data** | Vertex positions, normals, UVs | + Skeleton, bone influences, bind pose |
| **Coordinate Conversion** | Per-vertex in `BuildStaticMeshFromGeometry()` | Per-vertex + per-bone in skeleton extraction |
| **Winding Order** | Automatically reversed by Y-flip | Automatically reversed by Y-flip |
| **Transformation Baking** | Optional (`bTransformVertexToAbsolute`) | Optional (`bTransformVertexToAbsolute`) |
| **Skinning** | N/A | Extracted from FbxSkin/FbxCluster |
| **Animation** | N/A | Separate animation import phase |
| **LODs** | Imported from LODGroup nodes | Imported from LODGroup nodes |
| **Morph Targets** | N/A | Imported from FbxShape |
| **Smoothing Groups** | Imported, used for edge hardness | Imported, used for edge hardness |

### 6.2 Transformation Pipeline Differences

**StaticMesh Vertex Transform:**
```
FBX Control Point
    │
    ├─> Apply Node's TotalMatrix (world transform)
    │
    └─> ConvertPos() [Y-flip]
        │
        └─> Unreal Vertex Position
```

**SkeletalMesh Vertex Transform (without skinning):**
```
FBX Control Point
    │
    ├─> Apply Node's TotalMatrix (world transform)
    │
    └─> ConvertPos() [Y-flip]
        │
        └─> Unreal Vertex Position (+ Bone Indices + Weights)
```

**SkeletalMesh Vertex Transform (with SkinControlPointsToPose):**
```
FBX Control Point
    │
    ├─> For each bone influence:
    │   │
    │   ├─> Compute bone deformation matrix
    │   │   (RefPose^-1 * BindPose * Weight)
    │   │
    │   └─> Accumulate weighted deformation
    │
    ├─> Apply accumulated deformation
    │
    ├─> Apply Node's TotalMatrix (world transform)
    │
    └─> ConvertPos() [Y-flip]
        │
        └─> Unreal Vertex Position (+ Bone Indices + Weights)
```

### 6.3 Bone Transform Handling

**Critical Difference:** Skeletal meshes must handle bone transforms in addition to vertex positions.

```cpp
// For each bone in hierarchy
for (FbxNode* BoneNode : SortedBones)
{
    // Get bind pose matrix
    FbxAMatrix GlobalBindPose = GetBoneBindPoseMatrix(BoneNode);

    // Convert to Unreal coordinate system
    FTransform UnrealBoneTransform = Converter.ConvertTransform(GlobalBindPose);

    // If this is a child bone, make transform relative to parent
    if (ParentBoneIndex >= 0)
    {
        FTransform ParentTransform = RefPoseTransforms[ParentBoneIndex];
        FTransform LocalTransform = UnrealBoneTransform.GetRelativeTransform(ParentTransform);
        RefPoseTransforms[BoneIndex] = LocalTransform;
    }
    else
    {
        // Root bone - use global transform
        RefPoseTransforms[BoneIndex] = UnrealBoneTransform;
    }
}
```

**Important:** Bone transforms undergo the same coordinate conversion as vertices!

---

## 7. Critical Classes and Their Roles

### 7.1 Class Hierarchy

```
┌──────────────────────┐
│    UFactory          │  Unreal's asset factory base class
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│    UFbxFactory       │  FBX import factory
│                      │  • FactoryCreateFile() - main entry point
│                      │  • DetectImportType() - determines mesh type
│                      │  • ConfigureProperties() - shows import dialog
└──────────────────────┘

┌──────────────────────┐
│  FFbxImporter        │  Core importer (Singleton)
│  (Singleton)         │  • Manages FbxManager, FbxScene
│                      │  • Coordinates import phases
│                      │  • ImportStaticMesh()
│                      │  • ImportSkeletalMesh()
└──────────────────────┘

┌──────────────────────┐
│ FFbxDataConverter    │  Static utility class
│ (Static Class)       │  • ConvertPos() - position conversion
│                      │  • ConvertDir() - direction conversion
│                      │  • ConvertTransform() - matrix conversion
│                      │  • Handles coordinate system conversion
└──────────────────────┘

┌──────────────────────┐
│  FbxManager          │  FBX SDK manager (from Autodesk SDK)
│  FbxScene            │  FBX scene container
│  FbxGeometryConverter│  FBX SDK utility (triangulation)
└──────────────────────┘
```

### 7.2 Key Classes Deep Dive

#### UFbxFactory

**File:** `FbxFactory.h`, `FbxFactory.cpp`

**Responsibilities:**
- Entry point for FBX import system
- Handles file type detection
- Shows import options dialog
- Creates final UObject assets

**Key Methods:**
```cpp
class UFbxFactory : public UFactory
{
public:
    // Main import entry point
    virtual UObject* FactoryCreateFile(
        UClass* InClass,
        UObject* InParent,
        FName InName,
        EObjectFlags Flags,
        const FString& Filename,
        const TCHAR* Parms,
        FFeedbackContext* Warn,
        bool& bOutOperationCanceled) override;

    // Detect mesh type (static/skeletal/animation)
    bool DetectImportType(const FString& InFilename);

    // Show import options dialog
    virtual bool ConfigureProperties() override;

    // Recursively import nodes
    UObject* RecursiveImportNode(
        FFbxImporter* FFbxImporter,
        void* Node,
        UObject* InParent,
        FName InName,
        EObjectFlags Flags,
        FScopedSlowTask& SlowTask,
        TArray<UObject*>& OutNewAssets);

private:
    UPROPERTY()
    TObjectPtr<UFbxImportUI> ImportUI;  // Import options
};
```

#### FFbxImporter

**File:** `FbxImporter.h`, `FbxMainImport.cpp`, `FbxStaticMeshImport.cpp`, `FbxSkeletalMeshImport.cpp`

**Responsibilities:**
- Singleton that manages FBX SDK resources
- Loads and converts FBX scenes
- Extracts mesh data
- Handles coordinate system conversion

**Key Methods:**
```cpp
class FFbxImporter
{
public:
    // Singleton access
    static FFbxImporter* GetInstance(bool bDoNotCreate = false);
    static void DeleteInstance();

    // Import phases
    bool OpenFile(FString Filename);
    bool ImportFile(FString Filename, bool bPreventMaterialNameClash = false);
    void ConvertScene();  // CRITICAL - coordinate system conversion
    bool ImportFromFile(const FString& Filename, const FString& Type, bool bPreventMaterialNameClash = false);

    // StaticMesh import
    UStaticMesh* ImportStaticMesh(
        UObject* InParent,
        FbxNode* Node,
        const FName& Name,
        EObjectFlags Flags,
        UFbxStaticMeshImportData* ImportData,
        UStaticMesh* InStaticMesh = NULL,
        int LODIndex = 0,
        const FExistingStaticMeshData* ExistMeshDataPtr = nullptr);

    UStaticMesh* ImportStaticMeshAsSingle(
        UObject* InParent,
        TArray<FbxNode*>& MeshNodeArray,
        const FName InName,
        EObjectFlags Flags,
        UFbxStaticMeshImportData* TemplateImportData,
        UStaticMesh* InStaticMesh,
        int LODIndex = 0,
        const FExistingStaticMeshData* ExistMeshDataPtr = nullptr);

    bool BuildStaticMeshFromGeometry(
        FbxNode* Node,
        UStaticMesh* StaticMesh,
        TArray<FFbxMaterial>& MeshMaterials,
        int32 LODIndex,
        EVertexColorImportOption::Type VertexColorImportOption,
        const TMap<FVector3f, FColor>& ExistingVertexColorData,
        const FColor& VertexOverrideColor);

    void PostImportStaticMesh(
        UStaticMesh* StaticMesh,
        TArray<FbxNode*>& MeshNodeArray,
        int32 LODIndex = 0);

    // SkeletalMesh import
    USkeletalMesh* ImportSkeletalMesh(FImportSkeletalMeshArgs &ImportSkeletalMeshArgs);

    void SkinControlPointsToPose(
        FSkeletalMeshImportData& ImportData,
        FbxMesh* FbxMesh,
        FbxShape* FbxShape,
        bool bUseT0);

    // Animation import
    UAnimSequence* ImportAnimations(
        USkeleton* Skeleton,
        UObject* Outer,
        TArray<FbxNode*>& SortedLinks,
        const FString& Name,
        UFbxAnimSequenceImportData* TemplateImportData,
        TArray<FbxNode*>& NodeArray);

    // Utilities
    void ReleaseScene();
    FBXImportOptions* GetImportOptions() const;
    bool GetSceneInfo(FString Filename, FbxSceneInfo& SceneInfo, bool bPreventMaterialNameClash = false);

private:
    // FBX SDK objects
    FbxScene* Scene;
    FbxGeometryConverter* GeometryConverter;
    FbxManager* SdkManager;
    FbxImporter* Importer;

    // Import state
    ImportPhase CurPhase;
    FBXImportOptions* ImportOptions;

    // Data converters
    FFbxDataConverter Converter;
};
```

#### FFbxDataConverter

**File:** `FbxImporter.h`, `FbxUtilsImport.cpp`

**Responsibilities:**
- Static utility class for coordinate system conversion
- Converts FBX vectors/matrices to Unreal coordinate space
- Handles Y-axis flip for RH→LH conversion

**Key Methods:**
```cpp
class FFbxDataConverter
{
public:
    // Position conversion (applies Y-flip)
    static FVector ConvertPos(FbxVector4 Vector);

    // Direction conversion (applies Y-flip)
    static FVector ConvertDir(FbxVector4 Vector);

    // Euler angle conversion
    static FRotator ConvertEuler(FbxDouble3 Euler);

    // Scale conversion (no flip)
    static FVector ConvertScale(FbxDouble3 Vector);
    static FVector ConvertScale(FbxVector4 Vector);

    // Quaternion conversion
    static FRotator ConvertRotation(FbxQuaternion Quaternion);
    static FQuat ConvertRotToQuat(FbxQuaternion Quaternion);

    // Transform conversion
    static FTransform ConvertTransform(FbxAMatrix Matrix);

    // Matrix conversion (special handling for Y-axis row)
    static FMatrix ConvertMatrix(const FbxAMatrix& Matrix);
    static FbxAMatrix ConvertMatrix(const FMatrix& Matrix);

    // Distance conversion (cm)
    static float ConvertDist(FbxDouble Distance);

    // Color conversion (linear to sRGB)
    static FColor ConvertColor(FbxDouble3 Color);

    // Axis conversion matrices (set during ConvertScene)
    static void SetJointPostConversionMatrix(FbxAMatrix ConversionMatrix);
    static const FbxAMatrix& GetJointPostConversionMatrix();

    static void SetAxisConversionMatrix(FbxAMatrix ConversionMatrix);
    static const FbxAMatrix& GetAxisConversionMatrix();
    static const FbxAMatrix& GetAxisConversionMatrixInv();

private:
    static FbxAMatrix JointPostConversionMatrix;
    static FbxAMatrix AxisConversionMatrix;
    static FbxAMatrix AxisConversionMatrixInv;
};
```

**Implementation Details:**

```cpp
// Position conversion - THE CRITICAL Y-FLIP
FVector FFbxDataConverter::ConvertPos(FbxVector4 Vector)
{
    FVector Out;
    Out[0] =  Vector[0];   // X unchanged
    Out[1] = -Vector[1];   // Y FLIPPED (RH → LH)
    Out[2] =  Vector[2];   // Z unchanged
    UE::Fbx::Private::VerifyFiniteValue(Out);
    return Out;
}

// Direction conversion - same as position
FVector FFbxDataConverter::ConvertDir(FbxVector4 Vector)
{
    FVector Out;
    Out[0] =  Vector[0];
    Out[1] = -Vector[1];   // Y FLIPPED
    Out[2] =  Vector[2];
    UE::Fbx::Private::VerifyFiniteValue(Out);
    return Out;
}

// Matrix conversion - complex row/column flipping
FMatrix FFbxDataConverter::ConvertMatrix(const FbxAMatrix& Matrix)
{
    FMatrix UEMatrix;

    for (int i = 0; i < 4; ++i)
    {
        const FbxVector4 Row = Matrix.GetRow(i);
        if(i == 1)  // Y-axis row gets special treatment
        {
            UEMatrix.M[i][0] = -Row[0];  // Negate X
            UEMatrix.M[i][1] =  Row[1];  // Keep Y
            UEMatrix.M[i][2] = -Row[2];  // Negate Z
            UEMatrix.M[i][3] = -Row[3];  // Negate W
        }
        else  // X, Z, W rows
        {
            UEMatrix.M[i][0] =  Row[0];
            UEMatrix.M[i][1] = -Row[1];  // Negate Y
            UEMatrix.M[i][2] =  Row[2];
            UEMatrix.M[i][3] =  Row[3];
        }
    }
    UE::Fbx::Private::VerifyFiniteValue(UEMatrix);
    return UEMatrix;
}
```

---

## 8. Important Code Patterns and Best Practices

### 8.1 ComputeTotalMatrix Pattern

**Purpose:** Get the complete world-space transformation for an FBX node

```cpp
FbxAMatrix FFbxImporter::ComputeTotalMatrix(FbxNode* Node)
{
    FbxAMatrix Geometry;
    FbxVector4 Translation, Rotation, Scaling;

    // Get geometric transformations (stored separately in FBX)
    Translation = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    Rotation = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    Scaling = Node->GetGeometricScaling(FbxNode::eSourcePivot);
    Geometry.SetT(Translation);
    Geometry.SetR(Rotation);
    Geometry.SetS(Scaling);

    // Get global transform at current time
    FbxAMatrix& GlobalTransform = Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(Node);

    // Combine: Total = Global * Geometry
    FbxAMatrix TotalMatrix = GlobalTransform * Geometry;

    return TotalMatrix;
}
```

**Why this matters:**
- FBX separates "geometric" transforms from "node" transforms
- Geometric transforms are baked into the mesh and don't animate
- Must combine both to get correct vertex positions

### 8.2 Triangulation Pattern

**All meshes must be triangulated before processing:**

```cpp
if (!Mesh->IsTriangleMesh())
{
    UE_LOG(LogFbx, Display, TEXT("Triangulating mesh %s"), UTF8_TO_TCHAR(Mesh->GetName()));

    const bool bReplace = true;  // Replace original mesh
    FbxNodeAttribute* ConvertedNode = GeometryConverter->Triangulate(Mesh, bReplace);

    if(ConvertedNode != NULL && ConvertedNode->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        Mesh = (FbxMesh*)ConvertedNode;
    }
    else
    {
        // Triangulation failed - log error and abort
        AddTokenizedErrorMessage(...);
        return false;
    }
}
```

**Why this matters:**
- Unreal only supports triangle meshes
- FBX can contain n-gons (polygons with >3 vertices)
- Must triangulate before extracting vertices
- Triangulation can change material ordering!

### 8.3 Material Extraction Pattern

```cpp
void FFbxImporter::FindOrImportMaterialsFromNode(
    FbxNode* Node,
    TArray<UMaterialInterface*>& OutMaterials,
    TArray<FString>& UVSets,
    bool bForSkeletalMesh)
{
    int32 MaterialCount = Node->GetMaterialCount();

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
    {
        FbxSurfaceMaterial* FbxMaterial = Node->GetMaterial(MaterialIndex);

        // Check if already imported
        UMaterialInterface* UnrealMaterial = ImportedMaterialData.GetUnrealMaterial(*FbxMaterial);

        if (!UnrealMaterial && ImportOptions->bImportMaterials)
        {
            // Import new material
            UnrealMaterial = CreateUnrealMaterial(FbxMaterial, UVSets, bForSkeletalMesh);

            // Cache for reuse
            if (UnrealMaterial)
            {
                ImportedMaterialData.AddImportedMaterial(*FbxMaterial, *UnrealMaterial);
            }
        }

        if (!UnrealMaterial)
        {
            // Fallback to default material
            UnrealMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        }

        OutMaterials.Add(UnrealMaterial);
    }
}
```

**Why this matters:**
- Materials should not be duplicated
- Cache lookup prevents re-import
- Always have a fallback material

### 8.4 LOD Group Handling

```cpp
FbxNode* FFbxImporter::FindLODGroupNode(FbxNode* NodeLodGroup, int32 LodIndex, FbxNode* NodeToFind)
{
    if (!NodeLodGroup || !NodeLodGroup->GetNodeAttribute())
        return nullptr;

    if (NodeLodGroup->GetNodeAttribute()->GetAttributeType() != FbxNodeAttribute::eLODGroup)
        return nullptr;

    // LOD groups have display levels
    // Each level corresponds to a child node
    for (int32 ChildIndex = 0; ChildIndex < NodeLodGroup->GetChildCount(); ChildIndex++)
    {
        FbxNode* ChildNode = NodeLodGroup->GetChild(ChildIndex);

        // Check if this child is at the requested LOD level
        if (GetLODLevel(ChildNode, NodeLodGroup) == LodIndex)
        {
            // If NodeToFind specified, check for match
            if (NodeToFind)
            {
                if (ChildNode == NodeToFind || IsChildOfNode(NodeToFind, ChildNode))
                {
                    return ChildNode;
                }
            }
            else
            {
                // Return first mesh node at this LOD level
                if (ChildNode->GetMesh())
                {
                    return ChildNode;
                }
            }
        }
    }

    return nullptr;
}
```

**Why this matters:**
- FBX LOD groups organize meshes by detail level
- Must extract correct mesh for each LOD
- LOD structure varies by DCC tool

### 8.5 Error Handling and Validation

```cpp
// Always validate control point indices
int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointsCount)
{
    AddTokenizedErrorMessage(FTokenizedMessage::Create(
        EMessageSeverity::Error,
        FText::Format(LOCTEXT("InvalidControlPoint", "Invalid control point index {0}"), ControlPointIndex)
    ), FFbxErrors::Generic_Mesh_InvalidIndex);
    continue;
}

// Always verify finite values
FVector Position = Converter.ConvertPos(FbxPosition);
if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z))
{
    UE_LOG(LogFbx, Error, TEXT("Import mesh has infinite vertex position"));
    Position = FVector::ZeroVector;  // Clamp to safe value
}

// Always check for degenerate triangles
float Area = TriangleArea(V0, V1, V2);
if (Area < THRESH_POINTS_ARE_SAME)
{
    AddTokenizedErrorMessage(FTokenizedMessage::Create(
        EMessageSeverity::Warning,
        FText::Format(LOCTEXT("DegenerateTriangle", "Mesh has degenerate triangle at polygon {0}"), PolygonIndex)
    ), FFbxErrors::StaticMesh_AllTrianglesDegenerate);

    if (ImportOptions->bRemoveDegenerates)
    {
        continue;  // Skip this triangle
    }
}
```

---

## Summary and Key Takeaways

### Critical Insights

1. **Two-Stage Coordinate Conversion:**
   - Stage 1: FBX scene converted to RH Z-up by FbxAxisSystem::ConvertScene()
   - Stage 2: Per-vertex Y-flip in ConvertPos() converts RH to LH
   - This automatically reverses winding order (CCW → CW)

2. **No Explicit Winding Order Code:**
   - UE does NOT have explicit code to reverse triangle indices
   - The Y-flip in coordinate conversion naturally reverses winding
   - This is elegant but non-obvious!

3. **Singleton Architecture:**
   - FFbxImporter is a singleton managing FBX SDK resources
   - Prevents multiple FbxManager instances
   - Maintains state across import phases

4. **Phase-Based Import:**
   - NOTSTARTED → FILEOPENED → IMPORTED → FIXEDANDCONVERTED
   - Each phase has specific responsibilities
   - ConvertScene() is the critical coordinate transformation phase

5. **Skeletal vs Static Differences:**
   - Skeletal meshes add skeleton extraction and skinning data
   - Both use same coordinate conversion (ConvertPos/ConvertDir)
   - Skeletal meshes have optional bind pose deformation (SkinControlPointsToPose)

6. **Matrix Conversion Complexity:**
   - Matrix conversion has special handling for Y-axis row
   - Row 1: flip X, Z, W components (keep Y)
   - Other rows: flip only Y component
   - This ensures proper handedness for transformations

### Implementation Recommendations for Mundi

Based on UE5's architecture, here are recommendations for Mundi's FBX importer:

1. **Adopt Two-Stage Conversion:**
   - Use FbxAxisSystem::ConvertScene() for scene-level conversion
   - Apply Y-flip in your ConvertPos() function
   - This eliminates need for explicit winding order reversal

2. **Use Triangulation:**
   - Always call FbxGeometryConverter::Triangulate() before processing
   - Check Mesh->IsTriangleMesh() first to avoid redundant work

3. **Handle Geometric Transforms:**
   - Always combine Node transform + Geometric transform
   - Use ComputeTotalMatrix() pattern

4. **Validate All Data:**
   - Check for infinite/NaN values
   - Validate control point indices
   - Remove degenerate triangles (optional)

5. **Material Caching:**
   - Cache imported materials to avoid duplicates
   - Always have a fallback default material

6. **Error Messages:**
   - Use descriptive error messages with context
   - Include polygon/vertex indices in error messages
   - Separate warnings from errors

---

## Appendix: File Structure

### Key Files in UE5 FBX Pipeline

```
Engine/Source/Editor/UnrealEd/
├── Public/
│   └── FbxImporter.h                  (3500+ lines - main importer interface)
│
├── Classes/Factories/
│   └── FbxFactory.h                   (92 lines - factory entry point)
│
└── Private/Fbx/
    ├── FbxMainImport.cpp              (3498 lines - core import logic)
    ├── FbxStaticMeshImport.cpp        (3032 lines - static mesh pipeline)
    ├── FbxSkeletalMeshImport.cpp      (4946 lines - skeletal mesh pipeline)
    ├── FbxUtilsImport.cpp             (coordinate conversion utilities)
    ├── FbxFactory.cpp                 (factory implementation)
    ├── FbxMaterialImport.cpp          (material import)
    ├── FbxSceneImportFactory.cpp      (scene import)
    └── [many other FBX-related files]
```

### Total Lines of Code

- **FbxImporter.h:** ~3500 lines
- **FbxMainImport.cpp:** ~3500 lines
- **FbxStaticMeshImport.cpp:** ~3000 lines
- **FbxSkeletalMeshImport.cpp:** ~5000 lines
- **Total Core Pipeline:** ~15,000 lines

**Conclusion:** The UE5 FBX import pipeline is a massive, complex system. This document captures the essential architecture and workflows.

---

**Document Version:** 1.0
**Last Updated:** 2025-11-10
**Author:** Claude (Anthropic AI Assistant)
**Analysis Source:** Unreal Engine 5 Source Code
