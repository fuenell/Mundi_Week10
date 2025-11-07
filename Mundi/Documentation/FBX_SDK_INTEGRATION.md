# FBX SDK Integration Guide

Mundi 엔진에 Autodesk FBX SDK 2020.3.7을 통합하는 작업 가이드

**작성일:** 2025-11-07
**SDK 버전:** FBX SDK 2020.3.7
**SDK 경로:** `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7`

---

## 📋 목차

1. [개요](#개요)
2. [현재 상황 분석](#현재-상황-분석)
3. [통합 작업 단계](#통합-작업-단계)
4. [상세 구현 가이드](#상세-구현-가이드)
5. [트러블슈팅](#트러블슈팅)
6. [참고 자료](#참고-자료)

---

## 개요

### 목적
FBX 파일 포맷을 Mundi 엔진에서 임포트할 수 있도록 Autodesk FBX SDK를 통합합니다.

### 작업 범위
- [x] FBX SDK 파일 구조 분석
- [ ] ThirdParty 폴더에 SDK 파일 복사
- [ ] Visual Studio 프로젝트 설정 수정
- [ ] DLL 복사 자동화 설정
- [ ] 빌드 및 동작 테스트
- [ ] FFbxImporter 클래스 구현 (별도 작업)

### 기대 효과
- `.fbx` 파일 임포트 지원
- Maya, Blender, 3ds Max 등에서 생성된 3D 모델 사용 가능
- 기존 OBJ 포맷보다 풍부한 데이터 (애니메이션, 스켈레톤, 머티리얼) 지원

---

## 현재 상황 분석

### FBX SDK 설치 구조

```
C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\
├── include/
│   ├── fbxsdk.h              # Main header
│   ├── fbxsdk/               # SDK headers (전체 폴더)
│   └── libxml2/              # XML parsing library headers
├── lib/
│   └── x64/
│       ├── debug/
│       │   ├── libfbxsdk.dll         # Runtime DLL (Debug)
│       │   ├── libfbxsdk-md.lib      # Import library (Multi-threaded DLL)
│       │   ├── libfbxsdk-mt.lib      # Import library (Multi-threaded Static)
│       │   ├── libxml2-md.lib        # XML library dependency
│       │   └── zlib-md.lib           # Compression library dependency
│       └── release/
│           └── (동일 구조)
└── samples/                  # SDK 샘플 코드
```

### 프로젝트 Third-Party 라이브러리 통합 패턴

현재 프로젝트는 다음과 같은 구조로 외부 라이브러리를 통합합니다:

```
Mundi/ThirdParty/
├── DirectXTex/
│   ├── include/
│   └── lib/
│       ├── Debug/DirectXTex.lib
│       └── Release/DirectXTex.lib
├── DirectXTK/
│   ├── include/
│   └── lib/Debug/, lib/Release/
├── Lua/
│   ├── include/
│   └── lib/Debug/, lib/Release/
└── ImGui/
    └── (소스 코드 직접 포함)
```

**vcxproj 설정 예시 (Mundi.vcxproj:103, 109-110):**
```xml
<AdditionalIncludeDirectories>
  ThirdParty\DirectXTK\include;ThirdParty\Lua\include;...
</AdditionalIncludeDirectories>
<AdditionalLibraryDirectories>
  ThirdParty\Lua\lib\$(Configuration)\;...
</AdditionalLibraryDirectories>
<AdditionalDependencies>
  DirectXTK.lib;Lua.lib;%(AdditionalDependencies)
</AdditionalDependencies>
```

### 런타임 라이브러리 설정 확인 필요

FBX SDK는 `/MD` (Multi-threaded DLL)와 `/MT` (Multi-threaded Static) 두 가지 버전을 제공합니다.

**확인 방법:**
1. Visual Studio에서 프로젝트 속성 열기
2. `C/C++ → Code Generation → Runtime Library` 확인
3. 또는 `Mundi.vcxproj`에서 `<RuntimeLibrary>` 검색

**예상:** 현재 프로젝트는 `/MD` 사용 중 → `libfbxsdk-md.lib` 사용

---

## 통합 작업 단계

### Phase 1: SDK 파일 준비 및 복사

#### ✅ Task 1.1: ThirdParty/FBX 폴더 구조 생성

```bash
mkdir Mundi\ThirdParty\FBX
mkdir Mundi\ThirdParty\FBX\include
mkdir Mundi\ThirdParty\FBX\lib
mkdir Mundi\ThirdParty\FBX\lib\Debug
mkdir Mundi\ThirdParty\FBX\lib\Release
mkdir Mundi\ThirdParty\FBX\bin
mkdir Mundi\ThirdParty\FBX\bin\Debug
mkdir Mundi\ThirdParty\FBX\bin\Release
```

**PowerShell 한 줄 실행:**
```powershell
New-Item -ItemType Directory -Force -Path "Mundi\ThirdParty\FBX\include", "Mundi\ThirdParty\FBX\lib\Debug", "Mundi\ThirdParty\FBX\lib\Release", "Mundi\ThirdParty\FBX\bin\Debug", "Mundi\ThirdParty\FBX\bin\Release"
```

#### ✅ Task 1.2: Include 파일 복사

```powershell
# fbxsdk.h 복사
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\include\fbxsdk.h" -Destination "Mundi\ThirdParty\FBX\include\" -Force

# fbxsdk 폴더 전체 복사
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\include\fbxsdk" -Destination "Mundi\ThirdParty\FBX\include\" -Recurse -Force

# libxml2 폴더 복사 (선택사항 - 필요시)
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\include\libxml2" -Destination "Mundi\ThirdParty\FBX\include\" -Recurse -Force
```

#### ✅ Task 1.3: Library 파일 복사 (Debug)

```powershell
# Debug 라이브러리 복사
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\debug\libfbxsdk-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Debug\" -Force
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\debug\libxml2-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Debug\" -Force
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\debug\zlib-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Debug\" -Force
```

#### ✅ Task 1.4: Library 파일 복사 (Release)

```powershell
# Release 라이브러리 복사
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\release\libfbxsdk-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Release\" -Force
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\release\libxml2-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Release\" -Force
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\release\zlib-md.lib" -Destination "Mundi\ThirdParty\FBX\lib\Release\" -Force
```

#### ✅ Task 1.5: DLL 파일 복사

```powershell
# Debug DLL
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\debug\libfbxsdk.dll" -Destination "Mundi\ThirdParty\FBX\bin\Debug\" -Force

# Release DLL
Copy-Item "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\lib\x64\release\libfbxsdk.dll" -Destination "Mundi\ThirdParty\FBX\bin\Release\" -Force
```

**🎯 결과 확인:**
```
Mundi\ThirdParty\FBX\
├── include\
│   ├── fbxsdk.h
│   └── fbxsdk\
├── lib\
│   ├── Debug\
│   │   ├── libfbxsdk-md.lib
│   │   ├── libxml2-md.lib
│   │   └── zlib-md.lib
│   └── Release\
│       └── (동일)
└── bin\
    ├── Debug\
    │   └── libfbxsdk.dll
    └── Release\
        └── libfbxsdk.dll
```

---

### Phase 2: Visual Studio 프로젝트 설정

#### ✅ Task 2.1: Include 디렉토리 추가

**파일:** `Mundi.vcxproj`
**위치:** 각 Configuration의 `<AdditionalIncludeDirectories>` 태그

**수정 대상 라인:**
- Line 103: Debug Configuration
- Line 128: Debug_StandAlone Configuration
- Line 155: Release Configuration
- Line 183: Release_StandAlone Configuration

**변경 전:**
```xml
<AdditionalIncludeDirectories>$(ProjectDir);...; ThirdParty\DirectXTK\include;ThirdParty\Lua\include;ThirdParty\sol;ThirdParty</AdditionalIncludeDirectories>
```

**변경 후:**
```xml
<AdditionalIncludeDirectories>$(ProjectDir);...; ThirdParty\FBX\include;ThirdParty\DirectXTK\include;ThirdParty\Lua\include;ThirdParty\sol;ThirdParty</AdditionalIncludeDirectories>
```

**🔧 자동 치환 스크립트 (PowerShell):**
```powershell
$vcxproj = "Mundi\Mundi.vcxproj"
$content = Get-Content $vcxproj -Raw
$content = $content -replace '(ThirdParty\\DirectXTK\\include)', 'ThirdParty\FBX\include;$1'
Set-Content $vcxproj $content
```

#### ✅ Task 2.2: Library 디렉토리 추가

**수정 대상:**
- Line 109: Debug
- Line 134: Debug_StandAlone
- Line 163: Release
- Line 191: Release_StandAlone

**Debug/Debug_StandAlone 예시:**
```xml
<!-- 변경 전 -->
<AdditionalLibraryDirectories>%(AdditionalLibraryDirectories);ThirdParty\Lua\lib\Debug\;ThirdParty\DirectXTex\lib\Debug\;ThirdParty\DirectXTK\lib\Debug\</AdditionalLibraryDirectories>

<!-- 변경 후 -->
<AdditionalLibraryDirectories>%(AdditionalLibraryDirectories);ThirdParty\FBX\lib\Debug\;ThirdParty\Lua\lib\Debug\;ThirdParty\DirectXTex\lib\Debug\;ThirdParty\DirectXTK\lib\Debug\</AdditionalLibraryDirectories>
```

**Release/Release_StandAlone 예시:**
```xml
<AdditionalLibraryDirectories>%(AdditionalLibraryDirectories);ThirdParty\FBX\lib\Release\;ThirdParty\Lua\lib\$(Configuration)\;ThirdParty\DirectXTex\lib\$(Configuration)\;ThirdParty\DirectXTK\lib\$(Configuration)\</AdditionalLibraryDirectories>
```

#### ✅ Task 2.3: 링크할 라이브러리 추가

**수정 대상:**
- Line 110: Debug
- Line 135: Debug_StandAlone
- Line 164: Release
- Line 192: Release_StandAlone

**변경 전:**
```xml
<AdditionalDependencies>DirectXTK.lib;DirectXTex.lib;%(AdditionalDependencies);Lua.lib</AdditionalDependencies>
```

**변경 후:**
```xml
<AdditionalDependencies>libfbxsdk-md.lib;libxml2-md.lib;zlib-md.lib;DirectXTK.lib;DirectXTex.lib;%(AdditionalDependencies);Lua.lib</AdditionalDependencies>
```

**⚠️ 주의:**
- `/MD` (Multi-threaded DLL)인 경우: `libfbxsdk-md.lib` 사용
- `/MT` (Multi-threaded Static)인 경우: `libfbxsdk-mt.lib` 사용

---

### Phase 3: DLL 복사 자동화

#### ✅ Task 3.1: CustomPreBuildCopy 타겟에 DLL 복사 추가

**파일:** `Mundi.vcxproj`
**위치:** Line 767-866 (PowerShell 스크립트 영역)

**Debug Configuration 스크립트 수정 (Line 767-790):**

변경 전:
```xml
<PropertyGroup Condition="'$(Configuration)'=='Debug'">
  <PowerShellScript>
    Write-Host '[BuildEvent] Copying Data files (Debug)...';
    robocopy '$(ProjectDir)Data' '$(OutDir)Data' /E /XO /MT:8 /NFL /NDL /NJH /NJS /nc /ns /np;
    ...
    Write-Host '[BuildEvent] Asset copy successful.';
    exit 0
  </PowerShellScript>
</PropertyGroup>
```

변경 후:
```xml
<PropertyGroup Condition="'$(Configuration)'=='Debug'">
  <PowerShellScript>
    Write-Host '[BuildEvent] Copying Data files (Debug)...';
    robocopy '$(ProjectDir)Data' '$(OutDir)Data' /E /XO /MT:8 /NFL /NDL /NJH /NJS /nc /ns /np;
    $exitCode = $LASTEXITCODE;
    if ($exitCode -ge 8) { Write-Error 'Robocopy Data failed.' ; exit $exitCode };

    Write-Host '[BuildEvent] Copying Shaders files (Debug)...';
    robocopy '$(ProjectDir)Shaders' '$(OutDir)Shaders' /E /XO /MT:8 /NFL /NDL /NJH /NJS /nc /ns /np;
    $exitCode = $LASTEXITCODE;
    if ($exitCode -ge 8) { Write-Error 'Robocopy Shaders failed.' ; exit $exitCode };

    Write-Host '[BuildEvent] Copying FBX SDK DLL (Debug)...';
    Copy-Item -Path '$(ProjectDir)ThirdParty\FBX\bin\Debug\libfbxsdk.dll' -Destination '$(OutDir)' -Force;

    if (Test-Path '$(ProjectDir)editor.ini') {
    Write-Host '[BuildEvent] Copying editor.ini...';
    Copy-Item -Path '$(ProjectDir)editor.ini' -Destination '$(OutDir)' -Force
    };
    if (Test-Path '$(ProjectDir)imgui.ini') {
    Write-Host '[BuildEvent] Copying imgui.ini...';
    Copy-Item -Path '$(ProjectDir)imgui.ini' -Destination '$(OutDir)' -Force
    };

    Write-Host '[BuildEvent] Asset copy successful.';
    exit 0
  </PowerShellScript>
</PropertyGroup>
```

**동일한 방식으로 다음 Configuration도 수정:**
- Debug_StandAlone (Line 792-815)
- Release (Line 818-841)
- Release_StandAlone (Line 843-866)

**각 Configuration별로 추가할 코드:**
```powershell
Write-Host '[BuildEvent] Copying FBX SDK DLL (Debug/Release)...';
Copy-Item -Path '$(ProjectDir)ThirdParty\FBX\bin\Debug\libfbxsdk.dll' -Destination '$(OutDir)' -Force;
# Release인 경우: bin\Release\libfbxsdk.dll
```

---

### Phase 4: 빌드 및 동작 테스트

#### ✅ Task 4.1: 테스트 코드 작성

**파일 생성:** `Mundi\Source\Runtime\AssetManagement\FbxImporterTest.cpp`

```cpp
#include "pch.h"

// FBX SDK 테스트용 임시 코드
#ifdef _DEBUG
#include <fbxsdk.h>
#include <iostream>

namespace FbxTest
{
    void TestFbxSdkIntegration()
    {
        // FBX SDK Manager 초기화
        FbxManager* lSdkManager = FbxManager::Create();
        if (!lSdkManager)
        {
            std::cout << "[FBX Test] Failed to create FBX Manager!" << std::endl;
            return;
        }

        // 버전 정보 출력
        std::cout << "[FBX Test] FBX SDK Integration Successful!" << std::endl;
        std::cout << "[FBX Test] SDK Version: " << lSdkManager->GetVersion() << std::endl;

        // IOSettings 생성 테스트
        FbxIOSettings* ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
        lSdkManager->SetIOSettings(ios);

        std::cout << "[FBX Test] IOSettings created successfully." << std::endl;

        // 정리
        lSdkManager->Destroy();
        std::cout << "[FBX Test] Cleanup completed." << std::endl;
    }
}
#endif // _DEBUG
```

**테스트 함수 호출 (main.cpp 또는 EditorEngine.cpp에 임시 추가):**
```cpp
#ifdef _DEBUG
namespace FbxTest { void TestFbxSdkIntegration(); }
#endif

// 엔진 초기화 후 어딘가에서 호출
#ifdef _DEBUG
    FbxTest::TestFbxSdkIntegration();
#endif
```

#### ✅ Task 4.2: vcxproj에 테스트 파일 추가

**Mundi.vcxproj의 `<ItemGroup>` 섹션에 추가:**
```xml
<ClCompile Include="Source\Runtime\AssetManagement\FbxImporterTest.cpp" />
```

#### ✅ Task 4.3: 빌드 실행

```bash
# Clean build
msbuild Mundi.sln /t:Clean /p:Configuration=Debug /p:Platform=x64

# Debug 빌드
msbuild Mundi.sln /p:Configuration=Debug /p:Platform=x64
```

**예상 출력:**
```
[BuildEvent] Copying Data files (Debug)...
[BuildEvent] Copying Shaders files (Debug)...
[BuildEvent] Copying FBX SDK DLL (Debug)...
[BuildEvent] Asset copy successful.
```

#### ✅ Task 4.4: 실행 테스트

**실행:**
```bash
cd Binaries\Debug
.\Mundi.exe
```

**예상 콘솔 출력:**
```
[FBX Test] FBX SDK Integration Successful!
[FBX Test] SDK Version: 2020.3.7
[FBX Test] IOSettings created successfully.
[FBX Test] Cleanup completed.
```

#### ✅ Task 4.5: DLL 로드 확인

**방법 1: Process Explorer 사용**
- Mundi.exe 프로세스의 DLL 목록에서 `libfbxsdk.dll` 확인

**방법 2: Dependency Walker**
- `Binaries\Debug\Mundi.exe`를 Dependency Walker로 열어 확인

**방법 3: 수동 확인**
```bash
cd Binaries\Debug
dir libfbxsdk.dll
```

---

## 상세 구현 가이드

### FFbxImporter 클래스 구조 (Phase 5 - 별도 작업)

#### 파일 구조

```
Source\Runtime\AssetManagement\
├── FbxImporter.h        # 새 파일
└── FbxImporter.cpp      # 새 파일
```

#### FbxImporter.h

```cpp
#pragma once

#include "StaticMesh.h"
#include <string>
#include <vector>

// Forward declarations
namespace fbxsdk
{
    class FbxManager;
    class FbxScene;
    class FbxNode;
    class FbxMesh;
}

/**
 * FBX 파일을 Mundi 엔진의 StaticMesh로 변환하는 임포터
 *
 * 사용 예시:
 *   FFbxImporter importer;
 *   UStaticMesh* mesh = importer.ImportFbxFile(L"Data/Model/Character.fbx");
 */
class FFbxImporter
{
public:
    FFbxImporter();
    ~FFbxImporter();

    /**
     * FBX 파일을 로드하여 UStaticMesh로 변환
     * @param InFilePath FBX 파일 경로 (절대 경로 또는 Data/ 기준 상대 경로)
     * @return 변환된 StaticMesh (실패 시 nullptr)
     */
    UStaticMesh* ImportFbxFile(const std::wstring& InFilePath);

    /**
     * FBX 임포트 옵션
     */
    struct FImportOptions
    {
        bool bImportTextures = true;        // 텍스처 임포트 여부
        bool bImportMaterials = true;       // 머티리얼 임포트 여부
        bool bImportNormals = true;         // 노멀 데이터 임포트
        bool bImportTangents = false;       // 탄젠트 데이터 임포트
        bool bImportSkeleton = false;       // 스켈레톤 임포트 (향후 확장)
        bool bImportAnimations = false;     // 애니메이션 임포트 (향후 확장)

        float GlobalScale = 1.0f;           // 글로벌 스케일 (cm → m 변환 등)
        bool bConvertToLeftHanded = true;   // Right-Handed → Left-Handed 변환
        bool bFlipUVVertical = false;       // UV V좌표 반전 (텍스처 상하 반전)
    };

    // 옵션 설정
    void SetImportOptions(const FImportOptions& InOptions) { Options = InOptions; }
    const FImportOptions& GetImportOptions() const { return Options; }

private:
    // FBX SDK 객체
    fbxsdk::FbxManager* SdkManager = nullptr;
    fbxsdk::FbxScene* Scene = nullptr;
    FImportOptions Options;

    // 초기화 및 정리
    void InitializeSdk();
    void CleanupSdk();
    bool LoadScene(const std::string& InFilePath);

    // 노드 처리
    void ProcessNode(fbxsdk::FbxNode* InNode, UStaticMesh* OutMesh);
    void ProcessMesh(fbxsdk::FbxMesh* InFbxMesh, UStaticMesh* OutMesh);

    // 데이터 추출
    void ExtractVertices(fbxsdk::FbxMesh* InMesh, std::vector<FVector>& OutVertices);
    void ExtractNormals(fbxsdk::FbxMesh* InMesh, std::vector<FVector>& OutNormals);
    void ExtractUVs(fbxsdk::FbxMesh* InMesh, std::vector<FVector2>& OutUVs);
    void ExtractIndices(fbxsdk::FbxMesh* InMesh, std::vector<uint32_t>& OutIndices);

    // 좌표계 변환 (Critical!)
    FVector ConvertToLeftHanded(const FVector& InVector) const;
    void FlipWindingOrder(std::vector<uint32_t>& InOutIndices) const;

    // 유틸리티
    std::string WStringToString(const std::wstring& InWString) const;
};
```

#### FbxImporter.cpp (기본 구조)

```cpp
#include "pch.h"
#include "FbxImporter.h"
#include <fbxsdk.h>

using namespace fbxsdk;

FFbxImporter::FFbxImporter()
{
    InitializeSdk();
}

FFbxImporter::~FFbxImporter()
{
    CleanupSdk();
}

void FFbxImporter::InitializeSdk()
{
    // FBX Manager 생성
    SdkManager = FbxManager::Create();
    if (!SdkManager)
    {
        LOG_ERROR("FFbxImporter: Failed to create FBX Manager!");
        return;
    }

    // IOSettings 생성
    FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
    SdkManager->SetIOSettings(ios);

    // Scene 생성
    Scene = FbxScene::Create(SdkManager, "ImportScene");
}

void FFbxImporter::CleanupSdk()
{
    if (SdkManager)
    {
        SdkManager->Destroy();
        SdkManager = nullptr;
        Scene = nullptr; // Manager가 Scene도 함께 삭제
    }
}

UStaticMesh* FFbxImporter::ImportFbxFile(const std::wstring& InFilePath)
{
    if (!SdkManager || !Scene)
    {
        LOG_ERROR("FFbxImporter: SDK not initialized!");
        return nullptr;
    }

    // 1. FBX 파일 로드
    std::string filePath = WStringToString(InFilePath);
    if (!LoadScene(filePath))
    {
        LOG_ERROR("FFbxImporter: Failed to load FBX file: %s", filePath.c_str());
        return nullptr;
    }

    // 2. StaticMesh 생성
    UStaticMesh* staticMesh = ObjectFactory::NewObject<UStaticMesh>();
    if (!staticMesh)
    {
        LOG_ERROR("FFbxImporter: Failed to create StaticMesh!");
        return nullptr;
    }

    // 3. 루트 노드부터 재귀적으로 처리
    FbxNode* rootNode = Scene->GetRootNode();
    if (rootNode)
    {
        ProcessNode(rootNode, staticMesh);
    }

    // 4. 메시 최종화 (GPU 버퍼 생성 등)
    staticMesh->Finalize();

    return staticMesh;
}

bool FFbxImporter::LoadScene(const std::string& InFilePath)
{
    // Importer 생성
    FbxImporter* importer = FbxImporter::Create(SdkManager, "");

    // 파일 초기화
    if (!importer->Initialize(InFilePath.c_str(), -1, SdkManager->GetIOSettings()))
    {
        LOG_ERROR("FBX Import Error: %s", importer->GetStatus().GetErrorString());
        importer->Destroy();
        return false;
    }

    // Scene으로 임포트
    if (!importer->Import(Scene))
    {
        LOG_ERROR("FBX Import Error: Failed to import scene");
        importer->Destroy();
        return false;
    }

    importer->Destroy();

    // 좌표계 변환 (Right-Handed → Left-Handed)
    if (Options.bConvertToLeftHanded)
    {
        FbxAxisSystem targetAxis(FbxAxisSystem::eDirectX); // DirectX = Left-Handed, Z-Up
        targetAxis.ConvertScene(Scene);
    }

    // 단위 변환 (필요시 cm → m)
    FbxSystemUnit targetUnit(100.0); // 1m = 100cm
    targetUnit.ConvertScene(Scene);

    return true;
}

void FFbxImporter::ProcessNode(FbxNode* InNode, UStaticMesh* OutMesh)
{
    if (!InNode) return;

    // 현재 노드의 Mesh 속성 처리
    FbxMesh* fbxMesh = InNode->GetMesh();
    if (fbxMesh)
    {
        ProcessMesh(fbxMesh, OutMesh);
    }

    // 자식 노드 재귀 처리
    for (int i = 0; i < InNode->GetChildCount(); ++i)
    {
        ProcessNode(InNode->GetChild(i), OutMesh);
    }
}

void FFbxImporter::ProcessMesh(FbxMesh* InFbxMesh, UStaticMesh* OutMesh)
{
    // TODO: 메시 데이터 추출 및 변환
    // 1. Vertices 추출
    // 2. Normals 추출
    // 3. UVs 추출
    // 4. Indices 추출
    // 5. 좌표계 변환 적용
    // 6. OutMesh에 데이터 추가
}

// 좌표계 변환: Right-Handed → Left-Handed
FVector FFbxImporter::ConvertToLeftHanded(const FVector& InVector) const
{
    // FBX는 보통 Right-Handed Y-Up
    // Mundi는 Left-Handed Z-Up
    // 변환: (X, Y, Z) → (X, Z, Y) and flip X or Z
    return FVector(-InVector.x, InVector.z, InVector.y);
}

void FFbxImporter::FlipWindingOrder(std::vector<uint32_t>& InOutIndices) const
{
    // 삼각형 인덱스 순서 반전 (CCW → CW)
    for (size_t i = 0; i < InOutIndices.size(); i += 3)
    {
        std::swap(InOutIndices[i + 1], InOutIndices[i + 2]);
    }
}

std::string FFbxImporter::WStringToString(const std::wstring& InWString) const
{
    // UTF-8 변환 (간단한 버전)
    std::string result;
    result.reserve(InWString.size());
    for (wchar_t ch : InWString)
    {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}
```

#### ResourceManager 통합

**ResourceManager.h에 추가:**
```cpp
class UResourceManager
{
    // ...
    UStaticMesh* LoadFbxMesh(const std::wstring& InPath);
};
```

**ResourceManager.cpp에 추가:**
```cpp
#include "FbxImporter.h"

UStaticMesh* UResourceManager::LoadFbxMesh(const std::wstring& InPath)
{
    // 캐시 확인
    if (auto cached = GetCachedResource<UStaticMesh>(InPath))
    {
        return cached;
    }

    // FBX 임포트
    FFbxImporter importer;
    UStaticMesh* mesh = importer.ImportFbxFile(InPath);

    if (mesh)
    {
        CacheResource(InPath, mesh);
    }

    return mesh;
}
```

---

## 트러블슈팅

### ❌ 빌드 에러: "Cannot open include file: 'fbxsdk.h'"

**원인:** Include 경로가 제대로 설정되지 않음

**해결책:**
1. `Mundi.vcxproj`에서 `<AdditionalIncludeDirectories>` 확인
2. `ThirdParty\FBX\include` 경로가 모든 Configuration에 추가되었는지 확인
3. `fbxsdk.h` 파일이 실제로 `ThirdParty\FBX\include\` 폴더에 존재하는지 확인

### ❌ 링크 에러: "unresolved external symbol" (FBX SDK 함수)

**원인:** 라이브러리 링크 설정 누락

**해결책:**
1. `<AdditionalLibraryDirectories>` 확인
2. `<AdditionalDependencies>`에 `libfbxsdk-md.lib`, `libxml2-md.lib`, `zlib-md.lib` 추가 확인
3. Debug 빌드는 Debug 라이브러리, Release 빌드는 Release 라이브러리 사용 확인

### ❌ 런타임 에러: "The code execution cannot proceed because libfbxsdk.dll was not found"

**원인:** DLL이 실행 파일과 같은 폴더에 없음

**해결책:**
1. `Binaries\Debug\` 폴더에 `libfbxsdk.dll` 존재 확인
2. `CustomPreBuildCopy` 타겟에 DLL 복사 코드 추가 확인
3. 빌드 로그에서 `[BuildEvent] Copying FBX SDK DLL...` 메시지 확인

### ❌ 링크 에러: "LNK2038: mismatch detected for 'RuntimeLibrary'"

**원인:** 프로젝트와 FBX SDK의 런타임 라이브러리 설정 불일치

**해결책:**
1. 프로젝트 속성 → C/C++ → Code Generation → Runtime Library 확인
2. `/MD` (Multi-threaded DLL)인 경우: `libfbxsdk-md.lib` 사용
3. `/MT` (Multi-threaded Static)인 경우: `libfbxsdk-mt.lib` 사용
4. 프로젝트 전체를 일관된 설정으로 변경

### ❌ 좌표계 변환 후 메시가 뒤집혀 보임

**원인:** Winding Order 또는 좌표 변환 오류

**해결책:**
1. `FlipWindingOrder()` 함수로 인덱스 순서 반전
2. `FbxAxisSystem::ConvertScene()` 사용 확인
3. Blender 등에서 Export 시 축 설정 확인 (Z-Up 권장)
4. UV V 좌표 반전 시도: `v = 1.0f - v`

### ❌ 텍스처 경로를 찾을 수 없음

**원인:** FBX에 저장된 절대 경로가 현재 환경과 다름

**해결책:**
1. FBX의 텍스처 경로를 상대 경로로 변환
2. `Data/Textures/` 폴더 기준으로 재검색
3. `FbxFileTexture::GetRelativeFileName()` 사용

---

## 참고 자료

### 공식 문서
- [FBX SDK Documentation](https://help.autodesk.com/view/FBX/2020/ENU/)
- [FBX SDK Online Help](https://help.autodesk.com/view/FBX/2020/ENU/?guid=FBX_Developer_Help_welcome_to_the_fbx_sdk_html)

### 샘플 코드
- FBX SDK 설치 경로: `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\samples\`
- GitHub: [Autodesk FBX Samples](https://github.com/autodesk-adn)

### 좌표계 변환
- [README.md](README.md) - Mundi 엔진의 좌표계 문서
- FBX SDK: `FbxAxisSystem::ConvertScene()`
- DirectX Math Coordinate System

### Mundi 엔진 참고 코드
- [ObjManager.cpp](Source/Editor/ObjManager.cpp) - OBJ 임포터 좌표계 변환 로직
- [StaticMesh.h](Source/Runtime/AssetManagement/StaticMesh.h) - 메시 데이터 구조
- [ResourceManager.cpp](Source/Runtime/AssetManagement/ResourceManager.cpp) - 리소스 로딩 패턴

---

## 체크리스트

### Phase 1: SDK 파일 준비
- [ ] ThirdParty/FBX 폴더 구조 생성
- [ ] include 파일 복사 완료
- [ ] lib/Debug 파일 복사 완료
- [ ] lib/Release 파일 복사 완료
- [ ] bin/Debug DLL 복사 완료
- [ ] bin/Release DLL 복사 완료

### Phase 2: 프로젝트 설정
- [ ] AdditionalIncludeDirectories 수정 (4개 Configuration)
- [ ] AdditionalLibraryDirectories 수정 (4개 Configuration)
- [ ] AdditionalDependencies 수정 (4개 Configuration)
- [ ] 런타임 라이브러리 설정 확인 (/MD vs /MT)

### Phase 3: DLL 자동화
- [ ] Debug Configuration 스크립트 수정
- [ ] Debug_StandAlone Configuration 스크립트 수정
- [ ] Release Configuration 스크립트 수정
- [ ] Release_StandAlone Configuration 스크립트 수정

### Phase 4: 빌드 테스트
- [ ] FbxImporterTest.cpp 작성
- [ ] vcxproj에 테스트 파일 추가
- [ ] Clean Build 성공
- [ ] 빌드 출력에서 DLL 복사 메시지 확인
- [ ] Binaries/Debug/libfbxsdk.dll 존재 확인
- [ ] 실행 테스트 성공
- [ ] 콘솔 출력 확인 ("FBX SDK Version: 2020.3.7")

### Phase 5: FFbxImporter 구현 (별도 작업)
- [ ] FbxImporter.h/cpp 파일 생성
- [ ] 기본 구조 구현
- [ ] 좌표계 변환 구현
- [ ] ResourceManager 통합
- [ ] 실제 FBX 파일 테스트
- [ ] Editor 메뉴에 "Import FBX" 추가

---

**문서 버전:** 1.0
**최종 업데이트:** 2025-11-07
**작성자:** Claude Code
