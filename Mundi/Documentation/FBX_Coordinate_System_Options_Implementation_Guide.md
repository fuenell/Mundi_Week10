# FBX Import 좌표계 변환 옵션 구현 가이드

## 목차
1. [개요](#개요)
2. [Unreal Engine 5 분석 결과](#unreal-engine-5-분석-결과)
   - [핵심 메커니즘](#핵심-메커니즘)
   - [Import Options 구조](#import-options-구조)
   - [ConvertScene() 구현](#convertscene-구현)
   - [왜 Right-Handed 중간 단계인가?](#왜-right-handed-중간-단계인가)
   - [JointOrientationMatrix (SkeletalMesh 전용)](#jointorientationmatrix-skeletalmesh-전용)
3. [Mundi 적용 계획](#mundi-적용-계획)
4. [Phase 1: Import Options 확장](#phase-1-import-options-확장)
5. [Phase 2: FFbxDataConverter 클래스 추가](#phase-2-ffbxdataconverter-클래스-추가)
6. [Phase 3: 조건부 변환 로직 구현](#phase-3-조건부-변환-로직-구현)
   - [ConvertScene() 리팩토링](#32-convertscene-리팩토링)
   - [Vertex 변환 코드 수정](#33-vertex-변환-코드-수정-선택-사항)
   - [Skeletal Mesh Bone Transform에 JointPostConversionMatrix 적용](#34-skeletal-mesh-bone-transform에-jointpostconversionmatrix-적용)
7. [사용 예제](#사용-예제)
8. [마이그레이션 가이드](#마이그레이션-가이드)

---

## 개요

현재 Mundi 엔진의 FBX Import는 **항상 Unreal Engine 방식(Z-Up, -Y-Forward, Right-Handed → Y-Flip)** 으로 변환합니다.

이 문서는 **선택적 좌표계 변환 옵션**을 추가하여 다음을 가능하게 합니다:

- ✅ **옵션 1**: Unreal Engine 방식 (기본) - Z-Up, X-Forward, Left-Handed
- ✅ **옵션 2**: 직접 변환 방식 - Z-Up, X-Forward, Left-Handed (ConvertScene에서 직접)
- ✅ **옵션 3**: FBX 원본 유지 - 최소 변환 (단위만 변환)
- ✅ **옵션 4**: Custom - bForceFrontXAxis 등의 세부 제어

---

## Unreal Engine 5 분석 결과

### 핵심 메커니즘

UE5는 **2단계 변환 전략**을 사용:

```
Step 1: FbxAxisSystem::ConvertScene()
  - Target: Z-Up, -Y-Forward, Right-Handed (중간 좌표계)
  - 이유: Maya/Max 모델의 방향 보존

Step 2: FFbxDataConverter::ConvertPos() - Y축 반전
  - Right-Handed → Left-Handed 변환
  - Position, Normal, Tangent, Quaternion 모두 Y 관련 요소 반전
```

### Import Options 구조

**파일**: `Engine/Source/Editor/UnrealEd/Classes/Factories/FbxAssetImportData.h`

```cpp
class UFbxAssetImportData
{
    // 좌표계 변환 제어
    bool bConvertScene = true;          // Scene 좌표계 변환 여부
    bool bForceFrontXAxis = false;      // Front Axis를 +X로 강제
    bool bConvertSceneUnit = true;      // 단위를 cm로 변환

    // Transform 오프셋
    FVector ImportTranslation = FVector::ZeroVector;
    FRotator ImportRotation = FRotator::ZeroRotator;
    float ImportUniformScale = 1.0f;
};
```

### ConvertScene() 구현

**파일**: `FbxMainImport.cpp:1499-1580`

```cpp
void FFbxImporter::ConvertScene()
{
    if (ImportOptions->bConvertScene)
    {
        // Target: Right-Handed 중간 좌표계
        FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
        FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
        FbxAxisSystem::EFrontVector FrontVector =
            (FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd;  // -Y Forward

        if (ImportOptions->bForceFrontXAxis)
        {
            FrontVector = FbxAxisSystem::eParityEven;  // +X Forward
        }

        FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);

        // CRITICAL: FBX Root 제거 먼저!
        FbxRootNodeUtility::RemoveAllFbxRoots(Scene);

        // 좌표계 변환
        UnrealImportAxis.ConvertScene(Scene);
    }

    if (ImportOptions->bConvertSceneUnit)
    {
        FbxSystemUnit::cm.ConvertScene(Scene);
    }
}
```

### 왜 Right-Handed 중간 단계인가?

UE5 주석:
> "we use -Y as forward axis here when we import. This is odd considering our forward axis is technically +X but this is to mimic **Maya/Max behavior** where if you make a model facing +X facing, when you import that mesh, you want +X facing in engine."

**핵심**: Maya/Max에서 +X 방향 모델 → UE5에서도 +X 방향 유지

### JointOrientationMatrix (SkeletalMesh 전용)

**CRITICAL**: `bForceFrontXAxis = true`일 때 **Skeletal Mesh Bone Hierarchy에만** 추가 회전 행렬이 적용됩니다.

#### 발견 과정

Unreal Engine 5 소스 코드 분석 중 발견:

**파일**: `FbxMainImport.cpp:1523-1566`

```cpp
FbxAMatrix JointOrientationMatrix;
JointOrientationMatrix.SetIdentity();

if (GetImportOptions()->bForceFrontXAxis)
{
    FrontVector = FbxAxisSystem::eParityEven;  // +X Forward
}

if (sceneAxis != UnrealImportAxis)
{
    FbxRootNodeUtility::RemoveAllFbxRoots(Scene);
    UnrealImportAxis.ConvertScene(Scene);

    // CRITICAL: bForceFrontXAxis가 true면 추가 회전!
    if (GetImportOptions()->bForceFrontXAxis)
    {
        JointOrientationMatrix.SetR(FbxVector4(-90.0, -90.0, 0.0));  // ← Key logic
    }
}

// Static 변수에 저장
FFbxDataConverter::SetJointPostConversionMatrix(JointOrientationMatrix);
```

**파일**: `FbxSkeletalMeshImport.cpp:1211`

```cpp
// Bone Transform에 JointOrientationMatrix 적용
GlobalsPerLink[LinkIndex] = GlobalsPerLink[LinkIndex] * FFbxDataConverter::GetJointPostConversionMatrix();
```

#### 목적 및 동작

**목적**: -Y Forward (Import 좌표계) → +X Forward (Runtime 좌표계) 변환

- **ConvertScene() 단계**: FBX Scene을 Z-Up, -Y-Forward, Right-Handed로 변환
- **JointOrientationMatrix 단계**: **Bone Hierarchy만** 추가로 회전시켜 +X Forward로 변환

#### 수학적 의미

**Euler Rotation**: `(-90°, -90°, 0°)` = Pitch, Yaw, Roll

이 회전은 -Y 축을 +X 축으로 변환합니다:

```
초기 방향: -Y Forward (0, -1, 0)
회전 후:   +X Forward (1, 0, 0)

[변환 과정]
1. Pitch -90°: Y → -Z 방향으로 회전
2. Yaw -90°:   -Z → +X 방향으로 회전
3. Roll 0°:    추가 회전 없음
```

#### Static Mesh vs Skeletal Mesh

| 항목 | Static Mesh | Skeletal Mesh |
|------|-------------|---------------|
| **ConvertScene() 적용** | ✅ Yes | ✅ Yes |
| **Y-Flip 적용** | ✅ Yes (Vertex) | ✅ Yes (Vertex) |
| **JointOrientationMatrix 적용** | ❌ No (Bone 없음) | ✅ Yes (Bone만) |
| **적용 대상** | Vertex Position | Bone Transform |

**핵심**: JointOrientationMatrix는 **Bone Hierarchy에만** 영향을 주며, Vertex는 영향받지 않음!

#### Mundi에서 필요한가?

**현재 상태**: ❌ **필요 없음**

- Mundi는 `-Y Forward` 방식만 사용 (기본값: `bForceFrontXAxis = false`)
- `bForceFrontXAxis` 옵션을 추가하지 않는 한 구현 불필요

**향후 고려**: ⏳ **조건부 필요**

- `bForceFrontXAxis = true` 옵션을 추가하려면 구현 필요
- **Skeletal Mesh Import에만** 적용
- Static Mesh Import에는 영향 없음

#### 언제 사용되는가?

**사용 조건**:
1. `bForceFrontXAxis = true` (옵션 활성화)
2. `bConvertScene = true` (좌표계 변환 활성화)
3. Skeletal Mesh Import (Bone Hierarchy 존재)

**사용하지 않는 경우**:
- `bForceFrontXAxis = false` (기본값)
- Static Mesh Import (Bone 없음)
- Animation Import (별도 로직)

#### 3ds Max와의 연관성

3ds Max는 +X Forward 방식을 선호하므로, 3ds Max에서 export한 FBX를 Unreal Engine에서 사용할 때 `bForceFrontXAxis = true`를 설정하면:

1. Mesh Geometry: -Y Forward로 import
2. Bone Hierarchy: JointOrientationMatrix 적용으로 +X Forward로 보정
3. 결과: Mesh와 Bone이 올바른 상대 관계 유지

---

## Mundi 적용 계획

### 목표

1. ✅ **하위 호환성 유지**: 기존 FBX Import 동작 그대로
2. ✅ **선택적 변환**: `bConvertScene` 플래그로 제어
3. ✅ **UE5 호환**: 동일한 옵션 구조
4. ✅ **확장성**: 향후 추가 옵션 쉽게 추가 가능

### 구현 단계

```
Phase 1: Import Options 확장
  └─ FFbxImportOptions 구조체에 플래그 추가

Phase 2: FFbxDataConverter 클래스 추가
  └─ 변환 로직 캡슐화 및 재사용 가능하게

Phase 3: 조건부 변환 로직 구현
  └─ ConvertScene()에서 플래그 체크 및 분기
```

---

## Phase 1: Import Options 확장

### 1.1 FbxImportOptions.h 수정

**파일**: `Mundi/Source/Runtime/AssetManagement/FbxImportOptions.h`

```cpp
#pragma once

#include "pch.h"

/**
 * FBX Import 옵션
 * Unreal Engine의 UFbxAssetImportData와 유사한 구조
 */
struct FFbxImportOptions
{
    // === Import Type ===
    enum class EImportType
    {
        SkeletalMesh,
        StaticMesh,
        Animation
    };
    EImportType ImportType = EImportType::SkeletalMesh;

    // ========================================
    // 좌표계 변환 옵션 (Unreal Engine 호환)
    // ========================================

    /**
     * Scene 좌표계 변환 여부
     *
     * true:  FBX Scene을 Unreal-style 좌표계로 변환 (기본)
     *        Z-Up, -Y-Forward, Right-Handed → Y-Flip → Left-Handed
     *
     * false: FBX 원본 좌표계 유지 + Y-Flip만 적용
     *        (Axis Conversion Matrix = Identity)
     */
    bool bConvertScene = true;

    /**
     * Front Axis를 +X로 강제
     *
     * false: -Y Forward (기본, Maya/Max 호환)
     * true:  +X Forward (직관적, 일부 툴 호환)
     *
     * bConvertScene = true일 때만 유효
     */
    bool bForceFrontXAxis = false;

    /**
     * Scene 단위를 cm로 변환
     *
     * true:  FBX 단위 → Unreal Engine 단위(cm) 변환 (기본)
     * false: 원본 단위 유지
     */
    bool bConvertSceneUnit = true;

    // ========================================
    // Transform 오프셋 (Import 후 적용)
    // ========================================

    /**
     * Import 후 Translation 오프셋
     */
    FVector ImportTranslation = FVector(0.0f, 0.0f, 0.0f);

    /**
     * Import 후 Rotation 오프셋 (Degrees)
     */
    FRotator ImportRotation = FRotator(0.0f, 0.0f, 0.0f);

    /**
     * Import 후 Uniform Scale
     */
    float ImportUniformScale = 1.0f;

    // ========================================
    // 디버그 옵션
    // ========================================

    /**
     * 상세한 Import 로그 출력
     */
    bool bVerboseLogging = false;
};
```

### 1.2 기본 동작 (하위 호환성)

**현재 Mundi 동작**:
```cpp
FFbxImportOptions options;
// options.bConvertScene = true (기본값)
// options.bForceFrontXAxis = false (기본값)
// options.bConvertSceneUnit = true (기본값)

// → 기존과 동일하게 동작!
```

---

## Phase 2: FFbxDataConverter 클래스 추가

### 2.1 FFbxDataConverter.h 생성

**파일**: `Mundi/Source/Runtime/AssetManagement/FFbxDataConverter.h`

```cpp
#pragma once

#include "pch.h"
#include <fbxsdk.h>

/**
 * FBX 데이터 변환 유틸리티 클래스 (Unreal Engine 스타일)
 *
 * 좌표계 변환 로직을 캡슐화하여 재사용 가능하게 만듦
 * Static 클래스로 설계 (인스턴스 불필요)
 */
class FFbxDataConverter
{
public:
    // ========================================
    // Axis Conversion Matrix 관리
    // ========================================

    /**
     * Axis Conversion Matrix 설정
     * ConvertScene() 후 호출되어야 함
     *
     * @param Matrix - 소스→타겟 좌표계 변환 행렬
     */
    static void SetAxisConversionMatrix(const FbxAMatrix& Matrix);

    /**
     * Axis Conversion Matrix 가져오기
     */
    static const FbxAMatrix& GetAxisConversionMatrix();

    /**
     * Axis Conversion Matrix Inverse 가져오기
     */
    static const FbxAMatrix& GetAxisConversionMatrixInv();

    // ========================================
    // Joint Post-Conversion Matrix 관리
    // ========================================

    /**
     * Joint Post-Conversion Matrix 설정
     * bForceFrontXAxis = true일 때 (-90°, -90°, 0°) 회전 적용
     *
     * @param Matrix - Bone에 추가 적용할 회전 행렬
     *
     * NOTE: Skeletal Mesh Import에만 사용됨!
     */
    static void SetJointPostConversionMatrix(const FbxAMatrix& Matrix);

    /**
     * Joint Post-Conversion Matrix 가져오기
     * Skeletal Mesh Bone Transform에 적용
     */
    static const FbxAMatrix& GetJointPostConversionMatrix();

    // ========================================
    // 좌표 변환 함수 (Unreal Engine 방식)
    // ========================================

    /**
     * FbxVector4 Position을 Mundi FVector로 변환
     * Y축 반전으로 Right-Handed → Left-Handed 변환
     *
     * @param Vector - FBX Position Vector
     * @return Mundi FVector (Y축 반전 적용)
     */
    static FVector ConvertPos(const FbxVector4& Vector);

    /**
     * FbxVector4 Direction을 Mundi FVector로 변환
     * Normal, Tangent, Binormal에 사용
     *
     * @param Vector - FBX Direction Vector
     * @return Mundi FVector (Y축 반전, 정규화)
     */
    static FVector ConvertDir(const FbxVector4& Vector);

    /**
     * FbxQuaternion을 Mundi FQuat로 변환
     * Y, W 반전으로 Right-Handed → Left-Handed 변환
     *
     * @param Quaternion - FBX Quaternion
     * @return Mundi FQuat (Y, W 반전)
     */
    static FQuat ConvertRotToQuat(const FbxQuaternion& Quaternion);

    /**
     * FbxVector4 Scale을 Mundi FVector로 변환
     *
     * @param Vector - FBX Scale Vector
     * @return Mundi FVector (변환 없음)
     */
    static FVector ConvertScale(const FbxVector4& Vector);

    /**
     * FbxAMatrix를 Mundi FTransform으로 변환
     *
     * @param Matrix - FBX Transform Matrix
     * @return Mundi FTransform
     */
    static FTransform ConvertTransform(const FbxAMatrix& Matrix);

    /**
     * FbxMatrix를 Mundi FMatrix로 변환 (Y축 선택적 반전)
     *
     * @param Matrix - FBX Matrix
     * @return Mundi FMatrix
     */
    static FMatrix ConvertMatrix(const FbxMatrix& Matrix);

private:
    // Axis Conversion Matrix (Static)
    static FbxAMatrix AxisConversionMatrix;
    static FbxAMatrix AxisConversionMatrixInv;
    static bool bIsInitialized;

    // Joint Post-Conversion Matrix (Static) - SkeletalMesh Bone 전용
    static FbxAMatrix JointPostConversionMatrix;
    static bool bIsJointMatrixInitialized;
};
```

### 2.2 FFbxDataConverter.cpp 생성

**파일**: `Mundi/Source/Runtime/AssetManagement/FFbxDataConverter.cpp`

```cpp
#include "pch.h"
#include "FFbxDataConverter.h"

// Static 멤버 초기화
FbxAMatrix FFbxDataConverter::AxisConversionMatrix;
FbxAMatrix FFbxDataConverter::AxisConversionMatrixInv;
bool FFbxDataConverter::bIsInitialized = false;

FbxAMatrix FFbxDataConverter::JointPostConversionMatrix;
bool FFbxDataConverter::bIsJointMatrixInitialized = false;

void FFbxDataConverter::SetAxisConversionMatrix(const FbxAMatrix& Matrix)
{
    AxisConversionMatrix = Matrix;
    AxisConversionMatrixInv = Matrix.Inverse();
    bIsInitialized = true;
}

const FbxAMatrix& FFbxDataConverter::GetAxisConversionMatrix()
{
    if (!bIsInitialized)
    {
        AxisConversionMatrix.SetIdentity();
    }
    return AxisConversionMatrix;
}

const FbxAMatrix& FFbxDataConverter::GetAxisConversionMatrixInv()
{
    if (!bIsInitialized)
    {
        AxisConversionMatrixInv.SetIdentity();
    }
    return AxisConversionMatrixInv;
}

void FFbxDataConverter::SetJointPostConversionMatrix(const FbxAMatrix& Matrix)
{
    JointPostConversionMatrix = Matrix;
    bIsJointMatrixInitialized = true;
}

const FbxAMatrix& FFbxDataConverter::GetJointPostConversionMatrix()
{
    if (!bIsJointMatrixInitialized)
    {
        JointPostConversionMatrix.SetIdentity();
    }
    return JointPostConversionMatrix;
}

// ========================================
// 좌표 변환 함수 구현
// ========================================

FVector FFbxDataConverter::ConvertPos(const FbxVector4& Vector)
{
    return FVector(
        static_cast<float>(Vector[0]),
        static_cast<float>(-Vector[1]),  // Y축 반전 (RH → LH)
        static_cast<float>(Vector[2])
    );
}

FVector FFbxDataConverter::ConvertDir(const FbxVector4& Vector)
{
    FVector result(
        static_cast<float>(Vector[0]),
        static_cast<float>(-Vector[1]),  // Y축 반전
        static_cast<float>(Vector[2])
    );
    result.Normalize();
    return result;
}

FQuat FFbxDataConverter::ConvertRotToQuat(const FbxQuaternion& Quaternion)
{
    return FQuat(
        static_cast<float>(Quaternion[0]),    // X
        static_cast<float>(-Quaternion[1]),   // Y 반전
        static_cast<float>(Quaternion[2]),    // Z
        static_cast<float>(-Quaternion[3])    // W 반전
    );
}

FVector FFbxDataConverter::ConvertScale(const FbxVector4& Vector)
{
    return FVector(
        static_cast<float>(Vector[0]),
        static_cast<float>(Vector[1]),
        static_cast<float>(Vector[2])
    );
}

FTransform FFbxDataConverter::ConvertTransform(const FbxAMatrix& Matrix)
{
    FTransform result;

    // Position
    result.SetTranslation(ConvertPos(Matrix.GetT()));

    // Rotation
    result.SetRotation(ConvertRotToQuat(Matrix.GetQ()));

    // Scale
    result.SetScale3D(ConvertScale(Matrix.GetS()));

    return result;
}

FMatrix FFbxDataConverter::ConvertMatrix(const FbxMatrix& fbxMatrix)
{
    FMatrix result;

    // 행렬 복사
    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            result.M[row][col] = static_cast<float>(fbxMatrix.Get(row, col));
        }
    }

    // Y축 관련 요소 반전 (Right-Handed → Left-Handed)
    result.M[1][0] = -result.M[1][0];
    result.M[1][1] = -result.M[1][1];
    result.M[1][2] = -result.M[1][2];
    result.M[1][3] = -result.M[1][3];  // Translation Y

    return result;
}
```

---

## Phase 3: 조건부 변환 로직 구현

### 3.1 FbxImporter.h 수정

기존 `ConvertFbxPosition()` 등의 함수를 `FFbxDataConverter` 사용으로 대체:

```cpp
class FFbxImporter
{
public:
    // ... 기존 코드 ...

private:
    // === 좌표 변환 Helper 함수 (Deprecated) ===
    // FFbxDataConverter 사용 권장!

    FVector ConvertFbxPosition(const FbxVector4& pos)
    {
        return FFbxDataConverter::ConvertPos(pos);
    }

    FVector ConvertFbxDirection(const FbxVector4& dir)
    {
        return FFbxDataConverter::ConvertDir(dir);
    }

    FQuat ConvertFbxQuaternion(const FbxQuaternion& q)
    {
        return FFbxDataConverter::ConvertRotToQuat(q);
    }

    // ... 기존 코드 ...
};
```

### 3.2 ConvertScene() 리팩토링

**파일**: `FbxImporter.cpp`

```cpp
void FFbxImporter::ConvertScene()
{
    if (!Scene)
        return;

    // Axis Conversion Matrix 초기화 (Identity)
    FbxAMatrix axisConversionMatrix;
    axisConversionMatrix.SetIdentity();

    // Joint Post-Conversion Matrix 초기화 (Identity) - SkeletalMesh 전용
    FbxAMatrix jointPostConversionMatrix;
    jointPostConversionMatrix.SetIdentity();

    // === 좌표계 변환 (Unreal Engine 방식) ===
    if (CurrentOptions.bConvertScene)
    {
        // 원본 좌표계 가져오기
        FbxAxisSystem sceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

        // Target 좌표계 설정 (Unreal Engine 스타일)
        FbxAxisSystem::ECoordSystem CoordSystem = FbxAxisSystem::eRightHanded;
        FbxAxisSystem::EUpVector UpVector = FbxAxisSystem::eZAxis;
        FbxAxisSystem::EFrontVector FrontVector =
            (FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd;  // -Y Forward

        // bForceFrontXAxis 옵션 체크
        if (CurrentOptions.bForceFrontXAxis)
        {
            FrontVector = FbxAxisSystem::eParityEven;  // +X Forward
            UE_LOG("[FBX] bForceFrontXAxis enabled - using +X as Forward axis");
        }

        FbxAxisSystem UnrealImportAxis(UpVector, FrontVector, CoordSystem);

        // 좌표계가 다른 경우만 변환
        if (sceneAxis != UnrealImportAxis)
        {
            UE_LOG("[FBX] Converting scene coordinate system...");

            // CRITICAL: FBX Root 노드 제거 먼저 수행!
            UE_LOG("[FBX] Removing FBX root nodes (Unreal Engine style)");
            FbxRootNodeUtility::RemoveAllFbxRoots(Scene);

            // 좌표계 변환 수행
            UE_LOG("[FBX] Applying FbxAxisSystem::ConvertScene()");
            UnrealImportAxis.ConvertScene(Scene);

            // CRITICAL: bForceFrontXAxis = true면 JointOrientationMatrix 설정
            // -Y Forward → +X Forward 변환 (SkeletalMesh Bone Hierarchy 전용)
            if (CurrentOptions.bForceFrontXAxis)
            {
                jointPostConversionMatrix.SetR(FbxVector4(-90.0, -90.0, 0.0));
                UE_LOG("[FBX] JointOrientationMatrix set: (-90°, -90°, 0°)");
                UE_LOG("[FBX] This will convert Bone Hierarchy from -Y Forward to +X Forward");
            }

            // Axis Conversion Matrix 계산
            FbxAMatrix sourceMatrix, targetMatrix;
            sceneAxis.GetMatrix(sourceMatrix);
            UnrealImportAxis.GetMatrix(targetMatrix);
            axisConversionMatrix = sourceMatrix.Inverse() * targetMatrix;

            UE_LOG("[FBX] Axis Conversion Matrix calculated");

            // 변환 후 검증
            FbxAxisSystem convertedAxis = Scene->GetGlobalSettings().GetAxisSystem();
            int upSign, frontSign;
            FbxAxisSystem::EUpVector upVec = convertedAxis.GetUpVector(upSign);
            FbxAxisSystem::EFrontVector frontVec = convertedAxis.GetFrontVector(frontSign);
            FbxAxisSystem::ECoordSystem coordSys = convertedAxis.GetCoorSystem();

            UE_LOG("[FBX DEBUG] === After Conversion ===");
            UE_LOG("[FBX DEBUG] UpVector: %d (sign: %d)", (int)upVec, upSign);
            UE_LOG("[FBX DEBUG] FrontVector: %d (sign: %d)", (int)frontVec, frontSign);
            UE_LOG("[FBX DEBUG] CoordSystem: %s",
                coordSys == FbxAxisSystem::eRightHanded ? "RightHanded" : "LeftHanded");
        }
        else
        {
            UE_LOG("[FBX] Scene already in target coordinate system");
        }
    }
    else
    {
        UE_LOG("[FBX] bConvertScene = false - skipping coordinate conversion");
        UE_LOG("[FBX] Only Y-axis flip will be applied during vertex transformation");
    }

    // FFbxDataConverter에 Matrix 저장
    FFbxDataConverter::SetAxisConversionMatrix(axisConversionMatrix);
    FFbxDataConverter::SetJointPostConversionMatrix(jointPostConversionMatrix);

    // === 단위 변환 ===
    if (CurrentOptions.bConvertSceneUnit)
    {
        FbxSystemUnit sceneUnit = Scene->GetGlobalSettings().GetSystemUnit();

        if (sceneUnit != FbxSystemUnit::cm)
        {
            UE_LOG("[FBX] Converting scene unit to cm");
            FbxSystemUnit::cm.ConvertScene(Scene);
        }
        else
        {
            UE_LOG("[FBX] Scene already in cm unit");
        }
    }
    else
    {
        UE_LOG("[FBX] bConvertSceneUnit = false - keeping original unit");
    }

    // Animation Evaluator Reset (Unreal Engine 방식)
    Scene->GetAnimationEvaluator()->Reset();

    UE_LOG("[FBX] ConvertScene() complete");
    UE_LOG("[FBX] Next: Per-vertex Y-flip will convert Right-Handed to Left-Handed");
}
```

### 3.3 Vertex 변환 코드 수정 (선택 사항)

기존 `ConvertFbxPosition()` 호출을 `FFbxDataConverter::ConvertPos()`로 변경:

```cpp
// 기존 코드
vertex.Position = ConvertFbxPosition(transformedPos);
vertex.Normal = ConvertFbxDirection(transformedNormal);

// 권장 코드 (명시적)
vertex.Position = FFbxDataConverter::ConvertPos(transformedPos);
vertex.Normal = FFbxDataConverter::ConvertDir(transformedNormal);
```

### 3.4 Skeletal Mesh Bone Transform에 JointPostConversionMatrix 적용

**CRITICAL**: `bForceFrontXAxis = true`일 때만 Bone Transform에 JointPostConversionMatrix 적용

**파일**: `FbxImporter.cpp` - `ExtractBindPose()` 또는 `ExtractSkeleton()`

```cpp
bool FFbxImporter::ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton)
{
    // ... 기존 코드 (Bind Pose 추출) ...

    for (int i = 0; i < poseCount; i++)
    {
        FbxPose* pose = Scene->GetPose(i);
        if (pose->IsBindPose())
        {
            for (int j = 0; j < pose->GetCount(); j++)
            {
                FbxNode* node = pose->GetNode(j);
                FbxAMatrix bindPoseMatrix = pose->GetMatrix(j);

                // CRITICAL: JointPostConversionMatrix 적용 (Unreal Engine 방식)
                // bForceFrontXAxis = true일 때만 적용됨
                FbxAMatrix jointPostMatrix = FFbxDataConverter::GetJointPostConversionMatrix();
                bindPoseMatrix = bindPoseMatrix * jointPostMatrix;

                // Mundi FTransform으로 변환 (Y-Flip 포함)
                FTransform bindTransform = FFbxDataConverter::ConvertTransform(bindPoseMatrix);

                // Bone에 저장
                int32 boneIndex = OutSkeleton->FindBoneIndex(FString(node->GetName()));
                if (boneIndex != -1)
                {
                    OutSkeleton->SetBoneBindPose(boneIndex, bindTransform);
                }
            }
        }
    }

    return true;
}
```

**동작 원리**:

1. `bForceFrontXAxis = false` (기본값):
   - `JointPostConversionMatrix = Identity`
   - Bone Transform에 영향 없음
   - 기존 동작과 동일

2. `bForceFrontXAxis = true`:
   - `JointPostConversionMatrix = Euler(-90°, -90°, 0°)`
   - Bone Hierarchy가 -Y Forward → +X Forward로 회전
   - Vertex는 영향받지 않음 (Y-Flip만 적용)

**주의사항**:

- **Skeletal Mesh Import에만** 적용
- **Static Mesh Import**는 Bone이 없으므로 무시됨
- Bind Pose와 Animation 모두 동일한 Matrix 적용 필요

---

## 사용 예제

### 예제 1: 기본 사용 (Unreal Engine 방식)

```cpp
FFbxImporter importer;
FFbxImportOptions options;

// 기본값 사용 (모두 true)
// options.bConvertScene = true;
// options.bForceFrontXAxis = false;
// options.bConvertSceneUnit = true;

USkeletalMesh* mesh = importer.ImportSkeletalMesh("model.fbx", options);

// 결과: Z-Up, X-Forward, Left-Handed (Unreal Engine/Mundi 표준)
```

### 예제 2: Front Axis +X 강제

```cpp
FFbxImportOptions options;
options.bConvertScene = true;
options.bForceFrontXAxis = true;  // +X Forward

USkeletalMesh* mesh = importer.ImportSkeletalMesh("model.fbx", options);

// 결과: Z-Up, +X-Forward (명시적), Left-Handed
// 일부 툴에서 export한 FBX에 유용
```

### 예제 3: 최소 변환 (FBX 원본 유지)

```cpp
FFbxImportOptions options;
options.bConvertScene = false;      // 좌표계 변환 안 함!
options.bConvertSceneUnit = true;   // 단위만 변환

USkeletalMesh* mesh = importer.ImportSkeletalMesh("model.fbx", options);

// 결과: FBX 원본 좌표계 + Y-Flip만 적용
// 디버깅이나 특수한 경우에 유용
```

### 예제 4: Transform 오프셋 적용

```cpp
FFbxImportOptions options;
options.bConvertScene = true;

// Import 후 추가 Transform 적용
options.ImportTranslation = FVector(0.0f, 0.0f, 100.0f);  // 100cm 위로
options.ImportRotation = FRotator(0.0f, 90.0f, 0.0f);     // Y축 90도 회전
options.ImportUniformScale = 2.0f;                         // 2배 확대

USkeletalMesh* mesh = importer.ImportSkeletalMesh("model.fbx", options);

// 결과: 변환 후 추가 Transform 적용
// (향후 구현 예정)
```

---

## 마이그레이션 가이드

### 기존 코드와의 호환성

**좋은 소식**: 기본 동작은 **완전히 동일**합니다!

```cpp
// 기존 코드 (변경 불필요)
FFbxImportOptions options;
USkeletalMesh* mesh = importer.ImportSkeletalMesh("model.fbx", options);

// 모든 플래그가 기본값 true이므로 기존과 동일하게 동작
```

### 점진적 마이그레이션

#### Step 1: FFbxDataConverter 추가 (필수)

프로젝트에 새 파일 추가:
- `FFbxDataConverter.h`
- `FFbxDataConverter.cpp`

#### Step 2: ConvertScene() 리팩토링 (권장)

`FbxImporter.cpp`의 `ConvertScene()` 함수를 새 구현으로 교체

#### Step 3: Import Options 확장 (선택)

`FbxImportOptions.h`에 새 플래그 추가

#### Step 4: 기존 Helper 함수 대체 (선택)

`ConvertFbxPosition()` 등을 `FFbxDataConverter` 사용으로 변경

### 테스트 체크리스트

- [ ] 기존 FBX 파일이 동일하게 Import되는지 확인
- [ ] `bConvertScene = false` 옵션 테스트
- [ ] `bForceFrontXAxis = true` 옵션 테스트
- [ ] Static Mesh와 Skeletal Mesh 모두 테스트
- [ ] Winding Order가 올바른지 확인 (CW = Front Face)

---

## 구현 우선순위

### High Priority (즉시 구현)

1. ✅ **FFbxDataConverter 클래스 추가**
   - 변환 로직 캡슐화
   - 재사용 가능한 구조

2. ✅ **FFbxImportOptions 확장**
   - `bConvertScene`, `bForceFrontXAxis`, `bConvertSceneUnit` 추가
   - 기본값 설정으로 하위 호환성 유지

3. ✅ **ConvertScene() 리팩토링**
   - 조건부 변환 로직
   - Axis Conversion Matrix 계산

### Medium Priority (차후 구현)

4. ⏳ **Import Transform Offset**
   - `ImportTranslation`, `ImportRotation`, `ImportUniformScale` 적용
   - Post-import transform 처리

5. ⏳ **Advanced Options**
   - `bPreserveLocalTransform` - Local Transform 보존
   - `bImportMeshLODs` - LOD Import
   - `bImportMorphTargets` - Morph Target Import

### Low Priority (향후 고려)

6. 🔮 **UI Integration**
   - Editor에서 옵션 설정 가능한 UI
   - Import Preset 저장/로드

7. 🔮 **Batch Import**
   - 여러 FBX 파일 일괄 Import
   - 동일 옵션 적용

---

## 성능 고려사항

### Axis Conversion Matrix 캐싱

`FFbxDataConverter`는 **Static 클래스**로 설계:
- Axis Conversion Matrix를 한 번만 계산
- 모든 Vertex 변환에서 재사용
- 메모리 오버헤드 최소화 (Matrix 2개만)

### 조건부 변환

`bConvertScene = false` 시:
- `FbxAxisSystem::ConvertScene()` 스킵
- `FbxRootNodeUtility::RemoveAllFbxRoots()` 스킵
- Axis Conversion Matrix = Identity
- **성능 향상** (대형 Scene에서 유의미)

---

## 참고 자료

### Unreal Engine 5 Source Code

- `Engine/Source/Editor/UnrealEd/Classes/Factories/FbxAssetImportData.h`
  - Import Options 정의

- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxMainImport.cpp`
  - ConvertScene() 구현 (Line 1499-1580)
  - JointOrientationMatrix 설정 (Line 1523-1566)

- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxSkeletalMeshImport.cpp`
  - JointPostConversionMatrix 적용 (Line 1211)
  - Skeletal Mesh Bone Transform 처리

- `Engine/Source/Editor/UnrealEd/Private/Fbx/FbxUtilsImport.cpp`
  - FFbxDataConverter 구현 (Line 63-151)
  - SetJointPostConversionMatrix() / GetJointPostConversionMatrix()

- `Engine/Source/Runtime/Windows/D3D11RHI/Private/D3D11State.cpp`
  - Rasterizer State 설정 (Line 211)
  - `FrontCounterClockwise = true` (Unreal Engine의 CCW = Front Face)

### FBX SDK Documentation

- [FbxAxisSystem](https://help.autodesk.com/view/FBX/2020/ENU/?guid=FBX_Developer_Help_cpp_ref_class_fbx_axis_system_html)
- [FbxRootNodeUtility](https://help.autodesk.com/view/FBX/2020/ENU/?guid=FBX_Developer_Help_cpp_ref_class_fbx_root_node_utility_html)

### Mundi Documentation

- `Mundi/Documentation/Mundi_FBX_Import_Pipeline.md`
  - 현재 FBX Import Pipeline 문서

- `Mundi/README.md`
  - Mundi 엔진 좌표계 설명

---

## FAQ

### Q1: 기존 코드를 수정해야 하나요?

**A**: 아니요! 기본값이 모두 `true`이므로 기존 동작과 동일합니다.

### Q2: bConvertScene = false는 언제 사용하나요?

**A**: 다음 경우에 유용합니다:
- FBX가 이미 Mundi 좌표계와 일치
- 디버깅 목적 (최소 변환)
- 특수한 툴 파이프라인 (예: 커스텀 exporter)

### Q3: bForceFrontXAxis는 언제 사용하나요?

**A**: 일부 3D 툴(3ds Max 등)에서 export한 FBX가 -Y Forward 대신 +X Forward를 기대할 때 사용

### Q4: Winding Order는 어떻게 되나요?

**A**: 변경 없음! 여전히 Index Reversal 수행하여 CCW → CW 변환

### Q5: 성능 영향은?

**A**: 거의 없음. Axis Conversion Matrix 계산이 추가되지만 한 번만 수행됨

### Q6: JointOrientationMatrix는 무엇인가요?

**A**: `bForceFrontXAxis = true`일 때 **Skeletal Mesh Bone Hierarchy에만** 적용되는 추가 회전 행렬입니다.

- **목적**: -Y Forward (Import) → +X Forward (Runtime) 변환
- **적용 대상**: Bone Transform만 (Vertex는 영향 없음)
- **값**: Euler(-90°, -90°, 0°) 회전
- **현재 Mundi**: 불필요 (기본값 `bForceFrontXAxis = false`)
- **Static Mesh**: 항상 무시됨 (Bone 없음)

### Q7: bForceFrontXAxis = true는 언제 구현하나요?

**A**: 현재는 구현 불필요합니다.

- **현재**: Mundi는 -Y Forward만 사용 → JointOrientationMatrix 불필요
- **향후**: 3ds Max 호환성이 필요하면 구현 고려
- **우선순위**: Low (특수 케이스)

구현 시 추가 작업:
1. `FFbxDataConverter`에 `SetJointPostConversionMatrix()` 추가
2. `ConvertScene()`에서 `bForceFrontXAxis` 체크 후 Matrix 설정
3. `ExtractBindPose()`에서 Bone Transform에 적용
4. Animation Import에도 동일 로직 적용

---

## 변경 이력

| 버전 | 날짜 | 내용 |
|------|------|------|
| 1.0 | 2025-11-10 | Initial Draft - UE5 분석 및 Mundi 적용 계획 |
| 1.1 | 2025-11-10 | JointOrientationMatrix 분석 추가 - `bForceFrontXAxis` 상세 문서화 |

**v1.1 주요 변경사항**:
- UE5의 `JointOrientationMatrix` 동작 원리 분석 추가
- `bForceFrontXAxis = true`일 때 Skeletal Mesh Bone Transform 추가 회전 문서화
- Phase 2에 `FFbxDataConverter::SetJointPostConversionMatrix()` API 추가
- Phase 3에 `ConvertScene()` 및 `ExtractBindPose()` 구현 예제 추가
- FAQ에 JointOrientationMatrix 관련 항목 추가 (Q6, Q7)
- Static Mesh vs Skeletal Mesh 차이점 명확화

---

**End of Document**
