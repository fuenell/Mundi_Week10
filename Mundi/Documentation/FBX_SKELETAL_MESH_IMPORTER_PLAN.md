# FBX Skeletal Mesh Importer 구현 계획

**작성일:** 2025-11-07
**목표:** Unreal Engine 스타일의 확장 가능한 FBX Importer (SkeletalMesh 중심)
**현재 구현:** SkeletalMesh Import
**향후 확장:** StaticMesh, Animation Import

---

## 📋 목차

- [아키텍처 개요](#아키텍처-개요)
- [파일 구조](#파일-구조)
- [Phase 1: 기반 구조 설계](#phase-1-기반-구조-설계)
- [Phase 2: 공통 Scene 처리](#phase-2-공통-scene-처리)
- [Phase 3: SkeletalMesh Import](#phase-3-skeletalmesh-import-현재-목표)
- [Phase 4: 확장 인터페이스](#phase-4-확장-가능한-인터페이스)
- [Phase 5: Editor 통합](#phase-5-editor-통합)
- [Phase 6: 테스트](#phase-6-테스트-및-검증)
- [구현 체크리스트](#구현-체크리스트)
- [참고 자료](#참고-자료)

---

## 아키텍처 개요

### Unreal Engine 패턴 적용

```
UnFbx::FFbxImporter
├── ImportSkeletalMesh()    ← 현재 구현
├── ImportStaticMesh()      ← 향후
└── ImportAnimation()       ← 향후
```

### 핵심 클래스

| 클래스 | 역할 | 상태 |
|--------|------|------|
| `FFbxImporter` | FBX 파일 로딩 및 변환 | 구현 예정 |
| `USkeleton` | Bone 계층 구조 | 새로 추가 |
| `USkeletalMesh` | Skinned Mesh 데이터 | 새로 추가 |
| `FFbxAssetFactory` | Editor 통합 | 구현 예정 |

---

## 파일 구조

```
Source/Runtime/AssetManagement/
├── FbxImporter.h              # 핵심 임포터 클래스
├── FbxImporter.cpp
├── FbxImportOptions.h         # Import 옵션 정의
├── Skeleton.h                 # NEW: Bone 계층
├── Skeleton.cpp
├── SkeletalMesh.h             # NEW: Skinned Mesh
└── SkeletalMesh.cpp

Source/Runtime/Engine/Components/
└── SkeletalMeshComponent.h    # NEW: Rendering component

Source/Editor/
├── FbxAssetFactory.h          # Editor 통합
└── FbxAssetFactory.cpp
```

---

## Phase 1: 기반 구조 설계

### ✅ 체크리스트

- [ ] `FFbxImporter` 클래스 헤더 작성
- [ ] `EFbxImportType` enum 정의
- [ ] `FFbxImportOptions` 구조체 설계
- [ ] Forward declarations 정리

### 1.1 FFbxImporter 클래스 구조

**파일:** `Source/Runtime/AssetManagement/FbxImporter.h`

```cpp
#pragma once
#include "pch.h"
#include "FbxImportOptions.h"
#include <fbxsdk.h>

// Forward declarations
class USkeletalMesh;
class UStaticMesh;
class USkeleton;
class UAnimSequence;

class FFbxImporter
{
public:
    FFbxImporter();
    ~FFbxImporter();

    // ========================================
    // Import Functions (Type-specific)
    // ========================================

    /** SkeletalMesh Import (현재 구현 대상) */
    USkeletalMesh* ImportSkeletalMesh(
        const std::wstring& InFilePath,
        USkeleton** OutSkeleton,
        const FFbxImportOptions& InOptions
    );

    /** StaticMesh Import (향후 구현) */
    UStaticMesh* ImportStaticMesh(
        const std::wstring& InFilePath,
        const FFbxImportOptions& InOptions
    );

    /** Animation Import (향후 구현) */
    UAnimSequence* ImportAnimation(
        const std::wstring& InFilePath,
        USkeleton* InSkeleton,
        const FFbxImportOptions& InOptions
    );

    // ========================================
    // Scene Management
    // ========================================

    bool OpenFile(const std::wstring& InFilePath);
    void ReleaseScene();
    FbxScene* GetScene() const { return Scene; }

    void SetImportOptions(const FFbxImportOptions& InOptions);
    const FFbxImportOptions& GetImportOptions() const { return ImportOptions; }

private:
    // FBX SDK Objects
    FbxManager* SdkManager = nullptr;
    FbxScene* Scene = nullptr;
    FbxImporter* Importer = nullptr;

    FFbxImportOptions ImportOptions;

    // Scene Processing
    bool LoadScene(const std::string& InFilePath);
    void ConvertScene();
    void ConvertAxisSystem();
    void ConvertUnitSystem();

    // SkeletalMesh Processing
    USkeleton* CreateSkeleton(FbxNode* InRootNode);
    void BuildSkeletonHierarchy(FbxNode* InNode, USkeleton* InSkeleton, int32 ParentBoneIndex);
    bool IsBone(FbxNode* InNode) const;
    bool ProcessSkeletalMesh(FbxMesh* InFbxMesh, USkeletalMesh* OutMesh, USkeleton* InSkeleton);

    // Skin Weights
    struct FSkinWeightVertex
    {
        TArray<int32> BoneIndices;
        TArray<float> BoneWeights;
    };
    void ExtractSkinWeights(FbxMesh* InMesh, TArray<FSkinWeightVertex>& OutWeights);

    // Vertex Data
    void ExtractVertexPositions(FbxMesh* InMesh, TArray<FVector>& OutVertices);
    void ExtractNormals(FbxMesh* InMesh, TArray<FVector>& OutNormals);
    void ExtractUVs(FbxMesh* InMesh, TArray<FVector2>& OutUVs);
    void ExtractIndices(FbxMesh* InMesh, TArray<uint32_t>& OutIndices);

    // Coordinate Conversion
    FVector ConvertPosition(const FbxVector4& InVector) const;
    FVector ConvertNormal(const FbxVector4& InVector) const;
    FQuat ConvertRotation(const FbxQuaternion& InQuat) const;
    void FlipWindingOrder(TArray<uint32_t>& InOutIndices) const;

    // Utility
    std::string WStringToString(const std::wstring& InWString) const;
    FbxNode* FindRootBoneNode(FbxNode* InNode) const;
};
```

### 1.2 Import Type 정의

**파일:** `Source/Runtime/AssetManagement/FbxImportOptions.h`

```cpp
#pragma once

enum class EFbxImportType
{
    SkeletalMesh,    // 현재 구현
    StaticMesh,      // 향후 구현
    Animation        // 향후 구현
};

// Note: Mundi 엔진의 좌표계
// - Z-Up (위쪽)
// - X-Forward (앞쪽)
// - Y-Right (오른쪽)
// - Left-Handed (왼손 좌표계)
// - Vertex Winding: CW (시계방향)
```

### 1.3 Import Options 구조체

```cpp
struct FFbxImportOptions
{
    // ========================================
    // General Options
    // ========================================
    EFbxImportType ImportType = EFbxImportType::SkeletalMesh;

    float ImportUniformScale = 1.0f;
    FVector ImportTranslation = FVector::ZeroVector;
    FRotator ImportRotation = FRotator::ZeroRotator;

    // 좌표계 변환 (항상 DirectX: Left-Handed, Z-Up으로 변환)
    bool bConvertScene = true;

    // ========================================
    // Mesh Options (공통)
    // ========================================
    bool bImportMesh = true;
    bool bImportNormals = true;
    bool bImportTangents = false;
    bool bImportVertexColors = false;

    bool bImportMaterials = true;
    bool bImportTextures = true;
    std::wstring TextureImportPath = L"Data/Textures/";

    // ========================================
    // SkeletalMesh Specific
    // ========================================
    bool bImportSkeleton = true;
    bool bImportMorphTargets = false;
    bool bUpdateSkeletonReferencePose = true;
    bool bPreserveSmoothingGroups = true;
    bool bKeepOverlappingVertices = false;
    bool bComputeWeightedNormals = true;

    // ========================================
    // StaticMesh Specific (향후)
    // ========================================
    bool bCombineMeshes = true;
    bool bGenerateLightmapUVs = false;

    // ========================================
    // Animation Specific (향후)
    // ========================================
    bool bImportAnimation = false;
    float AnimationLength = 0.0f;
    int32 StartFrame = 0;
    int32 EndFrame = 0;
};
```

---

## Phase 2: 공통 Scene 처리

### ✅ 체크리스트

- [ ] `FFbxImporter` 생성자/소멸자 구현
- [ ] FBX SDK 초기화 (Manager, Scene, IOSettings)
- [ ] Scene 로딩 기능 구현
- [ ] 좌표계 변환 로직 구현
- [ ] 단위 변환 로직 구현

### 2.1 SDK 초기화

**파일:** `Source/Runtime/AssetManagement/FbxImporter.cpp`

```cpp
#include "pch.h"
#include "FbxImporter.h"
#include <fbxsdk.h>

FFbxImporter::FFbxImporter()
{
    // FBX Manager 생성
    SdkManager = FbxManager::Create();
    if (!SdkManager)
    {
        OutputDebugStringA("[FBX] Failed to create FBX Manager\n");
        return;
    }

    // IOSettings 생성
    FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
    SdkManager->SetIOSettings(ios);

    // Scene 생성
    Scene = FbxScene::Create(SdkManager, "ImportScene");
}

FFbxImporter::~FFbxImporter()
{
    ReleaseScene();

    if (SdkManager)
    {
        SdkManager->Destroy();
        SdkManager = nullptr;
        Scene = nullptr;
    }
}

void FFbxImporter::ReleaseScene()
{
    if (Importer)
    {
        Importer->Destroy();
        Importer = nullptr;
    }
}
```

### 2.2 Scene 로딩

```cpp
bool FFbxImporter::OpenFile(const std::wstring& InFilePath)
{
    std::string filePath = WStringToString(InFilePath);

    if (!LoadScene(filePath))
    {
        OutputDebugStringA("[FBX] Failed to load scene\n");
        return false;
    }

    // 자동 좌표계 변환
    ConvertScene();

    return true;
}

bool FFbxImporter::LoadScene(const std::string& InFilePath)
{
    // Importer 생성
    Importer = FbxImporter::Create(SdkManager, "");

    // 파일 초기화
    if (!Importer->Initialize(InFilePath.c_str(), -1, SdkManager->GetIOSettings()))
    {
        std::string error = "[FBX] Initialize failed: ";
        error += Importer->GetStatus().GetErrorString();
        error += "\n";
        OutputDebugStringA(error.c_str());
        return false;
    }

    // Scene에 임포트
    if (!Importer->Import(Scene))
    {
        OutputDebugStringA("[FBX] Import failed\n");
        return false;
    }

    return true;
}
```

### 2.3 좌표계 변환 (CRITICAL!)

```cpp
void FFbxImporter::ConvertScene()
{
    if (!ImportOptions.bConvertScene) return;

    ConvertAxisSystem();
    ConvertUnitSystem();
}

void FFbxImporter::ConvertAxisSystem()
{
    // Mundi 엔진의 좌표계: Z-Up, X-Forward, Y-Right, Left-Handed
    FbxAxisSystem mundiAxis(
        FbxAxisSystem::eZAxis,       // Z-Up
        FbxAxisSystem::eParityEven,  // X-Forward (ParityEven = positive X axis)
        FbxAxisSystem::eLeftHanded   // Left-Handed
    );

    FbxAxisSystem sceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

    if (sceneAxis != mundiAxis)
    {
        OutputDebugStringA("[FBX] Converting to Mundi coordinate system (Z-Up, X-Forward, Left-Handed)\n");
        mundiAxis.ConvertScene(Scene);
    }
}

void FFbxImporter::ConvertUnitSystem()
{
    // cm 단위로 변환
    FbxSystemUnit targetUnit(FbxSystemUnit::cm);
    FbxSystemUnit sceneUnit = Scene->GetGlobalSettings().GetSystemUnit();

    if (sceneUnit != targetUnit)
    {
        OutputDebugStringA("[FBX] Converting unit system to cm\n");
        targetUnit.ConvertScene(Scene);
    }
}
```

### 2.4 좌표 변환 헬퍼

```cpp
FVector FFbxImporter::ConvertPosition(const FbxVector4& InVector) const
{
    // ConvertScene 이후에는 이미 DirectX 좌표계로 변환됨
    return FVector(
        static_cast<float>(InVector[0]),
        static_cast<float>(InVector[1]),
        static_cast<float>(InVector[2])
    );
}

FVector FFbxImporter::ConvertNormal(const FbxVector4& InVector) const
{
    return FVector(
        static_cast<float>(InVector[0]),
        static_cast<float>(InVector[1]),
        static_cast<float>(InVector[2])
    ).GetNormalized();
}

FQuat FFbxImporter::ConvertRotation(const FbxQuaternion& InQuat) const
{
    return FQuat(
        static_cast<float>(InQuat[0]),
        static_cast<float>(InQuat[1]),
        static_cast<float>(InQuat[2]),
        static_cast<float>(InQuat[3])
    );
}

void FFbxImporter::FlipWindingOrder(TArray<uint32_t>& InOutIndices) const
{
    // 삼각형 winding order 반전 (필요시)
    for (size_t i = 0; i < InOutIndices.size(); i += 3)
    {
        std::swap(InOutIndices[i + 1], InOutIndices[i + 2]);
    }
}
```

---

## Phase 3: SkeletalMesh Import (현재 목표)

### ✅ 체크리스트

- [ ] `USkeleton` 클래스 구현
- [ ] `USkeletalMesh` 클래스 구현
- [ ] `USkeletalMeshComponent` 구현
- [ ] Bone 계층 추출 로직
- [ ] Skin Weights 추출 로직
- [ ] Bind Pose 처리
- [ ] 메시 데이터 추출

### 3.1 Skeleton 클래스

**파일:** `Source/Runtime/AssetManagement/Skeleton.h`

```cpp
#pragma once
#include "Object.h"

struct FBone
{
    UPROPERTY()
    FName BoneName;

    UPROPERTY()
    int32 ParentIndex = -1;  // -1 = root bone

    UPROPERTY()
    FTransform LocalTransform;   // Parent space

    UPROPERTY()
    FTransform GlobalTransform;  // World space
};

class USkeleton : public UObject
{
    DECLARE_CLASS(USkeleton, UObject)
    GENERATED_REFLECTION_BODY()
    DECLARE_DUPLICATE(USkeleton)

public:
    USkeleton() = default;
    virtual ~USkeleton() = default;

    // Bone 추가
    int32 AddBone(const FName& BoneName, int32 ParentIndex, const FTransform& LocalTransform);

    // Bone 검색
    int32 FindBoneIndex(const FName& BoneName) const;
    const FBone& GetBone(int32 BoneIndex) const { return Bones[BoneIndex]; }
    FBone& GetBone(int32 BoneIndex) { return Bones[BoneIndex]; }

    // Hierarchy
    int32 GetNumBones() const { return static_cast<int32>(Bones.size()); }
    const TArray<FBone>& GetBones() const { return Bones; }
    TArray<FBone>& GetBones() { return Bones; }

    // Transform 계산
    void RecalculateGlobalTransforms();
    FTransform GetBoneGlobalTransform(int32 BoneIndex) const;

    // Serialization
    virtual void Serialize(bool bIsLoading, JSON& InOutHandle) override;

private:
    UPROPERTY()
    TArray<FBone> Bones;

    // 빠른 검색을 위한 맵
    TMap<FName, int32> BoneNameToIndexMap;
};
```

**파일:** `Source/Runtime/AssetManagement/Skeleton.cpp`

```cpp
#include "pch.h"
#include "Skeleton.h"

IMPLEMENT_CLASS(USkeleton)

int32 USkeleton::AddBone(const FName& BoneName, int32 ParentIndex, const FTransform& LocalTransform)
{
    FBone bone;
    bone.BoneName = BoneName;
    bone.ParentIndex = ParentIndex;
    bone.LocalTransform = LocalTransform;
    bone.GlobalTransform = LocalTransform;

    int32 boneIndex = static_cast<int32>(Bones.size());
    Bones.push_back(bone);
    BoneNameToIndexMap[BoneName] = boneIndex;

    return boneIndex;
}

int32 USkeleton::FindBoneIndex(const FName& BoneName) const
{
    auto it = BoneNameToIndexMap.find(BoneName);
    if (it != BoneNameToIndexMap.end())
        return it->second;
    return -1;
}

void USkeleton::RecalculateGlobalTransforms()
{
    for (size_t i = 0; i < Bones.size(); ++i)
    {
        FBone& bone = Bones[i];

        if (bone.ParentIndex >= 0)
        {
            const FBone& parent = Bones[bone.ParentIndex];
            bone.GlobalTransform = bone.LocalTransform * parent.GlobalTransform;
        }
        else
        {
            bone.GlobalTransform = bone.LocalTransform;
        }
    }
}

FTransform USkeleton::GetBoneGlobalTransform(int32 BoneIndex) const
{
    if (BoneIndex >= 0 && BoneIndex < GetNumBones())
        return Bones[BoneIndex].GlobalTransform;
    return FTransform::Identity;
}

void USkeleton::Serialize(bool bIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bIsLoading, InOutHandle);

    // TODO: Bone 데이터 직렬화
}
```

### 3.2 SkeletalMesh 클래스

**파일:** `Source/Runtime/AssetManagement/SkeletalMesh.h`

```cpp
#pragma once
#include "StaticMesh.h"
#include "Skeleton.h"

struct FSkinWeightData
{
    uint8 BoneIndices[4] = {0, 0, 0, 0};   // Max 4 bones per vertex
    float BoneWeights[4] = {0, 0, 0, 0};   // Normalized weights (sum = 1.0)
};

class USkeletalMesh : public UStaticMesh
{
    DECLARE_CLASS(USkeletalMesh, UStaticMesh)
    GENERATED_REFLECTION_BODY()
    DECLARE_DUPLICATE(USkeletalMesh)

public:
    USkeletalMesh() = default;
    virtual ~USkeletalMesh() = default;

    // Skeleton
    void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }
    USkeleton* GetSkeleton() const { return Skeleton; }

    // Skin Weights
    void SetSkinWeights(const TArray<FSkinWeightData>& InWeights);
    const TArray<FSkinWeightData>& GetSkinWeights() const { return SkinWeights; }

    // Bind Pose (T-Pose or A-Pose)
    void SetBindPose(const TArray<FTransform>& InBindPose);
    const TArray<FTransform>& GetBindPose() const { return BindPose; }

    // Serialization
    virtual void Serialize(bool bIsLoading, JSON& InOutHandle) override;

private:
    UPROPERTY()
    USkeleton* Skeleton = nullptr;

    UPROPERTY()
    TArray<FSkinWeightData> SkinWeights;

    UPROPERTY()
    TArray<FTransform> BindPose;  // Bone space transforms at bind time
};
```

### 3.3 SkeletalMesh Import 메인 로직

**파일:** `Source/Runtime/AssetManagement/FbxImporter.cpp`

```cpp
USkeletalMesh* FFbxImporter::ImportSkeletalMesh(
    const std::wstring& InFilePath,
    USkeleton** OutSkeleton,
    const FFbxImportOptions& InOptions)
{
    SetImportOptions(InOptions);

    if (!OpenFile(InFilePath))
        return nullptr;

    // 1. Root Bone 찾기
    FbxNode* rootNode = Scene->GetRootNode();
    FbxNode* rootBone = FindRootBoneNode(rootNode);

    if (!rootBone)
    {
        OutputDebugStringA("[FBX] No skeleton found in scene\n");
        return nullptr;
    }

    // 2. Skeleton 생성
    USkeleton* skeleton = CreateSkeleton(rootBone);
    if (!skeleton)
    {
        OutputDebugStringA("[FBX] Failed to create skeleton\n");
        return nullptr;
    }

    if (OutSkeleton)
        *OutSkeleton = skeleton;

    // 3. SkeletalMesh 생성
    USkeletalMesh* skeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
    skeletalMesh->SetSkeleton(skeleton);

    // 4. Mesh 데이터 추출
    bool foundMesh = false;
    for (int i = 0; i < Scene->GetGeometryCount(); ++i)
    {
        FbxGeometry* geometry = Scene->GetGeometry(i);
        if (geometry->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            FbxMesh* mesh = (FbxMesh*)geometry;

            // Skin이 있는지 확인
            if (mesh->GetDeformerCount(FbxDeformer::eSkin) > 0)
            {
                ProcessSkeletalMesh(mesh, skeletalMesh, skeleton);
                foundMesh = true;
                break;  // 첫 번째 skinned mesh만 (향후 multi-mesh 지원)
            }
        }
    }

    if (!foundMesh)
    {
        OutputDebugStringA("[FBX] No skinned mesh found\n");
        return nullptr;
    }

    // 5. GPU 버퍼 생성
    skeletalMesh->Finalize();

    OutputDebugStringA("[FBX] SkeletalMesh import successful\n");
    return skeletalMesh;
}
```

### 3.4 Bone Hierarchy 추출

```cpp
USkeleton* FFbxImporter::CreateSkeleton(FbxNode* InRootNode)
{
    USkeleton* skeleton = ObjectFactory::NewObject<USkeleton>();

    // Recursive build
    BuildSkeletonHierarchy(InRootNode, skeleton, -1);

    // Global transforms 계산
    skeleton->RecalculateGlobalTransforms();

    std::string msg = "[FBX] Created skeleton with " +
                      std::to_string(skeleton->GetNumBones()) + " bones\n";
    OutputDebugStringA(msg.c_str());

    return skeleton;
}

void FFbxImporter::BuildSkeletonHierarchy(
    FbxNode* InNode,
    USkeleton* InSkeleton,
    int32 ParentBoneIndex)
{
    if (!InNode) return;

    bool isBone = IsBone(InNode);
    int32 currentBoneIndex = ParentBoneIndex;

    if (isBone)
    {
        // Local Transform 추출
        FbxAMatrix localTransform = InNode->EvaluateLocalTransform();

        FTransform transform;
        transform.SetTranslation(ConvertPosition(localTransform.GetT()));
        transform.SetRotation(ConvertRotation(localTransform.GetQ()));
        transform.SetScale3D(ConvertPosition(localTransform.GetS()));

        // Bone 추가
        FName boneName(InNode->GetName());
        currentBoneIndex = InSkeleton->AddBone(boneName, ParentBoneIndex, transform);

        std::string msg = "[FBX] Added bone: " + std::string(InNode->GetName()) +
                          " (Parent: " + std::to_string(ParentBoneIndex) + ")\n";
        OutputDebugStringA(msg.c_str());
    }

    // 자식 노드 재귀 처리
    for (int i = 0; i < InNode->GetChildCount(); ++i)
    {
        BuildSkeletonHierarchy(InNode->GetChild(i), InSkeleton, currentBoneIndex);
    }
}

bool FFbxImporter::IsBone(FbxNode* InNode) const
{
    if (!InNode) return false;

    FbxNodeAttribute* attr = InNode->GetNodeAttribute();
    if (attr)
    {
        FbxNodeAttribute::EType attrType = attr->GetAttributeType();
        if (attrType == FbxNodeAttribute::eSkeleton)
            return true;
    }

    // 이름으로 판단 (Bone, Joint 등)
    std::string name = InNode->GetName();
    if (name.find("Bone") != std::string::npos ||
        name.find("bone") != std::string::npos ||
        name.find("Joint") != std::string::npos ||
        name.find("joint") != std::string::npos)
    {
        return true;
    }

    return false;
}

FbxNode* FFbxImporter::FindRootBoneNode(FbxNode* InNode) const
{
    if (!InNode) return nullptr;

    // 현재 노드가 bone이면 반환
    if (IsBone(InNode))
        return InNode;

    // 자식 노드 검색
    for (int i = 0; i < InNode->GetChildCount(); ++i)
    {
        FbxNode* result = FindRootBoneNode(InNode->GetChild(i));
        if (result)
            return result;
    }

    return nullptr;
}
```

### 3.5 Skin Weights 추출

```cpp
void FFbxImporter::ExtractSkinWeights(
    FbxMesh* InMesh,
    TArray<FSkinWeightVertex>& OutWeights)
{
    int32 vertexCount = InMesh->GetControlPointsCount();
    OutWeights.resize(vertexCount);

    // 각 정점 초기화
    for (int32 i = 0; i < vertexCount; ++i)
    {
        OutWeights[i].BoneIndices.clear();
        OutWeights[i].BoneWeights.clear();
    }

    // Skin Deformer 처리
    int32 skinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);

    OutputDebugStringA("[FBX] Extracting skin weights...\n");

    for (int32 skinIndex = 0; skinIndex < skinCount; ++skinIndex)
    {
        FbxSkin* skin = (FbxSkin*)InMesh->GetDeformer(skinIndex, FbxDeformer::eSkin);
        int32 clusterCount = skin->GetClusterCount();

        for (int32 clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            FbxCluster* cluster = skin->GetCluster(clusterIndex);
            FbxNode* boneNode = cluster->GetLink();

            if (!boneNode) continue;

            // Bone 인덱스 찾기 (이름으로 매칭)
            int32 boneIndex = clusterIndex;  // 임시: Skeleton에서 찾아야 함

            // Cluster의 영향을 받는 정점들
            int32* indices = cluster->GetControlPointIndices();
            double* weights = cluster->GetControlPointWeights();
            int32 indexCount = cluster->GetControlPointIndicesCount();

            for (int32 i = 0; i < indexCount; ++i)
            {
                int32 vertexIndex = indices[i];
                float weight = static_cast<float>(weights[i]);

                if (vertexIndex >= 0 && vertexIndex < vertexCount && weight > 0.0001f)
                {
                    OutWeights[vertexIndex].BoneIndices.push_back(boneIndex);
                    OutWeights[vertexIndex].BoneWeights.push_back(weight);
                }
            }
        }
    }

    // Normalize weights (최대 4개 bone으로 제한)
    for (auto& weightData : OutWeights)
    {
        // 가중치가 큰 순서로 정렬
        // 상위 4개만 선택
        // 가중치 합이 1.0이 되도록 정규화
        // TODO: NormalizeSkinWeights() 구현
    }
}
```

### 3.6 ProcessSkeletalMesh

```cpp
bool FFbxImporter::ProcessSkeletalMesh(
    FbxMesh* InFbxMesh,
    USkeletalMesh* OutMesh,
    USkeleton* InSkeleton)
{
    if (!InFbxMesh || !OutMesh || !InSkeleton)
        return false;

    // Triangulate
    if (!InFbxMesh->IsTriangleMesh())
    {
        OutputDebugStringA("[FBX] Triangulating mesh...\n");
        FbxGeometryConverter converter(SdkManager);
        InFbxMesh = (FbxMesh*)converter.Triangulate(InFbxMesh, true);
    }

    // Vertex 데이터 추출
    TArray<FVector> vertices;
    TArray<FVector> normals;
    TArray<FVector2> uvs;
    TArray<uint32_t> indices;

    ExtractVertexPositions(InFbxMesh, vertices);
    ExtractNormals(InFbxMesh, normals);
    ExtractUVs(InFbxMesh, uvs);
    ExtractIndices(InFbxMesh, indices);

    // Skin Weights 추출
    TArray<FSkinWeightVertex> skinWeights;
    ExtractSkinWeights(InFbxMesh, skinWeights);

    // FSkinWeightData로 변환
    TArray<FSkinWeightData> finalWeights;
    finalWeights.resize(vertices.size());

    for (size_t i = 0; i < skinWeights.size(); ++i)
    {
        // TODO: FSkinWeightVertex → FSkinWeightData 변환
    }

    // Mesh 데이터 설정
    OutMesh->SetVertices(vertices);
    OutMesh->SetNormals(normals);
    OutMesh->SetUVs(uvs);
    OutMesh->SetIndices(indices);
    OutMesh->SetSkinWeights(finalWeights);

    std::string msg = "[FBX] Processed mesh: " +
                      std::to_string(vertices.size()) + " vertices, " +
                      std::to_string(indices.size() / 3) + " triangles\n";
    OutputDebugStringA(msg.c_str());

    return true;
}
```

---

## Phase 4: 확장 가능한 인터페이스

### ✅ 체크리스트

- [ ] `ImportStaticMesh()` 스텁 작성
- [ ] `ImportAnimation()` 스텁 작성
- [ ] 타입별 옵션 분리 구조 설계

### 4.1 향후 구현 메서드 스텁

```cpp
UStaticMesh* FFbxImporter::ImportStaticMesh(
    const std::wstring& InFilePath,
    const FFbxImportOptions& InOptions)
{
    // TODO: StaticMesh import 구현
    OutputDebugStringA("[FBX] StaticMesh import not implemented yet\n");
    return nullptr;
}

UAnimSequence* FFbxImporter::ImportAnimation(
    const std::wstring& InFilePath,
    USkeleton* InSkeleton,
    const FFbxImportOptions& InOptions)
{
    // TODO: Animation import 구현
    OutputDebugStringA("[FBX] Animation import not implemented yet\n");
    return nullptr;
}
```

---

## Phase 5: Editor 통합

### ✅ 체크리스트

- [ ] `FFbxAssetFactory` 클래스 구현
- [ ] Editor 메뉴 추가 (`Import FBX as SkeletalMesh`)
- [ ] Import Options 다이얼로그 구현
- [ ] Scene에 Actor 자동 생성

### 5.1 FbxAssetFactory

**파일:** `Source/Editor/FbxAssetFactory.h`

```cpp
#pragma once
#include "FbxImporter.h"

class FFbxAssetFactory
{
public:
    /**
     * SkeletalMesh를 파일에서 임포트
     */
    static USkeletalMesh* ImportSkeletalMeshFromFile(
        const std::wstring& InFilePath,
        const FFbxImportOptions& InOptions,
        USkeleton** OutSkeleton = nullptr
    );

    /**
     * Import Options 다이얼로그 표시
     * @return true if user confirmed, false if cancelled
     */
    static bool ShowImportOptionsDialog(FFbxImportOptions& OutOptions);

    // 향후 확장
    static UStaticMesh* ImportStaticMeshFromFile(...);
    static UAnimSequence* ImportAnimationFromFile(...);
};
```

**파일:** `Source/Editor/FbxAssetFactory.cpp`

```cpp
#include "pch.h"
#include "FbxAssetFactory.h"

USkeletalMesh* FFbxAssetFactory::ImportSkeletalMeshFromFile(
    const std::wstring& InFilePath,
    const FFbxImportOptions& InOptions,
    USkeleton** OutSkeleton)
{
    FFbxImporter importer;
    return importer.ImportSkeletalMesh(InFilePath, OutSkeleton, InOptions);
}

bool FFbxAssetFactory::ShowImportOptionsDialog(FFbxImportOptions& OutOptions)
{
    // TODO: ImGui 다이얼로그 구현
    // 옵션 설정 UI
    // OK/Cancel 버튼
    return true;
}
```

### 5.2 Editor 메뉴 통합

ObjManager 참고해서 메뉴 추가:
- `File → Import → FBX as Skeletal Mesh...`
- File Dialog로 FBX 파일 선택
- Import Options Dialog 표시
- Import 실행
- Scene에 ASkeletalMeshActor 생성

---

## Phase 6: 테스트 및 검증

### ✅ 체크리스트

- [ ] 간단한 rigged character 테스트
- [ ] Bone hierarchy 정확성 검증
- [ ] Skin weights 정확성 검증
- [ ] 좌표계 변환 검증
- [ ] 복잡한 모델 테스트

### 6.1 테스트 케이스

**Test 1: Simple Rigged Cube**
- Blender에서 간단한 Cube + 2개 Bone 생성
- FBX Export
- Mundi Import
- Bone 계층 확인

**Test 2: Character Model**
- Rigged character 모델
- 여러 Bone (10+ bones)
- Skin weights 확인
- Bind pose 확인

**Test 3: 좌표계 변환**
- 다양한 DCC 툴에서 Export (Blender, Maya, 3ds Max)
- 올바른 방향으로 표시되는지 확인

---

## 구현 체크리스트

### Phase 1: 기반 구조
- [ ] FbxImporter.h 작성
- [ ] FbxImportOptions.h 작성
- [ ] enum 정의 (EFbxImportType)

### Phase 2: Scene 처리
- [ ] FFbxImporter 생성자/소멸자
- [ ] OpenFile(), LoadScene()
- [ ] ConvertScene(), ConvertAxisSystem(), ConvertUnitSystem()
- [ ] 좌표 변환 헬퍼 함수들

### Phase 3: SkeletalMesh
- [ ] Skeleton.h/cpp 구현
- [ ] SkeletalMesh.h/cpp 구현
- [ ] ImportSkeletalMesh() 메인 로직
- [ ] CreateSkeleton(), BuildSkeletonHierarchy()
- [ ] ProcessSkeletalMesh()
- [ ] ExtractSkinWeights()
- [ ] Vertex 데이터 추출 함수들

### Phase 4: 확장
- [ ] ImportStaticMesh() 스텁
- [ ] ImportAnimation() 스텁

### Phase 5: Editor
- [ ] FbxAssetFactory.h/cpp
- [ ] Editor 메뉴 통합
- [ ] Import Options Dialog

### Phase 6: 테스트
- [ ] Simple rigged model 테스트
- [ ] Character model 테스트
- [ ] 좌표계 검증

---

## 참고 자료

### 기존 코드
- `ObjManager.cpp` - 좌표계 변환 패턴
- `StaticMesh.h` - 메시 데이터 구조
- `Object.h` - Reflection 시스템

### Unreal Engine
```
Engine/Source/Editor/UnrealEd/Private/Fbx/
├── FbxImporter.cpp
├── FbxSkeletalMeshImport.cpp   ← 주요 참고
├── FbxAnimSequenceImport.cpp   ← 향후 참고
└── FbxMaterialImport.cpp
```

### FBX SDK 문서
- [FBX SDK Documentation](https://help.autodesk.com/view/FBX/2020/ENU/)
- [FBX SDK Programming Guide](https://help.autodesk.com/view/FBX/2020/ENU/?guid=FBX_Developer_Help_welcome_to_the_fbx_sdk_html)

### 좌표계 변환
- **Mundi 엔진은 DirectX만 사용**: 항상 Left-Handed, Z-Up으로 변환
- FbxAxisSystem::eDirectX로 하드코딩
- FbxAxisSystem::ConvertScene() 사용
- ObjManager의 `bIsRightHanded` 패턴 참고

---

**마지막 업데이트:** 2025-11-07
**작성자:** Claude Code
