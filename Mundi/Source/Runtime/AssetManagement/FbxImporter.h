#pragma once

#include "pch.h"
#include "FbxImportOptions.h"
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

// Forward declarations
class USkeleton;
class UStaticMesh;
class USkeletalMesh;
struct FSkeletalMesh;
struct FStaticMesh;

/**
 * Autodesk FBX SDK를 사용한 FBX 파일 임포터
 * Unreal Engine의 FFbxImporter 패턴을 따름
 *
 * 현재 지원:
 * - SkeletalMesh Import (Skeleton, Skin Weights, Bind Pose)
 * - StaticMesh Import (Phase 0에서 추가됨)
 * - FBX Type Detection (자동 타입 감지 via EFbxImportType)
 *
 * 향후 지원 예정:
 * - Animation Import
 *
 * 좌표계:
 * - Mundi 엔진: Z-Up, X-Forward, Y-Right, Left-Handed
 * - FBX는 다양한 좌표계 지원 → 자동 변환
 *
 * Note: EFbxImportType enum은 FbxImportOptions.h에 정의되어 있습니다.
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
	 * FBX 파일에서 StaticMesh를 Import
	 * Skeleton과 Skinning이 없는 Static Geometry를 Import
	 *
	 * @param FilePath - FBX 파일 경로
	 * @param Options - Import 옵션
	 * @param OutMeshData - Import된 Mesh 데이터 (출력)
	 * @return 성공 여부
	 *
	 * @note Phase 0에서 구현됨
	 */
	bool ImportStaticMesh(const FString& FilePath, const FFbxImportOptions& Options, FStaticMesh& OutMeshData);

	/**
	 * Material 추출 (Scene에서 첫 번째 Mesh Node 자동 찾기)
	 * USkeletalMesh::Load()에서 호출 (FBX Scene이 아직 열려있는 상태)
	 * @param OutSkeletalMesh - Material 정보를 저장할 SkeletalMesh
	 * @return 성공 여부
	 */
	bool ExtractMaterialsFromScene(USkeletalMesh* OutSkeletalMesh);

	/**
	 * FBX 파일 타입 감지 (StaticMesh vs SkeletalMesh)
	 * FBX 파일을 분석하여 Skeleton, Skinning, Animation 유무를 확인
	 *
	 * 감지 로직:
	 * 1. FbxSkeleton 노드가 있으면 → SkeletalMesh
	 * 2. FbxSkin Deformer가 있으면 → SkeletalMesh
	 * 3. FbxAnimStack이 있으면 → SkeletalMesh
	 * 4. 모두 없으면 → StaticMesh
	 *
	 * @param FilePath - FBX 파일 경로
	 * @return 감지된 FBX 타입 (StaticMesh, SkeletalMesh, Animation)
	 *
	 * @note 이 함수는 내부적으로 LoadScene()을 호출하므로,
	 *       호출 후에는 Scene이 로드된 상태로 유지됩니다.
	 *       Import 작업 없이 타입만 확인하려면 ReleaseScene()을 호출하세요.
	 */
	EFbxImportType DetectFbxType(const FString& FilePath);

	/**
	 * 마지막 에러 메시지 가져오기
	 * @return 에러 메시지 문자열
	 */
	const FString& GetLastError() const { return LastErrorMessage; }

	/**
	 * FBX Scene을 파일에서 로드
	 * FFbxManager에서 Material 재등록을 위해 public으로 노출
	 * @param FilePath - FBX 파일 경로
	 * @return 성공 여부
	 */
	bool LoadScene(const FString& FilePath);

private:
	// === FBX SDK 관리 ===

	/**
	 * FBX Scene을 Mundi 좌표계로 변환
	 * Mundi: Z-Up, X-Forward, Y-Right, Left-Handed
	 *
	 * 이 함수는 좌표계 변환과 단위 변환을 모두 수행합니다.
	 * bConvertSceneUnit 옵션에 따라 meter (m) 단위로 변환 여부가 결정됩니다.
	 */
	void ConvertScene();

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

	/**
	 * Scene에서 모든 Mesh Node 찾기 (다중 Mesh 지원)
	 * @param Node - 시작 노드 (nullptr인 경우 RootNode부터)
	 * @param OutMeshNodes - 찾은 Mesh Node 배열 (출력)
	 */
	void FindAllMeshNodes(FbxNode* Node, TArray<FbxNode*>& OutMeshNodes);

	// === StaticMesh Import 내부 로직 ===

	/**
	 * StaticMesh 데이터 추출 (Vertex, Index, Normals, UVs, Tangents, Materials)
	 * @param MeshNode - Mesh를 가진 FBX Node
	 * @param OutVertices - Vertex 데이터 배열 (출력)
	 * @param OutIndices - Index 데이터 배열 (출력)
	 * @param OutPolygonMaterialIndices - Polygon별 Material Index 배열 (출력)
	 * @param OutMaterialNames - Material 이름 배열 (출력)
	 * @param InOutVertexOffset - 현재 Vertex Offset (병합 시 Index 조정용, 입출력)
	 * @return 성공 여부
	 */
	bool ExtractStaticMeshData(
		FbxNode* MeshNode,
		TArray<FNormalVertex>& OutVertices,
		TArray<uint32>& OutIndices,
		TArray<int32>& OutPolygonMaterialIndices,
		TArray<FString>& OutMaterialNames,
		uint32& InOutVertexOffset);

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
	bool ExtractMaterials(FbxScene* InScene, USkeletalMesh* OutSkeletalMesh);

	/**
	 * Vertex buffer 최적화 (중복 vertex welding)
	 * 동일한 속성을 가진 vertex를 병합하여 vertex 수 감소
	 * VertexToControlPointMap을 유지하여 skinning weight 보존
	 *
	 * @param Vertices - Vertex 배열 (in-place 수정)
	 * @param Indices - Index 배열 (in-place 수정)
	 * @param VertexToControlPointMap - Control point 매핑 (in-place 수정)
	 * @return 성공 여부
	 */
	bool OptimizeVertexBuffer(
		TArray<FSkinnedVertex>& Vertices,
		TArray<uint32>& Indices,
		TArray<int32>& VertexToControlPointMap);

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
