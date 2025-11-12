#include "pch.h"
#include "FbxManager.h"
#include "StaticMesh.h"
#include "SkeletalMesh.h"
#include "FbxImporter.h"
#include "FbxImportOptions.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "ObjectFactory.h"
#include "GlobalConsole.h"
#include "PathUtils.h"
#include "TextureConverter.h"
#include "Material.h"
#include "ResourceManager.h"
#include <filesystem>

// 정적 멤버 초기화
TMap<FString, FStaticMesh*> FFbxManager::FbxStaticMeshCache;
TMap<FString, FSkeletalMesh*> FFbxManager::FbxSkeletalMeshCache;

FString FFbxManager::GetFbxCachePath(const FString& FbxPath)
{
	// OBJ 시스템과 동일한 패턴 사용
	// "Data/Model/Fbx/Character.fbx" → "DerivedDataCache/Model/Fbx/Character.fbx.bin"
	FString CachePath = ConvertDataPathToCachePath(FbxPath);
	return CachePath + ".bin";
}

bool FFbxManager::ShouldRegenerateCache(const FString& FbxPath, const FString& CachePath)
{
	namespace fs = std::filesystem;

	// 1. 캐시 파일 존재 여부 확인
	if (!fs::exists(CachePath))
	{
		UE_LOG("FBX cache not found: %s", CachePath.c_str());
		return true;
	}

	// 2. 소스 FBX 존재 여부 확인
	if (!fs::exists(FbxPath))
	{
		UE_LOG("[error] Source FBX not found: %s", FbxPath.c_str());
		return false;  // 소스가 없으면 재생성 불가
	}

	// 3. 타임스탬프 비교
	auto FbxTime = fs::last_write_time(FbxPath);
	auto CacheTime = fs::last_write_time(CachePath);

	if (FbxTime > CacheTime)
	{
		UE_LOG("FBX source modified, cache is stale: %s", FbxPath.c_str());
		return true;
	}

	// 캐시 유효
	return false;
}

void FFbxManager::Clear()
{
	// Static Mesh 캐시 정리
	// 참고: FStaticMesh 객체는 FFbxManager가 소유 (FObjManager와 동일)
	for (auto& Pair : FbxStaticMeshCache)
	{
		delete Pair.second;  // FStaticMesh* 삭제
	}
	FbxStaticMeshCache.clear();

	// Skeletal Mesh 캐시 정리
	// 참고: FSkeletalMesh 객체는 FFbxManager가 소유 (FObjManager와 동일)
	for (auto& Pair : FbxSkeletalMeshCache)
	{
		delete Pair.second;  // FSkeletalMesh* 삭제
	}
	FbxSkeletalMeshCache.clear();

	UE_LOG("FFbxManager: Cleared caches (Static: %d, Skeletal: %d entries)",
		static_cast<int32>(FbxStaticMeshCache.size()), static_cast<int32>(FbxSkeletalMeshCache.size()));
}

TArray<FString> FFbxManager::GetAllStaticMeshPaths()
{
	TArray<FString> Paths;
	Paths.reserve(FbxStaticMeshCache.size());

	for (const auto& Pair : FbxStaticMeshCache)
	{
		Paths.push_back(Pair.first);
	}

	return Paths;
}

TArray<FString> FFbxManager::GetAllSkeletalMeshPaths()
{
	TArray<FString> Paths;
	Paths.reserve(FbxSkeletalMeshCache.size());

	for (const auto& Pair : FbxSkeletalMeshCache)
	{
		Paths.push_back(Pair.first);
	}

	return Paths;
}

