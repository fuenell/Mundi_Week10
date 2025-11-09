#include "pch.h"
#include "FbxImporter.h"
#include "Skeleton.h"
#include "SkeletalMesh.h"
#include "ResourceManager.h"
#include "GlobalConsole.h"

FFbxImporter::FFbxImporter()
	: SdkManager(nullptr)
	, Scene(nullptr)
	, Importer(nullptr)
{
	// FBX SDK Manager 초기화
	SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		SetError("Failed to create FBX SDK Manager");
		return;
	}

	// IO Settings 생성
	FbxIOSettings* IOSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(IOSettings);

	OutputDebugStringA("[FBX] FFbxImporter initialized successfully\n");
}

FFbxImporter::~FFbxImporter()
{
	ReleaseScene();

	// FBX SDK Manager 정리
	if (SdkManager)
	{
		SdkManager->Destroy();
		SdkManager = nullptr;
	}

	OutputDebugStringA("[FBX] FFbxImporter destroyed\n");
}

bool FFbxImporter::LoadScene(const FString& FilePath)
{
	// 기존 Scene 정리
	ReleaseScene();

	// Scene 생성
	Scene = FbxScene::Create(SdkManager, "ImportScene");
	if (!Scene)
	{
		SetError("Failed to create FBX Scene");
		return false;
	}

	// Importer 생성
	Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer)
	{
		SetError("Failed to create FBX Importer");
		return false;
	}

	// FBX 파일 로드
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		FString error = "Failed to initialize FBX Importer: ";
		error += Importer->GetStatus().GetErrorString();
		SetError(error);
		return false;
	}

	// Scene으로 Import
	if (!Importer->Import(Scene))
	{
		FString error = "Failed to import FBX file: ";
		error += Importer->GetStatus().GetErrorString();
		SetError(error);
		return false;
	}

	UE_LOG("[FBX] Scene loaded successfully: %s", FilePath.c_str());

	return true;
}

void FFbxImporter::ConvertScene()
{
	if (!Scene)
		return;

	// Mundi 엔진의 좌표계: Z-Up, X-Forward, Y-Right, Left-Handed
	FbxAxisSystem mundiAxis(
		FbxAxisSystem::eZAxis,       // Z-Up
		FbxAxisSystem::eParityEven,  // X-Forward (ParityEven = positive X axis)
		FbxAxisSystem::eLeftHanded   // Left-Handed
	);

	FbxAxisSystem sceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

	if (sceneAxis != mundiAxis)
	{
		UE_LOG("[FBX] Converting scene to Mundi coordinate system (Z-Up, X-Forward, Left-Handed)");
		mundiAxis.ConvertScene(Scene);
	}
	else
	{
		UE_LOG("[FBX] Scene already in Mundi coordinate system");
	}
}

void FFbxImporter::ConvertSceneUnit(float ScaleFactor)
{
	if (!Scene)
		return;

	FbxSystemUnit sceneUnit = Scene->GetGlobalSettings().GetSystemUnit();

	// Unreal Engine 방식: Scene Unit을 자동으로 m (meter) 단위로 변환
	// FBX 파일은 보통 cm 단위로 저장됨 (100cm = 1m)
	if (sceneUnit != FbxSystemUnit::m)
	{
		double sceneScale = sceneUnit.GetScaleFactor();
		UE_LOG("[FBX] Scene uses non-meter unit (scale: %.6f). Converting to meters...", sceneScale);
		FbxSystemUnit::m.ConvertScene(Scene);
		UE_LOG("[FBX] Scene unit converted to meters (1.0)");
	}
	else
	{
		UE_LOG("[FBX] Scene already uses meter unit (1.0)");
	}

	// 추가 사용자 지정 스케일 적용
	if (ScaleFactor != 1.0f)
	{
		FbxSystemUnit customUnit(ScaleFactor);
		UE_LOG("[FBX] Applying additional custom scale: %.2f", ScaleFactor);
		customUnit.ConvertScene(Scene);
	}
}

void FFbxImporter::ReleaseScene()
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
}

void FFbxImporter::TraverseNode(FbxNode* Node, std::function<void(FbxNode*)> ProcessFunc)
{
	if (!Node)
		return;

	// 현재 노드 처리
	ProcessFunc(Node);

	// 자식 노드 재귀 탐색
	for (int i = 0; i < Node->GetChildCount(); i++)
	{
		TraverseNode(Node->GetChild(i), ProcessFunc);
	}
}

