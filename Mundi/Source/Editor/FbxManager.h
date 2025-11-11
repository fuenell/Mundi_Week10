#pragma once
#include "UEContainer.h"
#include "String.h"
#include "FbxImportOptions.h"

// 전방 선언
class USkeletalMesh;
class UStaticMesh;
struct FStaticMesh;
struct FSkeletalMesh;

/**
 * FBX 메시 로딩 및 캐싱 관리 클래스 (Static Mesh와 Skeletal Mesh 모두 지원)
 * OBJ 파일을 위한 FObjManager와 유사한 구조
 *
 * 아키텍처:
 * - Static Mesh: FStaticMesh* 캐싱 (데이터 구조), UStaticMesh가 참조
 * - Skeletal Mesh: FSkeletalMesh* 캐싱 (데이터 구조), USkeletalMesh가 참조
 */
class FFbxManager
{
private:
	// ═══════════════════════════════════════════════════════════
	// Static Mesh 캐시 (FStaticMesh* - FObjManager 패턴 동일)
	// ═══════════════════════════════════════════════════════════
	static TMap<FString, FStaticMesh*> FbxStaticMeshCache;

	// ═══════════════════════════════════════════════════════════
	// Skeletal Mesh 캐시 (FSkeletalMesh* - FObjManager 패턴 동일)
	// ═══════════════════════════════════════════════════════════
	static TMap<FString, FSkeletalMesh*> FbxSkeletalMeshCache;

public:
	/**
	 * Data/Model/Fbx/ 디렉토리의 모든 FBX 파일 사전 로드
	 * 엔진 초기화 시 호출됨
	 * 자동으로 타입을 감지하여 적절한 캐시에 로드
	 */
	static void Preload();

	/**
	 * 캐시된 모든 FBX 데이터 정리 (Static 및 Skeletal 모두)
	 * 엔진 종료 시 호출됨
	 */
	static void Clear();

	/**
	 * 캐시된 모든 Static Mesh FBX 경로 반환
	 * @return Static Mesh FBX 파일 경로 목록
	 */
	static TArray<FString> GetAllStaticMeshPaths();

	/**
	 * 캐시된 모든 Skeletal Mesh FBX 경로 반환
	 * @return Skeletal Mesh FBX 파일 경로 목록
	 */
	static TArray<FString> GetAllSkeletalMeshPaths();

	// ═══════════════════════════════════════════════════════════
	// Static Mesh 로딩 (FObjManager 패턴 준수)
	// ═══════════════════════════════════════════════════════════

	/**
	 * FBX 파일에서 Static Mesh를 캐싱과 함께 로드
	 * @param PathFileName - .fbx 파일 경로
	 * @return 로드된 Static Mesh 데이터 또는 실패 시 nullptr
	 *
	 * 패턴: FObjManager::LoadObjStaticMeshAsset()와 동일
	 */
	static FStaticMesh* LoadFbxStaticMeshAsset(const FString& PathFileName);

	// ═══════════════════════════════════════════════════════════
	// Skeletal Mesh 로딩 (FObjManager 패턴 준수)
	// ═══════════════════════════════════════════════════════════

	/**
	 * FBX 파일에서 Skeletal Mesh를 캐싱과 함께 로드
	 * @param PathFileName - .fbx 파일 경로
	 * @return 로드된 Skeletal Mesh 데이터 또는 실패 시 nullptr
	 *
	 * 패턴: FObjManager::LoadObjStaticMeshAsset()와 동일
	 */
	static FSkeletalMesh* LoadFbxSkeletalMeshAsset(const FString& PathFileName);

private:
	/**
	 * FBX 파일의 캐시 파일 경로 생성
	 * @param FbxPath - 소스 FBX 경로 (예: "Data/Model/Character.fbx")
	 * @return 캐시 경로 (예: "Data/Model/Character.fbx.bin")
	 */
	static FString GetFbxCachePath(const FString& FbxPath);

	/**
	 * 캐시 재생성 필요 여부 확인
	 * @param FbxPath - 소스 FBX 경로
	 * @param CachePath - 바이너리 캐시 경로
	 * @return 캐시가 없거나 오래된 경우 true
	 */
	static bool ShouldRegenerateCache(const FString& FbxPath, const FString& CachePath);

	// ═══════════════════════════════════════════════════════════
	// Static Mesh 캐시 I/O
	// ═══════════════════════════════════════════════════════════

	/**
	 * 바이너리 캐시에서 Static Mesh 로드
	 * @param CachePath - .fbx.bin 파일 경로
	 * @param OutMesh - 출력 Static Mesh 데이터
	 * @return 성공적으로 로드된 경우 true
	 */
	static bool LoadStaticMeshFromCache(const FString& CachePath, FStaticMesh* OutMesh);

	/**
	 * Static Mesh를 바이너리 캐시로 저장
	 * @param CachePath - .fbx.bin 파일 경로
	 * @param Mesh - 저장할 Static Mesh 데이터
	 * @return 성공적으로 저장된 경우 true
	 */
	static bool SaveStaticMeshToCache(const FString& CachePath, const FStaticMesh* Mesh);

	// ═══════════════════════════════════════════════════════════
	// Skeletal Mesh 캐시 I/O
	// ═══════════════════════════════════════════════════════════

	/**
	 * 바이너리 캐시에서 Skeletal Mesh 로드
	 * @param CachePath - .fbx.bin 파일 경로
	 * @param OutMesh - 출력 Skeletal Mesh 데이터
	 * @return 성공적으로 로드된 경우 true
	 */
	static bool LoadSkeletalMeshFromCache(const FString& CachePath, FSkeletalMesh* OutMesh);

	/**
	 * Skeletal Mesh를 바이너리 캐시로 저장
	 * @param CachePath - .fbx.bin 파일 경로
	 * @param Mesh - 저장할 Skeletal Mesh 데이터
	 * @return 성공적으로 저장된 경우 true
	 */
	static bool SaveSkeletalMeshToCache(const FString& CachePath, const FSkeletalMesh* Mesh);

	// ═══════════════════════════════════════════════════════════
	// DDS 텍스처 변환 (FBX Import 직후)
	// ═══════════════════════════════════════════════════════════

	/**
	 * Skeletal Mesh에서 추출된 모든 텍스처를 DDS로 변환
	 * ExtractMaterialsFromScene() 직후 호출하여 첫 Import 시에도 DDS 캐시 생성
	 * @param SkeletalMesh - Material 정보가 포함된 SkeletalMesh
	 * @param FbxPath - FBX 파일 경로 (로깅용)
	 */
	static void ConvertExtractedTexturesToDDS(USkeletalMesh* SkeletalMesh, const FString& FbxPath);

	/**
	 * 단일 텍스처를 DDS로 강제 변환
	 * @param TexturePath - 텍스처 파일 경로 (예: Data/Model/Fbx/Character.fbm/texture.png)
	 */
	static void ForceDDSConversionForTexture(const FString& TexturePath);

	/**
	 * Static Mesh의 GroupInfos에서 Material을 추출하여 DDS로 변환
	 * ExtractMaterialsFromScene() 직후 호출하여 첫 Import 시에도 DDS 캐시 생성
	 * @param GroupInfos - Static Mesh의 Material 그룹 정보
	 * @param FbxPath - FBX 파일 경로 (로깅용)
	 */
	static void ConvertExtractedTexturesForStaticMesh(
		const TArray<FGroupInfo>& GroupInfos,
		const FString& FbxPath);
};
