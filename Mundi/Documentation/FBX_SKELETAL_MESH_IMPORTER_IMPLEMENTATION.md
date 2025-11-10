# FBX Skeletal Mesh Importer - 구현 문서

**작성일:** 2025-11-08
**버전:** Phase 5 PoC
**상태:** ✅ SkeletalMesh Import 완료 (렌더링 미구현)

---

## 📋 목차

- [개요](#개요)
- [아키텍처](#아키텍처)
- [Import 파이프라인](#import-파이프라인)
- [자료구조](#자료구조)
- [좌표계 변환](#좌표계-변환)
- [현재 한계점](#현재-한계점)
- [향후 작업](#향후-작업)
- [참고 자료](#참고-자료)

---

## 개요

Mundi 엔진의 FBX Skeletal Mesh Importer는 Autodesk FBX SDK를 사용하여 Blender, Maya, 3ds Max 등의 DCC 툴에서 제작한 Rigged Character 모델을 Import합니다.

### 현재 구현 상태

| 기능 | 상태 | 설명 |
|-----|------|------|
| **FBX Scene 로딩** | ✅ 완료 | FBX SDK를 통한 파일 로드 |
| **좌표계 변환** | ✅ 완료 | Z-Up, Left-Handed로 자동 변환 |
| **Skeleton 추출** | ✅ 완료 | Bone 계층 구조 추출 |
| **Mesh 데이터 추출** | ✅ 완료 | Vertex, Index, Normal, UV, Tangent |
| **Skin Weights 추출** | ⚠️ PoC | Control Point 기준 추출 (미완성) |
| **Bind Pose 추출** | ✅ 완료 | Inverse Bind Pose Matrix 추출 |
| **Editor 통합** | ✅ 완료 | MainToolbar에서 FBX Import 메뉴 |
| **Actor 생성** | ✅ 완료 | ASkeletalMeshActor 자동 생성 |
| **GPU 리소스 생성** | ❌ 미구현 | VertexBuffer/IndexBuffer 미생성 |
| **렌더링** | ❌ 미구현 | GPU Skinning 미구현 |

### 테스트 결과 (Blender FBX)

```
✅ Skeleton: 6 bones extracted
   - Bone [0]: Bone (Parent: -1)
   - Bone [1]: Bone.001 (Parent: 0)
   - Bone [2]: Bone.002 (Parent: 1)
   - Bone [3]: Bone.003 (Parent: 2)
   - Bone [4]: Bone.004 (Parent: 3)
   - Bone [5]: Bone.004_end (Parent: 4)

✅ Mesh Data: 1356 vertices, 452 triangles
✅ Skin Weights: Applied to all vertices
✅ Bind Pose: 5 bones with inverse bind matrices
✅ Actor Spawned: Successfully placed in scene
❌ Rendering: No visual output (GPU resources not created)
```

---

## 아키텍처

### 클래스 다이어그램

```
┌─────────────────────────────────────────────────────────────┐
│                      FFbxImporter                           │
├─────────────────────────────────────────────────────────────┤
│ - FbxManager* SdkManager                                    │
│ - FbxScene* Scene                                           │
│ - FbxImporter* Importer                                     │
│ - FFbxImportOptions CurrentOptions                          │
├─────────────────────────────────────────────────────────────┤
│ + ImportSkeletalMesh(FilePath, Options) : USkeletalMesh*   │
│ - LoadScene(FilePath) : bool                                │
│ - ConvertScene() : void                                     │
│ - ExtractSkeleton(RootNode) : USkeleton*                   │
│ - ExtractMeshData(MeshNode, OutMesh) : bool                │
│ - ExtractSkinWeights(Mesh, OutMesh) : bool                 │
│ - ExtractBindPose(Scene, OutSkeleton) : bool               │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ creates
                          ▼
        ┌─────────────────────────────────┐
        │      USkeletalMesh              │
        ├─────────────────────────────────┤
        │ - USkeleton* Skeleton           │
        │ - TArray<FSkinnedVertex> Vertices│
        │ - TArray<uint32> Indices        │
        │ - ID3D11Buffer* VertexBuffer    │
        │ - ID3D11Buffer* IndexBuffer     │
        ├─────────────────────────────────┤
        │ + SetSkeleton(Skeleton)         │
        │ + SetVertices(Vertices)         │
        │ + SetIndices(Indices)           │
        │ + CreateGPUResources(Device)    │
        └─────────────────────────────────┘
                          │
                          │ references
                          ▼
        ┌─────────────────────────────────┐
        │         USkeleton               │
        ├─────────────────────────────────┤
        │ - TArray<FBoneInfo> Bones       │
        │ - TMap<FString, int32> BoneMap  │
        ├─────────────────────────────────┤
        │ + AddBone(Name, Parent) : int32 │
        │ + FindBoneIndex(Name) : int32   │
        │ + GetBone(Index) : FBoneInfo&   │
        │ + SetBindPoseTransform(...)     │
        │ + SetInverseBindPoseMatrix(...) │
        │ + FinalizeBones()               │
        └─────────────────────────────────┘
```

### 주요 파일 구조

```
Mundi/Source/Runtime/AssetManagement/
├── FbxImporter.h                    # FFbxImporter 클래스 정의
├── FbxImporter.cpp                  # Import 파이프라인 구현 (800+ lines)
├── FbxImportOptions.h               # Import 옵션 정의
├── Skeleton.h                       # USkeleton 클래스 (Bone 계층)
├── Skeleton.cpp                     # Skeleton 관리 로직
├── SkeletalMesh.h                   # USkeletalMesh 클래스
└── SkeletalMesh.cpp                 # Mesh 데이터 관리

Mundi/Source/Runtime/Engine/
├── Components/
│   └── SkinnedMeshComponent.h/cpp   # 렌더링 컴포넌트 (GPU 미구현)
└── GameFramework/
    └── SkeletalMeshActor.h/cpp      # Actor 클래스

Mundi/Source/Slate/Widgets/
└── MainToolbarWidget.cpp            # Editor 메뉴 통합 (Import 버튼)
```

---

## Import 파이프라인

### 전체 흐름도

```
User: "Import FBX" (MainToolbar)
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 1: Scene 로딩 및 전처리                           │
├─────────────────────────────────────────────────────────┤
│ 1. LoadScene(FilePath)                                  │
│    - FBX SDK로 파일 로드                                │
│    - Scene 객체 생성                                    │
│                                                         │
│ 2. ConvertScene()                                       │
│    - 좌표계 변환: Z-Up, X-Forward, Left-Handed         │
│    - 단위 변환: ImportScale 적용                        │
│                                                         │
│ 3. Triangulate                                          │
│    - FbxGeometryConverter::Triangulate()               │
│    - 모든 Polygon을 Triangle로 변환                     │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 2: Mesh Node 탐색                                 │
├─────────────────────────────────────────────────────────┤
│ FindFirstMeshNode(RootNode)                             │
│    - Scene 계층 구조를 재귀적으로 탐색                  │
│    - FbxNodeAttribute::eMesh 타입 찾기                 │
│    - 첫 번째 Mesh Node 반환                             │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 3: Skeleton 추출                                  │
├─────────────────────────────────────────────────────────┤
│ ExtractSkeleton(RootNode)                               │
│    │                                                    │
│    ├─ 1. USkeleton 객체 생성                            │
│    │                                                    │
│    ├─ 2. 재귀적 Bone Hierarchy 구축                     │
│    │    - Lambda 함수로 재귀 탐색                        │
│    │    - FbxNodeAttribute::eSkeleton 체크              │
│    │    - Bone 이름, Parent Index 추출                  │
│    │    - Local Transform 추출 (FbxAMatrix)            │
│    │    - nodeToIndexMap에 매핑 저장                     │
│    │                                                    │
│    └─ 3. FinalizeBones()                                │
│         - Bone 계층 로그 출력                           │
│         - 검증 완료                                     │
│                                                         │
│ Result: USkeleton with N bones                          │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 4: USkeletalMesh 생성 및 연결                     │
├─────────────────────────────────────────────────────────┤
│ USkeletalMesh* skeletalMesh = NewObject<...>()         │
│ skeletalMesh->SetSkeleton(skeleton)                    │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 5: Mesh 데이터 추출                               │
├─────────────────────────────────────────────────────────┤
│ ExtractMeshData(MeshNode, OutSkeletalMesh)             │
│    │                                                    │
│    ├─ 1. FBX Mesh 가져오기                              │
│    │    - MeshNode->GetMesh()                          │
│    │    - Control Points 포인터 가져오기                │
│    │                                                    │
│    ├─ 2. Polygon 순회 (각 Triangle마다)                │
│    │    for (polyIndex in polygonCount)                │
│    │    {                                               │
│    │        for (vertInPoly = 0; vertInPoly < 3; ++i)  │
│    │        {                                           │
│    │            ├─ Position: Control Point에서 추출     │
│    │            ├─ Normal: GetPolygonVertexNormal()    │
│    │            ├─ UV: GetPolygonVertexUV()            │
│    │            ├─ Tangent: GetPolygonVertexTangent()  │
│    │            └─ 새로운 Vertex 생성 (중복 허용)       │
│    │        }                                           │
│    │    }                                               │
│    │                                                    │
│    └─ 3. SkeletalMesh에 데이터 설정                     │
│         - SetVertices(vertices)                        │
│         - SetIndices(indices)                          │
│                                                         │
│ Result: 1356 vertices, 1356 indices (452 triangles)    │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 6: Skin Weights 추출 (⚠️ PoC - 미완성)            │
├─────────────────────────────────────────────────────────┤
│ ExtractSkinWeights(FbxMesh, OutSkeletalMesh)           │
│    │                                                    │
│    ├─ 1. Skin Deformer 가져오기                         │
│    │    - GetDeformerCount(FbxDeformer::eSkin)         │
│    │    - 첫 번째 Skin 사용                             │
│    │                                                    │
│    ├─ 2. Cluster 순회 (각 Bone마다)                     │
│    │    for (cluster in clusterCount)                  │
│    │    {                                               │
│    │        ├─ Link Node (Bone) 가져오기                │
│    │        ├─ Skeleton에서 Bone Index 찾기             │
│    │        ├─ Control Point Indices 가져오기           │
│    │        ├─ Weights 가져오기                         │
│    │        └─ controlPointInfluences에 저장            │
│    │    }                                               │
│    │                                                    │
│    ├─ 3. Polygon Vertex별 Weight 재계산 (문제!)         │
│    │    - Control Point → Polygon Vertex 매핑 시도      │
│    │    - ExtractMeshData의 vertices와 다른 배열 생성    │
│    │    - ⚠️ 두 배열이 병합되지 않음!                    │
│    │                                                    │
│    └─ ⚠️ 현재 문제점:                                    │
│         - ExtractMeshData의 vertices: Bone Weights 없음 │
│         - ExtractSkinWeights의 vertices: 별도 생성      │
│         - 병합 로직 필요 (미구현)                        │
│                                                         │
│ Result: Skin weights 추출되나 실제 적용 안됨            │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 7: Bind Pose 추출                                 │
├─────────────────────────────────────────────────────────┤
│ ExtractBindPose(Scene, OutSkeleton)                     │
│    │                                                    │
│    ├─ 1. Scene에서 Bind Pose 찾기                       │
│    │    - GetPoseCount()                               │
│    │    - IsBindPose() 체크                             │
│    │                                                    │
│    ├─ 2. Pose Node 순회                                 │
│    │    for (node in poseNodeCount)                    │
│    │    {                                               │
│    │        ├─ Bone Index 찾기 (이름으로)               │
│    │        ├─ Bind Matrix 가져오기                     │
│    │        ├─ Inverse Matrix 계산                      │
│    │        └─ SetInverseBindPoseMatrix()              │
│    │    }                                               │
│    │                                                    │
│    └─ Result: Inverse Bind Pose Matrices 저장          │
│                                                         │
│ Result: 5 bones with inverse bind matrices             │
└─────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│ Phase 8: Editor 통합 (MainToolbarWidget)                │
├─────────────────────────────────────────────────────────┤
│ 1. ASkeletalMeshActor 생성                              │
│    - GWorld->SpawnActor<ASkeletalMeshActor>()         │
│                                                         │
│ 2. Mesh 설정                                            │
│    - NewActor->SetSkeletalMesh(ImportedMesh)          │
│                                                         │
│ 3. 위치 설정                                            │
│    - Camera 앞쪽 3.0 units에 배치                       │
│                                                         │
│ 4. 이름 설정                                            │
│    - FBX 파일명 기반 고유 이름 생성                     │
│                                                         │
│ ❌ GPU 리소스 생성 누락!                                 │
│    - CreateGPUResources() 호출 안됨                     │
│    - VertexBuffer/IndexBuffer 없음                     │
│                                                         │
│ Result: Actor spawned but not rendered                 │
└─────────────────────────────────────────────────────────┘
         │
         ▼
    ✅ Import Complete
    ❌ Rendering Fail
```

### 단계별 상세 설명

#### Phase 1: Scene 로딩 및 전처리

**파일:** `FbxImporter.cpp:179-208`

```cpp
bool FFbxImporter::LoadScene(const FString& FilePath)
{
    // 1. FBX Importer 생성
    Importer = FbxImporter::Create(SdkManager, "");

    // 2. 파일 초기화
    if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
        return false;

    // 3. Scene에 Import
    if (!Importer->Import(Scene))
        return false;

    return true;
}

void FFbxImporter::ConvertScene()
{
    // 좌표계 변환: Z-Up, X-Forward, Left-Handed
    FbxAxisSystem mundiAxis(
        FbxAxisSystem::eZAxis,      // Z-Up
        FbxAxisSystem::eParityEven, // X-Forward
        FbxAxisSystem::eLeftHanded  // Left-Handed
    );

    FbxAxisSystem sceneAxis = Scene->GetGlobalSettings().GetAxisSystem();
    if (sceneAxis != mundiAxis)
    {
        mundiAxis.ConvertScene(Scene);
    }
}
```

**중요 포인트:**
- FBX SDK는 다양한 좌표계 지원 (Right-Handed, Y-Up 등)
- Mundi는 DirectX 표준 (Left-Handed, Z-Up) 사용
- `FbxAxisSystem::ConvertScene()`이 자동 변환 수행
- Vertex winding order도 자동 보정됨

#### Phase 3: Skeleton 추출

**파일:** `FbxImporter.cpp:272-346`

```cpp
USkeleton* FFbxImporter::ExtractSkeleton(FbxNode* RootNode)
{
    USkeleton* skeleton = ObjectFactory::NewObject<USkeleton>();

    // FbxNode* → Bone Index 매핑
    TMap<FbxNode*, int32> nodeToIndexMap;

    // 재귀 Lambda 함수
    std::function<void(FbxNode*, int32)> extractBoneHierarchy;
    extractBoneHierarchy = [&](FbxNode* Node, int32 ParentIndex)
    {
        bool bIsBone = false;
        int32 currentIndex = -1;

        // Bone 타입 체크
        FbxNodeAttribute* attr = Node->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            // Bone 추가
            FString boneName = Node->GetName();
            currentIndex = skeleton->AddBone(boneName, ParentIndex);

            // Local Transform 추출
            FbxAMatrix localMatrix = Node->EvaluateLocalTransform();
            FTransform localTransform = ConvertFbxTransform(localMatrix);
            skeleton->SetBindPoseTransform(currentIndex, localTransform);

            // 매핑 저장
            nodeToIndexMap[Node] = currentIndex;
            bIsBone = true;
        }

        // 자식 노드 재귀 탐색
        int32 childParentIndex = bIsBone ? currentIndex : ParentIndex;
        for (int i = 0; i < Node->GetChildCount(); i++)
        {
            extractBoneHierarchy(Node->GetChild(i), childParentIndex);
        }
    };

    // Root부터 탐색 시작
    extractBoneHierarchy(RootNode, -1);

    skeleton->FinalizeBones();
    return skeleton;
}
```

**핵심 알고리즘:**
1. Lambda 재귀 함수로 Tree 탐색
2. `FbxNodeAttribute::eSkeleton` 타입 체크
3. Parent Index 전달하며 계층 구조 유지
4. `TMap<FbxNode*, int32>`로 FBX Node → Bone Index 매핑

#### Phase 5: Mesh 데이터 추출

**파일:** `FbxImporter.cpp:385-541`

**중요 개념:**
- **Control Point**: FBX의 원본 정점 위치 (228개)
- **Polygon Vertex**: 각 Triangle의 실제 정점 (1356개)
- FBX는 Polygon마다 다른 Normal/UV를 가질 수 있음
- 따라서 Control Point를 Expand하여 새로운 Vertex 생성

```cpp
// Control Points (위치 정보)
FbxVector4* controlPoints = fbxMesh->GetControlPoints();
int32 vertexCount = fbxMesh->GetControlPointsCount();  // 228

// Polygon 순회 (Triangle마다)
for (int32 polyIndex = 0; polyIndex < polygonCount; polyIndex++)
{
    for (int32 vertInPoly = 0; vertInPoly < 3; vertInPoly++)
    {
        FSkinnedVertex vertex;

        // Control Point Index 가져오기
        int32 ctrlPointIndex = fbxMesh->GetPolygonVertex(polyIndex, vertInPoly);

        // Position: Control Point에서 가져옴
        FbxVector4 pos = controlPoints[ctrlPointIndex];
        vertex.Position = FVector(pos[0], pos[1], pos[2]);

        // Normal: Polygon Vertex별로 다름
        FbxVector4 normal;
        fbxMesh->GetPolygonVertexNormal(polyIndex, vertInPoly, normal);
        vertex.Normal = FVector(normal[0], normal[1], normal[2]);

        // UV: Polygon Vertex별로 다름
        FbxVector2 uv;
        fbxMesh->GetPolygonVertexUV(polyIndex, vertInPoly, uvSetName, uv, unmapped);
        vertex.UV = FVector2D(uv[0], 1.0f - uv[1]);  // V축 Flip

        // 새로운 Vertex 추가 (중복 허용)
        vertices.push_back(vertex);
        indices.push_back(vertexIndexCounter++);
    }
}

// Result: 452 triangles × 3 vertices = 1356 vertices
```

#### Phase 6: Skin Weights 추출 (⚠️ 문제 있음)

**파일:** `FbxImporter.cpp:543-704`

**현재 구조:**
```cpp
// 1. Control Point별 Bone Influences 수집
TArray<TArray<FBoneInfluence>> controlPointInfluences;
controlPointInfluences.resize(controlPointCount);  // 228개

for (cluster in clusterCount)
{
    // Cluster = 하나의 Bone에 대한 영향
    int32* controlPointIndices = cluster->GetControlPointIndices();
    double* weights = cluster->GetControlPointWeights();

    for (int32 i = 0; i < indexCount; ++i)
    {
        int32 cpIndex = controlPointIndices[i];
        controlPointInfluences[cpIndex].push_back(
            FBoneInfluence(boneIndex, weight)
        );
    }
}

// 2. Polygon Vertex별 Weight 적용 시도
TArray<FSkinnedVertex> vertices;  // ⚠️ ExtractMeshData와 다른 배열!
for (polygon in polygonCount)
{
    for (vert in 3)
    {
        int32 ctrlPointIndex = fbxMesh->GetPolygonVertex(...);

        // Control Point의 Bone Influences 복사
        const TArray<FBoneInfluence>& influences =
            controlPointInfluences[ctrlPointIndex];

        // 상위 4개 선택, 정규화
        // ...

        vertices.push_back(vertex);  // ⚠️ 새 배열에 저장!
    }
}

// ⚠️ 문제: ExtractMeshData의 vertices와 병합 안됨!
```

**문제점:**
- `ExtractMeshData()`에서 생성한 `vertices` 배열: Bone Weights 없음
- `ExtractSkinWeights()`에서 생성한 `vertices` 배열: 별도로 생성
- 두 배열을 병합하는 로직 없음
- 결과: Mesh에는 Bone Weights가 실제로 적용 안됨

**해결 방법 (향후):**
1. `ExtractMeshData()`에서 Bone Weights 필드를 0으로 초기화
2. `ExtractSkinWeights()`에서 기존 배열 수정
3. 또는 Control Point → Polygon Vertex 매핑 테이블 생성

---

## 자료구조

### FBoneInfo (Skeleton.h:9-42)

```cpp
struct FBoneInfo
{
    // Bone 이름
    FString Name;

    // 부모 Bone 인덱스 (-1 = Root Bone)
    int32 ParentIndex;

    // Bind Pose Transform (Local Space)
    // 초기 상태에서 부모 본 기준의 상대 Transform
    FTransform BindPoseTransform;

    // Inverse Bind Pose Matrix (Global Space)
    // Skinning 시 사용 (Vertex를 Bone Space로 변환)
    FMatrix InverseBindPoseMatrix;
};
```

**용도:**
- `BindPoseTransform`: T-Pose 또는 A-Pose 상태의 Local Transform
- `InverseBindPoseMatrix`: GPU Skinning에서 Vertex를 Bone Space로 변환

**계산식 (Skinning):**
```
FinalVertexPosition = ∑(BoneWeight[i] × BoneMatrix[i] × InverseBindPoseMatrix[i] × OriginalPosition)
```

### USkeleton (Skeleton.h:55-150)

```cpp
class USkeleton : public UResourceBase
{
private:
    // Bone 배열 (인덱스 순서대로 저장)
    TArray<FBoneInfo> Bones;

    // Bone 이름 → 인덱스 맵 (빠른 검색용)
    TMap<FString, int32> BoneNameToIndexMap;

public:
    // Bone 추가 (Import 시)
    int32 AddBone(const FString& BoneName, int32 ParentIndex);

    // Bone 검색 (Skin Weights 추출 시)
    int32 FindBoneIndex(const FString& BoneName) const;
    const FBoneInfo& GetBone(int32 BoneIndex) const;

    // Bind Pose 설정 (Import 시)
    void SetBindPoseTransform(int32 BoneIndex, const FTransform& Transform);
    void SetInverseBindPoseMatrix(int32 BoneIndex, const FMatrix& Matrix);

    // 자식 Bone 검색 (향후 Animation용)
    TArray<int32> GetChildBones(int32 BoneIndex) const;
};
```

**주요 메서드:**
- `AddBone()`: FBX Import 시 호출, Bone 계층 구축
- `FindBoneIndex()`: Skin Weights 추출 시 Bone 이름으로 Index 찾기
- `SetInverseBindPoseMatrix()`: Bind Pose 추출 시 설정

### FSkinnedVertex (SkeletalMesh.h:39-71)

```cpp
struct FSkinnedVertex
{
    // 위치 (Local Space)
    FVector Position;

    // 법선 (Local Space)
    FVector Normal;

    // UV 좌표
    FVector2D UV;

    // Tangent (접선) - Normal Mapping용
    FVector4 Tangent;  // w = Bitangent 방향 (±1)

    // Bone 인덱스 (최대 4개)
    int32 BoneIndices[4];

    // Bone 가중치 (최대 4개, 합이 1.0)
    float BoneWeights[4];
};
```

**GPU Layout:**
```cpp
// D3D11 Input Layout
D3D11_INPUT_ELEMENT_DESC layout[] =
{
    { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  ... },
    { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, ... },
    { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, ... },
    { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, ... },
    { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT,  0, 48, ... },
    { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, ... },
};

// Total Size: 80 bytes per vertex
```

### USkeletalMesh (SkeletalMesh.h:83-190)

```cpp
class USkeletalMesh : public UResourceBase
{
private:
    // Skeleton 참조
    USkeleton* Skeleton = nullptr;

    // CPU Mesh 데이터
    TArray<FSkinnedVertex> Vertices;
    TArray<uint32> Indices;

    // GPU 리소스
    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;
    uint32 VertexCount = 0;
    uint32 IndexCount = 0;

public:
    // Skeleton 관리
    void SetSkeleton(USkeleton* InSkeleton);
    USkeleton* GetSkeleton() const;

    // Mesh 데이터 설정 (Import 시)
    void SetVertices(const TArray<FSkinnedVertex>& InVertices);
    void SetIndices(const TArray<uint32>& InIndices);

    // GPU 리소스 관리 (❌ 현재 호출 안됨)
    bool CreateGPUResources(ID3D11Device* Device);
    ID3D11Buffer* GetVertexBuffer() const;
    ID3D11Buffer* GetIndexBuffer() const;
};
```

**데이터 흐름:**
```
FBX Import → SetVertices(CPUData) → CreateGPUResources() → GPU Buffers
                                         ▲
                                         │
                                    ❌ 호출 안됨!
```

### USkinnedMeshComponent (SkinnedMeshComponent.h:27-88)

```cpp
class USkinnedMeshComponent : public UMeshComponent
{
protected:
    // SkeletalMesh 참조
    USkeletalMesh* SkeletalMesh = nullptr;

    // Material 슬롯
    TArray<UMaterialInterface*> MaterialSlots;

public:
    // SkeletalMesh 설정
    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

    // 렌더링 (❌ 현재 GPU Buffer 없어서 Early Return)
    void CollectMeshBatches(TArray<FMeshBatchElement>& OutBatches, ...);

    // Skeleton 접근
    USkeleton* GetSkeleton() const;
};
```

**렌더링 흐름:**
```cpp
void USkinnedMeshComponent::CollectMeshBatches(...)
{
    if (!SkeletalMesh)
        return;

    ID3D11Buffer* VertexBuffer = SkeletalMesh->GetVertexBuffer();
    ID3D11Buffer* IndexBuffer = SkeletalMesh->GetIndexBuffer();

    if (!VertexBuffer || !IndexBuffer)
        return;  // ❌ 여기서 중단!

    // Mesh Batch 추가...
}
```

---

## 좌표계 변환

### Mundi 엔진 좌표계

**표준:** DirectX Left-Handed, Z-Up

```
        +Z (Up)
         │
         │
         │
         └────────── +X (Forward)
        ╱
       ╱
     +Y (Right)
```

| 축 | 방향 | 설명 |
|----|------|------|
| **+X** | Forward | 캐릭터가 바라보는 방향 |
| **+Y** | Right | 오른쪽 방향 |
| **+Z** | Up | 위쪽 (중력 반대) |

**Handedness:** Left-Handed (왼손 좌표계)
- 왼손 법칙: 엄지(X) × 검지(Y) = 중지(Z)
- Vertex Winding: Clockwise (시계 방향 = Front Face)

### FBX 좌표계 예시

**Blender (기본):**
```
Right-Handed, Z-Up, Y-Forward

        +Z (Up)
         │
         │
         │
         └────────── +Y (Forward)
        ╱
       ╱
    -X (Right)
```

**Maya (기본):**
```
Right-Handed, Y-Up, Z-Forward

        +Y (Up)
         │
         │
         │
         └────────── +Z (Forward)
        ╱
       ╱
     +X (Right)
```

### 자동 변환 (FbxAxisSystem)

**FbxImporter.cpp:93-129**

```cpp
void FFbxImporter::ConvertScene()
{
    // Mundi 좌표계 정의
    FbxAxisSystem mundiAxis(
        FbxAxisSystem::eZAxis,       // Up Axis: Z
        FbxAxisSystem::eParityEven,  // Front Axis: X (ParityEven)
        FbxAxisSystem::eLeftHanded   // Handedness: Left
    );

    // Scene 좌표계 확인
    FbxAxisSystem sceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

    if (sceneAxis != mundiAxis)
    {
        // 자동 변환
        mundiAxis.ConvertScene(Scene);

        // ✅ 모든 Vertex, Normal, Transform 자동 변환됨!
        // ✅ Winding Order도 자동 보정됨!
    }

    // 단위 변환 (ImportScale 적용)
    ConvertSceneUnit(CurrentOptions.ImportScale);
}
```

**변환 매트릭스 예시 (Blender → Mundi):**
```
Blender (Right-Handed, Z-Up, Y-Forward)
    ↓
FbxAxisSystem::ConvertScene()
    ↓
Mundi (Left-Handed, Z-Up, X-Forward)

Transform Matrix:
[  0  1  0  0 ]    X_new = Y_old
[ -1  0  0  0 ]    Y_new = -X_old
[  0  0  1  0 ]    Z_new = Z_old
[  0  0  0  1 ]
```

**Winding Order 보정:**
- Right-Handed: Counter-Clockwise (CCW) = Front Face
- Left-Handed: Clockwise (CW) = Front Face
- `ConvertScene()`이 Triangle Index 순서 자동 반전

### 좌표 변환 Helper

**FbxImporter.cpp:794-810**

```cpp
FMatrix FFbxImporter::ConvertFbxMatrix(const FbxMatrix& fbxMatrix)
{
    FMatrix matrix;

    // FBX: Column-Major, Mundi: Row-Major
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            matrix.m[row][col] = static_cast<float>(
                fbxMatrix.Get(row, col)
            );
        }
    }

    return matrix;
}
```

---

## 현재 한계점

### 1. Skin Weights 미적용 ⚠️

**문제:**
- `ExtractMeshData()`와 `ExtractSkinWeights()`가 별도의 Vertex 배열 생성
- 두 배열을 병합하는 로직 없음
- 결과: Mesh의 Bone Weights가 실제로 적용 안됨

**증상:**
```cpp
// ExtractMeshData에서 생성
TArray<FSkinnedVertex> vertices;  // BoneWeights = {0,0,0,0}

// ExtractSkinWeights에서 생성
TArray<FSkinnedVertex> vertices;  // BoneWeights = 실제 값 (별도 배열!)

// SkeletalMesh에 설정된 것은 ExtractMeshData의 vertices
OutSkeletalMesh->SetVertices(vertices);  // ❌ Weights 없음!
```

**해결 방법:**
```cpp
// Option 1: ExtractMeshData에서 Control Point Index 저장
struct FTempVertex
{
    FSkinnedVertex Vertex;
    int32 ControlPointIndex;  // 추가!
};

// Option 2: ExtractSkinWeights에서 기존 배열 수정
void ExtractSkinWeights(FbxMesh* Mesh, USkeletalMesh* OutSkeletalMesh)
{
    // OutSkeletalMesh->GetVertices() 수정
    TArray<FSkinnedVertex>& vertices = OutSkeletalMesh->GetVerticesRef();

    // 각 Vertex의 ControlPointIndex 기반으로 Weights 적용
    // ...
}
```

### 2. GPU 리소스 미생성 ❌

**문제:**
- `CreateGPUResources()` 함수는 존재하나 호출 안됨
- VertexBuffer, IndexBuffer가 nullptr
- `CollectMeshBatches()`에서 Early Return

**코드 위치:**
```cpp
// MainToolbarWidget.cpp:581
USkeletalMesh* ImportedMesh = FbxImporter.ImportSkeletalMesh(PathStr, Options);

// ❌ CreateGPUResources() 호출 누락!

// NewActor->SetSkeletalMesh(ImportedMesh);
```

**해결 방법:**
```cpp
USkeletalMesh* ImportedMesh = FbxImporter.ImportSkeletalMesh(PathStr, Options);
if (ImportedMesh)
{
    // ✅ GPU 리소스 생성
    ImportedMesh->CreateGPUResources(GEngine->GetDevice());

    // Actor 생성...
}
```

### 3. GPU Skinning 미구현 ❌

**문제:**
- 현재 Shader (UberLit.hlsl)는 Static Mesh용
- Bone Indices/Weights를 Input으로 받지 않음
- Bone Matrix Constant Buffer 없음

**필요한 작업:**
1. SkinnedMesh 전용 Shader 생성
2. Bone Matrix CB 생성 (128 bones)
3. Vertex Shader에서 GPU Skinning 수행

**예시 Shader:**
```hlsl
// SkinnedMesh.hlsl
struct VS_INPUT_SKINNED
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    int4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

cbuffer BoneMatrices : register(b6)
{
    row_major float4x4 Bones[128];
};

PS_INPUT mainVS(VS_INPUT_SKINNED Input)
{
    // GPU Skinning
    float4 skinnedPos = float4(0,0,0,0);

    for (int i = 0; i < 4; i++)
    {
        int boneIdx = Input.BoneIndices[i];
        float weight = Input.BoneWeights[i];

        skinnedPos += weight * mul(float4(Input.Position, 1), Bones[boneIdx]);
    }

    // World Transform...
}
```

### 4. Animation 미지원 ❌

**현재 상태:**
- Bind Pose만 추출됨
- Animation Sequence 추출 로직 없음
- Animation Blueprint 시스템 없음

---

## 향후 작업

### Phase 6: GPU 리소스 및 렌더링 구현

**우선순위:** ⭐⭐⭐ (필수)

**작업 목록:**

1. **GPU 리소스 생성 추가**
   ```cpp
   // MainToolbarWidget.cpp
   USkeletalMesh* ImportedMesh = FbxImporter.ImportSkeletalMesh(...);
   ImportedMesh->CreateGPUResources(GEngine->GetDevice());
   ```

2. **Skin Weights 병합 로직**
   ```cpp
   // FbxImporter.cpp
   bool ExtractSkinWeights(FbxMesh* Mesh, USkeletalMesh* OutSkeletalMesh)
   {
       // 기존 Vertices 배열 가져오기
       TArray<FSkinnedVertex>& vertices = OutSkeletalMesh->GetVerticesRef();

       // Control Point → Polygon Vertex 매핑
       TMap<int32, TArray<int32>> cpToVertexMap;

       // Weights 적용
       // ...
   }
   ```

3. **SkinnedMesh Shader 생성**
   - `Shaders/Materials/SkinnedMesh.hlsl` 생성
   - Bone Matrix CB 지원
   - GPU Skinning 구현

4. **Bone Matrix 계산 시스템**
   ```cpp
   // USkinnedMeshComponent
   void UpdateBoneMatrices()
   {
       // Bind Pose → Current Pose Transform
       // InverseBindPoseMatrix 적용
       // Parent Transform 누적
   }
   ```

**예상 소요 시간:** 4-6시간

### Phase 7: StaticMesh Import 구현

**우선순위:** ⭐⭐ (중요)

**작업 목록:**
1. `ImportStaticMesh()` 구현
2. Bone 없는 Mesh 처리
3. UStaticMesh로 저장

**예상 소요 시간:** 2-3시간

### Phase 8: Animation Import 구현

**우선순위:** ⭐ (향후)

**작업 목록:**
1. `ImportAnimation()` 구현
2. FbxAnimStack/FbxAnimLayer 처리
3. Keyframe 추출
4. Animation Sequence 생성

**예상 소요 시간:** 8-12시간

---

## 참고 자료

### 코드 위치

| 파일 | 라인 | 설명 |
|------|------|------|
| `FbxImporter.cpp` | 179-262 | `ImportSkeletalMesh()` 메인 로직 |
| `FbxImporter.cpp` | 272-346 | `ExtractSkeleton()` Bone 계층 추출 |
| `FbxImporter.cpp` | 385-541 | `ExtractMeshData()` Vertex 추출 |
| `FbxImporter.cpp` | 543-704 | `ExtractSkinWeights()` Skin Weights |
| `FbxImporter.cpp` | 708-791 | `ExtractBindPose()` Bind Pose |
| `Skeleton.cpp` | 7-31 | `AddBone()` Bone 추가 |
| `SkeletalMesh.cpp` | 12-26 | `SetVertices()`, `SetIndices()` |
| `SkeletalMesh.cpp` | 28-88 | `CreateGPUResources()` |
| `MainToolbarWidget.cpp` | 570-626 | Editor 통합 (Import 버튼) |

### 관련 문서

- [FBX_SKELETAL_MESH_IMPORTER_PLAN.md](FBX_SKELETAL_MESH_IMPORTER_PLAN.md) - 원본 계획 문서
- [ARCHITECTURE.md](../ARCHITECTURE.md) - Mundi 엔진 아키텍처
- [CLAUDE.md](../CLAUDE.md) - 코딩 가이드라인
- [README.md](../README.md) - 좌표계 설명

### 외부 자료

- [FBX SDK Documentation](https://help.autodesk.com/view/FBX/2020/ENU/)
- [Unreal Engine FBX Import Pipeline](https://github.com/EpicGames/UnrealEngine/blob/release/Engine/Source/Editor/UnrealEd/Private/Fbx/FbxSkeletalMeshImport.cpp)
- [GPU Skinning Tutorial](https://www.3dgep.com/skeletal-animation-with-directx-11/)

---

**문서 버전:** 1.0
**최종 수정일:** 2025-11-08
**작성자:** Claude Code
**검토 상태:** ✅ Phase 5 PoC 완료, Phase 6 대기