FbxNode* FFbxImporter::FindFirstMeshNode(FbxNode* Node)
{
	if (!Scene)
		return nullptr;

	// 시작 노드가 없으면 Root부터
	if (!Node)
		Node = Scene->GetRootNode();

	// Mesh 속성이 있는지 확인
	if (Node->GetMesh())
		return Node;

	// 자식 노드에서 찾기
	for (int i = 0; i < Node->GetChildCount(); i++)
	{
		FbxNode* meshNode = FindFirstMeshNode(Node->GetChild(i));
		if (meshNode)
			return meshNode;
	}

	return nullptr;
}

USkeletalMesh* FFbxImporter::ImportSkeletalMesh(const FString& FilePath, const FFbxImportOptions& Options)
{
	CurrentOptions = Options;

	// 1. Scene 로드
	if (!LoadScene(FilePath))
	{
		return nullptr;
	}

	// 2. 단위 변환 (먼저 수행 - Unreal Engine 방식)
	// ConvertSceneUnit()은 자동으로 Scene Unit을 m로 변환하고,
	// 필요시 추가 사용자 지정 스케일을 적용
	ConvertSceneUnit(CurrentOptions.ImportScale);

	// 3. 좌표계 변환 (Unit 변환 이후 수행)
	if (CurrentOptions.bConvertScene)
	{
		ConvertScene();
	}

	// 4. Scene 전처리 (Triangulate, 중복 제거 등)
	FbxGeometryConverter geometryConverter(SdkManager);
	geometryConverter.Triangulate(Scene, true);

	if (CurrentOptions.bRemoveDegenerates)
	{
		geometryConverter.RemoveBadPolygonsFromMeshes(Scene);
	}

	// 5. Mesh Node 찾기
	FbxNode* meshNode = FindFirstMeshNode();
	if (!meshNode)
	{
		SetError("No mesh found in FBX file");
		return nullptr;
	}

	// 6. Skeleton 추출
	USkeleton* skeleton = ExtractSkeleton(Scene->GetRootNode());
	if (!skeleton)
	{
		SetError("Failed to extract skeleton");
		return nullptr;
	}

	// 7. SkeletalMesh 생성
	USkeletalMesh* skeletalMesh = ObjectFactory::NewObject<USkeletalMesh>();
	if (!skeletalMesh)
	{
		SetError("Failed to create SkeletalMesh object");
		return nullptr;
	}

	// Skeleton 연결
	skeletalMesh->SetSkeleton(skeleton);

	// 8. Mesh 데이터 추출 (Vertex, Normal, UV, Index)
	if (!ExtractMeshData(meshNode, skeletalMesh))
	{
		SetError("Failed to extract mesh data");
		return nullptr;
	}

	// 9. Skin Weights 및 Bind Pose 추출
	// ExtractSkinWeights()에서 FbxCluster를 통해 Inverse Bind Pose도 함께 추출
	if (!ExtractSkinWeights(meshNode->GetMesh(), skeletalMesh))
	{
		SetError("Failed to extract skin weights and bind pose");
		return nullptr;
	}

	// 10. GPU 리소스 생성 (Dynamic Vertex Buffer, Index Buffer)
	// CPU Skinning을 위해 Dynamic Buffer 사용
	ID3D11Device* Device = UResourceManager::GetInstance().GetDevice();
	if (!Device)
	{
		SetError("Failed to get D3D11 Device for GPU resource creation");
		return nullptr;
	}

	if (!skeletalMesh->CreateDynamicGPUResources(Device))
	{
		SetError("Failed to create Dynamic GPU resources (Vertex/Index buffers)");
		return nullptr;
	}

	UE_LOG("[FBX] ImportSkeletalMesh: Completed successfully (Dynamic Buffer for CPU Skinning)");

	return skeletalMesh;
}

UStaticMesh* FFbxImporter::ImportStaticMesh(const FString& FilePath, const FFbxImportOptions& Options)
{
	// TODO: Phase 4에서 구현 예정
	SetError("ImportStaticMesh is not implemented yet");
	UE_LOG("[FBX] ImportStaticMesh: TODO - Phase 4에서 구현 예정");
	return nullptr;
}

