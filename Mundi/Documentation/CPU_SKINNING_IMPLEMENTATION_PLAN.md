# CPU Skinning 구현 계획

**작성일:** 2025-11-08
**상태:** 📋 계획 단계
**목표:** SkeletalMesh의 CPU 기반 Vertex Skinning 구현

---

## 📋 목차

- [개요](#개요)
- [현재 상황 분석](#현재-상황-분석)
- [Phase 1: Skin Weights 병합](#phase-1-skin-weights-병합)
- [Phase 2: Bone Transform 시스템](#phase-2-bone-transform-시스템)
- [Phase 3: CPU Skinning 계산](#phase-3-cpu-skinning-계산)
- [Phase 4: Dynamic GPU Buffer 업데이트](#phase-4-dynamic-gpu-buffer-업데이트)
- [Phase 5: 최적화 및 테스트](#phase-5-최적화-및-테스트)
- [전체 작업 일정](#전체-작업-일정)
- [체크리스트](#체크리스트)

---

## 개요

### 목표
FBX로 Import한 SkeletalMesh를 CPU에서 Skinning하여 렌더링

### Skinning이란?
Bone Transform에 따라 Vertex 위치를 변형하는 과정:
```
FinalVertexPosition = Σ(BoneWeight[i] × BoneMatrix[i] × OriginalPosition)
```

### 구현 방식
- **CPU Skinning**: 매 프레임 CPU에서 Vertex 계산 → GPU 업로드
- **GPU Skinning (향후)**: Vertex Shader에서 계산 (더 효율적)

---

## 현재 상황 분석

### ✅ 완료된 사항
1. **FBX Import**: Skeleton, Mesh 데이터 추출 완료
2. **Bind Pose 추출**: Inverse Bind Pose Matrix 완료
3. **GPU 리소스 생성**: Vertex/Index Buffer 생성 (Dual-buffer)
4. **Bind Pose 렌더링**: Static Mesh로 렌더링 작동 확인

### ❌ 미완성 사항
1. **Skin Weights 병합**: ExtractMeshData와 ExtractSkinWeights가 별도 배열 생성
2. **Bone Transform 시스템**: Runtime Transform 계산 로직 없음
3. **CPU Skinning 계산**: Vertex Skinning 로직 없음
4. **Dynamic GPU Buffer**: 매 프레임 Vertex 업데이트 메커니즘 없음

### 핵심 문제점

#### 문제 1: Skin Weights 미적용
**위치:** `FbxImporter.cpp:385-707`

```cpp
// ExtractMeshData (Line 385-550)
TArray<FSkinnedVertex> vertices;  // BoneWeights = {0,0,0,0}
OutSkeletalMesh->SetVertices(vertices);

// ExtractSkinWeights (Line 552-707)
TArray<FSkinnedVertex> vertices;  // 새로운 배열! (병합 안됨)
// OutSkeletalMesh->SetVertices() 호출 안함 ❌
```

**결과:** Mesh의 Bone Weights가 모두 0

#### 문제 2: GPU Buffer가 Static
**위치:** `SkeletalMesh.cpp:67-71`

```cpp
D3D11_BUFFER_DESC vbDesc = {};
vbDesc.Usage = D3D11_USAGE_DEFAULT;  // ❌ CPU 수정 불가
vbDesc.CPUAccessFlags = 0;           // ❌ CPU 접근 불가
```

**필요:** `D3D11_USAGE_DYNAMIC` + `D3D11_CPU_ACCESS_WRITE`

---

## Phase 1: Skin Weights 병합

**우선순위:** ⭐⭐⭐ (필수)
**예상 시간:** 1-2시간
**상태:** ⬜ 미작업

### 목표
ExtractMeshData의 vertices에 Bone Weights를 올바르게 적용

### 구현 계획

#### 1.1. USkeletalMesh에 매핑 데이터 추가

**파일:** `SkeletalMesh.h`

```cpp
class USkeletalMesh : public UResourceBase
{
private:
    TArray<FSkinnedVertex> Vertices;
    TArray<uint32> Indices;

    // ✅ 추가: Polygon Vertex → Control Point 매핑
    TArray<int32> VertexToControlPointMap;

public:
    // ✅ 추가: 매핑 데이터 설정
    void SetVertexToControlPointMap(const TArray<int32>& InMap)
    {
        VertexToControlPointMap = InMap;
    }

    // ✅ 추가: 매핑 데이터 가져오기
    const TArray<int32>& GetVertexToControlPointMap() const
    {
        return VertexToControlPointMap;
    }

    // ✅ 추가: Vertices 참조 가져오기 (수정 가능)
    TArray<FSkinnedVertex>& GetVerticesRef()
    {
        return Vertices;
    }
};
```

#### 1.2. ExtractMeshData 수정

**파일:** `FbxImporter.cpp:385-550`

```cpp
bool FFbxImporter::ExtractMeshData(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh)
{
    FbxMesh* fbxMesh = MeshNode->GetMesh();
    // ...

    TArray<FSkinnedVertex> vertices;
    TArray<uint32> indices;

    // ✅ 추가: Control Point Index 저장
    TArray<int32> vertexToControlPointMap;

    int32 vertexIndexCounter = 0;
    for (int32 polyIndex = 0; polyIndex < polygonCount; polyIndex++)
    {
        for (int32 vertInPoly = 0; vertInPoly < 3; vertInPoly++)
        {
            // Control Point Index 가져오기
            int32 controlPointIndex = fbxMesh->GetPolygonVertex(polyIndex, vertInPoly);

            FSkinnedVertex vertex;

            // Position
            FbxVector4 pos = controlPoints[controlPointIndex];
            vertex.Position = FVector(pos[0], pos[1], pos[2]);

            // Normal, UV, Tangent...
            // (기존 코드)

            vertices.push_back(vertex);
            indices.push_back(vertexIndexCounter);

            // ✅ 매핑 저장
            vertexToControlPointMap.push_back(controlPointIndex);

            vertexIndexCounter++;
        }
    }

    OutSkeletalMesh->SetVertices(vertices);
    OutSkeletalMesh->SetIndices(indices);

    // ✅ 매핑 저장
    OutSkeletalMesh->SetVertexToControlPointMap(vertexToControlPointMap);

    UE_LOG("[FBX] Extracted %zu vertices with control point mapping", vertices.size());

    return true;
}
```

#### 1.3. ExtractSkinWeights 수정

**파일:** `FbxImporter.cpp:552-707`

```cpp
bool FFbxImporter::ExtractSkinWeights(FbxMesh* fbxMesh, USkeletalMesh* OutSkeletalMesh)
{
    // ... (Control Point별 Bone Influences 수집 - 기존 코드)

    TArray<FControlPointInfluence> controlPointInfluences;
    controlPointInfluences.resize(controlPointCount);

    // Cluster 순회 (기존 코드)
    for (int32 clusterIndex = 0; clusterIndex < clusterCount; clusterIndex++)
    {
        // ... (기존 코드)
    }

    // ✅ 기존 Vertices 가져오기 (수정 가능)
    TArray<FSkinnedVertex>& vertices = OutSkeletalMesh->GetVerticesRef();
    const TArray<int32>& vertexToControlPointMap =
        OutSkeletalMesh->GetVertexToControlPointMap();

    if (vertices.size() != vertexToControlPointMap.size())
    {
        SetError("ExtractSkinWeights: Vertex count mismatch");
        return false;
    }

    // ✅ 각 Vertex에 Bone Weights 적용
    for (int32 vertIndex = 0; vertIndex < vertices.size(); vertIndex++)
    {
        int32 controlPointIndex = vertexToControlPointMap[vertIndex];

        if (controlPointIndex < 0 || controlPointIndex >= controlPointInfluences.size())
            continue;

        const FControlPointInfluence& influence = controlPointInfluences[controlPointIndex];

        // 상위 4개 Bone만 사용
        int32 influenceCount = std::min((int32)influence.BoneIndices.size(), 4);

        // Weight 총합 계산 (정규화용)
        float totalWeight = 0.0f;
        for (int32 i = 0; i < influenceCount; i++)
        {
            totalWeight += influence.Weights[i];
        }

        // Bone Indices와 Weights 설정
        for (int32 i = 0; i < 4; i++)
        {
            if (i < influenceCount && totalWeight > 0.0f)
            {
                vertices[vertIndex].BoneIndices[i] = influence.BoneIndices[i];
                vertices[vertIndex].BoneWeights[i] = influence.Weights[i] / totalWeight;
            }
            else
            {
                vertices[vertIndex].BoneIndices[i] = 0;
                vertices[vertIndex].BoneWeights[i] = 0.0f;
            }
        }
    }

    UE_LOG("[FBX] Applied skin weights to %zu vertices", vertices.size());

    return true;
}
```

### 검증 방법
```cpp
// Import 후 확인
const TArray<FSkinnedVertex>& vertices = skeletalMesh->GetVertices();
for (const auto& v : vertices)
{
    UE_LOG("Vertex BoneWeights: [%.3f, %.3f, %.3f, %.3f]",
           v.BoneWeights[0], v.BoneWeights[1],
           v.BoneWeights[2], v.BoneWeights[3]);
}
```

---

## Phase 2: Bone Transform 시스템

**우선순위:** ⭐⭐⭐ (필수)
**예상 시간:** 1-2시간
**상태:** ⬜ 미작업

### 목표
Runtime에서 Bone Transform을 관리하는 시스템 구축

### 구현 계획

#### 2.1. USkeleton에 Helper 메서드 추가

**파일:** `Skeleton.h`

```cpp
class USkeleton : public UResourceBase
{
public:
    // ✅ 추가: Bone 개수 반환
    int32 GetBoneCount() const
    {
        return static_cast<int32>(Bones.size());
    }
};
```

#### 2.2. USkinnedMeshComponent에 Bone Transform 추가

**파일:** `SkinnedMeshComponent.h`

```cpp
class USkinnedMeshComponent : public UMeshComponent
{
protected:
    USkeletalMesh* SkeletalMesh = nullptr;
    TArray<UMaterialInterface*> MaterialSlots;
    TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;

    // ✅ 추가: Runtime Bone Transforms
    TArray<FMatrix> BoneMatrices;  // Component Space (World)
    bool bNeedsBoneTransformUpdate = true;

public:
    // ✅ 추가: Bone Transform 계산
    void UpdateBoneTransforms();

    // ✅ 추가: Bone Matrix 가져오기
    const TArray<FMatrix>& GetBoneMatrices() const { return BoneMatrices; }

    // ✅ 추가: Specific Bone Transform 설정 (향후 Animation용)
    void SetBoneTransform(int32 BoneIndex, const FTransform& Transform);
};
```

#### 2.3. UpdateBoneTransforms 구현

**파일:** `SkinnedMeshComponent.cpp`

```cpp
void USkinnedMeshComponent::UpdateBoneTransforms()
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
        return;

    USkeleton* skeleton = SkeletalMesh->GetSkeleton();
    int32 boneCount = skeleton->GetBoneCount();

    if (boneCount == 0)
        return;

    // Bone Matrices 초기화
    BoneMatrices.resize(boneCount);

    // 각 Bone의 Component Space Transform 계산
    for (int32 boneIndex = 0; boneIndex < boneCount; boneIndex++)
    {
        const FBoneInfo& boneInfo = skeleton->GetBone(boneIndex);

        // Phase 1: Bind Pose만 사용 (Animation은 나중에)
        FMatrix localMatrix = boneInfo.BindPoseTransform.ToMatrix();

        // Parent Transform 누적 (계층 구조)
        if (boneInfo.ParentIndex >= 0)
        {
            FMatrix parentMatrix = BoneMatrices[boneInfo.ParentIndex];
            BoneMatrices[boneIndex] = localMatrix * parentMatrix;
        }
        else
        {
            // Root Bone
            BoneMatrices[boneIndex] = localMatrix;
        }

        // Inverse Bind Pose 적용 (Skinning을 위해)
        BoneMatrices[boneIndex] =
            boneInfo.InverseBindPoseMatrix * BoneMatrices[boneIndex];
    }

    bNeedsBoneTransformUpdate = false;

    UE_LOG("USkinnedMeshComponent: Updated %d bone transforms", boneCount);
}

void USkinnedMeshComponent::SetBoneTransform(int32 BoneIndex, const FTransform& Transform)
{
    // TODO: 향후 Animation 시스템에서 사용
    bNeedsBoneTransformUpdate = true;
}
```

#### 2.4. 초기화 시 Bone Transform 계산

**파일:** `SkinnedMeshComponent.cpp`

```cpp
void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    SkeletalMesh = InSkeletalMesh;

    // Material 슬롯 초기화 (기존 코드)
    // ...

    // ✅ Bone Transform 업데이트
    if (SkeletalMesh && SkeletalMesh->GetSkeleton())
    {
        UpdateBoneTransforms();
    }

    MarkWorldPartitionDirty();
}
```

---

## Phase 3: CPU Skinning 계산

**우선순위:** ⭐⭐⭐ (필수, 핵심)
**예상 시간:** 2-3시간
**상태:** ⬜ 미작업

### 목표
매 프레임 Bone Transform을 사용하여 Vertex 위치 계산

### Skinning 수식
```
SkinnedPosition = Σ(i=0 to 3) [ BoneWeight[i] × (BoneMatrix[i] × OriginalPosition) ]
SkinnedNormal = Normalize( Σ(i=0 to 3) [ BoneWeight[i] × (BoneMatrix[i] × OriginalNormal) ] )
```

### 구현 계획

#### 3.1. USkinnedMeshComponent에 Skinning 추가

**파일:** `SkinnedMeshComponent.h`

```cpp
class USkinnedMeshComponent : public UMeshComponent
{
protected:
    TArray<FMatrix> BoneMatrices;
    bool bNeedsBoneTransformUpdate = true;

    // ✅ 추가: CPU Skinning 결과 저장
    TArray<FNormalVertex> SkinnedVertices;  // GPU 전송용
    bool bEnableCPUSkinning = true;         // Skinning 활성화 플래그

public:
    // ✅ 추가: CPU Skinning 수행
    void PerformCPUSkinning();

    // ✅ 추가: Skinning 활성화/비활성화
    void SetEnableCPUSkinning(bool bEnable) { bEnableCPUSkinning = bEnable; }
    bool IsCPUSkinningEnabled() const { return bEnableCPUSkinning; }
};
```

#### 3.2. PerformCPUSkinning 구현

**파일:** `SkinnedMeshComponent.cpp`

```cpp
void USkinnedMeshComponent::PerformCPUSkinning()
{
    if (!SkeletalMesh || !bEnableCPUSkinning)
        return;

    const TArray<FSkinnedVertex>& sourceVertices = SkeletalMesh->GetVertices();
    const TArray<FMatrix>& boneMatrices = GetBoneMatrices();

    if (sourceVertices.empty() || boneMatrices.empty())
        return;

    // Skinned Vertices 준비
    SkinnedVertices.resize(sourceVertices.size());

    // 각 Vertex Skinning
    for (int32 vertIndex = 0; vertIndex < sourceVertices.size(); vertIndex++)
    {
        const FSkinnedVertex& srcVert = sourceVertices[vertIndex];
        FNormalVertex& dstVert = SkinnedVertices[vertIndex];

        // Skinning 계산
        FVector skinnedPos(0, 0, 0);
        FVector skinnedNormal(0, 0, 0);
        FVector skinnedTangent(0, 0, 0);

        // 최대 4개의 Bone Influence 적용
        for (int32 i = 0; i < 4; i++)
        {
            int32 boneIndex = srcVert.BoneIndices[i];
            float weight = srcVert.BoneWeights[i];

            if (weight > 0.0f && boneIndex >= 0 && boneIndex < boneMatrices.size())
            {
                const FMatrix& boneMatrix = boneMatrices[boneIndex];

                // Position Skinning (Affine Transform)
                FVector4 pos4 = FVector4(srcVert.Position.X,
                                        srcVert.Position.Y,
                                        srcVert.Position.Z,
                                        1.0f);  // w=1 (위치)
                FVector4 transformedPos = pos4 * boneMatrix;
                skinnedPos += FVector(transformedPos.X,
                                     transformedPos.Y,
                                     transformedPos.Z) * weight;

                // Normal Skinning (3x3 회전만 적용)
                FVector4 normal4 = FVector4(srcVert.Normal.X,
                                           srcVert.Normal.Y,
                                           srcVert.Normal.Z,
                                           0.0f);  // w=0 (방향)
                FVector4 transformedNormal = normal4 * boneMatrix;
                skinnedNormal += FVector(transformedNormal.X,
                                        transformedNormal.Y,
                                        transformedNormal.Z) * weight;

                // Tangent Skinning
                FVector4 tangent4 = FVector4(srcVert.Tangent.X,
                                            srcVert.Tangent.Y,
                                            srcVert.Tangent.Z,
                                            0.0f);  // w=0 (방향)
                FVector4 transformedTangent = tangent4 * boneMatrix;
                skinnedTangent += FVector(transformedTangent.X,
                                         transformedTangent.Y,
                                         transformedTangent.Z) * weight;
            }
        }

        // 결과 저장 (FNormalVertex 형식)
        dstVert.pos = skinnedPos;
        dstVert.normal = skinnedNormal.Normalize();
        dstVert.tex = srcVert.UV;

        // Tangent 저장 (w 성분 유지)
        FVector normalizedTangent = skinnedTangent.Normalize();
        dstVert.Tangent = FVector4(normalizedTangent.X,
                                   normalizedTangent.Y,
                                   normalizedTangent.Z,
                                   srcVert.Tangent.W);

        dstVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
```

### 최적화 고려사항
```cpp
// 향후: SIMD 최적화
#include <immintrin.h>  // SSE/AVX

// 향후: 멀티스레드
#include <execution>
std::for_each(std::execution::par, vertices.begin(), vertices.end(),
              [&](FSkinnedVertex& v) { /* Skinning */ });
```

---

## Phase 4: Dynamic GPU Buffer 업데이트

**우선순위:** ⭐⭐⭐ (필수)
**예상 시간:** 2-3시간
**상태:** ⬜ 미작업

### 목표
CPU Skinning 결과를 매 프레임 GPU로 업로드

### 현재 문제점
```cpp
// SkeletalMesh.cpp:68
vbDesc.Usage = D3D11_USAGE_DEFAULT;  // ❌ CPU 수정 불가
vbDesc.CPUAccessFlags = 0;           // ❌ CPU 접근 불가
```

### 구현 계획

#### 4.1. USkeletalMesh에 Dynamic Buffer 옵션 추가

**파일:** `SkeletalMesh.h`

```cpp
class USkeletalMesh : public UResourceBase
{
private:
    bool bUseDynamicBuffer = false;  // ✅ Dynamic Buffer 사용 여부

public:
    // ✅ 기존: Static Buffer (Bind Pose용)
    bool CreateGPUResources(ID3D11Device* Device);

    // ✅ 추가: Dynamic Buffer (CPU Skinning용)
    bool CreateDynamicGPUResources(ID3D11Device* Device);

    // ✅ 추가: Vertex Buffer 업데이트
    bool UpdateVertexBuffer(ID3D11DeviceContext* Context,
                           const TArray<FNormalVertex>& NewVertices);

    // ✅ 추가: Dynamic Buffer 사용 여부
    void SetUseDynamicBuffer(bool bDynamic) { bUseDynamicBuffer = bDynamic; }
    bool UsesDynamicBuffer() const { return bUseDynamicBuffer; }
};
```

#### 4.2. CreateDynamicGPUResources 구현

**파일:** `SkeletalMesh.cpp`

```cpp
bool USkeletalMesh::CreateDynamicGPUResources(ID3D11Device* Device)
{
    if (!Device)
    {
        OutputDebugStringA("[SkeletalMesh] CreateDynamicGPUResources: Device is null\n");
        return false;
    }

    if (Vertices.empty() || Indices.empty())
    {
        OutputDebugStringA("[SkeletalMesh] CreateDynamicGPUResources: No vertex/index data\n");
        return false;
    }

    // 기존 리소스 정리
    ReleaseGPUResources();

    // GPU Vertices 준비 (초기 데이터)
    TArray<FNormalVertex> GPUVertices;
    GPUVertices.reserve(VertexCount);

    for (const FSkinnedVertex& SkinnedVert : Vertices)
    {
        FNormalVertex NormalVert;
        NormalVert.pos = SkinnedVert.Position;
        NormalVert.normal = SkinnedVert.Normal;
        NormalVert.tex = SkinnedVert.UV;
        NormalVert.Tangent = SkinnedVert.Tangent;
        NormalVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

        GPUVertices.push_back(NormalVert);
    }

    // ✅ Dynamic Vertex Buffer 생성
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;              // ✅ Dynamic!
    vbDesc.ByteWidth = sizeof(FNormalVertex) * VertexCount;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // ✅ CPU Write 허용

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = GPUVertices.data();

    HRESULT hr = Device->CreateBuffer(&vbDesc, &vbData, &VertexBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("[SkeletalMesh] Failed to create Dynamic Vertex Buffer\n");
        return false;
    }

    // Index Buffer는 Static으로 유지 (변경 없음)
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.Usage = D3D11_USAGE_DEFAULT;  // Static
    ibDesc.ByteWidth = sizeof(uint32) * IndexCount;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = Indices.data();

    hr = Device->CreateBuffer(&ibDesc, &ibData, &IndexBuffer);
    if (FAILED(hr))
    {
        OutputDebugStringA("[SkeletalMesh] Failed to create Index Buffer\n");
        ReleaseGPUResources();
        return false;
    }

    bUseDynamicBuffer = true;

    UE_LOG("[SkeletalMesh] Dynamic GPU resources created (%u vertices, %u indices)",
           VertexCount, IndexCount);

    return true;
}
```

#### 4.3. UpdateVertexBuffer 구현

**파일:** `SkeletalMesh.cpp`

```cpp
bool USkeletalMesh::UpdateVertexBuffer(ID3D11DeviceContext* Context,
                                       const TArray<FNormalVertex>& NewVertices)
{
    if (!Context || !VertexBuffer)
    {
        OutputDebugStringA("[SkeletalMesh] UpdateVertexBuffer: Invalid context or buffer\n");
        return false;
    }

    if (!bUseDynamicBuffer)
    {
        OutputDebugStringA("[SkeletalMesh] UpdateVertexBuffer: Not a dynamic buffer\n");
        return false;
    }

    if (NewVertices.size() != VertexCount)
    {
        OutputDebugStringA("[SkeletalMesh] UpdateVertexBuffer: Vertex count mismatch\n");
        return false;
    }

    // Map Vertex Buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = Context->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD,
                              0, &mappedResource);
    if (FAILED(hr))
    {
        OutputDebugStringA("[SkeletalMesh] UpdateVertexBuffer: Failed to map buffer\n");
        return false;
    }

    // Vertex 데이터 복사
    memcpy(mappedResource.pData, NewVertices.data(),
           sizeof(FNormalVertex) * VertexCount);

    // Unmap
    Context->Unmap(VertexBuffer, 0);

    return true;
}
```

#### 4.4. FBX Import 시 Dynamic Buffer 생성

**파일:** `MainToolbarWidget.cpp` (FBX Import 부분)

```cpp
// Import 완료 후
USkeletalMesh* ImportedMesh = FbxImporter.ImportSkeletalMesh(PathStr, Options);
if (ImportedMesh)
{
    // ✅ Dynamic GPU 리소스 생성 (CPU Skinning용)
    ID3D11Device* Device = UResourceManager::GetInstance().GetDevice();
    if (!ImportedMesh->CreateDynamicGPUResources(Device))
    {
        UE_LOG("Failed to create dynamic GPU resources");
        return;
    }

    // Actor 생성...
}
```

#### 4.5. TickComponent에서 GPU 업데이트

**파일:** `SkinnedMeshComponent.cpp`

```cpp
void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!SkeletalMesh || !bEnableCPUSkinning)
        return;

    // ✅ Bone Transform 업데이트 (필요시)
    if (bNeedsBoneTransformUpdate)
    {
        UpdateBoneTransforms();
    }

    // ✅ CPU Skinning 수행
    PerformCPUSkinning();

    // ✅ GPU Buffer 업데이트
    if (SkeletalMesh->UsesDynamicBuffer() && !SkinnedVertices.empty())
    {
        ID3D11DeviceContext* Context = UResourceManager::GetInstance().GetContext();
        SkeletalMesh->UpdateVertexBuffer(Context, SkinnedVertices);
    }
}
```

#### 4.6. UResourceManager에 Context Getter 추가

**파일:** `ResourceManager.h`

```cpp
class UResourceManager
{
public:
    // ✅ 추가: Device Context 가져오기
    ID3D11DeviceContext* GetContext() const
    {
        return pDeviceContext;
    }
};
```

---

## Phase 5: 최적화 및 테스트

**우선순위:** ⭐⭐ (선택)
**예상 시간:** 2-4시간
**상태:** ⬜ 미작업

### 최적화 옵션

#### 5.1. Bounding Box 업데이트

**파일:** `SkinnedMeshComponent.cpp`

```cpp
void USkinnedMeshComponent::UpdateSkinnedBoundingBox()
{
    if (SkinnedVertices.empty())
        return;

    FVector minPos = SkinnedVertices[0].pos;
    FVector maxPos = SkinnedVertices[0].pos;

    for (const auto& v : SkinnedVertices)
    {
        minPos = FVector::Min(minPos, v.pos);
        maxPos = FVector::Max(maxPos, v.pos);
    }

    // AABB 업데이트
    // ...
}
```

#### 5.2. Parallel Skinning (멀티스레드)

**파일:** `SkinnedMeshComponent.cpp`

```cpp
#include <execution>

void USkinnedMeshComponent::PerformCPUSkinning()
{
    // C++17 Parallel Algorithms
    std::for_each(std::execution::par,
                  sourceVertices.begin(), sourceVertices.end(),
                  [&](const FSkinnedVertex& srcVert) {
                      int32 vertIndex = &srcVert - &sourceVertices[0];
                      // Skinning 계산...
                  });
}
```

#### 5.3. LOD (Level of Detail) 지원

```cpp
// 향후: 카메라와의 거리에 따라 Skinning Quality 조절
if (distanceToCamera > LOD_THRESHOLD)
{
    // Simplified Skinning (Bone 1개만 사용)
}
```

### 테스트 계획

#### 테스트 1: Bind Pose 렌더링
```
1. FBX Import
2. Actor Spawn
3. 결과: Bind Pose 그대로 렌더링 (Skinning 전)
```

#### 테스트 2: CPU Skinning 작동 확인
```
1. Bone Transform 수동 변경
   - SetBoneTransform(0, newTransform)
2. TickComponent 호출
3. 결과: Mesh 변형 확인
```

#### 테스트 3: 성능 측정
```
1. 1000+ vertices mesh import
2. FPS 측정
3. CPU 사용률 확인
```

---

## 전체 작업 일정

| Phase | 작업 | 우선순위 | 예상 시간 | 누적 시간 |
|-------|------|----------|-----------|-----------|
| **Phase 1** | Skin Weights 병합 | ⭐⭐⭐ | 1-2h | 1-2h |
| **Phase 2** | Bone Transform 시스템 | ⭐⭐⭐ | 1-2h | 2-4h |
| **Phase 3** | CPU Skinning 계산 | ⭐⭐⭐ | 2-3h | 4-7h |
| **Phase 4** | Dynamic GPU Buffer | ⭐⭐⭐ | 2-3h | 6-10h |
| **Phase 5** | 최적화 및 테스트 | ⭐⭐ | 2-4h | 8-14h |

**총 예상 시간:** 8-14시간

---

## 체크리스트

### Phase 1: Skin Weights 병합
- [ ] `SkeletalMesh.h`에 `VertexToControlPointMap` 추가
- [ ] `SkeletalMesh.h`에 `GetVerticesRef()` 추가
- [ ] `FbxImporter.cpp` - `ExtractMeshData` 수정
- [ ] `FbxImporter.cpp` - `ExtractSkinWeights` 수정
- [ ] 빌드 및 테스트
- [ ] Bone Weights 확인 (로그 출력)

### Phase 2: Bone Transform 시스템
- [ ] `Skeleton.h`에 `GetBoneCount()` 추가
- [ ] `SkinnedMeshComponent.h`에 `BoneMatrices` 추가
- [ ] `SkinnedMeshComponent.cpp` - `UpdateBoneTransforms()` 구현
- [ ] `SkinnedMeshComponent.cpp` - `SetSkeletalMesh()` 수정
- [ ] 빌드 및 테스트
- [ ] Bone Transform 로그 확인

### Phase 3: CPU Skinning 계산
- [ ] `SkinnedMeshComponent.h`에 `SkinnedVertices` 추가
- [ ] `SkinnedMeshComponent.cpp` - `PerformCPUSkinning()` 구현
- [ ] 빌드 및 테스트
- [ ] Skinned Vertex 위치 확인

### Phase 4: Dynamic GPU Buffer
- [ ] `SkeletalMesh.h`에 Dynamic Buffer 옵션 추가
- [ ] `SkeletalMesh.cpp` - `CreateDynamicGPUResources()` 구현
- [ ] `SkeletalMesh.cpp` - `UpdateVertexBuffer()` 구현
- [ ] `ResourceManager.h`에 `GetContext()` 추가
- [ ] `SkinnedMeshComponent.cpp` - `TickComponent()` 구현
- [ ] `MainToolbarWidget.cpp` - FBX Import 수정
- [ ] 빌드 및 테스트
- [ ] 렌더링 확인

### Phase 5: 최적화 및 테스트
- [ ] Bounding Box 업데이트 구현
- [ ] 성능 측정
- [ ] 최적화 적용
- [ ] 최종 테스트

---

## 참고 자료

### 관련 문서
- [FBX_SKELETAL_MESH_IMPORTER_IMPLEMENTATION.md](FBX_SKELETAL_MESH_IMPORTER_IMPLEMENTATION.md)
- [ARCHITECTURE.md](../ARCHITECTURE.md)
- [CLAUDE.md](../CLAUDE.md)

### 관련 코드 위치
| 파일 | 라인 | 설명 |
|------|------|------|
| `FbxImporter.cpp` | 385-550 | ExtractMeshData |
| `FbxImporter.cpp` | 552-707 | ExtractSkinWeights |
| `SkeletalMesh.h` | 88-186 | USkeletalMesh 클래스 |
| `SkeletalMesh.cpp` | 28-85 | CreateGPUResources |
| `SkinnedMeshComponent.h` | 27-88 | USkinnedMeshComponent 클래스 |
| `Skeleton.h` | 55-150 | USkeleton 클래스 |

### 외부 참고
- [GPU Skinning Tutorial](https://www.3dgep.com/skeletal-animation-with-directx-11/)
- [Skeletal Animation Math](https://research.ncl.ac.uk/game/mastersdegree/graphicsforgames/skeletalanimation/Tutorial%209%20-%20Skeletal%20Animation.pdf)

---

**문서 버전:** 1.0
**최종 수정일:** 2025-11-08
**작성자:** Claude Code
**상태:** 📋 계획 단계
