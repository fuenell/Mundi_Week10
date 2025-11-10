#pragma once

#include "pch.h"
#include "FbxImportOptions.h"
#include <fbxsdk.h>

// Forward declarations
class USkeletalMesh;
class USkeleton;
class UStaticMesh;

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
	 * FBX 파일에서 SkeletalMesh를 Import
	 * @param FilePath - FBX 파일 경로
	 * @param Options - Import 옵션
	 * @return Import된 SkeletalMesh (실패 시 nullptr)
	 */
	USkeletalMesh* ImportSkeletalMesh(const FString& FilePath, const FFbxImportOptions& Options);

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
	 * @param OutSkeletalMesh - 데이터를 저장할 SkeletalMesh
	 * @return 성공 여부
	 */
	bool ExtractMeshData(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh);

	/**
	 * Skin Weights 추출 (Bone Influences)
	 * @param FbxMesh - FBX Mesh 객체
	 * @param OutSkeletalMesh - 데이터를 저장할 SkeletalMesh
	 * @return 성공 여부
	 */
	bool ExtractSkinWeights(FbxMesh* Mesh, USkeletalMesh* OutSkeletalMesh);

	/**
	 * Bind Pose 추출 (초기 Bone Transform)
	 * @param Scene - FBX Scene
	 * @param OutSkeleton - 데이터를 저장할 Skeleton
	 * @return 성공 여부
	 */
	bool ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton);

	// === Helper 함수 ===

	/**
	 * FbxAMatrix를 Mundi FTransform으로 변환
	 * @param fbxMatrix - FBX 행렬
	 * @return Mundi Transform 구조체
	 */
	FTransform ConvertFbxTransform(const FbxAMatrix& fbxMatrix);

	/**
	 * FbxMatrix를 Mundi FMatrix로 변환 (Unreal Engine 스타일 - Y축 선택적 반전)
	 * Right-Handed → Left-Handed 변환 및 Winding Order 자동 보존
	 * @param fbxMatrix - FBX 행렬
	 * @return Mundi Matrix (Y축 반전 적용됨)
	 */
	FMatrix ConvertFbxMatrixWithYAxisFlip(const FbxMatrix& fbxMatrix);

	/**
	 * FbxMatrix를 Mundi FMatrix로 변환 (기존 방식 - 직접 복사)
	 * @param fbxMatrix - FBX 행렬
	 * @return Mundi Matrix
	 */
	FMatrix ConvertFbxMatrix(const FbxMatrix& fbxMatrix);

	/**
	 * FbxVector4 Position을 Mundi FVector로 변환 (Y축 반전)
	 * @param pos - FBX Position Vector
	 * @return Mundi Vector (Y축 반전 적용됨)
	 */
	FVector ConvertFbxPosition(const FbxVector4& pos);

	/**
	 * FbxVector4 Direction을 Mundi FVector로 변환 (Y축 반전)
	 * Normal, Tangent, Binormal에 사용
	 * @param dir - FBX Direction Vector
	 * @return Mundi Vector (Y축 반전, 정규화됨)
	 */
	FVector ConvertFbxDirection(const FbxVector4& dir);

	/**
	 * FbxQuaternion을 Mundi FQuat로 변환 (Y, W 반전)
	 * Right-Handed → Left-Handed Quaternion 변환
	 * @param q - FBX Quaternion
	 * @return Mundi Quaternion (Y, W 반전 적용됨)
	 */
	FQuat ConvertFbxQuaternion(const FbxQuaternion& q);

	/**
	 * FbxVector4 Scale을 Mundi FVector로 변환 (변환 없음)
	 * @param scale - FBX Scale Vector
	 * @return Mundi Vector (변환 없이 복사)
	 */
	FVector ConvertFbxScale(const FbxVector4& scale);

	/**
	 * TotalMatrix의 Scale에 음수가 홀수 개 있는지 확인 (Unreal Engine 방식)
	 * Odd Negative Scale일 경우 Vertex Order를 reverse해야 올바른 winding order 유지
	 * @param TotalMatrix - Transform Matrix
	 * @return Scale에 음수가 1개 또는 3개면 true
	 */
	bool IsOddNegativeScale(const FbxAMatrix& TotalMatrix);

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