USkeleton* FFbxImporter::ExtractSkeleton(FbxNode* RootNode)
{
	if (!RootNode)
	{
		SetError("ExtractSkeleton: RootNode is null");
		return nullptr;
	}

	// Skeleton 객체 생성
	USkeleton* skeleton = ObjectFactory::NewObject<USkeleton>();
	if (!skeleton)
	{
		SetError("ExtractSkeleton: Failed to create Skeleton object");
		return nullptr;
	}

	UE_LOG("[FBX] Extracting skeleton hierarchy...");

	// FbxNode* → Mundi Bone Index 매핑 (FBX 노드 포인터를 키로 사용)
	TMap<FbxNode*, int32> nodeToIndexMap;

	// 재귀적으로 Skeleton 노드 추출
	std::function<void(FbxNode*, int32)> extractBoneHierarchy;
	extractBoneHierarchy = [&](FbxNode* Node, int32 ParentIndex)
	{
		if (!Node)
			return;

		// 현재 노드가 Skeleton 노드인지 확인
		FbxNodeAttribute* attr = Node->GetNodeAttribute();
		bool bIsBone = false;
		int32 currentIndex = -1;

		if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			// Bone으로 추가
			FString boneName = Node->GetName();
			currentIndex = skeleton->AddBone(boneName, ParentIndex);

			if (currentIndex >= 0)
			{
				// 매핑 저장
				nodeToIndexMap[Node] = currentIndex;

				// Local Transform 추출
				FbxAMatrix localMatrix = Node->EvaluateLocalTransform();

				// FbxAMatrix → FTransform 변환
				FTransform localTransform = ConvertFbxTransform(localMatrix);

				// Bind Pose Transform 설정
				skeleton->SetBindPoseTransform(currentIndex, localTransform);

				bIsBone = true;

				UE_LOG("[FBX] Extracted bone [%d]: %s (Parent: %d)",
					currentIndex, boneName.c_str(), ParentIndex);
			}
		}

		// 자식 노드 재귀 탐색
		// Bone인 경우 현재 인덱스를, 아닌 경우 부모 인덱스를 전달
		int32 childParentIndex = bIsBone ? currentIndex : ParentIndex;

		for (int i = 0; i < Node->GetChildCount(); i++)
		{
			extractBoneHierarchy(Node->GetChild(i), childParentIndex);
		}
	};

	// Root부터 탐색 시작 (Root 자체는 Bone이 아닐 수 있으므로 -1로 시작)
	extractBoneHierarchy(RootNode, -1);

	// Skeleton 최종화
	skeleton->FinalizeBones();

	return skeleton;
}

// Helper: FbxAMatrix → FTransform 변환
FTransform FFbxImporter::ConvertFbxTransform(const FbxAMatrix& fbxMatrix)
{
	FTransform transform;

	// Translation 추출
	FbxVector4 fbxTranslation = fbxMatrix.GetT();
	transform.Translation = FVector(
		static_cast<float>(fbxTranslation[0]),
		static_cast<float>(fbxTranslation[1]),
		static_cast<float>(fbxTranslation[2])
	);

	// Rotation 추출 (Quaternion)
	FbxQuaternion fbxRotation = fbxMatrix.GetQ();
	transform.Rotation = FQuat(
		static_cast<float>(fbxRotation[0]),  // X
		static_cast<float>(fbxRotation[1]),  // Y
		static_cast<float>(fbxRotation[2]),  // Z
		static_cast<float>(fbxRotation[3])   // W
	);

	// Scale 추출
	FbxVector4 fbxScale = fbxMatrix.GetS();
	transform.Scale3D = FVector(
		static_cast<float>(fbxScale[0]),
		static_cast<float>(fbxScale[1]),
		static_cast<float>(fbxScale[2])
	);

	return transform;
}

