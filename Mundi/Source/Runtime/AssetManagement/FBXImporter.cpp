#include "pch.h"
#include "FBXImporter.h"
#include <fbxsdk.h>

IMPLEMENT_CLASS(UFBXImporter)

UFBXImporter& UFBXImporter::GetInstance()
{
	static UFBXImporter Instance;
	return Instance;
}

UFBXImporter::UFBXImporter()
	: Manager(nullptr)
	, IOSettings(nullptr)
	, Importer(nullptr)
	, Scene(nullptr)
	, bIsInitialized(false)
{
	Initialize();
}

UFBXImporter::~UFBXImporter()
{
	Shutdown();
}

bool UFBXImporter::Initialize()
{
	if (bIsInitialized)
	{
		return true;
	}

	// FBX Manager 생성
	Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG("[FBX Error] Failed to create FBX Manager\n");
		return false;
	}

	// IO Settings 생성
	IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	Manager->SetIOSettings(IOSettings);

	// Scene 생성
	Scene = FbxScene::Create(Manager, "ImportScene");
	if (!Scene)
	{
		UE_LOG("[FBX Error] Failed to create FBX Scene\n");
		Shutdown();
		return false;
	}

	bIsInitialized = true;
	UE_LOG("[FBX] SDK initialized successfully\n");
	return true;
}

void UFBXImporter::Shutdown()
{
	if (Importer)
	{
		Importer->Destroy();
		Importer = nullptr;
	}

	if (Scene)
	{
		Scene->Destroy();
		Scene = nullptr;
	}

	if (IOSettings)
	{
		IOSettings->Destroy();
		IOSettings = nullptr;
	}

	if (Manager)
	{
		Manager->Destroy();
		Manager = nullptr;
	}

	bIsInitialized = false;
}

FMeshData* UFBXImporter::LoadFBXMesh(const FString& FilePath)
{
	if (!bIsInitialized)
	{
		UE_LOG("[FBX Error] FBX SDK not initialized\n");
		return nullptr;
	}

	// FString to std::string to const char*
	std::string FilePathStr(FilePath.begin(), FilePath.end());
	const char* Filename = FilePathStr.c_str();

	// FBX 파일 임포트
	if (!ImportFBXFile(Filename))
	{
		UE_LOG("[FBX Error] Failed to import FBX file: %s\n", FilePath.c_str());
		return nullptr;
	}

	// 메시 데이터 생성
	FMeshData* MeshData = new FMeshData();

	// Scene 처리
	ProcessScene(MeshData);

	UE_LOG("[FBX] Successfully loaded FBX: %s (Vertices: %d, Indices: %d)\n",
		FilePath.c_str(), static_cast<int32>(MeshData->Vertices.size()), static_cast<int32>(MeshData->Indices.size()));

	return MeshData;
}

bool UFBXImporter::ImportFBXFile(const char* Filename)
{
	// Importer 생성
	if (Importer)
	{
		Importer->Destroy();
	}

	Importer = FbxImporter::Create(Manager, "");

	// 파일 초기화
	if (!Importer->Initialize(Filename, -1, Manager->GetIOSettings()))
	{
		FbxString Error = Importer->GetStatus().GetErrorString();
		UE_LOG("[FBX Error] FBX Importer Initialize failed: %s\n", Error.Buffer());
		return false;
	}

	// Scene 초기화
	Scene->Clear();

	// Scene에 Import
	if (!Importer->Import(Scene))
	{
		FbxString Error = Importer->GetStatus().GetErrorString();
		UE_LOG("[FBX Error] FBX Import failed: %s\n", Error.Buffer());
		return false;
	}

	// 좌표계 변환
	ConvertCoordinateSystem(Scene);

	// 단위 변환
	ConvertUnit(Scene);

	// 삼각형화
	TriangulateScene(Scene);

	return true;
}

void UFBXImporter::ProcessScene(FMeshData* OutMeshData)
{
	if (!Scene || !OutMeshData)
	{
		return;
	}

	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ProcessNode(RootNode, OutMeshData);
	}
}

void UFBXImporter::ProcessNode(FbxNode* Node, FMeshData* OutMeshData)
{
	if (!Node || !OutMeshData)
	{
		return;
	}

	// 현재 노드의 메시 처리
	FbxMesh* Mesh = Node->GetMesh();
	if (Mesh)
	{
		ProcessMesh(Mesh, OutMeshData);
	}

	// 자식 노드 재귀 처리
	for (int32 i = 0; i < Node->GetChildCount(); ++i)
	{
		ProcessNode(Node->GetChild(i), OutMeshData);
	}
}

