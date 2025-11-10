#pragma once

#include "pch.h"
#include "FbxImportOptions.h"
#include <fbxsdk.h>

// Forward declarations
class USkeleton;
class UStaticMesh;
struct FSkeletalMesh;

/**
 * Autodesk FBX SDK를 사용한 FBX 파일 임포터
 * Unreal Engine의 FFbxImporter 패턴을 따름
 *
 * 현재 지원:
 * - SkeletalMesh Import (Skeleton, Skin Weights, Bind Pose)
 *
 * 향후 지원 예정:
 * - StaticMesh Import
 * - Animation Import
 *
 * 좌표계:
 * - Mundi 엔진: Z-Up, X-Forward, Y-Right, Left-Handed
 * - FBX는 다양한 좌표계 지원 → 자동 변환
 */
class FFbxImporter
{
public:
	/**
	 * 생성자 - FBX SDK Manager 초기화
	 */
	FFbxImporter();

	/**
	 * 소멸자 - FBX SDK 리소스 정리
	 */
	~FFbxImporter();

	// === Public Import Interface ===

	/**
	 * FBX 파일에서 SkeletalMesh 데이터를 Import
	 * @param FilePath - FBX 파일 경로
	 * @param Options - Import 옵션
	 * @param OutMeshData - Import된 Mesh 데이터 (출력)
	 * @return 성공 여부
	 */
	bool ImportSkeletalMesh(const FString& FilePath, const FFbxImportOptions& Options, FSkeletalMesh& OutMeshData);

	/**
	 * FBX 파일에서 StaticMesh를 Import (미구현)
	 * @param FilePath - FBX 파일 경로
	 * @param Options - Import 옵션
	 * @return Import된 StaticMesh (실패 시 nullptr)
	 *
	 * TODO: Phase 4에서 구현 예정
	 */
	UStaticMesh* ImportStaticMesh(const FString& FilePath, const FFbxImportOptions& Options);

	/**
	 * 마지막 에러 메시지 가져오기
	 * @return 에러 메시지 문자열
	 */
	const FString& GetLastError() const { return LastErrorMessage; }

private:
	// === FBX SDK 관리 ===

	/**
	 * FBX Scene을 파일에서 로드
	 * @param FilePath - FBX 파일 경로
	 * @return 성공 여부
	 */
	bool LoadScene(const FString& FilePath);

	/**
	 * FBX Scene을 Mundi 좌표계로 변환
	 * Mundi: Z-Up, X-Forward, Y-Right, Left-Handed
	 */
	void ConvertScene();

	/**
	 * FBX Scene의 단위를 변환
	 * @param ScaleFactor - 스케일 배율
	 */
	void ConvertSceneUnit(float ScaleFactor);

	/**
	 * 현재 Scene 정리
	 */
	void ReleaseScene();

	// === Node Hierarchy 탐색 ===

	/**
	 * FbxNode 계층 구조를 재귀적으로 탐색
	 * @param Node - 탐색할 노드
	 * @param ProcessFunc - 각 노드에 대해 실행할 함수
	 */
	void TraverseNode(FbxNode* Node, std::function<void(FbxNode*)> ProcessFunc);

	/**
	 * Scene에서 첫 번째 Mesh Node 찾기
	 * @param Node - 시작 노드 (nullptr인 경우 RootNode부터)
	 * @return 첫 번째 Mesh를 가진 노드 (없으면 nullptr)
	 */
	FbxNode* FindFirstMeshNode(FbxNode* Node = nullptr);

	// === SkeletalMesh Import 내부 로직 ===

	/**
	 * Skeleton 추출 (Bone Hierarchy)
	 * @param RootNode - FBX Scene의 Root Node
	 * @return 생성된 Skeleton 객체
	 */
	USkeleton* ExtractSkeleton(FbxNode* RootNode);

	/**
	 * Mesh 데이터 추출 (Vertex, Index, Normals, UVs, Skin Weights)
	 * @param MeshNode - Mesh를 가진 FBX Node
	 * @param OutMeshData - 데이터를 저장할 FSkeletalMesh 구조체
	 * @return 성공 여부
	 */
	bool ExtractMeshData(FbxNode* MeshNode, FSkeletalMesh& OutMeshData);

	/**
	 * Skin Weights 추출 (Bone Influences)
	 * @param FbxMesh - FBX Mesh 객체
	 * @param OutMeshData - 데이터를 저장할 FSkeletalMesh 구조체
	 * @return 성공 여부
	 */
	bool ExtractSkinWeights(FbxMesh* Mesh, FSkeletalMesh& OutMeshData);

	/**
	 * Bind Pose 추출 (초기 Bone Transform)
	 * @param Scene - FBX Scene
	 * @param OutSkeleton - 데이터를 저장할 Skeleton
	 * @return 성공 여부
	 */
	bool ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton);

	/**
	 * Material과 Texture 정보 추출
	 * @param MeshNode - Mesh를 가진 FBX Node
	 * @param OutSkeletalMesh - Material 정보를 저장할 SkeletalMesh
	 * @return 성공 여부
	 */
	bool ExtractMaterials(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh);

	// === Helper 함수 ===

	/**
	 * FbxAMatrix를 Mundi FTransform으로 변환
	 * @param fbxMatrix - FBX 행렬
	 * @return Mundi Transform 구조체
	 */
	FTransform ConvertFbxTransform(const FbxAMatrix& fbxMatrix);

	/**
	 * FbxMatrix를 Mundi FMatrix로 변환
	 * @param fbxMatrix - FBX 행렬
	 * @return Mundi Matrix
	 */
	FMatrix ConvertFbxMatrix(const FbxMatrix& fbxMatrix);

	// === 에러 처리 ===

	/**
	 * 에러 메시지 설정
	 * @param Message - 에러 메시지
	 */
	void SetError(const FString& Message);

private:
	// FBX SDK 객체
	FbxManager* SdkManager;			// FBX SDK Manager (전역 관리)
	FbxScene* Scene;				// 현재 로드된 Scene
	FbxImporter* Importer;			// FBX File Importer

	// Import 설정
	FFbxImportOptions CurrentOptions;

	// 에러 메시지
	FString LastErrorMessage;
};