bool FFbxImporter::ExtractMeshData(FbxNode* MeshNode, USkeletalMesh* OutSkeletalMesh)
{
	if (!MeshNode || !OutSkeletalMesh)
	{
		SetError("ExtractMeshData: Invalid parameters");
		return false;
	}

	FbxMesh* fbxMesh = MeshNode->GetMesh();
	if (!fbxMesh)
	{
		SetError("ExtractMeshData: Node has no mesh");
		return false;
	}

	UE_LOG("[FBX] Extracting mesh data...");

	// Vertex 및 Index 데이터 준비
	int32 vertexCount = fbxMesh->GetControlPointsCount();
	int32 polygonCount = fbxMesh->GetPolygonCount();

	if (vertexCount == 0 || polygonCount == 0)
	{
		SetError("ExtractMeshData: Mesh has no vertices or polygons");
		return false;
	}

	UE_LOG("[FBX] Mesh has %d control points, %d polygons", vertexCount, polygonCount);

	// FBX는 Polygon당 3개의 vertex를 가짐 (Triangulated)
	// FBX는 각 폴리곤 vertex마다 별도의 normal/UV를 가질 수 있음
	TArray<FSkinnedVertex> vertices;
	TArray<uint32> indices;

	// Vertex → Control Point 매핑 배열
	// ExtractSkinWeights에서 Bone Weights를 적용할 때 사용
	TArray<int32> vertexToControlPointMap;

	// FBX Control Points (위치 정보)
	FbxVector4* controlPoints = fbxMesh->GetControlPoints();

	// UNREAL ENGINE 방식: Vertex는 원본 그대로 유지 (Mesh Local Space)
	// 좌표계 변환은 InverseBindPose에서 처리
	FbxNode* meshNode = fbxMesh->GetNode();

	// Normal, UV Element 가져오기
	FbxGeometryElementNormal* normalElement = fbxMesh->GetElementNormal();
	FbxGeometryElementUV* uvElement = fbxMesh->GetElementUV();
	FbxGeometryElementTangent* tangentElement = fbxMesh->GetElementTangent();

	// Polygon 순회 (각 Triangle)
	int32 vertexIndexCounter = 0;

	for (int32 polyIndex = 0; polyIndex < polygonCount; polyIndex++)
	{
		int32 polygonSize = fbxMesh->GetPolygonSize(polyIndex);

		// Triangulate되었으므로 모든 polygon은 triangle이어야 함
		if (polygonSize != 3)
		{
			UE_LOG("[FBX] Warning: Polygon %d has %d vertices (expected 3)", polyIndex, polygonSize);
			continue;
		}

		// Triangle의 3개 vertex 처리
		for (int32 vertInPoly = 0; vertInPoly < 3; vertInPoly++)
		{
			FSkinnedVertex vertex;

			// Control Point Index 가져오기
			int32 controlPointIndex = fbxMesh->GetPolygonVertex(polyIndex, vertInPoly);

			// Position 추출 (원본 Local Space 유지)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			FbxVector4 fbxPos = controlPoints[controlPointIndex];
			vertex.Position = FVector(
				static_cast<float>(fbxPos[0]),
				static_cast<float>(fbxPos[1]),
				static_cast<float>(fbxPos[2])
			);

			// Normal 추출 (원본 Local Space 유지)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			if (normalElement)
			{
				FbxVector4 fbxNormal;
				fbxMesh->GetPolygonVertexNormal(polyIndex, vertInPoly, fbxNormal);
				fbxNormal.Normalize();
				vertex.Normal = FVector(
					static_cast<float>(fbxNormal[0]),
					static_cast<float>(fbxNormal[1]),
					static_cast<float>(fbxNormal[2])
				);
			}
			else
			{
				vertex.Normal = FVector(0, 0, 1); // Default normal
			}

			// UV 추출
			if (uvElement)
			{
				FbxVector2 fbxUV;
				bool unmapped;
				fbxMesh->GetPolygonVertexUV(polyIndex, vertInPoly, uvElement->GetName(), fbxUV, unmapped);
				vertex.UV = FVector2D(
					static_cast<float>(fbxUV[0]),
					static_cast<float>(fbxUV[1])
				);
			}
			else
			{
				vertex.UV = FVector2D(0, 0); // Default UV
			}

			// Tangent 추출 (원본 Local Space 유지, 선택적)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			if (tangentElement)
			{
				int32 tangentIndex = 0;
				if (tangentElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					tangentIndex = controlPointIndex;
				}
				else if (tangentElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					tangentIndex = vertexIndexCounter;
				}

				FbxVector4 fbxTangent = tangentElement->GetDirectArray().GetAt(tangentIndex);
				fbxTangent.Normalize();
				vertex.Tangent = FVector4(
					static_cast<float>(fbxTangent[0]),
					static_cast<float>(fbxTangent[1]),
					static_cast<float>(fbxTangent[2]),
					static_cast<float>(fbxTangent[3])  // W 성분은 원본 유지 (handedness)
				);
			}
			else
			{
				vertex.Tangent = FVector4(1, 0, 0, 1); // Default tangent
			}

			// Bone Weights는 나중에 ExtractSkinWeights에서 설정
			for (int i = 0; i < 4; ++i)
			{
				vertex.BoneIndices[i] = 0;
				vertex.BoneWeights[i] = 0.0f;
			}

			// Vertex 추가
			vertices.push_back(vertex);
			indices.push_back(vertexIndexCounter);

			// Control Point Index 매핑 저장
			vertexToControlPointMap.push_back(controlPointIndex);

			vertexIndexCounter++;
		}
	}

	UE_LOG("[FBX] Extracted %zu vertices, %zu indices", vertices.size(), indices.size());

	// SkeletalMesh에 데이터 설정
	OutSkeletalMesh->SetVertices(vertices);
	OutSkeletalMesh->SetIndices(indices);

	// Vertex → Control Point 매핑 저장
	OutSkeletalMesh->SetVertexToControlPointMap(vertexToControlPointMap);

	UE_LOG("[FBX] Stored vertex to control point mapping (%zu entries)", vertexToControlPointMap.size());

	return true;
}

