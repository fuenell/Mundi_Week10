#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "Enums.h"
#include "Object.h"

/**
 * FBX 파일을 임포트하는 싱글톤 클래스
 */
class UFBXImporter : public UObject
{
public:
	DECLARE_CLASS(UFBXImporter, UObject)

	static UFBXImporter& GetInstance();

	UFBXImporter(const UFBXImporter&) = delete;
	UFBXImporter(UFBXImporter&&) = delete;

	UFBXImporter& operator=(const UFBXImporter&) = delete;
	UFBXImporter& operator=(UFBXImporter&&) = delete;

	/**
	 * FBX 파일을 로드하고 메시 데이터를 반환 (스태틱 메시)
	 * @param FilePath FBX 파일 경로
	 * @return 메시 데이터 (실패 시 nullptr)
	 */
	FMeshData* LoadFBXMesh(const FString& FilePath);

	/**
	 * FBX 파일을 로드하고 스켈레탈 메시 데이터를 반환
	 * @param FilePath FBX 파일 경로
	 * @return 스켈레탈 메시 데이터 (실패 시 nullptr)
	 */
	FSkeletalMeshData* LoadFBXSkeletalMesh(const FString& FilePath);

	/**
	 * FBX SDK 초기화 상태 확인
	 * @return 초기화 여부
	 */
	bool IsInitialized() const { return bIsInitialized; }

	UFBXImporter();

protected:
	~UFBXImporter() override;

private:
	/**
	 * FBX SDK 초기화
	 */
	bool Initialize();

	/**
	 * FBX SDK 해제
	 */
	void Shutdown();

	/**
	 * FBX 파일을 Scene으로 임포트
	 * @param Filename 파일 경로
	 * @return 성공 여부
	 */
	bool ImportFBXFile(const char* Filename);

	/**
	 * FBX Scene을 순회하여 메시 데이터 추출
	 * @param OutMeshData 출력 메시 데이터
	 */
	void ProcessScene(FMeshData* OutMeshData);

	/**
	 * FBX Node를 재귀적으로 처리
	 * @param Node 처리할 노드
	 * @param OutMeshData 출력 메시 데이터
	 */
	void ProcessNode(FbxNode* Node, FMeshData* OutMeshData);

	/**
	 * FBX Mesh를 처리하여 정점/인덱스 데이터 추출
	 * @param Mesh 처리할 메시
	 * @param OutMeshData 출력 메시 데이터
	 */
	void ProcessMesh(FbxMesh* Mesh, FMeshData* OutMeshData);

	/**
	 * FBX 좌표계를 DirectX 좌표계로 변환
	 * @param Scene FBX Scene
	 */
	void ConvertCoordinateSystem(FbxScene* Scene);

	/**
	 * FBX 단위를 센티미터로 변환
	 * @param Scene FBX Scene
	 */
	void ConvertUnit(FbxScene* Scene);

	/**
	 * 메시를 삼각형으로 변환
	 * @param Scene FBX Scene
	 */
	void TriangulateScene(FbxScene* Scene);

	// 스켈레탈 메시 관련 함수들

	/**
	 * FBX Scene을 순회하여 스켈레탈 메시 데이터 추출
	 * @param OutSkeletalMeshData 출력 스켈레탈 메시 데이터
	 */
	void ProcessSkeletalScene(FSkeletalMeshData* OutSkeletalMeshData);

	/**
	 * FBX Node를 재귀적으로 처리 (스켈레탈 메시)
	 * @param Node 처리할 노드
	 * @param OutSkeletalMeshData 출력 스켈레탈 메시 데이터
	 */
	void ProcessSkeletalNode(FbxNode* Node, FSkeletalMeshData* OutSkeletalMeshData);

	/**
	 * FBX Mesh를 처리하여 스켈레탈 메시 데이터 추출
	 * @param Mesh 처리할 메시
	 * @param OutSkeletalMeshData 출력 스켈레탈 메시 데이터
	 */
	void ProcessSkeletalMesh(FbxMesh* Mesh, FSkeletalMeshData* OutSkeletalMeshData);

	/**
	 * FBX Scene에서 스켈레톤 빌드
	 * @param OutSkeleton 출력 스켈레톤
	 */
	void BuildSkeleton(FSkeleton* OutSkeleton);

	/**
	 * FBX Node를 재귀적으로 순회하여 본 계층 구조 빌드
	 * @param Node 처리할 노드
	 * @param ParentIndex 부모 본 인덱스
	 * @param OutSkeleton 출력 스켈레톤
	 */
	void BuildBoneHierarchy(FbxNode* Node, int32 ParentIndex, FSkeleton* OutSkeleton);

	/**
	 * FBX Mesh에서 스킨 웨이트 추출
	 * @param Mesh 처리할 메시
	 * @param Skeleton 스켈레톤
	 * @param OutBoneWeights 출력 본 웨이트 배열
	 * @param VertexCount 정점 개수
	 */
	void ExtractSkinWeights(FbxMesh* Mesh, const FSkeleton& Skeleton, TArray<FBoneWeight>& OutBoneWeights, int32 VertexCount);

	/**
	 * FBX 행렬을 DirectX 행렬로 변환
	 * @param FbxMatrix FBX 행렬
	 * @return DirectX 행렬
	 */
	DirectX::XMFLOAT4X4 ConvertFbxMatrixToXMFloat4x4(const FbxAMatrix& FbxMatrix);

private:
	FbxManager* Manager;
	FbxIOSettings* IOSettings;
	FbxImporter* Importer;
	FbxScene* Scene;

	bool bIsInitialized;
};
