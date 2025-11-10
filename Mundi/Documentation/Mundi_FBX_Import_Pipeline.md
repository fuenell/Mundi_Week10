# Mundi Engine - FBX Import Pipeline Documentation

## 목차
1. [개요](#개요)
2. [좌표계 변환 전략](#좌표계-변환-전략)
3. [Winding Order 처리](#winding-order-처리)
4. [Import 파이프라인](#import-파이프라인)
5. [Static Mesh vs Skeletal Mesh](#static-mesh-vs-skeletal-mesh)
6. [핵심 함수 레퍼런스](#핵심-함수-레퍼런스)
7. [Unreal Engine과의 차이점](#unreal-engine과의-차이점)

---

## 개요

Mundi 엔진의 FBX Import 시스템은 Autodesk FBX SDK를 사용하여 FBX 파일에서 Static Mesh와 Skeletal Mesh를 Import합니다. 이 문서는 Mundi 엔진의 FBX Import 프로세스, 좌표계 변환, Winding Order 처리 방식을 설명합니다.

### 지원 기능
- ✅ **Static Mesh Import** - 단순 3D 모델
- ✅ **Skeletal Mesh Import** - Skeleton + Skin Weights + Bind Pose
- 🚧 **Animation Import** (향후 지원 예정)

### 좌표계
- **Mundi 엔진**: Z-Up, X-Forward, Y-Right, **Left-Handed**
- **D3D11 Winding Order**: **Clockwise (CW) = Front Face** (기본값)
- **FBX**: 다양한 좌표계 지원 → FBX SDK로 자동 변환

---

## 좌표계 변환 전략

### 1단계: FBX Scene 좌표계 변환 (ConvertScene)

FBX SDK의 `FbxAxisSystem::ConvertScene()`을 사용하여 Scene 전체를 Unreal Engine 스타일의 좌표계로 변환:

```cpp
// Unreal Engine 방식: Z-Up, -Y-Forward, Right-Handed
FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
FbxAxisSystem::EFrontVector FrontVector = (FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd;  // -Y Forward

FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);
UnrealImportAxis.ConvertScene(Scene);
```

**변환 결과:**
- **Before**: 임의의 FBX 좌표계 (Y-Up, Z-Up, Left/Right-Handed 등)
- **After**: Z-Up, -Y-Forward, **Right-Handed**

**중요**: 이 단계에서는 **Right-Handed**로 변환됩니다!

### 2단계: Y축 반전 (ConvertFbxPosition)

Vertex별로 Y축을 반전하여 Left-Handed로 변환:

```cpp
FVector FFbxImporter::ConvertFbxPosition(const FbxVector4& pos)
{
    return FVector(
        static_cast<float>(pos[0]),      // X unchanged
        -static_cast<float>(pos[1]),     // Y FLIPPED
        static_cast<float>(pos[2])       // Z unchanged
    );
}
```

**변환 결과:**
- **Before**: Z-Up, -Y-Forward, Right-Handed
- **After**: Z-Up, X-Forward, **Left-Handed** (Mundi 좌표계)

### 3단계: Index Reversal (Mundi 전용)

Y축 반전은 **Handedness만 변경**하고, **Winding Order는 변경하지 않습니다**. 따라서 삼각형이 CCW로 남아있어 Mundi의 CW 기준에 맞추기 위해 Index를 reverse:

```cpp
// Mundi 엔진: CW = Front Face (D3D11 기본)
TArray<uint32>& indicesRef = OutSkeletalMesh->GetIndicesRef();
for (size_t i = 0; i < indicesRef.size(); i += 3)
{
    std::swap(indicesRef[i], indicesRef[i + 2]);  // [0,1,2] → [2,1,0] (CCW → CW)
}
```

**최종 결과:**
- **Coordinate System**: Z-Up, X-Forward, Y-Right, Left-Handed ✓
- **Winding Order**: Clockwise (CW) ✓

---

## Winding Order 처리

### Y-Flip의 기하학적 의미

많은 개발자들이 Y축 반전이 winding order를 자동으로 반전시킨다고 오해하지만, **실제로는 그렇지 않습니다**.

#### 예제: X-Z 평면의 삼각형

```
Original (RH, CCW):
v0(0, 1, 0) → v1(1, 0, 0) → v2(0, 0, 0)

After Y-Flip:
v0(0, -1, 0) → v1(1, 0, 0) → v2(0, 0, 0)
```

**카메라가 +Z에서 바라볼 때:**
- Y좌표만 변경됨 (1 → -1)
- **X-Z 평면에서의 순서는 여전히 CCW!**

### Mundi vs Unreal Engine 차이점

| 항목 | Unreal Engine | Mundi Engine |
|------|---------------|--------------|
| **D3D11 설정** | `FrontCounterClockwise = TRUE` | `FrontCounterClockwise = FALSE` (기본) |
| **Front Face** | CCW | CW |
| **Index Reversal** | ❌ 불필요 | ✅ 필수 |
| **이유** | Y-flip 후 CCW 삼각형을 그대로 front로 인식 | Y-flip 후 CCW→CW 변환 필요 |

### D3D11 Rasterizer 설정

```cpp
// Mundi/Source/Runtime/RHI/D3D11RHI.cpp:637-641
D3D11_RASTERIZER_DESC deafultrasterizerdesc = {};
deafultrasterizerdesc.FillMode = D3D11_FILL_SOLID;
deafultrasterizerdesc.CullMode = D3D11_CULL_BACK;
deafultrasterizerdesc.DepthClipEnable = TRUE;
// FrontCounterClockwise 기본값 FALSE (CW = Front Face, Mundi 엔진 기본)
```

**참고**: Unreal Engine은 `FrontCounterClockwise = TRUE`로 설정하여 CCW를 front face로 처리하지만, Mundi는 기존 렌더링 시스템이 모두 CW 기준으로 작성되어 있어 이 설정을 유지합니다.

---

## Import 파이프라인

### 전체 흐름

```
┌──────────────────────────────────────────────────────────────┐
│ 1. LoadScene(FilePath)                                       │
│    - FBX 파일 로드                                            │
│    - Triangulate (모든 polygon을 triangle로 변환)             │
└────────────────────┬─────────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────────┐
│ 2. ConvertScene()                                            │
│    - FBX SDK: 좌표계 변환 (Z-Up, -Y-Forward, Right-Handed)  │
│    - RemoveAllFbxRoots() (Unreal Engine 방식)               │
└────────────────────┬─────────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────────┐
│ 3. ExtractMeshData()                                         │
│    - Vertex, Index, Normal, UV, Tangent 추출                │
│    - Raw FBX 데이터 → Component Space                       │
└────────────────────┬─────────────────────────────────────────┘
                     │
                     ▼
         ┌───────────┴───────────┐
         │                       │
         ▼                       ▼
┌──────────────────┐    ┌──────────────────┐
│  STATIC MESH     │    │ SKELETAL MESH    │
│                  │    │                  │
│ 4a. Transform    │    │ 4b. Extract      │
│   Vertices       │    │   Skeleton       │
│   (Global Space) │    │   (Hierarchy)    │
│                  │    │                  │
│ 5a. Y-Flip       │    │ 5b. Extract      │
│   ConvertPos()   │    │   SkinWeights    │
│                  │    │   + Y-Flip       │
│                  │    │                  │
│ 6a. Index        │    │ 6b. Extract      │
│   Reversal       │    │   BindPose       │
│   (CCW→CW)       │    │   + Index        │
│                  │    │     Reversal     │
└──────────────────┘    └──────────────────┘
```

### 단계별 설명

#### Phase 1: LoadScene
```cpp
bool FFbxImporter::LoadScene(const FString& FilePath)
{
    // 1. FBX 파일 로드
    Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings());
    Importer->Import(Scene);

    // 2. Triangulate (모든 Polygon을 Triangle로 변환)
    FbxGeometryConverter geometryConverter(SdkManager);
    geometryConverter.Triangulate(Scene, true);

    return true;
}
```

**역할:**
- FBX 파일을 Scene으로 로드
- 모든 Polygon을 Triangle로 변환 (Quad → 2 Triangles)
- 이후 단계에서 모든 face가 정확히 3개의 vertex를 가짐

#### Phase 2: ConvertScene
```cpp
void FFbxImporter::ConvertScene()
{
    // Unreal Engine 방식: 불필요한 FBX Root 노드 제거
    FbxRootNodeUtility::RemoveAllFbxRoots(Scene);

    // Z-Up, -Y-Forward, Right-Handed로 변환
    FbxAxisSystem UnrealImportAxis(
        FbxAxisSystem::eZAxis,                                    // Z-Up
        (FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd, // -Y Forward
        FbxAxisSystem::eRightHanded                               // Right-Handed
    );

    UnrealImportAxis.ConvertScene(Scene);

    // Animation Evaluator Reset
    Scene->GetAnimationEvaluator()->Reset();
}
```

**역할:**
- FBX Scene을 통일된 좌표계로 변환
- **중요**: 아직 Right-Handed 상태 (Left-Handed 변환은 나중에 Vertex별로 수행)
- Unreal Engine의 Import 방식을 그대로 따름

#### Phase 3: ExtractMeshData
```cpp
bool FFbxImporter::ExtractMeshData(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh)
{
    FbxMesh* fbxMesh = MeshNode->GetMesh();

    // Polygon 순회 (모든 Triangle)
    for (int32 polyIndex = 0; polyIndex < polygonCount; polyIndex++)
    {
        for (int32 vertInPoly = 0; vertInPoly < 3; vertInPoly++)
        {
            // 1. Control Point (Vertex Position)
            int32 controlPointIndex = fbxMesh->GetPolygonVertex(polyIndex, vertInPoly);
            FbxVector4 fbxPos = fbxMesh->GetControlPointAt(controlPointIndex);
            vertex.Position = FVector(fbxPos[0], fbxPos[1], fbxPos[2]);  // Raw, 아직 변환 안함

            // 2. Normal
            FbxVector4 fbxNormal;
            fbxMesh->GetPolygonVertexNormal(polyIndex, vertInPoly, fbxNormal);
            vertex.Normal = FVector(fbxNormal[0], fbxNormal[1], fbxNormal[2]);

            // 3. UV
            FbxVector2 fbxUV;
            fbxMesh->GetPolygonVertexUV(polyIndex, vertInPoly, uvSetName, fbxUV, unmapped);
            vertex.TexCoord = FVector2(fbxUV[0], 1.0f - fbxUV[1]);  // V축 반전

            // 4. Tangent (있는 경우)
            vertex.Tangent = ...;

            vertices.push_back(vertex);
            indices.push_back(vertexIndexCounter++);
        }
    }

    OutSkeletalMesh->SetVertices(vertices);
    OutSkeletalMesh->SetIndices(indices);
}
```

**역할:**
- Raw FBX 데이터 추출
- 아직 좌표 변환 없음 (Component Space)
- Polygon → Vertex 매핑 수행

---

## Static Mesh vs Skeletal Mesh

### Static Mesh Pipeline

Static Mesh는 Skeleton이 없는 단순 3D 모델입니다.

#### 1. Transform Vertices (Global Space)

```cpp
// Geometry Transform (Pivot/Offset)
FbxVector4 geoTranslation = MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot);
FbxVector4 geoRotation = MeshNode->GetGeometricRotation(FbxNode::eSourcePivot);
FbxVector4 geoScaling = MeshNode->GetGeometricScaling(FbxNode::eSourcePivot);
FbxAMatrix geometryTransform(geoTranslation, geoRotation, geoScaling);

// Global Transform (Scene에서의 위치)
FbxAMatrix globalTransform = Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(MeshNode);

// Total Transform = GlobalTransform * Geometry (Unreal Engine 방식)
FbxAMatrix totalTransform = globalTransform * geometryTransform;
```

**Unreal Engine의 ComputeTotalMatrix() 구현:**
- `bTransformVertexToAbsolute = true` (Static Mesh 기본값)
- Vertex를 World Space로 변환
- Geometry Offset (Pivot) 적용

#### 2. Y-Flip + Index Reversal

```cpp
// Vertex 변환
for (auto& vertex : verticesRef)
{
    // 1. TotalTransform 적용
    FbxVector4 fbxPos(vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0);
    FbxVector4 transformedPos = totalTransform.MultT(fbxPos);

    // 2. Y축 반전 (RightHanded → LeftHanded)
    vertex.Position = ConvertFbxPosition(transformedPos);

    // 3. Normal, Tangent도 동일하게 변환
    vertex.Normal = ConvertFbxDirection(transformedNormal);
    vertex.Tangent = ...;
}

// Index Reversal (CCW → CW)
TArray<uint32>& indicesRef = OutSkeletalMesh->GetIndicesRef();
for (size_t i = 0; i < indicesRef.size(); i += 3)
{
    std::swap(indicesRef[i], indicesRef[i + 2]);  // [0,1,2] → [2,1,0]
}
```

### Skeletal Mesh Pipeline

Skeletal Mesh는 Skeleton + Skin Weights를 가진 애니메이션 가능한 모델입니다.

#### 1. Extract Skeleton (Bone Hierarchy)

```cpp
USkeleton* FFbxImporter::ExtractSkeleton(FbxNode* RootNode)
{
    USkeleton* skeleton = ObjectFactory::NewObject<USkeleton>();

    // Bone Hierarchy 재귀적 탐색
    std::function<void(FbxNode*, int32)> ProcessBone = [&](FbxNode* Node, int32 ParentIndex)
    {
        // Bone 정보 생성
        FBoneInfo boneInfo;
        boneInfo.Name = Node->GetName();
        boneInfo.ParentIndex = ParentIndex;

        // Local Transform (부모 기준 상대 Transform)
        FbxAMatrix localTransform = Node->EvaluateLocalTransform();
        boneInfo.LocalTransform = ConvertFbxTransform(localTransform);

        int32 boneIndex = skeleton->AddBone(boneInfo);

        // 자식 Bone 재귀 처리
        for (int32 i = 0; i < Node->GetChildCount(); i++)
        {
            ProcessBone(Node->GetChild(i), boneIndex);
        }
    };

    ProcessBone(RootNode, -1);
    return skeleton;
}
```

**역할:**
- FBX Node Hierarchy → Bone Hierarchy
- 각 Bone의 Local Transform 추출
- Parent-Child 관계 유지

#### 2. Extract Skin Weights (Vertex → Bone Mapping)

```cpp
bool FFbxImporter::ExtractSkinWeights(FbxMesh* fbxMesh, USkeletalMesh* OutSkeletalMesh)
{
    FbxSkin* fbxSkin = (FbxSkin*)fbxMesh->GetDeformer(0, FbxDeformer::eSkin);

    // 각 Bone Cluster 순회
    for (int32 clusterIndex = 0; clusterIndex < fbxSkin->GetClusterCount(); clusterIndex++)
    {
        FbxCluster* cluster = fbxSkin->GetCluster(clusterIndex);
        FbxNode* linkNode = cluster->GetLink();
        int32 boneIndex = skeleton->FindBoneIndexByName(linkNode->GetName());

        // Bind Pose Matrix 가져오기
        FbxAMatrix transformMatrix;       // Mesh의 Global Transform (Bind Pose 시점)
        FbxAMatrix transformLinkMatrix;   // Bone의 Global Transform (Bind Pose 시점)
        cluster->GetTransformMatrix(transformMatrix);
        cluster->GetTransformLinkMatrix(transformLinkMatrix);

        // 이 Cluster가 영향을 주는 Control Point (Vertex) 순회
        int32* controlPointIndices = cluster->GetControlPointIndices();
        double* controlPointWeights = cluster->GetControlPointWeights();

        for (int32 i = 0; i < cluster->GetControlPointIndicesCount(); i++)
        {
            int32 controlPointIndex = controlPointIndices[i];
            float weight = static_cast<float>(controlPointWeights[i]);

            // 이 Control Point를 사용하는 모든 Vertex에 Weight 적용
            // (FBX에서는 Control Point 기준, Mundi는 Vertex 기준)
            for (int32 vertexIndex : vertexToControlPointMap)
            {
                if (vertexToControlPointMap[vertexIndex] == controlPointIndex)
                {
                    AddBoneInfluence(vertices[vertexIndex], boneIndex, weight);
                }
            }
        }

        // Vertex Transform to Mesh Global Space
        FbxAMatrix totalTransform = transformMatrix * geometryTransform;

        for (auto& vertex : vertices)
        {
            vertex.Position = ConvertFbxPosition(totalTransform.MultT(fbxPos));
            vertex.Normal = ConvertFbxDirection(normalTransform.MultT(fbxNormal));
            vertex.Tangent = ...;
        }

        // Index Reversal (CCW → CW)
        for (size_t i = 0; i < indicesRef.size(); i += 3)
        {
            std::swap(indicesRef[i], indicesRef[i + 2]);
        }
    }
}
```

**역할:**
- Vertex → Bone 영향도 매핑
- 각 Vertex는 최대 4개의 Bone에 영향받음
- Weight 정규화 (합이 1.0)
- Vertex를 Mesh Global Space로 변환 + Y-Flip
- Index Reversal (CCW → CW)

#### 3. Extract Bind Pose (초기 Bone Transform)

```cpp
bool FFbxImporter::ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton)
{
    FbxPose* bindPose = Scene->GetPose(0);  // 첫 번째 Bind Pose

    for (int32 i = 0; i < bindPose->GetCount(); i++)
    {
        FbxNode* node = bindPose->GetNode(i);
        FbxMatrix bindPoseMatrix = bindPose->GetMatrix(i);

        int32 boneIndex = OutSkeleton->FindBoneIndexByName(node->GetName());
        if (boneIndex != -1)
        {
            // Y축 선택적 반전 (Unreal Engine 방식)
            FMatrix mundiMatrix = ConvertFbxMatrixWithYAxisFlip(bindPoseMatrix);
            OutSkeleton->SetGlobalBindPoseMatrix(boneIndex, mundiMatrix);
        }
    }
}
```

**역할:**
- Bind Pose (Skinning 전 초기 자세) 추출
- 각 Bone의 Global Transform 저장
- 런타임에 Skinning 계산 시 사용: `InverseBindPoseMatrix * CurrentBoneMatrix`

---

## 핵심 함수 레퍼런스

### FFbxImporter::LoadScene()
```cpp
bool LoadScene(const FString& FilePath);
```
- FBX 파일을 Scene으로 로드
- Triangulate 수행
- 반환: 성공 여부

### FFbxImporter::ConvertScene()
```cpp
void ConvertScene();
```
- FBX Scene을 Unreal-style 좌표계로 변환
- Z-Up, -Y-Forward, Right-Handed
- Unreal Engine의 FbxMainImport.cpp:1528-1532 방식

### FFbxImporter::ExtractMeshData()
```cpp
bool ExtractMeshData(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh);
```
- Vertex, Index, Normal, UV, Tangent 추출
- Static Mesh의 경우 Transform + Y-Flip + Index Reversal 수행
- 반환: 성공 여부

### FFbxImporter::ExtractSkinWeights()
```cpp
bool ExtractSkinWeights(FbxMesh* fbxMesh, USkeletalMesh* OutSkeletalMesh);
```
- FbxSkin → Bone Influences 매핑
- Vertex Transform to Mesh Global Space
- Y-Flip + Index Reversal 수행
- 반환: 성공 여부

### FFbxImporter::ExtractSkeleton()
```cpp
USkeleton* ExtractSkeleton(FbxNode* RootNode);
```
- Bone Hierarchy 추출
- Local Transform 계산
- 반환: USkeleton 객체

### FFbxImporter::ExtractBindPose()
```cpp
bool ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton);
```
- Bind Pose Matrix 추출
- Global Transform 저장
- 반환: 성공 여부

### 좌표 변환 Helper 함수

#### ConvertFbxPosition()
```cpp
FVector ConvertFbxPosition(const FbxVector4& pos)
{
    return FVector(pos[0], -pos[1], pos[2]);  // Y축 반전
}
```
- **용도**: Position 변환
- **변환**: Right-Handed → Left-Handed
- **Y축 반전**: `-Y`

#### ConvertFbxDirection()
```cpp
FVector ConvertFbxDirection(const FbxVector4& dir)
{
    FVector result(dir[0], -dir[1], dir[2]);  // Y축 반전
    result.Normalize();
    return result;
}
```
- **용도**: Normal, Tangent, Binormal 변환
- **변환**: Right-Handed → Left-Handed
- **정규화**: 자동 수행

#### ConvertFbxQuaternion()
```cpp
FQuat ConvertFbxQuaternion(const FbxQuaternion& q)
{
    return FQuat(q[0], -q[1], q[2], -q[3]);  // Y, W 반전
}
```
- **용도**: Rotation (Quaternion) 변환
- **변환**: Right-Handed → Left-Handed
- **Y, W 반전**: Quaternion handedness 변환 규칙

#### ConvertFbxMatrixWithYAxisFlip()
```cpp
FMatrix ConvertFbxMatrixWithYAxisFlip(const FbxMatrix& fbxMatrix)
{
    FMatrix result;
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            result.M[row][col] = static_cast<float>(fbxMatrix.Get(row, col));
        }
    }

    // Y축 관련 요소 반전 (행렬 기반 handedness 변환)
    result.M[1][0] = -result.M[1][0];  // Row 1, Col 0
    result.M[1][1] = -result.M[1][1];  // Row 1, Col 1
    result.M[1][2] = -result.M[1][2];  // Row 1, Col 2
    result.M[1][3] = -result.M[1][3];  // Row 1, Col 3 (Translation Y)

    return result;
}
```
- **용도**: Transform Matrix 변환 (Bind Pose 등)
- **변환**: Right-Handed → Left-Handed
- **Y축 선택적 반전**: Winding Order 자동 보존

#### IsOddNegativeScale()
```cpp
bool IsOddNegativeScale(const FbxAMatrix& TotalMatrix)
{
    FbxVector4 Scale = TotalMatrix.GetS();
    int32 NegativeNum = 0;

    if (Scale[0] < 0) NegativeNum++;
    if (Scale[1] < 0) NegativeNum++;
    if (Scale[2] < 0) NegativeNum++;

    return NegativeNum == 1 || NegativeNum == 3;
}
```
- **용도**: Mirror Transform 감지
- **반환**: Scale에 음수가 1개 또는 3개면 `true`
- **Unreal Engine 방식**: Odd Negative Scale 시 추가 처리 필요 (향후 구현)

---

## Unreal Engine과의 차이점

### 1. Winding Order 처리 방식

| 항목 | Unreal Engine | Mundi Engine |
|------|---------------|--------------|
| **D3D11 설정** | `FrontCounterClockwise = TRUE` | `FrontCounterClockwise = FALSE` |
| **Front Face** | Counter-Clockwise (CCW) | Clockwise (CW) |
| **Y-Flip 후 처리** | 그대로 사용 (CCW 유지) | Index Reversal 수행 (CCW → CW) |
| **Index Reversal** | ❌ 불필요 | ✅ 필수 |

**이유:**
- Unreal Engine은 렌더링 시스템이 CCW를 front face로 처리하도록 설계됨
- Mundi는 기존 렌더링 시스템이 CW 기준으로 작성되어 있음 (D3D11 기본값)
- FBX Import에서 Index Reversal로 CCW→CW 변환 수행

### 2. 코드 위치 비교

#### Unreal Engine (D3D11State.cpp:211)
```cpp
RasterizerDesc.FrontCounterClockwise = true;  // CCW = Front Face
```

#### Mundi Engine (D3D11RHI.cpp:641)
```cpp
// FrontCounterClockwise 기본값 FALSE (CW = Front Face, Mundi 엔진 기본)
```

#### Unreal Engine (FbxStaticMeshImport.cpp:1165)
```cpp
// Triangle 생성 - 순서 그대로 [0,1,2]
const FTriangleID NewTriangleID = MeshDescription->CreateTriangle(
    PolygonGroupID, CornerInstanceIDs, &NewEdgeIDs
);
// Index Reversal 없음!
```

#### Mundi Engine (FbxImporter.cpp:660-663, 791-794)
```cpp
// Index Reversal 수행
TArray<uint32>& indicesRef = OutSkeletalMesh->GetIndicesRef();
for (size_t i = 0; i < indicesRef.size(); i += 3)
{
    std::swap(indicesRef[i], indicesRef[i + 2]);  // [0,1,2] → [2,1,0] (CCW → CW)
}
```

### 3. 좌표계 변환은 동일

| 단계 | Unreal Engine | Mundi Engine | 비고 |
|------|---------------|--------------|------|
| ConvertScene | Z-Up, -Y-Forward, RH | Z-Up, -Y-Forward, RH | ✅ 동일 |
| Y-Flip | ConvertPos() Y 반전 | ConvertFbxPosition() Y 반전 | ✅ 동일 |
| 최종 좌표계 | Z-Up, X-Forward, LH | Z-Up, X-Forward, LH | ✅ 동일 |

**결론**: 좌표계 변환 로직은 Unreal Engine과 완전히 동일하며, 차이점은 **Winding Order 처리 방식**만 다릅니다.

---

## 디버깅 팁

### 1. 좌표계 변환 확인

FBX Import 시 로그 출력:
```
[FBX DEBUG] === Original Scene Coordinate System ===
[FBX DEBUG] UpVector: 2 (sign: 1)        // 2 = Z-Axis
[FBX DEBUG] FrontVector: 0 (sign: 1)     // 0 = X-Axis
[FBX DEBUG] CoordSystem: RightHanded

[FBX DEBUG] === After Conversion ===
[FBX DEBUG] UpVector: 2 (sign: 1)        // Z-Up
[FBX DEBUG] FrontVector: 1 (sign: -1)    // -Y-Forward
[FBX DEBUG] CoordSystem: RightHanded     // 아직 RH
[FBX] ConvertPos() will flip Y-axis to convert Right-Handed to Left-Handed
```

### 2. Winding Order 테스트

D3D11 Rasterizer State 변경으로 테스트 가능:

```cpp
// 테스트 1: Culling 비활성화
rasterizerDesc.CullMode = D3D11_CULL_NONE;
// 결과: 모든 면이 보여야 함

// 테스트 2: CCW를 Front Face로 설정
rasterizerDesc.FrontCounterClockwise = TRUE;
// 결과: Index Reversal 없이 올바르게 보여야 함 (Unreal Engine 방식)
```

### 3. Transform Matrix 검증

```cpp
UE_LOG("[FBX] Global Transform - T:(%.3f, %.3f, %.3f) R:(%.3f, %.3f, %.3f) S:(%.3f, %.3f, %.3f)",
    globalTransform.GetT()[0], globalTransform.GetT()[1], globalTransform.GetT()[2],
    globalTransform.GetR()[0], globalTransform.GetR()[1], globalTransform.GetR()[2],
    globalTransform.GetS()[0], globalTransform.GetS()[1], globalTransform.GetS()[2]);
```

**확인 사항:**
- Translation: 모델의 World 위치
- Rotation: Euler Angles (Degrees)
- Scale: 음수가 있으면 Mirror Transform (OddNegativeScale 확인 필요)

---

## 참고 자료

### Unreal Engine Source Code
- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxMainImport.cpp` - Main Import Logic
- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxStaticMeshImport.cpp` - Static Mesh Pipeline
- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxSkeletalMeshImport.cpp` - Skeletal Mesh Pipeline
- `Engine/Source/Runtime/Windows/D3D11RHI/Private/D3D11State.cpp` - D3D11 Rasterizer Settings

### Mundi Engine Documentation
- `Mundi/Documentation/UnrealEngine_FBX_Import_Analysis.md` - UE5 FBX Import 상세 분석
- `Mundi/Documentation/UnrealEngine_FBX_Import_Pipeline_Architecture.md` - UE5 Pipeline 구조 분석
- `Mundi/README.md` - Mundi 엔진 좌표계 설명

### FBX SDK Documentation
- [Autodesk FBX SDK Documentation](https://help.autodesk.com/view/FBX/2020/ENU/)
- FbxAxisSystem - Coordinate System Conversion
- FbxGeometryConverter - Triangulation
- FbxSkin, FbxCluster - Skinning Data

---

## 변경 이력

| 버전 | 날짜 | 내용 |
|------|------|------|
| 1.0 | 2025-11-10 | Initial Documentation - FBX Import Pipeline 완료 |
| | | - Static Mesh, Skeletal Mesh Import 지원 |
| | | - Winding Order 처리 (Index Reversal) 구현 |
| | | - Unreal Engine 방식 기반 좌표계 변환 |

---

**End of Document**