bool FFbxImporter::ExtractSkinWeights(FbxMesh* fbxMesh, USkeletalMesh* OutSkeletalMesh)
{
	if (!fbxMesh || !OutSkeletalMesh)
	{
		SetError("ExtractSkinWeights: Invalid parameters");
		return false;
	}

	USkeleton* skeleton = OutSkeletalMesh->GetSkeleton();
	if (!skeleton)
	{
		SetError("ExtractSkinWeights: SkeletalMesh has no Skeleton");
		return false;
	}

	UE_LOG("[FBX] Extracting skin weights...");

	// Skin Deformer 가져오기
	int32 deformerCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
	if (deformerCount == 0)
	{
		UE_LOG("[FBX] Warning: Mesh has no skin deformer. All vertices will use bone index 0.");
		return true; // 에러는 아니지만 skin weights가 없음
	}

	// 첫 번째 Skin Deformer 사용
	FbxSkin* fbxSkin = static_cast<FbxSkin*>(fbxMesh->GetDeformer(0, FbxDeformer::eSkin));
	if (!fbxSkin)
	{
		SetError("ExtractSkinWeights: Failed to get skin deformer");
		return false;
	}

	int32 clusterCount = fbxSkin->GetClusterCount();
	UE_LOG("[FBX] Skin has %d clusters (bones)", clusterCount);

	// IMPORTANT: Geometry Transform 추출 (Unreal Engine 방식)
	// Mesh Node의 Geometric Translation/Rotation/Scaling
	FbxNode* meshNode = fbxMesh->GetNode();
	FbxVector4 geoTranslation = meshNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	FbxVector4 geoRotation = meshNode->GetGeometricRotation(FbxNode::eSourcePivot);
	FbxVector4 geoScaling = meshNode->GetGeometricScaling(FbxNode::eSourcePivot);
	FbxAMatrix geometryTransform(geoTranslation, geoRotation, geoScaling);

	UE_LOG("[FBX] Geometry Transform - T:(%.3f, %.3f, %.3f) R:(%.3f, %.3f, %.3f) S:(%.3f, %.3f, %.3f)",
		geoTranslation[0], geoTranslation[1], geoTranslation[2],
		geoRotation[0], geoRotation[1], geoRotation[2],
		geoScaling[0], geoScaling[1], geoScaling[2]);

	// Control Point → Bone Influences 매핑
	// FBX는 Control Point 기준으로 Bone Weight를 저장
	// 하지만 우리는 Polygon Vertex 기준으로 저장해야 함

	int32 controlPointCount = fbxMesh->GetControlPointsCount();

	// Control Point별 Bone Influences 저장
	struct FControlPointInfluence
	{
		TArray<int32> BoneIndices;
		TArray<float> Weights;
	};

	TArray<FControlPointInfluence> controlPointInfluences;
	controlPointInfluences.resize(controlPointCount);

	// CRITICAL: Mesh Transform을 사용해 Vertex를 Global Space로 변환
	// 첫 번째 Cluster에서 Mesh Transform을 가져와서 모든 Vertex에 적용
	FbxAMatrix meshGlobalTransform;
	bool bMeshTransformExtracted = false;

	// 각 Cluster(Bone) 순회
	for (int32 clusterIndex = 0; clusterIndex < clusterCount; clusterIndex++)
	{
		FbxCluster* cluster = fbxSkin->GetCluster(clusterIndex);
		if (!cluster)
			continue;

		// Cluster가 영향을 주는 Bone 찾기
		FbxNode* linkNode = cluster->GetLink();
		if (!linkNode)
			continue;

		FString boneName = linkNode->GetName();
		int32 boneIndex = skeleton->FindBoneIndex(boneName);

		if (boneIndex < 0)
		{
			UE_LOG("[FBX] Warning: Bone '%s' not found in skeleton", boneName.c_str());
			continue;
		}

		// UNREAL ENGINE 방식: Cluster API에서 Bind Pose Transform 가져오기
		// cluster->GetTransformLinkMatrix()는 Bind Pose 시점의 Bone Global Transform
		// cluster->GetTransformMatrix()는 Bind Pose 시점의 Mesh Global Transform
		// 이 값들은 ConvertScene() 후의 값이다 (FBX SDK가 자동 처리)
		FbxAMatrix transformLinkMatrix;
		FbxAMatrix transformMatrix;
		cluster->GetTransformLinkMatrix(transformLinkMatrix);  // Bone Global at Bind Pose
		cluster->GetTransformMatrix(transformMatrix);          // Mesh Global at Bind Pose

		// 디버그: 첫 번째 Bone의 변환된 Transform 출력
		if (boneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] === First Bone Cluster Transform Analysis ===");
			UE_LOG("[FBX DEBUG] Bone Name: %s", boneName.c_str());

			// TransformLinkMatrix (Bone Global Transform) 출력
			UE_LOG("[FBX DEBUG] TransformLinkMatrix (Bone Global):");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					transformLinkMatrix.Get(row, 0),
					transformLinkMatrix.Get(row, 1),
					transformLinkMatrix.Get(row, 2),
					transformLinkMatrix.Get(row, 3));
			}

			// TransformMatrix (Mesh Global Transform) 출력
			UE_LOG("[FBX DEBUG] TransformMatrix (Mesh Global):");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					transformMatrix.Get(row, 0),
					transformMatrix.Get(row, 1),
					transformMatrix.Get(row, 2),
					transformMatrix.Get(row, 3));
			}

			// GeometryTransform 출력
			UE_LOG("[FBX DEBUG] GeometryTransform:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					geometryTransform.Get(row, 0),
					geometryTransform.Get(row, 1),
					geometryTransform.Get(row, 2),
					geometryTransform.Get(row, 3));
			}
		}

		// 첫 번째 Cluster 처리 시: Vertex를 Mesh Global Space로 변환
		if (!bMeshTransformExtracted)
		{
			meshGlobalTransform = transformMatrix;
			bMeshTransformExtracted = true;

			// CRITICAL: Vertex들을 Mesh Global Space로 변환
			// transformMatrix는 Bind Pose에서의 Mesh Global Transform
			// geometryTransform은 Mesh의 Pivot/Geometry Offset
			// Vertex는 현재 Component Space (Raw FBX)에 있으므로
			// (transformMatrix × geometryTransform)을 적용해서 Global Space로 이동
			FbxAMatrix totalTransform = transformMatrix * geometryTransform;

			// Normal/Tangent용 Transform (Translation 제거)
			FbxAMatrix normalTransform = totalTransform;
			normalTransform.SetT(FbxVector4(0, 0, 0, 0));

			UE_LOG("[FBX] Transforming vertices to Mesh Global Space using transformMatrix × geometryTransform");

			TArray<FSkinnedVertex>& vertices = OutSkeletalMesh->GetVerticesRef();

			for (auto& vertex : vertices)
			{
				// Position 변환
				FbxVector4 fbxPos(vertex.Position.X, vertex.Position.Y, vertex.Position.Z, 1.0);
				FbxVector4 transformedPos = totalTransform.MultT(fbxPos);

				FVector originalPos = vertex.Position;
				vertex.Position = FVector(
					static_cast<float>(transformedPos[0]),
					static_cast<float>(transformedPos[1]),
					static_cast<float>(transformedPos[2])
				);

				// Normal 변환
				FbxVector4 fbxNormal(vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z, 0.0);
				FbxVector4 transformedNormal = normalTransform.MultT(fbxNormal);
				transformedNormal.Normalize();

				vertex.Normal = FVector(
					static_cast<float>(transformedNormal[0]),
					static_cast<float>(transformedNormal[1]),
					static_cast<float>(transformedNormal[2])
				);

				// Tangent 변환
				FbxVector4 fbxTangent(vertex.Tangent.X, vertex.Tangent.Y, vertex.Tangent.Z, 0.0);
				FbxVector4 transformedTangent = normalTransform.MultT(fbxTangent);
				transformedTangent.Normalize();

				vertex.Tangent = FVector4(
					static_cast<float>(transformedTangent[0]),
					static_cast<float>(transformedTangent[1]),
					static_cast<float>(transformedTangent[2]),
				vertex.Tangent.W
			);
			}

			UE_LOG("[FBX] Vertex transformation complete. Vertex count: %d", vertices.size());
		}

		// Global Bind Pose Matrix 저장 (CPU Skinning에서 직접 사용)
		FMatrix globalBindPoseMatrix = ConvertFbxMatrix(FbxMatrix(transformLinkMatrix));
		skeleton->SetGlobalBindPoseMatrix(boneIndex, globalBindPoseMatrix);

		// 디버그: 변환된 GlobalBindPoseMatrix 출력
		if (boneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] After ConvertFbxMatrix - GlobalBindPoseMatrix:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					globalBindPoseMatrix.M[row][0],
					globalBindPoseMatrix.M[row][1],
					globalBindPoseMatrix.M[row][2],
					globalBindPoseMatrix.M[row][3]);
			}
		}

		// Inverse Bind Pose Matrix 계산
		//
		// UNREAL ENGINE 방식: Vertex는 Mesh Global Space에 있음
		// 위에서 transformMatrix × geometryTransform을 Vertex에 적용했음
		// 따라서 InverseBindPose = BoneGlobal^-1 (단순 역행렬)
		//
		// Skinning 공식: SkinnedVertex = GlobalSpaceVertex × (InverseBindPose × BoneTransform)
		//
		// Bind Pose 검증:
		// InverseBindPose × GlobalBindPose = BoneGlobal^-1 × BoneGlobal = Identity
		FbxAMatrix inverseBindMatrix = transformLinkMatrix.Inverse();

		// Skeleton에 Inverse Bind Pose Matrix 설정
		FMatrix inverseBindPoseMatrix = ConvertFbxMatrix(FbxMatrix(inverseBindMatrix));
		skeleton->SetInverseBindPoseMatrix(boneIndex, inverseBindPoseMatrix);

		// 디버그: 변환된 InverseBindPoseMatrix 출력
		if (boneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] After ConvertFbxMatrix - InverseBindPoseMatrix:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					inverseBindPoseMatrix.M[row][0],
					inverseBindPoseMatrix.M[row][1],
					inverseBindPoseMatrix.M[row][2],
					inverseBindPoseMatrix.M[row][3]);
			}

			// InverseBindPose × GlobalBindPose 계산
			// 디버그 목적: Bind Pose에서 어떤 변환이 적용되는지 확인
			// InverseBindPose × BoneGlobal = BoneGlobal^-1 × BoneGlobal = Identity (이론적으로)
			FMatrix testMatrix = inverseBindPoseMatrix * globalBindPoseMatrix;
			UE_LOG("[FBX DEBUG] InverseBindPose × GlobalBindPose (should be Identity):");

			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					testMatrix.M[row][0],
					testMatrix.M[row][1],
					testMatrix.M[row][2],
					testMatrix.M[row][3]);
			}
		}

		UE_LOG("[FBX] Set bind poses for bone [%d]: %s (Global + Inverse from Cluster)", boneIndex, boneName.c_str());

		// Cluster의 Control Point Indices와 Weights 가져오기
		int32* controlPointIndices = cluster->GetControlPointIndices();
		double* weights = cluster->GetControlPointWeights();
		int32 indexCount = cluster->GetControlPointIndicesCount();

		// Control Point별로 Bone Influence 추가
		for (int32 i = 0; i < indexCount; i++)
		{
			int32 cpIndex = controlPointIndices[i];
			float weight = static_cast<float>(weights[i]);

			if (cpIndex >= 0 && cpIndex < controlPointCount && weight > 0.0f)
			{
				controlPointInfluences[cpIndex].BoneIndices.push_back(boneIndex);
				controlPointInfluences[cpIndex].Weights.push_back(weight);
			}
		}
	}

	// 이제 SkeletalMesh의 각 Vertex에 Bone Weight 적용
	// ExtractMeshData에서 생성한 Vertex들을 가져와서 Bone Weights를 적용

	// 기존 Vertices 가져오기 (수정 가능)
	TArray<FSkinnedVertex>& vertices = OutSkeletalMesh->GetVerticesRef();
	const TArray<int32>& vertexToControlPointMap = OutSkeletalMesh->GetVertexToControlPointMap();

	// 매핑 데이터 검증
	if (vertices.size() != vertexToControlPointMap.size())
	{
		SetError("ExtractSkinWeights: Vertex count mismatch with control point map");
		return false;
	}

	if (vertices.empty())
	{
		UE_LOG("[FBX] Warning: No vertices to apply skin weights");
		return true;
	}

	// 각 Vertex에 Bone Weights 적용
	for (size_t vertIndex = 0; vertIndex < vertices.size(); vertIndex++)
	{
		int32 controlPointIndex = vertexToControlPointMap[vertIndex];

		// Control Point Index 범위 검증
		if (controlPointIndex < 0 || controlPointIndex >= controlPointCount)
		{
			UE_LOG("[FBX] Warning: Invalid control point index %d for vertex %zu", controlPointIndex, vertIndex);
			continue;
		}

		// 이 Control Point의 Bone Influences 가져오기
		const FControlPointInfluence& influence = controlPointInfluences[controlPointIndex];

		// 최대 4개의 Bone Influence만 사용
		int32 influenceCount = std::min(static_cast<int32>(influence.BoneIndices.size()), 4);

		// Weight 정규화를 위한 총합 계산
		float totalWeight = 0.0f;
		for (int32 i = 0; i < influenceCount; i++)
		{
			totalWeight += influence.Weights[i];
		}

		// Bone Indices와 Weights 설정
		for (int32 i = 0; i < 4; i++)
		{
			if (i < influenceCount && totalWeight > 0.0f)
			{
				vertices[vertIndex].BoneIndices[i] = influence.BoneIndices[i];
				vertices[vertIndex].BoneWeights[i] = influence.Weights[i] / totalWeight; // 정규화
			}
			else
			{
				vertices[vertIndex].BoneIndices[i] = 0;
				vertices[vertIndex].BoneWeights[i] = 0.0f;
			}
		}
	}

	UE_LOG("[FBX] Applied skin weights to %zu vertices", vertices.size());

	return true;
}