void FFbxManager::Preload()
{
	namespace fs = std::filesystem;

	// Data/Model/Fbx/ 디렉토리에서 모든 .fbx 파일 검색
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

			// FBX 타입 감지 (Static vs Skeletal)
			FFbxImporter Importer;
			EFbxImportType FbxType = Importer.DetectFbxType(FbxPath);

			if (FbxType == EFbxImportType::StaticMesh)
			{
				// Static Mesh로 로드
				FStaticMesh* Mesh = LoadFbxStaticMeshAsset(FbxPath);
				if (Mesh)
				{
					StaticMeshCount++;
				}
			}
			else if (FbxType == EFbxImportType::SkeletalMesh)
			{
				// Skeletal Mesh로 로드
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

FStaticMesh* FFbxManager::LoadFbxStaticMeshAsset(const FString& PathFileName)
{
	// 이미 캐시에 있는지 확인
	auto It = FbxStaticMeshCache.find(PathFileName);
	if (It != FbxStaticMeshCache.end())
	{
		UE_LOG("FFbxManager: Static Mesh FBX cache hit: %s", PathFileName.c_str());
		return It->second;
	}

	// 캐시 경로 결정
	FString CachePath = GetFbxCachePath(PathFileName);

	// 캐시 디렉토리가 없으면 생성
	namespace fs = std::filesystem;
	fs::path CacheFileDirPath(CachePath);
	if (CacheFileDirPath.has_parent_path())
	{
		fs::create_directories(CacheFileDirPath.parent_path());
	}

	FStaticMesh* Mesh = new FStaticMesh();

	// 캐시에서 로드 시도
	bool bCacheValid = !ShouldRegenerateCache(PathFileName, CachePath);
	if (bCacheValid && LoadStaticMeshFromCache(CachePath, Mesh))
	{
		UE_LOG("FFbxManager: Loaded Static Mesh FBX from cache: %s", PathFileName.c_str());

		// ═══════════════════════════════════════════════════════════
		// CRITICAL FIX: 캐시 로드 후 Material 재등록
		// ═══════════════════════════════════════════════════════════
		// 캐시에서 Mesh 지오메트리는 로드했지만, Material은 ResourceManager에 등록되지 않음
		// FBX Scene을 다시 열어서 Material만 추출 및 등록 (Mesh 파싱은 스킵)
		FFbxImporter Importer;
		if (Importer.LoadScene(PathFileName))
		{
			USkeletalMesh* TempUSkeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
			if (Importer.ExtractMaterialsFromScene(TempUSkeletalMesh))
			{
				const TArray<FString>& ExtractedMaterialNames = TempUSkeletalMesh->GetMaterialNames();
				UE_LOG("FFbxManager: Re-registered %d materials from cached Static Mesh FBX",
					ExtractedMaterialNames.size());

				// Material 이름을 GroupInfos에 복사 (첫 Baking 때와 동일)
				for (size_t i = 0; i < Mesh->GroupInfos.size() && i < ExtractedMaterialNames.size(); ++i)
				{
					Mesh->GroupInfos[i].InitialMaterialName = ExtractedMaterialNames[i];
				}
			}
			ObjectFactory::DeleteObject(TempUSkeletalMesh);
		}
		else
		{
			UE_LOG("[warning] FFbxManager: Failed to re-open FBX Scene for Material registration: %s",
				PathFileName.c_str());
		}

		FbxStaticMeshCache[PathFileName] = Mesh;
		return Mesh;
	}

	// 캐시 무효 또는 없음 - FFbxImporter를 사용하여 FBX 파싱
	UE_LOG("FFbxManager: Parsing Static Mesh FBX (cache miss): %s", PathFileName.c_str());

	FFbxImporter Importer;
	FFbxImportOptions Options;  // 기본 옵션 사용

	bool bSuccess = Importer.ImportStaticMesh(PathFileName, Options, *Mesh);

	if (!bSuccess)
	{
		UE_LOG("[error] FFbxManager: Failed to import Static Mesh FBX: %s", PathFileName.c_str());
		delete Mesh;
		return nullptr;
	}

	// ═══════════════════════════════════════════════════════════
	// Material 추출 (FBX Scene이 아직 열려있음)
	// ═══════════════════════════════════════════════════════════
	// ExtractMaterialsFromScene()은 USkeletalMesh*만 받으므로 임시 SkeletalMesh 사용
	// (Material extraction은 Scene에서 직접 추출하므로 Mesh 타입 무관)
	USkeletalMesh* TempUSkeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
	if (Importer.ExtractMaterialsFromScene(TempUSkeletalMesh))
	{
		const TArray<FString>& ExtractedMaterialNames = TempUSkeletalMesh->GetMaterialNames();
		UE_LOG("FFbxManager: Extracted %d materials from Static Mesh FBX", ExtractedMaterialNames.size());

		// CRITICAL FIX: 추출된 Material 이름을 FStaticMesh의 GroupInfos에 복사
		// ExtractMaterialsFromScene()가 실제 FBX Material 노드에서 생성한 Material 이름 사용
		// 이를 통해 SetMaterialByName()이 ResourceManager에서 Material을 찾을 수 있음
		for (size_t i = 0; i < Mesh->GroupInfos.size() && i < ExtractedMaterialNames.size(); ++i)
		{
			Mesh->GroupInfos[i].InitialMaterialName = ExtractedMaterialNames[i];
			UE_LOG("FFbxManager: Updated GroupInfo[%zu] Material: %s", i, ExtractedMaterialNames[i].c_str());
		}

		// 추출된 텍스처들을 즉시 DDS로 변환
		// (첫 Import 시에도 DDS 캐시가 생성되도록 보장)
		ConvertExtractedTexturesForStaticMesh(Mesh->GroupInfos, PathFileName);
	}
	else
	{
		UE_LOG("[warning] FFbxManager: Failed to extract materials from Static Mesh FBX");
	}
	// 임시 SkeletalMesh 삭제
	ObjectFactory::DeleteObject(TempUSkeletalMesh);

	// 캐시에 저장
	SaveStaticMeshToCache(CachePath, Mesh);

	// 메모리 캐시에 추가하고 반환
	FbxStaticMeshCache[PathFileName] = Mesh;
	return Mesh;
}

FSkeletalMesh* FFbxManager::LoadFbxSkeletalMeshAsset(const FString& PathFileName)
{
	// 이미 캐시에 있는지 확인
	auto It = FbxSkeletalMeshCache.find(PathFileName);
	if (It != FbxSkeletalMeshCache.end())
	{
		UE_LOG("FFbxManager: Skeletal Mesh FBX cache hit: %s", PathFileName.c_str());
		return It->second;
	}

	// 캐시 경로 결정
	FString CachePath = GetFbxCachePath(PathFileName);

	// 캐시 디렉토리가 없으면 생성
	namespace fs = std::filesystem;
	fs::path CacheFileDirPath(CachePath);
	if (CacheFileDirPath.has_parent_path())
	{
		fs::create_directories(CacheFileDirPath.parent_path());
	}

	FSkeletalMesh* Mesh = new FSkeletalMesh();

	// 캐시에서 로드 시도
	bool bCacheValid = !ShouldRegenerateCache(PathFileName, CachePath);
	if (bCacheValid && LoadSkeletalMeshFromCache(CachePath, Mesh))
	{
		UE_LOG("FFbxManager: Loaded Skeletal Mesh FBX from cache: %s", PathFileName.c_str());

		// ═══════════════════════════════════════════════════════════
		// CRITICAL FIX: 캐시 로드 후 Material 재등록
		// ═══════════════════════════════════════════════════════════
		// 캐시에서 Mesh 지오메트리는 로드했지만, Material은 ResourceManager에 등록되지 않음
		// FBX Scene을 다시 열어서 Material만 추출 및 등록 (Mesh 파싱은 스킵)
		FFbxImporter Importer;
		if (Importer.LoadScene(PathFileName))
		{
			USkeletalMesh* TempUSkeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
			if (Importer.ExtractMaterialsFromScene(TempUSkeletalMesh))
			{
				// Material 이름을 FSkeletalMesh에 복사 (첫 Baking 때와 동일)
				Mesh->MaterialNames = TempUSkeletalMesh->GetMaterialNames();
				UE_LOG("FFbxManager: Re-registered %d materials from cached Skeletal Mesh FBX",
					Mesh->MaterialNames.size());
			}
			ObjectFactory::DeleteObject(TempUSkeletalMesh);
		}
		else
		{
			UE_LOG("[warning] FFbxManager: Failed to re-open FBX Scene for Material registration: %s",
				PathFileName.c_str());
		}

		FbxSkeletalMeshCache[PathFileName] = Mesh;
		return Mesh;
	}

	// 캐시 무효 또는 없음 - FFbxImporter를 사용하여 FBX 파싱
	UE_LOG("FFbxManager: Parsing Skeletal Mesh FBX (cache miss): %s", PathFileName.c_str());

	FFbxImporter Importer;
	FFbxImportOptions Options;  // 기본 옵션 사용

	bool bSuccess = Importer.ImportSkeletalMesh(PathFileName, Options, *Mesh);

	if (!bSuccess || !Mesh->IsValid())
	{
		UE_LOG("[error] FFbxManager: Failed to import Skeletal Mesh FBX: %s", PathFileName.c_str());
		delete Mesh;
		return nullptr;
	}

	// ═══════════════════════════════════════════════════════════
	// Material 추출 (FBX Scene이 아직 열려있음)
	// ═══════════════════════════════════════════════════════════
	// 임시 USkeletalMesh를 만들어서 Material 추출
	USkeletalMesh* TempUSkeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
	if (Importer.ExtractMaterialsFromScene(TempUSkeletalMesh))
	{
		// Material 이름을 FSkeletalMesh에 복사
		Mesh->MaterialNames = TempUSkeletalMesh->GetMaterialNames();
		UE_LOG("FFbxManager: Extracted %d materials from FBX", Mesh->MaterialNames.size());

		// 추출된 텍스처들을 즉시 DDS로 변환
		// (첫 Import 시에도 DDS 캐시가 생성되도록 보장)
		ConvertExtractedTexturesToDDS(TempUSkeletalMesh, PathFileName);
	}
	else
	{
		UE_LOG("[warning] FFbxManager: Failed to extract materials from FBX, using default material");
	}
	// 임시 USkeletalMesh 삭제
	ObjectFactory::DeleteObject(TempUSkeletalMesh);

	// 캐시에 저장
	SaveSkeletalMeshToCache(CachePath, Mesh);

	// 메모리 캐시에 추가하고 반환
	FbxSkeletalMeshCache[PathFileName] = Mesh;
	return Mesh;
}

bool FFbxManager::LoadStaticMeshFromCache(const FString& CachePath, FStaticMesh* OutMesh)
{
	FWindowsBinReader Reader(CachePath);
	if (!Reader.IsOpen())
	{
		UE_LOG("[error] FFbxManager: Failed to open cache file for reading: %s", CachePath.c_str());
		return false;
	}

	// 매직 넘버 읽기 및 검증
	uint32 MagicNumber, Version;
	uint8 TypeFlag;
	Reader << MagicNumber;
	Reader << Version;
	Reader << TypeFlag;

	if (MagicNumber != 0x46425843)
	{
		UE_LOG("[error] FFbxManager: Invalid FBX cache file (bad magic number): %s", CachePath.c_str());
		return false;
	}

	if (Version != 1)
	{
		UE_LOG("[error] FFbxManager: Unsupported FBX cache version %d: %s", Version, CachePath.c_str());
		return false;
	}

	if (TypeFlag != 0)
	{
		UE_LOG("[error] FFbxManager: Invalid cache type flag (expected StaticMesh=0, got %d): %s", TypeFlag, CachePath.c_str());
		return false;
	}

	// FStaticMesh의 operator<< 사용하여 나머지 데이터 읽기
	Reader << *OutMesh;
	Reader.Close();

	UE_LOG("FFbxManager: Loaded Static Mesh from cache: %s (%d vertices, %d indices)",
		CachePath.c_str(), static_cast<uint32>(OutMesh->Vertices.size()), static_cast<uint32>(OutMesh->Indices.size()));

	return true;
}

bool FFbxManager::SaveStaticMeshToCache(const FString& CachePath, const FStaticMesh* Mesh)
{
	FWindowsBinWriter Writer(CachePath);
	if (!Writer.IsOpen())
	{
		UE_LOG("[error] FFbxManager: Failed to open cache file for writing: %s", CachePath.c_str());
		return false;
	}

	// 매직 넘버와 버전 쓰기
	uint32 MagicNumber = 0x46425843;  // "FBXC" (hex)
	uint32 Version = 1;
	uint8 TypeFlag = 0;  // 0 = StaticMesh, 1 = SkeletalMesh
	Writer << MagicNumber;
	Writer << Version;
	Writer << TypeFlag;

	// FStaticMesh의 operator<< 사용하여 나머지 데이터 쓰기
	Writer << const_cast<FStaticMesh&>(*Mesh);
	Writer.Close();

	UE_LOG("FFbxManager: Saved Static Mesh to cache: %s (%d vertices, %d indices)",
		CachePath.c_str(), static_cast<uint32>(Mesh->Vertices.size()), static_cast<uint32>(Mesh->Indices.size()));

	return true;
}

bool FFbxManager::LoadSkeletalMeshFromCache(const FString& CachePath, FSkeletalMesh* OutMesh)
{
	FWindowsBinReader Reader(CachePath);
	if (!Reader.IsOpen())
	{
		UE_LOG("[error] FFbxManager: Failed to open cache file for reading: %s", CachePath.c_str());
		return false;
	}

	// FSkeletalMesh의 operator>> 사용하여 역직렬화
	Reader >> *OutMesh;
	Reader.Close();

	UE_LOG("FFbxManager: Loaded Skeletal Mesh from cache: %s (%zu vertices, %zu indices)",
		CachePath.c_str(), OutMesh->Vertices.size(), OutMesh->Indices.size());

	return true;
}

bool FFbxManager::SaveSkeletalMeshToCache(const FString& CachePath, const FSkeletalMesh* Mesh)
{
	FWindowsBinWriter Writer(CachePath);
	if (!Writer.IsOpen())
	{
		UE_LOG("[error] FFbxManager: Failed to open cache file for writing: %s", CachePath.c_str());
		return false;
	}

	// FSkeletalMesh의 operator<< 사용하여 직렬화
	Writer << *Mesh;
	Writer.Close();

	UE_LOG("FFbxManager: Saved Skeletal Mesh to cache: %s (%zu vertices, %zu indices)",
		CachePath.c_str(), Mesh->Vertices.size(), Mesh->Indices.size());

	return true;
}

// ═══════════════════════════════════════════════════════════
// DDS 텍스처 변환 구현
// ═══════════════════════════════════════════════════════════

void FFbxManager::ConvertExtractedTexturesToDDS(USkeletalMesh* SkeletalMesh, const FString& FbxPath)
{
	if (!SkeletalMesh)
	{
		UE_LOG("[warning] FFbxManager::ConvertExtractedTexturesToDDS: Invalid SkeletalMesh");
		return;
	}

	auto& RM = UResourceManager::GetInstance();
	const TArray<FString>& MaterialNames = SkeletalMesh->GetMaterialNames();

	if (MaterialNames.empty())
	{
		UE_LOG("[FBX] No materials to convert for: %s", FbxPath.c_str());
		return;
	}

	UE_LOG("[FBX] Converting extracted textures to DDS for: %s", FbxPath.c_str());

	for (const FString& MaterialName : MaterialNames)
	{
		UMaterial* Material = RM.Get<UMaterial>(MaterialName);
		if (!Material)
		{
			UE_LOG("[warning] Material not found in ResourceManager: %s", MaterialName.c_str());
			continue;
		}

		const FMaterialInfo& MatInfo = Material->GetMaterialInfo();

		// Diffuse 텍스처 변환
		if (!MatInfo.DiffuseTextureFileName.empty())
		{
			ForceDDSConversionForTexture(MatInfo.DiffuseTextureFileName);
		}

		// Normal 텍스처 변환
		if (!MatInfo.NormalTextureFileName.empty())
		{
			ForceDDSConversionForTexture(MatInfo.NormalTextureFileName);
		}
	}

	UE_LOG("[FBX] DDS conversion completed for %zu materials", MaterialNames.size());
}

void FFbxManager::ForceDDSConversionForTexture(const FString& TexturePath)
{
	namespace fs = std::filesystem;

	// DDS 파일은 스킵
	fs::path TexPath(UTF8ToWide(TexturePath));
	if (TexPath.extension() == L".dds")
	{
		return;
	}

	// 소스 파일 존재 확인
	fs::path AbsolutePath = TexPath;
	if (TexPath.is_relative())
	{
		AbsolutePath = fs::current_path() / TexPath;
		AbsolutePath = AbsolutePath.lexically_normal();
	}

	if (!fs::exists(AbsolutePath))
	{
		UE_LOG("[warning] Texture not found for DDS conversion: %s", TexturePath.c_str());
		return;
	}

	// DDS 캐시 경로 생성
	FString DDSCachePath = FTextureConverter::GetDDSCachePath(TexturePath);

	// DDS 변환 필요 여부 확인
	if (FTextureConverter::ShouldRegenerateDDS(TexturePath, DDSCachePath))
	{
		// 권장 포맷으로 변환 (알파 포함, sRGB)
		DXGI_FORMAT TargetFormat = FTextureConverter::GetRecommendedFormat(true, true);

		if (FTextureConverter::ConvertToDDS(TexturePath, DDSCachePath, TargetFormat))
		{
			UE_LOG("[FBX] Successfully converted texture to DDS: %s", DDSCachePath.c_str());
		}
		else
		{
			UE_LOG("[warning] Failed to convert texture to DDS: %s", TexturePath.c_str());
		}
	}
	else
	{
		UE_LOG("[FBX] DDS cache already valid: %s", DDSCachePath.c_str());
	}
}

void FFbxManager::ConvertExtractedTexturesForStaticMesh(
	const TArray<FGroupInfo>& GroupInfos,
	const FString& FbxPath)
{
	if (GroupInfos.empty())
	{
		return;
	}

	auto& RM = UResourceManager::GetInstance();

	UE_LOG("[FBX] Converting extracted textures to DDS for Static Mesh: %s", FbxPath.c_str());

	// GroupInfos의 각 Material에 대해 DDS 변환
	for (const FGroupInfo& Group : GroupInfos)
	{
		if (Group.InitialMaterialName.empty())
			continue;

		// ResourceManager에서 Material 조회
		UMaterial* Material = RM.Get<UMaterial>(Group.InitialMaterialName);
		if (!Material)
		{
			UE_LOG("[warning] Material not found in ResourceManager: %s", Group.InitialMaterialName.c_str());
			continue;
		}

		const FMaterialInfo& MatInfo = Material->GetMaterialInfo();

		// Diffuse 텍스처 변환
		if (!MatInfo.DiffuseTextureFileName.empty())
		{
			ForceDDSConversionForTexture(MatInfo.DiffuseTextureFileName);
		}

		// Normal 텍스처 변환
		if (!MatInfo.NormalTextureFileName.empty())
		{
			ForceDDSConversionForTexture(MatInfo.NormalTextureFileName);
		}
	}

	UE_LOG("[FBX] DDS conversion completed for Static Mesh (%zu materials)", GroupInfos.size());
}