void UFBXImporter::ProcessMesh(FbxMesh* Mesh, FMeshData* OutMeshData)
{
	if (!Mesh || !OutMeshData)
	{
		return;
	}

	int32 VertexCount = Mesh->GetControlPointsCount();
	int32 PolygonCount = Mesh->GetPolygonCount();

	// 정점 위치 가져오기
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// 기존 정점 개수 저장 (인덱스 오프셋 계산용)
	uint32 VertexOffset = static_cast<uint32>(OutMeshData->Vertices.size());

	// 폴리곤 순회
	for (int32 i = 0; i < PolygonCount; ++i)
	{
		int32 PolygonSize = Mesh->GetPolygonSize(i);

		// 삼각형만 처리 (TriangulateScene에서 삼각형화했으므로 항상 3)
		if (PolygonSize == 3)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				int32 ControlPointIndex = Mesh->GetPolygonVertex(i, j);

				// 정점 위치
				FbxVector4 Position = ControlPoints[ControlPointIndex];
				FVector Vertex(
					static_cast<float>(Position[0]),
					static_cast<float>(Position[1]),
					static_cast<float>(Position[2])
				);

				// Normal 가져오기
				FbxVector4 FbxNormal;
				Mesh->GetPolygonVertexNormal(i, j, FbxNormal);
				FVector Normal(
					static_cast<float>(FbxNormal[0]),
					static_cast<float>(FbxNormal[1]),
					static_cast<float>(FbxNormal[2])
				);

				// UV 가져오기
				FVector2D UV(0.0f, 0.0f);
				FbxLayerElementUV* UVElement = Mesh->GetLayer(0) ? Mesh->GetLayer(0)->GetUVs() : nullptr;
				if (UVElement)
				{
					int32 UVIndex = Mesh->GetTextureUVIndex(i, j);
					if (UVIndex >= 0 && UVIndex < UVElement->GetDirectArray().GetCount())
					{
						FbxVector2 FbxUV = UVElement->GetDirectArray().GetAt(UVIndex);
						UV.X = static_cast<float>(FbxUV[0]);
						UV.Y = 1.0f - static_cast<float>(FbxUV[1]); // DirectX UV 좌표계 (Y 반전)
					}
				}

				// 데이터 추가
				OutMeshData->Vertices.push_back(Vertex);
				OutMeshData->Normal.push_back(Normal);
				OutMeshData->UV.push_back(UV);

				// 인덱스 추가
				OutMeshData->Indices.push_back(VertexOffset + static_cast<uint32>(OutMeshData->Vertices.size()) - 1);
			}
		}
	}
}

void UFBXImporter::ConvertCoordinateSystem(FbxScene* InScene)
{
	if (!InScene)
	{
		return;
	}

	// DirectX 좌표계로 변환 (왼손 좌표계, Y-up)
	FbxAxisSystem SceneAxisSystem = InScene->GetGlobalSettings().GetAxisSystem();
	FbxAxisSystem DirectXAxisSystem(FbxAxisSystem::eYAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);

	if (SceneAxisSystem != DirectXAxisSystem)
	{
		DirectXAxisSystem.ConvertScene(InScene);
		UE_LOG("[FBX] Converted coordinate system to DirectX\n");
	}
}

void UFBXImporter::ConvertUnit(FbxScene* InScene)
{
	if (!InScene)
	{
		return;
	}

	// 센티미터로 변환
	FbxSystemUnit SceneSystemUnit = InScene->GetGlobalSettings().GetSystemUnit();
	if (SceneSystemUnit != FbxSystemUnit::cm)
	{
		FbxSystemUnit::cm.ConvertScene(InScene);
		UE_LOG("[FBX] Converted units to centimeters\n");
	}
}

void UFBXImporter::TriangulateScene(FbxScene* InScene)
{
	if (!InScene)
	{
		return;
	}

	// 메시를 삼각형으로 변환
	FbxGeometryConverter GeometryConverter(Manager);
	bool bSuccess = GeometryConverter.Triangulate(InScene, true);

	if (bSuccess)
	{
		UE_LOG("[FBX] Triangulated FBX scene\n");
	}
	else
	{
		UE_LOG("[FBX Warning] Failed to triangulate FBX scene\n");
	}
}

// ===============================================================
// 스켈레탈 메시 관련 구현
// ===============================================================