bool FFbxImporter::ExtractBindPose(FbxScene* Scene, USkeleton* OutSkeleton)
{
	if (!Scene || !OutSkeleton)
	{
		SetError("ExtractBindPose: Invalid parameters");
		return false;
	}

	UE_LOG("[FBX] Extracting bind pose...");

	// FBX Scene의 Bind Pose 개수 확인
	int32 poseCount = Scene->GetPoseCount();

	if (poseCount == 0)
	{
		UE_LOG("[FBX] Warning: No bind pose found in scene");
		return true; // Bind Pose가 없어도 에러는 아님 (Local Transform 사용)
	}

	UE_LOG("[FBX] Scene has %d poses", poseCount);

	// 첫 번째 Bind Pose 사용 (보통 하나만 존재)
	FbxPose* bindPose = nullptr;

	for (int32 i = 0; i < poseCount; i++)
	{
		FbxPose* pose = Scene->GetPose(i);
		if (pose && pose->IsBindPose())
		{
			bindPose = pose;
			UE_LOG("[FBX] Found bind pose");
			break;
		}
	}

	if (!bindPose)
	{
		UE_LOG("[FBX] Warning: No bind pose found, using local transforms");
		return true;
	}

	// Bind Pose의 각 Node에 대한 Transform 정보 추출
	int32 poseNodeCount = bindPose->GetCount();
	UE_LOG("[FBX] Bind pose has %d nodes", poseNodeCount);

	for (int32 i = 0; i < poseNodeCount; i++)
	{
		FbxNode* node = bindPose->GetNode(i);
		if (!node)
			continue;

		FString nodeName = node->GetName();

		// Skeleton에서 이 Bone 찾기
		int32 boneIndex = OutSkeleton->FindBoneIndex(nodeName);
		if (boneIndex < 0)
		{
			// Skeleton에 없는 노드는 무시 (Bone이 아닐 수 있음)
			continue;
		}

		// Bind Pose Matrix 가져오기
		// IMPORTANT: ConvertScene() 적용 후의 Transform 사용
		// bindPose->GetMatrix(i)는 변환 전 원본 데이터이므로 사용하지 않음
		// node->EvaluateGlobalTransform()은 ConvertScene() 적용 후의 Global Transform
		FbxAMatrix fbxBindMatrix = node->EvaluateGlobalTransform();

		// Inverse Bind Pose Matrix 계산
		// Skinning 시 Vertex를 Bone Space로 변환하는데 사용
		FbxAMatrix fbxInverseBindMatrix = fbxBindMatrix.Inverse();

		// FbxAMatrix를 Mundi FMatrix로 변환 (Row-Major)
		FMatrix inverseBindPoseMatrix = ConvertFbxMatrix(FbxMatrix(fbxInverseBindMatrix));

		// Skeleton에 Inverse Bind Pose Matrix 설정
		OutSkeleton->SetInverseBindPoseMatrix(boneIndex, inverseBindPoseMatrix);

		UE_LOG("[FBX] Set inverse bind pose for bone [%d]: %s", boneIndex, nodeName.c_str());
	}

	UE_LOG("[FBX] Bind pose extraction completed");
	return true;
}

// Helper: FbxMatrix → FMatrix 변환
FMatrix FFbxImporter::ConvertFbxMatrix(const FbxMatrix& fbxMatrix)
{
	FMatrix matrix;

	// FBX는 Row-Major, Mundi도 Row-Major이므로 직접 복사 가능
	for (int32 row = 0; row < 4; row++)
	{
		for (int32 col = 0; col < 4; col++)
		{
			matrix.M[row][col] = static_cast<float>(fbxMatrix.Get(row, col));
		}
	}

	return matrix;
}

void FFbxImporter::SetError(const FString& Message)
{
	LastErrorMessage = Message;
	FString errorMsg = "[FBX ERROR] " + Message + "\n";
	OutputDebugStringA(errorMsg.c_str());
}

