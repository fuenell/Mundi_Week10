# Unreal Engine 5 FBX Import Analysis

**Complete Implementation Guide for FBX Import**

This document provides an exhaustive analysis of how Unreal Engine 5 handles FBX import, covering coordinate system conversion, vertex transformation, winding order handling, and the complete import pipeline for both static and skeletal meshes.

---

## Table of Contents

1. [Overview](#overview)
2. [Coordinate System Conversion](#coordinate-system-conversion)
3. [Matrix Computation](#matrix-computation)
4. [Winding Order Rules](#winding-order-rules)
5. [Static Mesh Import Pipeline](#static-mesh-import-pipeline)
6. [Skeletal Mesh Import Pipeline](#skeletal-mesh-import-pipeline)
7. [Bind Pose and Skinning](#bind-pose-and-skinning)
8. [Implementation Checklist](#implementation-checklist)

---

## Overview

Unreal Engine's FBX import system is designed to convert assets from various DCC tools (Maya, Max, Blender) into UE's left-handed Z-up coordinate system while preserving the intended appearance and handedness.

**Key Source Files:**
- `FbxMainImport.cpp` - Core conversion logic, coordinate system handling
- `FbxStaticMeshImport.cpp` - Static mesh import (rigid geometry)
- `FbxSkeletalMeshImport.cpp` - Skeletal mesh import (skinned geometry)

---

## Coordinate System Conversion

### ConvertScene Function

**Location:** `FbxMainImport.cpp`, lines 1499-1580

**Purpose:** Converts the entire FBX scene to Unreal's coordinate system (Z-up, Left-Handed, -Y forward).

```cpp
void FFbxImporter::ConvertScene()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FFbxImporter::ConvertScene);

    // Save original file axis system and unit system
    FileAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    FileUnitSystem = Scene->GetGlobalSettings().GetSystemUnit();

    FbxAMatrix AxisConversionMatrix;
    AxisConversionMatrix.SetIdentity();

    FbxAMatrix JointOrientationMatrix;
    JointOrientationMatrix.SetIdentity();

    if (GetImportOptions()->bConvertScene)
    {
        // UE uses -Y as forward axis here when importing
        // This mimics Maya/Max behavior where a model facing +X
        // will also face +X in the engine
        // The only issue is hand flipping: Max/Maya is RHS but UE is LHS
        FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
        FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
        FbxAxisSystem::EFrontVector FrontVector =
            (FbxAxisSystem::EFrontVector) - FbxAxisSystem::eParityOdd;

        if (GetImportOptions()->bForceFrontXAxis)
        {
            FrontVector = FbxAxisSystem::eParityEven;
        }

        FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);
        FbxAxisSystem SourceSetup = Scene->GetGlobalSettings().GetAxisSystem();

        if (SourceSetup != UnrealImportAxis)
        {
            FbxRootNodeUtility::RemoveAllFbxRoots(Scene);
            UnrealImportAxis.ConvertScene(Scene);

            FbxAMatrix SourceMatrix;
            SourceSetup.GetMatrix(SourceMatrix);
            FbxAMatrix UE4Matrix;
            UnrealImportAxis.GetMatrix(UE4Matrix);
            AxisConversionMatrix = SourceMatrix.Inverse() * UE4Matrix;

            if (GetImportOptions()->bForceFrontXAxis)
            {
                JointOrientationMatrix.SetR(FbxVector4(-90.0, -90.0, 0.0));
            }
        }
    }

    FFbxDataConverter::SetJointPostConversionMatrix(JointOrientationMatrix);
    FFbxDataConverter::SetAxisConversionMatrix(AxisConversionMatrix);

    // Convert scene units to centimeters (UE base unit)
    if (GetImportOptions()->bConvertSceneUnit &&
        Scene->GetGlobalSettings().GetSystemUnit() != FbxSystemUnit::cm)
    {
        FbxSystemUnit::cm.ConvertScene(Scene);
    }

    // Reset animation evaluation cache after transform changes
    Scene->GetAnimationEvaluator()->Reset();
}
```

**Key Insights:**

1. **Target System:** Z-Up, -Y Forward (ParityOdd), Right-Handed (temporarily)
   - The "right-handed" setting is misleading - UE converts to left-handed later
   - The -Y forward mimics DCC tool behavior for intuitive imports

2. **Axis Conversion Matrix:** Computed as `SourceMatrix.Inverse() * UE4Matrix`
   - Transforms from source coordinate system to UE coordinate system
   - Applied to vertices, normals, and tangents

3. **Unit Conversion:** Always converts to centimeters (UE's base unit)

---

## Matrix Computation

### ComputeTotalMatrix

**Location:** `FbxMainImport.cpp`, lines 2060-2098

**Purpose:** Computes the complete transformation matrix for a mesh node, including geometric transforms and pivot offsets.

```cpp
FbxAMatrix FFbxImporter::ComputeTotalMatrix(FbxNode* Node)
{
    FbxAMatrix Geometry;
    FbxVector4 Translation, Rotation, Scaling;

    // Get geometric transforms (3ds Max uses these for mesh offsets)
    Translation = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    Rotation = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    Scaling = Node->GetGeometricScaling(FbxNode::eSourcePivot);
    Geometry.SetT(Translation);
    Geometry.SetR(Rotation);
    Geometry.SetS(Scaling);

    // Get global transform (includes pivot offsets and pre/post rotations)
    FbxAMatrix& GlobalTransform = Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(Node);

    // Can only bake pivot if not transforming vertex to absolute position
    if (!ImportOptions->bTransformVertexToAbsolute)
    {
        if (ImportOptions->bBakePivotInVertex)
        {
            FbxAMatrix PivotGeometry;
            FbxVector4 RotationPivot = Node->GetRotationPivot(FbxNode::eSourcePivot);
            FbxVector4 FullPivot;
            FullPivot[0] = -RotationPivot[0];
            FullPivot[1] = -RotationPivot[1];
            FullPivot[2] = -RotationPivot[2];
            PivotGeometry.SetT(FullPivot);
            Geometry = Geometry * PivotGeometry;
        }
        else
        {
            // No vertex transform and no bake pivot - mesh as-is
            Geometry.SetIdentity();
        }
    }

    // Always add geometric transform (3ds Max offset to local transform)
    FbxAMatrix TotalMatrix = ImportOptions->bTransformVertexToAbsolute ?
        GlobalTransform * Geometry : Geometry;

    return TotalMatrix;
}
```

**For Skeletal Meshes:**

**Location:** `FbxMainImport.cpp`, lines 2048-2058

```cpp
FbxAMatrix FFbxImporter::ComputeSkeletalMeshTotalMatrix(FbxNode* Node, FbxNode *RootSkeletalNode)
{
    if (ImportOptions->bImportScene &&
        !ImportOptions->bTransformVertexToAbsolute &&
        RootSkeletalNode != nullptr &&
        RootSkeletalNode != Node)
    {
        // Transform mesh relative to skeleton root
        FbxAMatrix GlobalTransform =
            Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(Node);
        FbxAMatrix GlobalSkeletalMeshRootTransform =
            Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(RootSkeletalNode);
        FbxAMatrix TotalMatrix =
            GlobalSkeletalMeshRootTransform.Inverse() * GlobalTransform;
        return TotalMatrix;
    }
    return ComputeTotalMatrix(Node);
}
```

**Key Insights:**

1. **Geometric Transform:** Mesh-level offset (3ds Max specific)
2. **Global Transform:** Node's world transform including hierarchy
3. **Pivot Baking:** Optional, bakes rotation pivot into vertices
4. **Skeletal Relative:** Skeletal meshes transform relative to skeleton root

---

## Winding Order Rules

### IsOddNegativeScale

**Location:** `FbxMainImport.cpp`, lines 2100-2110

**Purpose:** Determines if winding order needs reversal based on negative scale.

```cpp
bool FFbxImporter::IsOddNegativeScale(FbxAMatrix& TotalMatrix)
{
    FbxVector4 Scale = TotalMatrix.GetS();
    int32 NegativeNum = 0;

    if (Scale[0] < 0) NegativeNum++;
    if (Scale[1] < 0) NegativeNum++;
    if (Scale[2] < 0) NegativeNum++;

    return NegativeNum == 1 || NegativeNum == 3;
}
```

**Mathematical Reasoning:**

A transformation matrix changes handedness (and thus winding order) when its determinant is negative. For a scale-only transformation:

```
det(Scale Matrix) = ScaleX * ScaleY * ScaleZ
```

- **0 negative scales:** `det > 0` → No handedness change → Keep winding order
- **1 negative scale:** `det < 0` → Handedness changed → **REVERSE winding order**
- **2 negative scales:** `det > 0` → No handedness change → Keep winding order
- **3 negative scales:** `det < 0` → Handedness changed → **REVERSE winding order**

**Result:** Odd number of negative scales (1 or 3) = reverse winding order

---

## Static Mesh Import Pipeline

### Vertex Position Transformation

**Location:** `FbxStaticMeshImport.cpp`, lines 728-743

```cpp
// Compute transformation matrices
FbxAMatrix TotalMatrix;
FbxAMatrix TotalMatrixForNormal;
TotalMatrix = ComputeTotalMatrix(Node);
TotalMatrixForNormal = TotalMatrix.Inverse();
TotalMatrixForNormal = TotalMatrixForNormal.Transpose();

int32 VertexCount = Mesh->GetControlPointsCount();
bool OddNegativeScale = IsOddNegativeScale(TotalMatrix);

// Fill the vertex array
MeshDescription->ReserveNewVertices(VertexCount);
for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
{
    int32 RealVertexIndex = VertexOffset + VertexIndex;
    FbxVector4 FbxPosition = Mesh->GetControlPoints()[VertexIndex];

    // Transform position by total matrix
    FbxPosition = TotalMatrix.MultT(FbxPosition);

    // Convert to UE coordinate system
    FVector3f VertexPosition = (FVector3f)Converter.ConvertPos(FbxPosition);

    FVertexID AddedVertexId = MeshDescription->CreateVertex();
    VertexPositions[AddedVertexId] = VertexPosition;
}
```

**Key Steps:**

1. **TotalMatrix:** Position transformation matrix
2. **TotalMatrixForNormal:** `(TotalMatrix^-1)^T` for normal transformation
3. **MultT:** Multiply and transpose (transform point by matrix)
4. **ConvertPos:** Convert FBX coordinate system to UE coordinate system

### Triangle Building (NO Winding Order Reversal!)

**Location:** `FbxStaticMeshImport.cpp`, lines 893-1165

**CRITICAL DISCOVERY:** Static meshes **do not reverse vertex order** based on `OddNegativeScale`. The winding order is preserved as-is from FBX!

```cpp
for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
{
    int32 PolygonVertexCount = Mesh->GetPolygonSize(PolygonIndex);

    // Create vertex instances in FBX order (0, 1, 2)
    for (int32 CornerIndex = 0; CornerIndex < PolygonVertexCount; CornerIndex++)
    {
        const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
        const FVertexID VertexID(VertexOffset + ControlPointIndex);

        // Create vertex instance
        FVertexInstanceID VertexInstanceID(VertexInstanceIndex);
        CornerInstanceIDs[CornerIndex] = VertexInstanceID;

        FVertexInstanceID AddedVertexInstanceId =
            MeshDescription->CreateVertexInstance(VertexID);

        // ... Load UVs, normals, tangents, colors ...
    }

    // Create triangle with vertices in order [0, 1, 2]
    // NO reversal even if OddNegativeScale is true!
    const FTriangleID NewTriangleID =
        MeshDescription->CreateTriangle(PolygonGroupID, CornerInstanceIDs, &NewEdgeIDs);
}
```

**Why No Reversal?**

UE5 static mesh import relies on the FBX SDK's `ConvertScene()` to handle coordinate system conversion. The FBX SDK internally adjusts vertex winding when converting between coordinate systems, so UE doesn't need to reverse it manually.

### Normal and Tangent Transformation

**Location:** `FbxStaticMeshImport.cpp`, lines 973-1008

```cpp
if (LayerElementNormal)
{
    int NormalMapIndex = (NormalMappingMode == FbxLayerElement::eByControlPoint) ?
        ControlPointIndex : RealFbxVertexIndex;
    int NormalValueIndex = (NormalReferenceMode == FbxLayerElement::eDirect) ?
        NormalMapIndex : LayerElementNormal->GetIndexArray().GetAt(NormalMapIndex);

    FbxVector4 TempValue = LayerElementNormal->GetDirectArray().GetAt(NormalValueIndex);

    // Transform normal by inverse-transpose matrix
    TempValue = TotalMatrixForNormal.MultT(TempValue);

    // Convert and normalize
    FVector3f TangentZ = (FVector3f)Converter.ConvertDir(TempValue);
    VertexInstanceNormals[AddedVertexInstanceId] = TangentZ.GetSafeNormal();

    // Tangent transformation
    if (bHasNTBInformation)
    {
        TempValue = LayerElementTangent->GetDirectArray().GetAt(TangentValueIndex);
        TempValue = TotalMatrixForNormal.MultT(TempValue);
        FVector3f TangentX = (FVector3f)Converter.ConvertDir(TempValue);
        VertexInstanceTangents[AddedVertexInstanceId] = TangentX.GetSafeNormal();

        // Binormal (negated for left-handed system)
        TempValue = LayerElementBinormal->GetDirectArray().GetAt(BinormalValueIndex);
        TempValue = TotalMatrixForNormal.MultT(TempValue);
        FVector3f TangentY = (FVector3f)-Converter.ConvertDir(TempValue);

        // Compute binormal sign for tangent space handedness
        VertexInstanceBinormalSigns[AddedVertexInstanceId] =
            GetBasisDeterminantSign(
                (FVector)TangentX.GetSafeNormal(),
                (FVector)TangentY.GetSafeNormal(),
                (FVector)TangentZ.GetSafeNormal()
            );
    }
}
```

**Normal Transform Math:**

Normals must be transformed by the inverse-transpose of the transformation matrix:

```
Normal' = (M^-1)^T * Normal
```

This preserves perpendicularity to the surface under non-uniform scaling.

### UV Coordinate Handling

**Location:** `FbxStaticMeshImport.cpp`, lines 916-930

```cpp
for (int32 UVLayerIndex = 0; UVLayerIndex < FBXUVs.UniqueUVCount; UVLayerIndex++)
{
    FVector2f FinalUVVector(0.0f, 0.0f);
    if (FBXUVs.LayerElementUV[UVLayerIndex] != NULL)
    {
        int UVMapIndex = (FBXUVs.UVMappingMode[UVLayerIndex] == FbxLayerElement::eByControlPoint)
            ? ControlPointIndex : RealFbxVertexIndex;
        int32 UVIndex = (FBXUVs.UVReferenceMode[UVLayerIndex] == FbxLayerElement::eDirect) ?
            UVMapIndex : FBXUVs.LayerElementUV[UVLayerIndex]->GetIndexArray().GetAt(UVMapIndex);

        FbxVector2 UVVector = FBXUVs.LayerElementUV[UVLayerIndex]->GetDirectArray().GetAt(UVIndex);

        FinalUVVector.X = static_cast<float>(UVVector[0]);
        FinalUVVector.Y = 1.f - static_cast<float>(UVVector[1]);   // Flip Y for DirectX
    }
    VertexInstanceUVs.Set(AddedVertexInstanceId, UVLayerIndex, FinalUVVector);
}
```

**Key Points:**

1. **Y-Flip:** UVs have Y-coordinate flipped (1.0 - Y) for DirectX/UE convention
2. **Mapping Modes:**
   - `eByControlPoint`: One UV per vertex
   - `eByPolygonVertex`: One UV per vertex instance (corner)
3. **Reference Modes:**
   - `eDirect`: Use index directly
   - `eIndexToDirect`: Use index array for indirection

---

## Skeletal Mesh Import Pipeline

### Vertex Position Transformation (Initial)

**Location:** `FbxSkeletalMeshImport.cpp`, lines 1519-1548

```cpp
bool FFbxImporter::FillSkeletalMeshImportPoints(
    FSkeletalMeshImportData* OutData,
    FbxNode* RootNode,
    FbxNode* Node,
    FbxShape* FbxShape)
{
    FbxMesh* FbxMesh = Node->GetMesh();

    const int32 ControlPointsCount = FbxMesh->GetControlPointsCount();
    const int32 ExistPointNum = OutData->Points.Num();
    OutData->Points.AddUninitialized(ControlPointsCount);

    // Compute transformation matrix
    FbxAMatrix TotalMatrix = ComputeSkeletalMeshTotalMatrix(Node, RootNode);

    // Transform each vertex
    for (int32 ControlPointsIndex = 0; ControlPointsIndex < ControlPointsCount; ControlPointsIndex++)
    {
        FbxVector4 Position;
        if (FbxShape)
        {
            Position = FbxShape->GetControlPoints()[ControlPointsIndex];
        }
        else
        {
            Position = FbxMesh->GetControlPoints()[ControlPointsIndex];
        }

        // Transform by total matrix
        FbxVector4 FinalPosition = TotalMatrix.MultT(Position);

        // Convert to UE coordinate system
        FVector ConvertedPosition = Converter.ConvertPos(FinalPosition);

        OutData->Points[ExistPointNum + ControlPointsIndex] = (FVector3f)ConvertedPosition;
    }

    return true;
}
```

### Triangle Building with Winding Order Reversal

**Location:** `FbxSkeletalMeshImport.cpp`, lines 3601-3756

**CRITICAL:** Skeletal meshes **DO reverse vertex order** based on `OddNegativeScale`!

```cpp
bool OddNegativeScale = IsOddNegativeScale(TotalMatrix);

for (int32 TriangleIndex = ExistFaceNum, LocalIndex = 0;
     TriangleIndex < ExistFaceNum + TriangleCount;
     TriangleIndex++, LocalIndex++)
{
    SkeletalMeshImportData::FTriangle& Triangle = ImportData.Faces[TriangleIndex];

    // Process normals and tangents
    for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
    {
        // REVERSE vertex order if odd negative scale
        int32 UnrealVertexIndex = OddNegativeScale ? 2 - VertexIndex : VertexIndex;

        int32 ControlPointIndex = Mesh->GetPolygonVertex(LocalIndex, VertexIndex);

        // Transform and store normals
        if (bHasNormalInformation)
        {
            const int32 TmpIndex = LocalIndex * 3 + VertexIndex;
            const int32 NormalMapIndex = (NormalMappingMode == FbxLayerElement::eByControlPoint) ?
                ControlPointIndex : TmpIndex;
            const int32 NormalValueIndex = (NormalReferenceMode == FbxLayerElement::eDirect) ?
                NormalMapIndex : LayerElementNormal->GetIndexArray().GetAt(NormalMapIndex);

            // Transform tangent
            if (bHasTangentInformation)
            {
                FbxVector4 TempValue = LayerElementTangent->GetDirectArray().GetAt(TangentValueIndex);
                TempValue = TotalMatrixForNormal.MultT(TempValue);
                Triangle.TangentX[UnrealVertexIndex] = (FVector3f)Converter.ConvertDir(TempValue);
                Triangle.TangentX[UnrealVertexIndex].Normalize();

                // Binormal (negated)
                TempValue = LayerElementBinormal->GetDirectArray().GetAt(BinormalValueIndex);
                TempValue = TotalMatrixForNormal.MultT(TempValue);
                Triangle.TangentY[UnrealVertexIndex] = (FVector3f)-Converter.ConvertDir(TempValue);
                Triangle.TangentY[UnrealVertexIndex].Normalize();
            }

            // Normal
            TempValue = LayerElementNormal->GetDirectArray().GetAt(NormalValueIndex);
            TempValue = TotalMatrixForNormal.MultT(TempValue);
            Triangle.TangentZ[UnrealVertexIndex] = (FVector3f)Converter.ConvertDir(TempValue);
            Triangle.TangentZ[UnrealVertexIndex].Normalize();
        }
    }

    // Build wedges (vertex instances)
    for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
    {
        // REVERSE vertex order if odd negative scale
        int32 UnrealVertexIndex = OddNegativeScale ? 2 - VertexIndex : VertexIndex;

        TmpWedges[UnrealVertexIndex].MatIndex = Triangle.MatIndex;
        TmpWedges[UnrealVertexIndex].VertexIndex =
            ExistPointNum + Mesh->GetPolygonVertex(LocalIndex, VertexIndex);
        TmpWedges[UnrealVertexIndex].Color = FColor::White;
    }

    // UVs with reversal
    for (uint32 UVLayerIndex = 0; UVLayerIndex < UniqueUVCount; UVLayerIndex++)
    {
        if (LayerElementUV[UVLayerIndex] != NULL)
        {
            for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
            {
                // REVERSE vertex order if odd negative scale
                int32 UnrealVertexIndex = OddNegativeScale ? 2 - VertexIndex : VertexIndex;

                int32 lControlPointIndex = Mesh->GetPolygonVertex(LocalIndex, VertexIndex);
                int32 UVMapIndex = (UVMappingMode[UVLayerIndex] == FbxLayerElement::eByControlPoint) ?
                    lControlPointIndex : LocalIndex * 3 + VertexIndex;
                int32 UVIndex = (UVReferenceMode[UVLayerIndex] == FbxLayerElement::eDirect) ?
                    UVMapIndex : LayerElementUV[UVLayerIndex]->GetIndexArray().GetAt(UVMapIndex);
                FbxVector2 UVVector = LayerElementUV[UVLayerIndex]->GetDirectArray().GetAt(UVIndex);

                TmpWedges[UnrealVertexIndex].UVs[UVLayerIndex].X = static_cast<float>(UVVector[0]);
                TmpWedges[UnrealVertexIndex].UVs[UVLayerIndex].Y = 1.f - static_cast<float>(UVVector[1]);
            }
        }
    }

    // Store wedges in triangle (now in correct winding order)
    for (int32 CornerIndex = 0; CornerIndex < 3; CornerIndex++)
    {
        ImportData.Wedges.Add(TmpWedges[CornerIndex]);
    }

    Triangle.WedgeIndex[0] = ExistWedgesNum + 0;
    Triangle.WedgeIndex[1] = ExistWedgesNum + 1;
    Triangle.WedgeIndex[2] = ExistWedgesNum + 2;
    ExistWedgesNum += 3;
}
```

**Winding Order Transformation:**

```
FBX Vertices: [0, 1, 2]

If OddNegativeScale == false:
    UnrealVertexIndex[0] = 0
    UnrealVertexIndex[1] = 1
    UnrealVertexIndex[2] = 2
    Final order: [0, 1, 2]  (unchanged)

If OddNegativeScale == true:
    UnrealVertexIndex[0] = 2 - 0 = 2
    UnrealVertexIndex[1] = 2 - 1 = 1
    UnrealVertexIndex[2] = 2 - 2 = 0
    Final order: [2, 1, 0]  (reversed)
```

**Result:** Triangle vertices are stored as `[v2, v1, v0]` when odd negative scale detected.

---

## Bind Pose and Skinning

### Skinning Deformation

**Location:** `FbxSkeletalMeshImport.cpp`, lines 288-479

**Purpose:** Deforms vertices from bind pose to animation pose (or T-pose).

```cpp
void UnFbx::FFbxImporter::SkinControlPointsToPose(
    FSkeletalMeshImportData& ImportData,
    FbxMesh* FbxMesh,
    FbxShape* FbxShape,
    bool bUseT0)
{
    FbxTime poseTime = FBXSDK_TIME_INFINITE;
    if (bUseT0)
    {
        poseTime = 0;  // Evaluate at time zero (T-pose or bind pose)
    }

    int32 VertexCount = FbxMesh->GetControlPointsCount();

    // Create vertex array for deformation
    FbxVector4* VertexArray = new FbxVector4[VertexCount];

    // Copy control points (or morph target shape points)
    if (FbxShape)
    {
        check(FbxShape->GetControlPointsCount() == VertexCount);
        FMemory::Memcpy(VertexArray, FbxShape->GetControlPoints(),
            VertexCount * sizeof(FbxVector4));
    }
    else
    {
        FMemory::Memcpy(VertexArray, FbxMesh->GetControlPoints(),
            VertexCount * sizeof(FbxVector4));
    }

    int32 ClusterCount = 0;
    int32 SkinCount = FbxMesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32 i = 0; i < SkinCount; i++)
    {
        ClusterCount += ((FbxSkin*)(FbxMesh->GetDeformer(i, FbxDeformer::eSkin)))->GetClusterCount();
    }

    // Deform vertices if mesh has skin clusters
    if (ClusterCount)
    {
        FbxAMatrix MeshMatrix = ComputeTotalMatrix(FbxMesh->GetNode());

        // Get link mode (how skinning is applied)
        FbxCluster::ELinkMode lClusterMode =
            ((FbxSkin*)FbxMesh->GetDeformer(0, FbxDeformer::eSkin))->GetCluster(0)->GetLinkMode();

        TArray<FbxAMatrix> ClusterDeformations;
        ClusterDeformations.AddZeroed(VertexCount);

        TArray<double> ClusterWeights;
        ClusterWeights.AddZeroed(VertexCount);

        if (lClusterMode == FbxCluster::eAdditive)
        {
            for (int32 i = 0; i < VertexCount; i++)
            {
                ClusterDeformations[i].SetIdentity();
            }
        }

        // Process each skin deformer
        for (int32 i = 0; i < SkinCount; ++i)
        {
            int32 lClusterCount =
                ((FbxSkin*)FbxMesh->GetDeformer(i, FbxDeformer::eSkin))->GetClusterCount();

            for (int32 j = 0; j < lClusterCount; ++j)
            {
                FbxCluster* Cluster =
                    ((FbxSkin*)FbxMesh->GetDeformer(i, FbxDeformer::eSkin))->GetCluster(j);

                if (!Cluster || !Cluster->GetLink())
                    continue;

                FbxNode* Link = Cluster->GetLink();

                // Transformation matrices
                FbxAMatrix lReferenceGlobalInitPosition;    // Mesh at bind pose
                FbxAMatrix lReferenceGlobalCurrentPosition; // Mesh at current time
                FbxAMatrix lClusterGlobalInitPosition;      // Bone at bind pose
                FbxAMatrix lClusterGlobalCurrentPosition;   // Bone at current time
                FbxAMatrix lReferenceGeometry;
                FbxAMatrix lClusterGeometry;

                FbxAMatrix lClusterRelativeInitPosition;
                FbxAMatrix lClusterRelativeCurrentPositionInverse;
                FbxAMatrix lVertexTransformMatrix;

                // Get reference transform (mesh at bind pose)
                Cluster->GetTransformMatrix(lReferenceGlobalInitPosition);
                lReferenceGlobalCurrentPosition = MeshMatrix;

                // Multiply by geometric transformation
                lReferenceGeometry = GetGeometry(FbxMesh->GetNode());
                lReferenceGlobalInitPosition *= lReferenceGeometry;

                // Get link transform (bone transforms)
                Cluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
                lClusterGlobalCurrentPosition =
                    Link->GetScene()->GetAnimationEvaluator()->GetNodeGlobalTransform(Link, poseTime);

                // Compute relative transforms
                lClusterRelativeInitPosition =
                    lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;
                lClusterRelativeCurrentPositionInverse =
                    lReferenceGlobalCurrentPosition.Inverse() * lClusterGlobalCurrentPosition;

                // Compute vertex transformation
                lVertexTransformMatrix =
                    lClusterRelativeCurrentPositionInverse * lClusterRelativeInitPosition;

                // Apply to each vertex influenced by this bone
                int32 lVertexIndexCount = Cluster->GetControlPointIndicesCount();

                for (int32 k = 0; k < lVertexIndexCount; ++k)
                {
                    int32 lIndex = Cluster->GetControlPointIndices()[k];

                    if (lIndex >= VertexCount)
                        continue;

                    double lWeight = Cluster->GetControlPointWeights()[k];

                    if (lWeight == 0.0)
                        continue;

                    // Compute influence of this bone
                    FbxAMatrix lInfluence = lVertexTransformMatrix;
                    MatrixScale(lInfluence, lWeight);

                    if (lClusterMode == FbxCluster::eAdditive)
                    {
                        MatrixAddToDiagonal(lInfluence, 1.0 - lWeight);
                        ClusterDeformations[lIndex] = lInfluence * ClusterDeformations[lIndex];
                        ClusterWeights[lIndex] = 1.0;
                    }
                    else // eNORMALIZE or eTOTAL1
                    {
                        MatrixAdd(ClusterDeformations[lIndex], lInfluence);
                        ClusterWeights[lIndex] += lWeight;
                    }
                }
            }
        }

        // Apply accumulated deformations to vertices
        for (int32 i = 0; i < VertexCount; i++)
        {
            FbxVector4 lSrcVertex = VertexArray[i];
            FbxVector4& lDstVertex = VertexArray[i];
            double lWeight = ClusterWeights[i];

            if (lWeight != 0.0)
            {
                lDstVertex = ClusterDeformations[i].MultT(lSrcVertex);

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

        // Update ImportData with deformed positions
        int32 ExistPointNum = ImportData.Points.Num();
        int32 StartPointIndex = ExistPointNum - VertexCount;
        for (int32 ControlPointsIndex = 0; ControlPointsIndex < VertexCount; ControlPointsIndex++)
        {
            ImportData.Points[ControlPointsIndex + StartPointIndex] =
                (FVector3f)Converter.ConvertPos(MeshMatrix.MultT(VertexArray[ControlPointsIndex]));
        }
    }

    delete[] VertexArray;
}
```

**Key Bind Pose Matrices:**

1. **TransformMatrix:** Mesh's global transform at bind pose
2. **TransformLinkMatrix:** Bone's global transform at bind pose
3. **Vertex Transformation:**
   ```
   BindPoseToCurrentPose =
       (MeshCurrentGlobal^-1 * BoneCurrentGlobal) *
       (BoneBindGlobal^-1 * MeshBindGlobal)

   Simplified:
       = (MeshCurrent^-1 * BoneCurrent) * (BoneBind^-1 * MeshBind)
   ```

**Link Modes:**

- **eNormalize:** Normalize weights to sum to 1.0 after deformation
- **eTotalOne:** Weights already sum to 1.0, blend with undeformed vertex
- **eAdditive:** Additive blending (rare)

---

## Implementation Checklist

### Phase 1: Coordinate System Conversion

- [ ] Implement `ConvertScene()` equivalent
  - [ ] Store original axis system and unit system
  - [ ] Define target axis system: Z-Up, -Y Forward, Right-Handed (intermediate)
  - [ ] Compute axis conversion matrix
  - [ ] Apply FBX SDK's `ConvertScene()` or manual conversion
  - [ ] Convert units to centimeters

- [ ] Implement axis conversion matrix computation
  - [ ] Get source axis system matrix
  - [ ] Get target axis system matrix
  - [ ] Compute: `AxisConversionMatrix = SourceMatrix^-1 * TargetMatrix`

### Phase 2: Matrix Computation

- [ ] Implement `ComputeTotalMatrix()`
  - [ ] Extract geometric translation, rotation, scaling
  - [ ] Get node global transform
  - [ ] Handle pivot baking option
  - [ ] Combine: `TotalMatrix = GlobalTransform * Geometry` (if baking)
  - [ ] Or: `TotalMatrix = Geometry` (if not baking)

- [ ] Implement `ComputeSkeletalMeshTotalMatrix()`
  - [ ] Get mesh global transform
  - [ ] Get skeleton root global transform
  - [ ] Compute relative: `TotalMatrix = SkeletonRoot^-1 * MeshGlobal`

- [ ] Implement normal transformation matrix
  - [ ] Compute: `NormalMatrix = (TotalMatrix^-1)^T`

### Phase 3: Winding Order Detection

- [ ] Implement `IsOddNegativeScale()`
  - [ ] Extract scale from total matrix
  - [ ] Count negative scale components
  - [ ] Return: `(NegativeCount == 1 || NegativeCount == 3)`

### Phase 4: Static Mesh Import

- [ ] Transform vertices
  - [ ] Loop through control points
  - [ ] Transform: `Position' = TotalMatrix * Position`
  - [ ] Convert coordinate system: `ConvertPos()`
  - [ ] Store in vertex array

- [ ] Build triangles (NO winding reversal)
  - [ ] Loop through polygons
  - [ ] Get polygon vertices: `GetPolygonVertex(polygonIndex, 0/1/2)`
  - [ ] Create vertex instances in FBX order: `[0, 1, 2]`
  - [ ] **Do NOT reverse order based on OddNegativeScale**

- [ ] Transform normals and tangents
  - [ ] Transform by `NormalMatrix`
  - [ ] Normalize after transformation
  - [ ] Negate binormal for left-handed system

- [ ] Handle UVs
  - [ ] Extract UV coordinates
  - [ ] Flip Y: `UV.y = 1.0 - UV.y`

### Phase 5: Skeletal Mesh Import

- [ ] Fill vertex positions (initial)
  - [ ] Implement `FillSkeletalMeshImportPoints()`
  - [ ] Transform by `ComputeSkeletalMeshTotalMatrix()`
  - [ ] Convert coordinate system

- [ ] Build triangles WITH winding reversal
  - [ ] Compute `OddNegativeScale`
  - [ ] Loop through triangles
  - [ ] **FOR EACH VERTEX:**
    - [ ] Compute: `UnrealVertexIndex = OddNegativeScale ? (2 - VertexIndex) : VertexIndex`
    - [ ] Store data in `TmpWedges[UnrealVertexIndex]`
  - [ ] Store wedges in order: `[TmpWedges[0], TmpWedges[1], TmpWedges[2]]`
  - [ ] Result: Vertices automatically in correct winding order

- [ ] Transform normals/tangents (with reversal)
  - [ ] Use same reversal logic: `UnrealVertexIndex = OddNegativeScale ? (2 - i) : i`
  - [ ] Transform by `NormalMatrix`
  - [ ] Negate binormal

- [ ] Handle UVs (with reversal)
  - [ ] Use same reversal logic
  - [ ] Flip Y coordinate

### Phase 6: Bind Pose and Skinning

- [ ] Implement skinning deformation
  - [ ] Get cluster count (bone influences)
  - [ ] For each cluster:
    - [ ] Get `TransformMatrix` (mesh at bind pose)
    - [ ] Get `TransformLinkMatrix` (bone at bind pose)
    - [ ] Get current bone transform at target time
    - [ ] Compute vertex transformation matrix
  - [ ] Accumulate weighted transformations
  - [ ] Apply to vertices
  - [ ] Update vertex positions with deformed result

- [ ] Handle different link modes
  - [ ] eNormalize: Divide by total weight
  - [ ] eTotalOne: Blend with original vertex
  - [ ] eAdditive: Additive blending

### Phase 7: Testing and Validation

- [ ] Test coordinate system conversion
  - [ ] Import mesh from Y-Up system (Max/Maya)
  - [ ] Verify Z-Up in UE
  - [ ] Check forward direction

- [ ] Test winding order
  - [ ] Import mesh with normal scale → Should render correctly
  - [ ] Import mesh with negative scale (X=-1) → Should render correctly
  - [ ] Import mesh with negative scale (X=-1, Y=-1, Z=-1) → Should render correctly
  - [ ] Verify normals point outward in all cases

- [ ] Test skeletal mesh
  - [ ] Import T-pose skeletal mesh
  - [ ] Verify bind pose is correct
  - [ ] Apply animation, verify deformation
  - [ ] Check skinning weights

- [ ] Test edge cases
  - [ ] Meshes with geometric transforms (3ds Max)
  - [ ] Meshes with pivot offsets
  - [ ] Mirrored meshes
  - [ ] Negative uniform scale
  - [ ] Mixed negative scales (X=-1, Y=1, Z=1)

---

## Summary of Critical Differences

### Static Mesh vs Skeletal Mesh

| Aspect | Static Mesh | Skeletal Mesh |
|--------|-------------|---------------|
| **Winding Order Reversal** | ❌ NO (relies on FBX SDK) | ✅ YES (manual reversal) |
| **OddNegativeScale Usage** | Computed but NOT used for reversal | Used to reverse vertex order |
| **Vertex Order** | Always `[0, 1, 2]` from FBX | `[2, 1, 0]` if odd negative scale |
| **Transformation** | `ComputeTotalMatrix()` | `ComputeSkeletalMeshTotalMatrix()` |
| **Skinning** | N/A | Bind pose deformation required |

### Key Takeaways

1. **Coordinate Conversion:** FBX SDK's `ConvertScene()` handles most coordinate system conversion automatically

2. **Winding Order:**
   - Static meshes: Trust FBX SDK conversion, no manual reversal
   - Skeletal meshes: Manual reversal based on odd negative scale count

3. **Normal Transformation:** Always use inverse-transpose matrix

4. **UV Handling:** Always flip Y-coordinate (DirectX convention)

5. **Skeletal Bind Pose:** Requires complex matrix math to compute vertex deformation from bind pose to animation pose

---

## Implementation Example

### Pseudo-Code for Skeletal Mesh Triangle with Winding Order

```cpp
bool OddNegativeScale = IsOddNegativeScale(TotalMatrix);

for (int TriangleIndex = 0; TriangleIndex < TriangleCount; TriangleIndex++)
{
    Vertex TmpWedges[3];

    // Read FBX data in original order
    for (int VertexIndex = 0; VertexIndex < 3; VertexIndex++)
    {
        // Determine target index (reversed if needed)
        int UnrealVertexIndex = OddNegativeScale ? (2 - VertexIndex) : VertexIndex;

        // Read from FBX in order [0, 1, 2]
        int ControlPointIndex = FbxMesh->GetPolygonVertex(TriangleIndex, VertexIndex);

        // Store in TmpWedges at reversed index if needed
        TmpWedges[UnrealVertexIndex].VertexIndex = ControlPointIndex;
        TmpWedges[UnrealVertexIndex].UV = ReadUV(TriangleIndex, VertexIndex);
        TmpWedges[UnrealVertexIndex].Normal = ReadNormal(TriangleIndex, VertexIndex);
    }

    // Now TmpWedges contains vertices in correct order
    // If OddNegativeScale: [v2, v1, v0]
    // If !OddNegativeScale: [v0, v1, v2]
    Triangle.Wedges[0] = TmpWedges[0];
    Triangle.Wedges[1] = TmpWedges[1];
    Triangle.Wedges[2] = TmpWedges[2];
}
```

---

## Conclusion

This document provides a complete blueprint for implementing FBX import exactly as Unreal Engine 5 does. The most critical insight is the **difference between static and skeletal mesh winding order handling**:

- **Static meshes** rely on FBX SDK's coordinate conversion and preserve the FBX vertex order
- **Skeletal meshes** manually reverse vertex order when odd negative scale is detected

Following this guide ensures:
- ✅ Correct coordinate system conversion
- ✅ Proper winding order (no inside-out meshes)
- ✅ Correct normal and tangent transformation
- ✅ Accurate skeletal mesh skinning
- ✅ Compatible with assets from Maya, Max, Blender, and other DCC tools

**File analyzed:** Unreal Engine 5 source code
**Date:** 2025-11-10
**Version:** UE5 (latest main branch)