FSkeletalMeshData* UFBXImporter::LoadFBXSkeletalMesh(const FString& FilePath)
{
	if (!bIsInitialized)
	{
		UE_LOG("[FBX Error] FBX SDK not initialized\n");
		return nullptr;
	}

	// FString to std::string to const char*
	std::string FilePathStr(FilePath.begin(), FilePath.end());
	const char* Filename = FilePathStr.c_str();

	// FBX 파일 임포트
	if (!ImportFBXFile(Filename))
	{
		UE_LOG("[FBX Error] Failed to import FBX file: %s\n", FilePath.c_str());
		return nullptr;
	}

	// 스켈레탈 메시 데이터 생성
	FSkeletalMeshData* SkeletalMeshData = new FSkeletalMeshData();

	// 스켈레톤 빌드
	BuildSkeleton(&SkeletalMeshData->Skeleton);

	// Scene 처리
	ProcessSkeletalScene(SkeletalMeshData);

	UE_LOG("[FBX] Successfully loaded Skeletal Mesh: %s\n", FilePath.c_str());
	UE_LOG("[FBX]   - Vertices: %d\n", static_cast<int32>(SkeletalMeshData->Vertices.size()));
	UE_LOG("[FBX]   - Indices: %d\n", static_cast<int32>(SkeletalMeshData->Indices.size()));
	UE_LOG("[FBX]   - Bones: %d\n", static_cast<int32>(SkeletalMeshData->Skeleton.Bones.size()));

	return SkeletalMeshData;
}

void UFBXImporter::ProcessSkeletalScene(FSkeletalMeshData* OutSkeletalMeshData)
{
	if (!Scene || !OutSkeletalMeshData)
	{
		return;
	}

	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ProcessSkeletalNode(RootNode, OutSkeletalMeshData);
	}
}

void UFBXImporter::ProcessSkeletalNode(FbxNode* Node, FSkeletalMeshData* OutSkeletalMeshData)
{
	if (!Node || !OutSkeletalMeshData)
	{
		return;
	}

	// 현재 노드의 메시 처리
	FbxMesh* Mesh = Node->GetMesh();
	if (Mesh)
	{
		ProcessSkeletalMesh(Mesh, OutSkeletalMeshData);
	}

	// 자식 노드 재귀 처리
	for (int32 i = 0; i < Node->GetChildCount(); ++i)
	{
		ProcessSkeletalNode(Node->GetChild(i), OutSkeletalMeshData);
	}
}

void UFBXImporter::ProcessSkeletalMesh(FbxMesh* Mesh, FSkeletalMeshData* OutSkeletalMeshData)
{
	if (!Mesh || !OutSkeletalMeshData)
	{
		return;
	}

	int32 VertexCount = Mesh->GetControlPointsCount();
	int32 PolygonCount = Mesh->GetPolygonCount();

	// 정점 위치 가져오기
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// 기존 정점 개수 저장
	uint32 VertexOffset = static_cast<uint32>(OutSkeletalMeshData->Vertices.size());

	// 임시 정점 데이터
	TArray<FVector> TempVertices;
	TArray<FVector> TempNormals;
	TArray<FVector2D> TempUVs;

	// 폴리곤 순회
	for (int32 i = 0; i < PolygonCount; ++i)
	{
		int32 PolygonSize = Mesh->GetPolygonSize(i);

		if (PolygonSize == 3)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				int32 ControlPointIndex = Mesh->GetPolygonVertex(i, j);

				// 정점 위치
				FbxVector4 Position = ControlPoints[ControlPointIndex];
				FVector Vertex(
					static_cast<float>(Position[0]),
					static_cast<float>(Position[1]),
					static_cast<float>(Position[2])
				);

				// Normal 가져오기
				FbxVector4 FbxNormal;
				Mesh->GetPolygonVertexNormal(i, j, FbxNormal);
				FVector Normal(
					static_cast<float>(FbxNormal[0]),
					static_cast<float>(FbxNormal[1]),
					static_cast<float>(FbxNormal[2])
				);

				// UV 가져오기
				FVector2D UV(0.0f, 0.0f);
				FbxLayerElementUV* UVElement = Mesh->GetLayer(0) ? Mesh->GetLayer(0)->GetUVs() : nullptr;
				if (UVElement)
				{
					int32 UVIndex = Mesh->GetTextureUVIndex(i, j);
					if (UVIndex >= 0 && UVIndex < UVElement->GetDirectArray().GetCount())
					{
						FbxVector2 FbxUV = UVElement->GetDirectArray().GetAt(UVIndex);
						UV.X = static_cast<float>(FbxUV[0]);
						UV.Y = 1.0f - static_cast<float>(FbxUV[1]);
					}
				}

				// 데이터 추가
				TempVertices.push_back(Vertex);
				TempNormals.push_back(Normal);
				TempUVs.push_back(UV);

				// 인덱스 추가
				OutSkeletalMeshData->Indices.push_back(VertexOffset + static_cast<uint32>(TempVertices.size()) - 1);
			}
		}
	}

	// 최종 데이터에 추가
	OutSkeletalMeshData->Vertices.insert(OutSkeletalMeshData->Vertices.end(), TempVertices.begin(), TempVertices.end());
	OutSkeletalMeshData->Normal.insert(OutSkeletalMeshData->Normal.end(), TempNormals.begin(), TempNormals.end());
	OutSkeletalMeshData->UV.insert(OutSkeletalMeshData->UV.end(), TempUVs.begin(), TempUVs.end());

	// 스킨 웨이트 추출
	ExtractSkinWeights(Mesh, OutSkeletalMeshData->Skeleton, OutSkeletalMeshData->BoneWeights, static_cast<int32>(TempVertices.size()));
}

void UFBXImporter::BuildSkeleton(FSkeleton* OutSkeleton)
{
	if (!Scene || !OutSkeleton)
	{
		return;
	}

	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		// 루트부터 본 계층 구조 빌드
		BuildBoneHierarchy(RootNode, -1, OutSkeleton);
	}

	UE_LOG("[FBX] Built skeleton with %d bones\n", static_cast<int32>(OutSkeleton->Bones.size()));
}

void UFBXImporter::BuildBoneHierarchy(FbxNode* Node, int32 ParentIndex, FSkeleton* OutSkeleton)
{
	if (!Node || !OutSkeleton)
	{
		return;
	}

	FbxNodeAttribute* NodeAttribute = Node->GetNodeAttribute();
	int32 CurrentBoneIndex = ParentIndex;

	// Skeleton 노드인지 확인
	if (NodeAttribute && NodeAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		FBoneInfo BoneInfo;
		BoneInfo.Name = Node->GetName();
		BoneInfo.ParentIndex = ParentIndex;

		// 로컬 변환 가져오기
		FbxAMatrix LocalTransform = Node->EvaluateLocalTransform();
		BoneInfo.LocalTransform = ConvertFbxMatrixToXMFloat4x4(LocalTransform);

		// 글로벌 바인드 포즈 가져오기
		FbxAMatrix GlobalTransform = Node->EvaluateGlobalTransform();
		BoneInfo.GlobalBindPose = ConvertFbxMatrixToXMFloat4x4(GlobalTransform);

		CurrentBoneIndex = OutSkeleton->AddBone(BoneInfo);
	}

	// 자식 노드 재귀 처리
	for (int32 i = 0; i < Node->GetChildCount(); ++i)
	{
		BuildBoneHierarchy(Node->GetChild(i), CurrentBoneIndex, OutSkeleton);
	}
}

void UFBXImporter::ExtractSkinWeights(FbxMesh* Mesh, const FSkeleton& Skeleton, TArray<FBoneWeight>& OutBoneWeights, int32 VertexCount)
{
	if (!Mesh)
	{
		return;
	}

	// 본 웨이트 배열 초기화
	OutBoneWeights.resize(VertexCount);

	// Skin Deformer 가져오기
	int32 DeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

	for (int32 DeformerIndex = 0; DeformerIndex < DeformerCount; ++DeformerIndex)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(DeformerIndex, FbxDeformer::eSkin));
		if (!Skin)
		{
			continue;
		}

		int32 ClusterCount = Skin->GetClusterCount();

		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster)
			{
				continue;
			}

			// 클러스터에 연결된 본 찾기
			FbxNode* BoneNode = Cluster->GetLink();
			if (!BoneNode)
			{
				continue;
			}

			FString BoneName = BoneNode->GetName();
			int32 BoneIndex = Skeleton.FindBoneIndex(BoneName);

			if (BoneIndex == -1)
			{
				continue;
			}

			// 영향받는 정점들의 인덱스와 가중치
			int32* VertexIndices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();
			int32 IndexCount = Cluster->GetControlPointIndicesCount();

			for (int32 i = 0; i < IndexCount; ++i)
			{
				int32 VertexIndex = VertexIndices[i];
				float Weight = static_cast<float>(Weights[i]);

				if (VertexIndex >= VertexCount)
				{
					continue;
				}

				// 가중치 추가 (최대 4개)
				FBoneWeight& BoneWeight = OutBoneWeights[VertexIndex];
				for (int32 j = 0; j < 4; ++j)
				{
					if (BoneWeight.Weights[j] == 0.0f)
					{
						BoneWeight.BoneIndices[j] = BoneIndex;
						BoneWeight.Weights[j] = Weight;
						break;
					}
				}
			}
		}
	}

	// 모든 본 웨이트 정규화
	for (FBoneWeight& BoneWeight : OutBoneWeights)
	{
		BoneWeight.Normalize();
	}
}

DirectX::XMFLOAT4X4 UFBXImporter::ConvertFbxMatrixToXMFloat4x4(const FbxAMatrix& FbxMatrix)
{
	DirectX::XMFLOAT4X4 Result;

	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			Result.m[Row][Col] = static_cast<float>(FbxMatrix.Get(Row, Col));
		}
	}

	return Result;
}
