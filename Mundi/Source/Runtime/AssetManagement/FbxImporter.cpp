#include "pch.h"
#include "FbxImporter.h"
#include "Skeleton.h"
#include "SkeletalMesh.h"
#include "ResourceManager.h"
#include "GlobalConsole.h"
#include "Material.h"
#include <filesystem>

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
		FString Error = "Failed to initialize FBX Importer: ";
		Error += Importer->GetStatus().GetErrorString();
		SetError(Error);
		return false;
	}

	// Scene으로 Import
	// 이 import에서 텍스처 자동추출 (FBX SDK가 Embedded Texture 감지)
	if (!Importer->Import(Scene))
	{
		FString Error = "Failed to import FBX file: ";
		Error += Importer->GetStatus().GetErrorString();
		SetError(Error);
		return false;
	}

	UE_LOG("[FBX] Scene loaded successfully: %s", FilePath.c_str());

	return true;
}

void FFbxImporter::ConvertScene()
{
	if (!Scene)
		return;

	// Axis Conversion Matrix 초기화 (Identity)
	FbxAMatrix AxisConversionMatrix;
	AxisConversionMatrix.SetIdentity();

	// Joint Post-Conversion Matrix 초기화 (Identity) - SkeletalMesh 전용
	FbxAMatrix JointPostConversionMatrix;
	JointPostConversionMatrix.SetIdentity();

	// === 좌표계 변환 (Unreal Engine 방식) ===
	if (CurrentOptions.bConvertScene)
	{
		// 원본 Scene Axis System 정보 출력
		FbxAxisSystem SceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

		int UpSign;
		FbxAxisSystem::EUpVector UpVector = SceneAxis.GetUpVector(UpSign);
		int FrontSign;
		FbxAxisSystem::EFrontVector FrontVector = SceneAxis.GetFrontVector(FrontSign);
		FbxAxisSystem::ECoordSystem CoordSystem = SceneAxis.GetCoorSystem();

		UE_LOG("[FBX DEBUG] === Original Scene Coordinate System ===");
		UE_LOG("[FBX DEBUG] UpVector: %d (sign: %d)", (int)UpVector, UpSign);
		UE_LOG("[FBX DEBUG] FrontVector: %d (sign: %d)", (int)FrontVector, FrontSign);
		UE_LOG("[FBX DEBUG] CoordSystem: %s", CoordSystem == FbxAxisSystem::eRightHanded ? "RightHanded" : "LeftHanded");

		// Target 좌표계 설정 (Unreal Engine 스타일)
		FbxAxisSystem::ECoordSystem TargetCoordSystem = FbxAxisSystem::eRightHanded;
		FbxAxisSystem::EUpVector TargetUpVector = FbxAxisSystem::eZAxis;
		FbxAxisSystem::EFrontVector TargetFrontVector = (FbxAxisSystem::EFrontVector)-FbxAxisSystem::eParityOdd;  // -Y Forward

		// bForceFrontXAxis 옵션 체크
		if (CurrentOptions.bForceFrontXAxis)
		{
			TargetFrontVector = FbxAxisSystem::eParityEven;  // +X Forward
			UE_LOG("[FBX] bForceFrontXAxis enabled - using +X as Forward axis");
		}

		FbxAxisSystem UnrealImportAxis(TargetUpVector, TargetFrontVector, TargetCoordSystem);

		// 좌표계가 다른 경우만 변환
		if (SceneAxis != UnrealImportAxis)
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
				JointPostConversionMatrix.SetR(FbxVector4(-90.0, -90.0, 0.0));
				UE_LOG("[FBX] JointOrientationMatrix set: (-90 degrees, -90 degrees, 0 degrees)");
				UE_LOG("[FBX] This will convert Bone Hierarchy from -Y Forward to +X Forward");
			}

			// Axis Conversion Matrix 계산
			FbxAMatrix SourceMatrix, TargetMatrix;
			SceneAxis.GetMatrix(SourceMatrix);
			UnrealImportAxis.GetMatrix(TargetMatrix);
			AxisConversionMatrix = SourceMatrix.Inverse() * TargetMatrix;

			UE_LOG("[FBX] Axis Conversion Matrix calculated");

			// 변환 후 검증
			FbxAxisSystem ConvertedAxis = Scene->GetGlobalSettings().GetAxisSystem();
			int ConvertedUpSign;
			FbxAxisSystem::EUpVector ConvertedUpVector = ConvertedAxis.GetUpVector(ConvertedUpSign);
			int ConvertedFrontSign;
			FbxAxisSystem::EFrontVector ConvertedFrontVector = ConvertedAxis.GetFrontVector(ConvertedFrontSign);
			FbxAxisSystem::ECoordSystem ConvertedCoordSystem = ConvertedAxis.GetCoorSystem();

			UE_LOG("[FBX DEBUG] === After Conversion ===");
			UE_LOG("[FBX DEBUG] UpVector: %d (sign: %d)", (int)ConvertedUpVector, ConvertedUpSign);
			UE_LOG("[FBX DEBUG] FrontVector: %d (sign: %d)", (int)ConvertedFrontVector, ConvertedFrontSign);
			UE_LOG("[FBX DEBUG] CoordSystem: %s", ConvertedCoordSystem == FbxAxisSystem::eRightHanded ? "RightHanded" : "LeftHanded");
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
	FFbxDataConverter::SetAxisConversionMatrix(AxisConversionMatrix);
	FFbxDataConverter::SetJointPostConversionMatrix(JointPostConversionMatrix);

	// === 단위 변환 ===
	if (CurrentOptions.bConvertSceneUnit)
	{
		FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
		double SceneScale = SceneUnit.GetScaleFactor();

		UE_LOG("[FBX] Original scene unit scale factor: %.6f", SceneScale);

		if (SceneUnit != FbxSystemUnit::m)
		{
			UE_LOG("[FBX] Converting scene unit to meters (m)");
			FbxSystemUnit::m.ConvertScene(Scene);
		}
		else
		{
			UE_LOG("[FBX] Scene already in meter (m) unit");
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
		FbxNode* MeshNode = FindFirstMeshNode(Node->GetChild(i));
		if (MeshNode)
			return MeshNode;
	}

	return nullptr;
}

void FFbxImporter::FindAllMeshNodes(FbxNode* Node, TArray<FbxNode*>& OutMeshNodes)
{
	if (!Scene)
		return;

	// 시작 노드가 없으면 Root부터
	if (!Node)
		Node = Scene->GetRootNode();

	// 현재 노드가 Mesh를 가지고 있으면 추가
	if (Node->GetMesh())
	{
		OutMeshNodes.push_back(Node);
	}

	// 모든 자식 노드 탐색 (첫 번째만이 아니라 전부!)
	for (int i = 0; i < Node->GetChildCount(); i++)
	{
		FindAllMeshNodes(Node->GetChild(i), OutMeshNodes);
	}
}

bool FFbxImporter::ImportSkeletalMesh(const FString& FilePath, const FFbxImportOptions& Options, FSkeletalMesh& OutMeshData)
{
	CurrentOptions = Options;

	// 1. Scene 로드
	if (!LoadScene(FilePath))
	{
		return false;
	}

	// 2. 좌표계 및 단위 변환
	// ConvertScene()은 좌표계 변환 + 단위 변환(cm)을 수행 (Unreal Engine 방식)
	// bConvertSceneUnit 옵션에 따라 단위 변환 수행 여부 결정
	if (CurrentOptions.bConvertScene)
	{
		ConvertScene();
	}

	// 3. 추가 사용자 지정 스케일 적용 (ImportScale != 1.0일 때만)
	if (CurrentOptions.ImportScale != 1.0f)
	{
		FbxSystemUnit CustomUnit(CurrentOptions.ImportScale);
		UE_LOG("[FBX] Applying additional custom scale: %.2f", CurrentOptions.ImportScale);
		CustomUnit.ConvertScene(Scene);
	}

	// 4. Scene 전처리 (Triangulate, 중복 제거 등)
	FbxGeometryConverter GeometryConverter(SdkManager);
	GeometryConverter.Triangulate(Scene, true);

	if (CurrentOptions.bRemoveDegenerates)
	{
		GeometryConverter.RemoveBadPolygonsFromMeshes(Scene);
	}

	// 5. 모든 Mesh Node 찾기 (다중 Mesh 지원)
	TArray<FbxNode*> meshNodes;
	FindAllMeshNodes(nullptr, meshNodes);

	if (meshNodes.empty())
	{
		SetError("No mesh found in FBX file");
		return false;
	}

	// 6. Skeleton 추출
	USkeleton* Skeleton = ExtractSkeleton(Scene->GetRootNode());
	if (!Skeleton)
	{
		SetError("Failed to extract skeleton");
		return false;
	}

	// Skeleton을 OutMeshData에 설정
	OutMeshData.Skeleton = Skeleton;

	// 7. 모든 Mesh 데이터 추출 및 병합
	TArray<FSkinnedVertex> mergedVertices;
	TArray<uint32> mergedIndices;
	TArray<int32> mergedVertexToControlPointMap;
	TArray<int32> mergedPolygonMaterialIndices;
	TMap<FString, int32> materialNameToGlobalIndex;  // Material 이름 → Global Material Index
	TArray<FString> globalMaterialNames;

	uint32 currentVertexOffset = 0;

	for (size_t meshIdx = 0; meshIdx < meshNodes.size(); meshIdx++)
	{
		FbxNode* meshNode = meshNodes[meshIdx];

		// 임시 Mesh 데이터 구조체
		FSkeletalMesh tempMeshData;
		tempMeshData.Skeleton = Skeleton;

		// 이 Mesh의 데이터 추출
		if (!ExtractMeshData(meshNode, tempMeshData))
		{
			continue;
		}

		// Skin Weights 추출
		if (!ExtractSkinWeights(meshNode->GetMesh(), tempMeshData))
		{
			continue;
		}

		// Vertex 병합
		mergedVertices.insert(mergedVertices.end(),
			tempMeshData.Vertices.begin(),
			tempMeshData.Vertices.end());

		// Index 병합 (Vertex Offset 적용)
		for (uint32 idx : tempMeshData.Indices)
		{
			mergedIndices.push_back(idx + currentVertexOffset);
		}

		// VertexToControlPointMap 병합
		mergedVertexToControlPointMap.insert(mergedVertexToControlPointMap.end(),
			tempMeshData.VertexToControlPointMap.begin(),
			tempMeshData.VertexToControlPointMap.end());

		// Material 이름을 Global Material Index로 변환
		for (const FString& localMatName : tempMeshData.MaterialNames)
		{
			if (materialNameToGlobalIndex.find(localMatName) == materialNameToGlobalIndex.end())
			{
				int32 newGlobalIndex = static_cast<int32>(globalMaterialNames.size());
				materialNameToGlobalIndex[localMatName] = newGlobalIndex;
				globalMaterialNames.push_back(localMatName);
			}
		}

		// Polygon Material Index 병합 (Local Index → Global Index 변환)
		for (int32 localMatIndex : tempMeshData.PolygonMaterialIndices)
		{
			// Local Material Index를 Material 이름으로 변환
			if (localMatIndex >= 0 && localMatIndex < tempMeshData.MaterialNames.size())
			{
				const FString& matName = tempMeshData.MaterialNames[localMatIndex];
				int32 globalMatIndex = materialNameToGlobalIndex[matName];
				mergedPolygonMaterialIndices.push_back(globalMatIndex);
			}
			else
			{
				mergedPolygonMaterialIndices.push_back(0); // Default
			}
		}

		currentVertexOffset += static_cast<uint32>(tempMeshData.Vertices.size());
	}

	// 8. 병합된 Mesh에서 Material별로 Index Buffer 재배치 및 GroupInfo 생성
	TArray<FGroupInfo> finalGroupInfos;
	TArray<uint32> finalIndices;

	if (!mergedPolygonMaterialIndices.empty())
	{
		// Material별로 Polygon 그룹화
		TMap<int32, TArray<int32>> materialToPolygons;
		int32 polygonCount = static_cast<int32>(mergedPolygonMaterialIndices.size());

		for (int32 polyIndex = 0; polyIndex < polygonCount; polyIndex++)
		{
			int32 matIndex = mergedPolygonMaterialIndices[polyIndex];
			materialToPolygons[matIndex].push_back(polyIndex);
		}

		// Material별로 Index 재배치
		uint32 currentStartIndex = 0;

		for (auto& pair : materialToPolygons)
		{
			int32 matIndex = pair.first;
			const TArray<int32>& polygons = pair.second;

			if (polygons.empty())
				continue;

			FGroupInfo groupInfo;
			groupInfo.StartIndex = currentStartIndex;
			groupInfo.IndexCount = static_cast<uint32>(polygons.size() * 3);

			// Global Material 이름 설정
			if (matIndex >= 0 && matIndex < globalMaterialNames.size())
			{
				groupInfo.InitialMaterialName = globalMaterialNames[matIndex];
			}

			// 이 Material의 모든 Polygon Index를 새 버퍼에 추가
			for (int32 polyIndex : polygons)
			{
				uint32 polyStartIdx = polyIndex * 3;
				finalIndices.push_back(mergedIndices[polyStartIdx + 0]);
				finalIndices.push_back(mergedIndices[polyStartIdx + 1]);
				finalIndices.push_back(mergedIndices[polyStartIdx + 2]);
			}

			currentStartIndex += groupInfo.IndexCount;
			finalGroupInfos.push_back(groupInfo);
		}
	}
	else
	{
		// Material 정보가 없으면 전체를 하나의 그룹으로
		finalIndices = mergedIndices;
		FGroupInfo groupInfo;
		groupInfo.StartIndex = 0;
		groupInfo.IndexCount = static_cast<uint32>(finalIndices.size());
		groupInfo.InitialMaterialName = "";
		finalGroupInfos.push_back(groupInfo);
	}

	// 병합된 데이터를 OutMeshData에 설정
	OutMeshData.Vertices = std::move(mergedVertices);
	OutMeshData.Indices = std::move(finalIndices);
	OutMeshData.VertexToControlPointMap = std::move(mergedVertexToControlPointMap);
	OutMeshData.GroupInfos = std::move(finalGroupInfos);

	return true;
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
	USkeleton* Skeleton = ObjectFactory::NewObject<USkeleton>();
	if (!Skeleton)
	{
		SetError("ExtractSkeleton: Failed to create Skeleton object");
		return nullptr;
	}

	UE_LOG("[FBX] Extracting skeleton hierarchy...");

	// FbxNode* → Mundi Bone Index 매핑 (FBX 노드 포인터를 키로 사용)
	TMap<FbxNode*, int32> NodeToIndexMap;

	// 재귀적으로 Skeleton 노드 추출
	std::function<void(FbxNode*, int32)> ExtractBoneHierarchy;
	ExtractBoneHierarchy = [&](FbxNode* Node, int32 ParentIndex)
	{
		if (!Node)
			return;

		// 현재 노드가 Skeleton 노드인지 확인
		FbxNodeAttribute* Attr = Node->GetNodeAttribute();
		bool bIsBone = false;
		int32 CurrentIndex = -1;

		if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			// Bone으로 추가
			FString BoneName = Node->GetName();
			CurrentIndex = Skeleton->AddBone(BoneName, ParentIndex);

			if (CurrentIndex >= 0)
			{
				// 매핑 저장
				NodeToIndexMap[Node] = CurrentIndex;

				// Local Transform 추출
				FbxAMatrix LocalMatrix = Node->EvaluateLocalTransform();

				// FbxAMatrix → FTransform 변환
				FTransform LocalTransform = ConvertFbxTransform(LocalMatrix);

				// Bind Pose Transform 설정
				Skeleton->SetBindPoseTransform(CurrentIndex, LocalTransform);

				bIsBone = true;

				UE_LOG("[FBX] Extracted bone [%d]: %s (Parent: %d)",
					CurrentIndex, BoneName.c_str(), ParentIndex);
			}
		}

		// 자식 노드 재귀 탐색
		// Bone인 경우 현재 인덱스를, 아닌 경우 부모 인덱스를 전달
		int32 ChildParentIndex = bIsBone ? CurrentIndex : ParentIndex;

		for (int i = 0; i < Node->GetChildCount(); i++)
		{
			ExtractBoneHierarchy(Node->GetChild(i), ChildParentIndex);
		}
	};

	// Root부터 탐색 시작 (Root 자체는 Bone이 아닐 수 있으므로 -1로 시작)
	ExtractBoneHierarchy(RootNode, -1);

	// Skeleton 최종화
	Skeleton->FinalizeBones();

	return Skeleton;
}

// Helper: FbxAMatrix → FTransform 변환 (Unreal Engine 스타일)
FTransform FFbxImporter::ConvertFbxTransform(const FbxAMatrix& fbxMatrix)
{
	FTransform Transform;

	// Translation 추출 및 변환 (Y축 반전)
	FbxVector4 FbxTranslation = fbxMatrix.GetT();
	Transform.Translation = ConvertFbxPosition(FbxTranslation);

	// Rotation 추출 및 변환 (Y, W 반전)
	FbxQuaternion FbxRotation = fbxMatrix.GetQ();
	Transform.Rotation = ConvertFbxQuaternion(FbxRotation);

	// Scale 추출 (변환 없음)
	FbxVector4 FbxScale = fbxMatrix.GetS();
	Transform.Scale3D = ConvertFbxScale(FbxScale);

	return Transform;
}

bool FFbxImporter::ExtractMeshData(FbxNode* MeshNode, FSkeletalMesh& OutMeshData)
{
	if (!MeshNode)
	{
		SetError("ExtractMeshData: Invalid parameters");
		return false;
	}

	FbxMesh* FbxMesh = MeshNode->GetMesh();
	if (!FbxMesh)
	{
		SetError("ExtractMeshData: Node has no mesh");
		return false;
	}

	UE_LOG("[FBX] Extracting mesh data...");

	// Vertex 및 Index 데이터 준비
	int32 VertexCount = FbxMesh->GetControlPointsCount();
	int32 PolygonCount = FbxMesh->GetPolygonCount();

	if (VertexCount == 0 || PolygonCount == 0)
	{
		SetError("ExtractMeshData: Mesh has no vertices or polygons");
		return false;
	}

	UE_LOG("[FBX] Mesh has %d control points, %d polygons", VertexCount, PolygonCount);

	// FBX는 Polygon당 3개의 vertex를 가짐 (Triangulated)
	// FBX는 각 폴리곤 vertex마다 별도의 normal/UV를 가질 수 있음
	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;

	// Vertex → Control Point 매핑 배열
	// ExtractSkinWeights에서 Bone Weights를 적용할 때 사용
	TArray<int32> VertexToControlPointMap;

	// FBX Control Points (위치 정보)
	FbxVector4* ControlPoints = FbxMesh->GetControlPoints();

	// UNREAL ENGINE 방식: Vertex는 원본 그대로 유지 (Mesh Local Space)
	// 좌표계 변환은 InverseBindPose에서 처리
	FbxNode* MeshNodePtr = FbxMesh->GetNode();

	// Normal, UV Element 가져오기
	FbxGeometryElementNormal* NormalElement = FbxMesh->GetElementNormal();
	FbxGeometryElementUV* UvElement = FbxMesh->GetElementUV();
	FbxGeometryElementTangent* TangentElement = FbxMesh->GetElementTangent();


	// Skeleton 정보 (Static vs Skeletal 구분용)
	USkeleton* Skeleton = OutMeshData.Skeleton;

	// Material Element 가져오기 (다중 Material 지원)
	FbxGeometryElementMaterial* materialElement = FbxMesh->GetElementMaterial();

	// Polygon별 Material 인덱스 저장
	TArray<int32> polygonMaterialIndices;
	polygonMaterialIndices.resize(PolygonCount, 0);  // 기본값 0

	if (materialElement)
	{
		if (materialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
		{
			// Polygon별로 Material 인덱스가 할당됨
			if (materialElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
			{
				for (int32 polyIndex = 0; polyIndex < PolygonCount; polyIndex++)
				{
					int32 materialIndex = materialElement->GetIndexArray().GetAt(polyIndex);
					polygonMaterialIndices[polyIndex] = materialIndex;
				}
			}
		}
		else if (materialElement->GetMappingMode() == FbxGeometryElement::eAllSame)
		{
			// 전체 Mesh가 하나의 Material 사용
			int32 materialIndex = materialElement->GetIndexArray().GetAt(0);
			for (int32 polyIndex = 0; polyIndex < PolygonCount; polyIndex++)
			{
				polygonMaterialIndices[polyIndex] = materialIndex;
			}
		}
	}

	// Polygon 순회 (각 Triangle)
	int32 VertexIndexCounter = 0;

	for (int32 PolyIndex = 0; PolyIndex < PolygonCount; PolyIndex++)
	{
		int32 PolygonSize = FbxMesh->GetPolygonSize(PolyIndex);

		// Triangulate되었으므로 모든 polygon은 triangle이어야 함
		if (PolygonSize != 3)
		{
			UE_LOG("[FBX] Warning: Polygon %d has %d vertices (expected 3)", PolyIndex, PolygonSize);
			continue;
		}

		// Triangle의 3개 vertex 처리
		for (int32 VertInPoly = 0; VertInPoly < 3; VertInPoly++)
		{
			FSkinnedVertex Vertex;

			// Control Point Index 가져오기
			int32 ControlPointIndex = FbxMesh->GetPolygonVertex(PolyIndex, VertInPoly);

			// Position 추출 (원본 Local Space 유지)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			FbxVector4 FbxPos = ControlPoints[ControlPointIndex];
			Vertex.Position = FVector(
				static_cast<float>(FbxPos[0]),
				static_cast<float>(FbxPos[1]),
				static_cast<float>(FbxPos[2])
			);

			// Normal 추출 (원본 Local Space 유지)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			if (NormalElement)
			{
				FbxVector4 FbxNormal;
				FbxMesh->GetPolygonVertexNormal(PolyIndex, VertInPoly, FbxNormal);
				FbxNormal.Normalize();
				Vertex.Normal = FVector(
					static_cast<float>(FbxNormal[0]),
					static_cast<float>(FbxNormal[1]),
					static_cast<float>(FbxNormal[2])
				);
			}
			else
			{
				Vertex.Normal = FVector(0, 0, 1); // Default normal
			}

			// UV 추출
			if (UvElement)
			{
				FbxVector2 FbxUV;
				bool bUnmapped;
				FbxMesh->GetPolygonVertexUV(PolyIndex, VertInPoly, UvElement->GetName(), FbxUV, bUnmapped);

				// DirectX UV 좌표계: V를 반전 (OpenGL/Blender는 V가 아래→위, DirectX는 위→아래)
				Vertex.UV = FVector2D(
					static_cast<float>(FbxUV[0]),      // U (그대로)
					1.0f - static_cast<float>(FbxUV[1]) // V (반전)
				);
			}
			else
			{
				Vertex.UV = FVector2D(0, 0); // Default UV
			}

			// Tangent 추출 (원본 Local Space 유지, 선택적)
			// 좌표계 변환은 InverseBindPose에서 처리됨
			if (TangentElement)
			{
				int32 TangentIndex = 0;
				if (TangentElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					TangentIndex = ControlPointIndex;
				}
				else if (TangentElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					TangentIndex = FbxMesh->GetPolygonVertexIndex(PolyIndex) + VertInPoly;
				}

				FbxVector4 FbxTangent = TangentElement->GetDirectArray().GetAt(TangentIndex);
				FbxTangent.Normalize();
				Vertex.Tangent = FVector4(
					static_cast<float>(FbxTangent[0]),
					static_cast<float>(FbxTangent[1]),
					static_cast<float>(FbxTangent[2]),
					static_cast<float>(FbxTangent[3])  // W 성분은 원본 유지 (handedness)
				);
			}
			else
			{
				Vertex.Tangent = FVector4(1, 0, 0, 1); // Default tangent
			}

			// Bone Weights는 나중에 ExtractSkinWeights에서 설정
			for (int i = 0; i < 4; ++i)
			{
				Vertex.BoneIndices[i] = 0;
				Vertex.BoneWeights[i] = 0.0f;
			}

			// Vertex와 Index 저장
			Vertices.Add(Vertex);
			Indices.Add(VertexIndexCounter++);
			VertexToControlPointMap.Add(ControlPointIndex);
		}
	}

	UE_LOG("[FBX] Extracted %zu vertices, %zu indices", Vertices.Num(), Indices.Num());

	// Material 이름 배열 생성
	TArray<FString> materialNames;
	for (int32 i = 0; i < MeshNode->GetMaterialCount(); i++)
	{
		FbxSurfaceMaterial* mat = MeshNode->GetMaterial(i);
		if (mat)
		{
			materialNames.push_back(mat->GetName());
		}
	}

	// FSkeletalMesh에 데이터 설정 (Move Semantics)
	OutMeshData.Vertices = std::move(Vertices);
	OutMeshData.Indices = std::move(Indices);
	OutMeshData.VertexToControlPointMap = std::move(VertexToControlPointMap);
	OutMeshData.PolygonMaterialIndices = std::move(polygonMaterialIndices);
	OutMeshData.MaterialNames = std::move(materialNames);

	// STATIC MESH 처리: Skeleton이 없는 경우 Unreal Engine 방식 적용
	// Skeletal Mesh는 ExtractSkinWeights()에서 변환되므로 여기서는 건너뜀
	// Skeleton 변수는 함수 시작 부분에서 이미 선언됨
	if (!Skeleton || Skeleton->GetBoneCount() == 0)
	{
		UE_LOG("[FBX] No skeleton detected - applying Static Mesh transform (Unreal Engine style)");

		// UNREAL ENGINE 방식: ComputeTotalMatrix() 구현
		// bTransformVertexToAbsolute = true (Static Mesh 기본값)
		// TotalMatrix = GlobalTransform * Geometry

		// Geometry Transform 가져오기 (Pivot/Offset)
		FbxVector4 GeoTranslation = MeshNodePtr->GetGeometricTranslation(FbxNode::eSourcePivot);
		FbxVector4 GeoRotation = MeshNodePtr->GetGeometricRotation(FbxNode::eSourcePivot);
		FbxVector4 GeoScaling = MeshNodePtr->GetGeometricScaling(FbxNode::eSourcePivot);
		FbxAMatrix GeometryTransform(GeoTranslation, GeoRotation, GeoScaling);

		// Global Transform 가져오기 (ConvertScene 후의 값)
		FbxAMatrix GlobalTransform = Scene->GetAnimationEvaluator()->GetNodeGlobalTransform(MeshNodePtr);

		// Total Transform = GlobalTransform * Geometry (Unreal Engine 방식)
		FbxAMatrix TotalTransform = GlobalTransform * GeometryTransform;

		// Normal/Tangent용 Transform (Inverse Transpose)
		FbxAMatrix TotalTransformForNormal = TotalTransform.Inverse();
		TotalTransformForNormal = TotalTransformForNormal.Transpose();

		UE_LOG("[FBX] Global Transform - T:(%.3f, %.3f, %.3f) R:(%.3f, %.3f, %.3f) S:(%.3f, %.3f, %.3f)",
			GlobalTransform.GetT()[0], GlobalTransform.GetT()[1], GlobalTransform.GetT()[2],
			GlobalTransform.GetR()[0], GlobalTransform.GetR()[1], GlobalTransform.GetR()[2],
			GlobalTransform.GetS()[0], GlobalTransform.GetS()[1], GlobalTransform.GetS()[2]);

		UE_LOG("[FBX] Geometry Transform - T:(%.3f, %.3f, %.3f) R:(%.3f, %.3f, %.3f) S:(%.3f, %.3f, %.3f)",
			GeoTranslation[0], GeoTranslation[1], GeoTranslation[2],
			GeoRotation[0], GeoRotation[1], GeoRotation[2],
			GeoScaling[0], GeoScaling[1], GeoScaling[2]);

		// Vertex 변환 적용 (Unreal Engine 방식)
		for (auto& Vertex : OutMeshData.Vertices)
		{
			// 1. TotalTransform 적용 (FBX Space)
			FbxVector4 FbxPos(Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z, 1.0);
			FbxVector4 TransformedPos = TotalTransform.MultT(FbxPos);

			// 2. ConvertPos() - Y축 반전 (RightHanded → LeftHanded)
			Vertex.Position = ConvertFbxPosition(TransformedPos);

			// Normal 변환 (Inverse Transpose Transform 사용)
			FbxVector4 FbxNormal(Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, 0.0);
			FbxVector4 TransformedNormal = TotalTransformForNormal.MultT(FbxNormal);
			Vertex.Normal = ConvertFbxDirection(TransformedNormal);

			// Tangent 변환 (Inverse Transpose Transform 사용)
			FbxVector4 FbxTangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z, 0.0);
			FbxVector4 TransformedTangent = TotalTransformForNormal.MultT(FbxTangent);
			FVector Tangent3D = ConvertFbxDirection(TransformedTangent);
			Vertex.Tangent = FVector4(Tangent3D.X, Tangent3D.Y, Tangent3D.Z, Vertex.Tangent.W);
		}

		// CRITICAL DIFFERENCE: Mundi vs Unreal Engine Winding Order
		// - Unreal Engine: CCW = Front Face (FrontCounterClockwise = TRUE)
		// - Mundi Engine: CW = Front Face (FrontCounterClockwise = FALSE, D3D11 기본)
		//
		// Y-flip은 Handedness만 변경 (RH→LH), Winding Order는 변경 안 함 (CCW 유지)
		// 따라서 Mundi는 CCW→CW 변환을 위해 Index Reversal 필요!
		for (size_t i = 0; i < OutMeshData.Indices.Num(); i += 3)
		{
			std::swap(OutMeshData.Indices[i], OutMeshData.Indices[i + 2]);  // [0,1,2] → [2,1,0] (CCW → CW)
		}

		UE_LOG("[FBX] Static Mesh transform complete. Vertex count: %d", OutMeshData.Indices.Num());
		UE_LOG("[FBX] Triangle indices reversed (CCW → CW) for Mundi winding order");
	}

	return true;
}

bool FFbxImporter::ExtractSkinWeights(FbxMesh* Mesh, FSkeletalMesh& OutMeshData)
{
	if (!Mesh)
	{
		SetError("ExtractSkinWeights: Invalid parameters");
		return false;
	}

	USkeleton* Skeleton = OutMeshData.Skeleton;
	if (!Skeleton)
	{
		SetError("ExtractSkinWeights: FSkeletalMesh has no Skeleton");
		return false;
	}

	UE_LOG("[FBX] Extracting skin weights...");

	// Skin Deformer 가져오기
	int32 DeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	if (DeformerCount == 0)
	{
		UE_LOG("[FBX] Warning: Mesh has no skin deformer. All vertices will use bone index 0.");
		return true; // 에러는 아니지만 skin weights가 없음
	}

	// 첫 번째 Skin Deformer 사용
	FbxSkin* SkinDeformer = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
	if (!SkinDeformer)
	{
		SetError("ExtractSkinWeights: Failed to get skin deformer");
		return false;
	}

	int32 ClusterCount = SkinDeformer->GetClusterCount();
	UE_LOG("[FBX] Skin has %d clusters (bones)", ClusterCount);

	// IMPORTANT: Geometry Transform 추출 (Unreal Engine 방식)
	// Mesh Node의 Geometric Translation/Rotation/Scaling
	FbxNode* MeshNode = Mesh->GetNode();
	FbxVector4 GeoTranslation = MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	FbxVector4 GeoRotation = MeshNode->GetGeometricRotation(FbxNode::eSourcePivot);
	FbxVector4 GeoScaling = MeshNode->GetGeometricScaling(FbxNode::eSourcePivot);
	FbxAMatrix GeometryTransform(GeoTranslation, GeoRotation, GeoScaling);

	UE_LOG("[FBX] Geometry Transform - T:(%.3f, %.3f, %.3f) R:(%.3f, %.3f, %.3f) S:(%.3f, %.3f, %.3f)",
		GeoTranslation[0], GeoTranslation[1], GeoTranslation[2],
		GeoRotation[0], GeoRotation[1], GeoRotation[2],
		GeoScaling[0], GeoScaling[1], GeoScaling[2]);

	// Control Point → Bone Influences 매핑
	// FBX는 Control Point 기준으로 Bone Weight를 저장
	// 하지만 우리는 Polygon Vertex 기준으로 저장해야 함

	int32 ControlPointCount = Mesh->GetControlPointsCount();

	// Control Point별 Bone Influences 저장
	struct FControlPointInfluence
	{
		TArray<int32> BoneIndices;
		TArray<float> Weights;
	};

	TArray<FControlPointInfluence> ControlPointInfluences;
	ControlPointInfluences.SetNum(ControlPointCount);

	// CRITICAL: Mesh Transform을 사용해 Vertex를 Global Space로 변환
	// 첫 번째 Cluster에서 Mesh Transform을 가져와서 모든 Vertex에 적용
	FbxAMatrix MeshGlobalTransform;
	bool bMeshTransformExtracted = false;

	// 각 Cluster(Bone) 순회
	for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
	{
		FbxCluster* Cluster = SkinDeformer->GetCluster(ClusterIndex);
		if (!Cluster)
			continue;

		// Cluster가 영향을 주는 Bone 찾기
		FbxNode* LinkNode = Cluster->GetLink();
		if (!LinkNode)
			continue;

		FString BoneName = LinkNode->GetName();
		int32 BoneIndex = Skeleton->FindBoneIndex(BoneName);

		if (BoneIndex < 0)
		{
			UE_LOG("[FBX] Warning: Bone '%s' not found in skeleton", BoneName.c_str());
			continue;
		}

		// UNREAL ENGINE 방식: Cluster API에서 Bind Pose Transform 가져오기
		// Cluster->GetTransformLinkMatrix()는 Bind Pose 시점의 Bone Global Transform
		// Cluster->GetTransformMatrix()는 Bind Pose 시점의 Mesh Global Transform
		// 이 값들은 ConvertScene() 후의 값이다 (FBX SDK가 자동 처리)
		FbxAMatrix TransformLinkMatrix;
		FbxAMatrix TransformMatrix;
		Cluster->GetTransformLinkMatrix(TransformLinkMatrix);  // Bone Global at Bind Pose
		Cluster->GetTransformMatrix(TransformMatrix);          // Mesh Global at Bind Pose

		// 디버그: 첫 번째 Bone의 변환된 Transform 출력
		if (BoneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] === First Bone Cluster Transform Analysis ===");
			UE_LOG("[FBX DEBUG] Bone Name: %s", BoneName.c_str());

			// TransformLinkMatrix (Bone Global Transform) 출력
			UE_LOG("[FBX DEBUG] TransformLinkMatrix (Bone Global):");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					TransformLinkMatrix.Get(row, 0),
					TransformLinkMatrix.Get(row, 1),
					TransformLinkMatrix.Get(row, 2),
					TransformLinkMatrix.Get(row, 3));
			}

			// TransformMatrix (Mesh Global Transform) 출력
			UE_LOG("[FBX DEBUG] TransformMatrix (Mesh Global):");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					TransformMatrix.Get(row, 0),
					TransformMatrix.Get(row, 1),
					TransformMatrix.Get(row, 2),
					TransformMatrix.Get(row, 3));
			}

			// GeometryTransform 출력
			UE_LOG("[FBX DEBUG] GeometryTransform:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					GeometryTransform.Get(row, 0),
					GeometryTransform.Get(row, 1),
					GeometryTransform.Get(row, 2),
					GeometryTransform.Get(row, 3));
			}
		}

		// 첫 번째 Cluster 처리 시: Vertex를 Mesh Global Space로 변환
		if (!bMeshTransformExtracted)
		{
			MeshGlobalTransform = TransformMatrix;
			bMeshTransformExtracted = true;

			// UNREAL ENGINE 방식: Vertex들을 Mesh Global Space로 변환
			// TransformMatrix는 Bind Pose에서의 Mesh Global Transform
			// GeometryTransform은 Mesh의 Pivot/Geometry Offset
			// Vertex는 현재 Component Space (Raw FBX)에 있으므로
			// (TransformMatrix × GeometryTransform)을 적용해서 Global Space로 이동
			FbxAMatrix TotalTransform = TransformMatrix * GeometryTransform;

			// Odd Negative Scale 확인 (Unreal Engine 방식)
			bool bOddNegativeScale = IsOddNegativeScale(TotalTransform);

			// Normal/Tangent용 Transform (Inverse Transpose)
			FbxAMatrix NormalTransform = TotalTransform;
			NormalTransform.SetT(FbxVector4(0, 0, 0, 0));

			UE_LOG("[FBX] Transforming vertices to Mesh Global Space (Unreal Engine style)");

			TArray<FSkinnedVertex>& Vertices = OutMeshData.Vertices;

			for (auto& Vertex : Vertices)
			{
				// Position 변환 (FBX Transform 적용 후 ConvertFbxPosition으로 Y축 반전)
				FbxVector4 FbxPos;
				FbxPos.Set(Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z, 1.0);
				FbxVector4 TransformedPos = TotalTransform.MultT(FbxPos);
				Vertex.Position = ConvertFbxPosition(TransformedPos);

				// Normal 변환 (FBX Transform 적용 후 ConvertFbxDirection으로 Y축 반전)
				FbxVector4 FbxNormal;
				FbxNormal.Set(Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, 0.0);
				FbxVector4 TransformedNormal = NormalTransform.MultT(FbxNormal);
				Vertex.Normal = ConvertFbxDirection(TransformedNormal);

				// Tangent 변환 (FBX Transform 적용 후 ConvertFbxDirection으로 Y축 반전)
				FbxVector4 FbxTangent;
				FbxTangent.Set(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z, 0.0);
				FbxVector4 TransformedTangent = NormalTransform.MultT(FbxTangent);
				FVector Tangent3D = ConvertFbxDirection(TransformedTangent);
				Vertex.Tangent = FVector4(Tangent3D.X, Tangent3D.Y, Tangent3D.Z, Vertex.Tangent.W);
			}

			UE_LOG("[FBX] Vertex transformation complete. Vertex count: %d", Vertices.Num());

			// CRITICAL DIFFERENCE: Mundi vs Unreal Engine Winding Order
			// - Unreal Engine: CCW = Front Face (FrontCounterClockwise = TRUE)
			// - Mundi Engine: CW = Front Face (FrontCounterClockwise = FALSE, D3D11 기본)
			//
			// Y-flip은 Handedness만 변경 (RH→LH), Winding Order는 변경 안 함 (CCW 유지)
			// 따라서 Mundi는 CCW→CW 변환을 위해 Index Reversal 필요!
			TArray<uint32>& IndicesRef = OutMeshData.Indices;
			for (size_t i = 0; i < IndicesRef.Num(); i += 3)
			{
				std::swap(IndicesRef[i], IndicesRef[i + 2]);  // [0,1,2] → [2,1,0] (CCW → CW)
			}

			UE_LOG("[FBX] Triangle indices reversed (CCW → CW) for Mundi winding order");
		}

		// UNREAL ENGINE 방식: Global Bind Pose Matrix 저장 (Y축 반전 적용)
		// ConvertFbxMatrixWithYAxisFlip() 사용하여 Y축 선택적 반전
		FMatrix GlobalBindPoseMatrix = ConvertFbxMatrixWithYAxisFlip(FbxMatrix(TransformLinkMatrix));
		Skeleton->SetGlobalBindPoseMatrix(BoneIndex, GlobalBindPoseMatrix);

		// 디버그: 변환된 GlobalBindPoseMatrix 출력
		if (BoneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] After ConvertFbxMatrixWithYAxisFlip - GlobalBindPoseMatrix:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					GlobalBindPoseMatrix.M[row][0],
					GlobalBindPoseMatrix.M[row][1],
					GlobalBindPoseMatrix.M[row][2],
					GlobalBindPoseMatrix.M[row][3]);
			}
		}

		// UNREAL ENGINE 방식: Inverse Bind Pose Matrix 계산
		// Vertex는 Mesh Global Space에 있음 (ConvertFbxPosition으로 Y축 반전 적용됨)
		// InverseBindPose = BoneGlobal^-1 (단순 역행렬)
		// ConvertFbxMatrixWithYAxisFlip()를 Inverse에도 적용하여 Y축 반전 일관성 유지
		//
		// Skinning 공식: SkinnedVertex = GlobalSpaceVertex × (InverseBindPose × BoneTransform)
		//
		// Bind Pose 검증:
		// InverseBindPose × GlobalBindPose = BoneGlobal^-1 × BoneGlobal = Identity
		FbxAMatrix InverseBindMatrix = TransformLinkMatrix.Inverse();

		// Skeleton에 Inverse Bind Pose Matrix 설정 (Y축 반전 적용)
		FMatrix InverseBindPoseMatrix = ConvertFbxMatrixWithYAxisFlip(FbxMatrix(InverseBindMatrix));
		Skeleton->SetInverseBindPoseMatrix(BoneIndex, InverseBindPoseMatrix);

		// 디버그: 변환된 InverseBindPoseMatrix 출력
		if (BoneIndex == 0)
		{
			UE_LOG("[FBX DEBUG] After ConvertFbxMatrixWithYAxisFlip - InverseBindPoseMatrix:");
			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					InverseBindPoseMatrix.M[row][0],
					InverseBindPoseMatrix.M[row][1],
					InverseBindPoseMatrix.M[row][2],
					InverseBindPoseMatrix.M[row][3]);
			}

			// InverseBindPose × GlobalBindPose 계산
			// 디버그 목적: Bind Pose에서 어떤 변환이 적용되는지 확인
			// InverseBindPose × BoneGlobal = BoneGlobal^-1 × BoneGlobal = Identity (이론적으로)
			FMatrix TestMatrix = InverseBindPoseMatrix * GlobalBindPoseMatrix;
			UE_LOG("[FBX DEBUG] InverseBindPose × GlobalBindPose (should be Identity):");

			for (int row = 0; row < 4; row++)
			{
				UE_LOG("[FBX DEBUG]   Row %d: (%.6f, %.6f, %.6f, %.6f)",
					row,
					TestMatrix.M[row][0],
					TestMatrix.M[row][1],
					TestMatrix.M[row][2],
					TestMatrix.M[row][3]);
			}
		}

		UE_LOG("[FBX] Set bind poses for bone [%d]: %s (Global + Inverse from Cluster)", BoneIndex, BoneName.c_str());

		// Cluster의 Control Point Indices와 Weights 가져오기
		int32* ControlPointIndices = Cluster->GetControlPointIndices();
		double* Weights = Cluster->GetControlPointWeights();
		int32 IndexCount = Cluster->GetControlPointIndicesCount();

		// Control Point별로 Bone Influence 추가
		for (int32 i = 0; i < IndexCount; i++)
		{
			int32 CpIndex = ControlPointIndices[i];
			float Weight = static_cast<float>(Weights[i]);

			if (CpIndex >= 0 && CpIndex < ControlPointCount && Weight > 0.0f)
			{
				ControlPointInfluences[CpIndex].BoneIndices.Add(BoneIndex);
				ControlPointInfluences[CpIndex].Weights.Add(Weight);
			}
		}
	}

	// 이제 FSkeletalMesh의 각 Vertex에 Bone Weight 적용
	// ExtractMeshData에서 생성한 Vertex들을 가져와서 Bone Weights를 적용

	// 기존 Vertices 가져오기 (수정 가능)
	TArray<FSkinnedVertex>& Vertices = OutMeshData.Vertices;
	const TArray<int32>& VertexToControlPointMap = OutMeshData.VertexToControlPointMap;

	// 매핑 데이터 검증
	if (Vertices.Num() != VertexToControlPointMap.Num())
	{
		SetError("ExtractSkinWeights: Vertex count mismatch with control point map");
		return false;
	}

	if (Vertices.IsEmpty())
	{
		UE_LOG("[FBX] Warning: No vertices to apply skin weights");
		return true;
	}

	// 각 Vertex에 Bone Weights 적용
	for (size_t VertIndex = 0; VertIndex < Vertices.Num(); VertIndex++)
	{
		int32 ControlPointIndex = VertexToControlPointMap[VertIndex];

		// Control Point Index 범위 검증
		if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
		{
			UE_LOG("[FBX] Warning: Invalid control point index %d for vertex %zu", ControlPointIndex, VertIndex);
			continue;
		}

		// 이 Control Point의 Bone Influences 가져오기
		const FControlPointInfluence& Influence = ControlPointInfluences[ControlPointIndex];

		// 최대 4개의 Bone Influence만 사용
		int32 InfluenceCount = std::min(static_cast<int32>(Influence.BoneIndices.Num()), 4);

		// Weight 정규화를 위한 총합 계산
		float TotalWeight = 0.0f;
		for (int32 i = 0; i < InfluenceCount; i++)
		{
			TotalWeight += Influence.Weights[i];
		}

		// Bone Indices와 Weights 설정
		for (int32 i = 0; i < 4; i++)
		{
			if (i < InfluenceCount && TotalWeight > 0.0f)
			{
				Vertices[VertIndex].BoneIndices[i] = Influence.BoneIndices[i];
				Vertices[VertIndex].BoneWeights[i] = Influence.Weights[i] / TotalWeight; // 정규화
			}
			else
			{
				Vertices[VertIndex].BoneIndices[i] = 0;
				Vertices[VertIndex].BoneWeights[i] = 0.0f;
			}
		}
	}

	UE_LOG("[FBX] Applied skin weights to %zu vertices", Vertices.Num());

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
	int32 PoseCount = Scene->GetPoseCount();

	if (PoseCount == 0)
	{
		UE_LOG("[FBX] Warning: No bind pose found in scene");
		return true; // Bind Pose가 없어도 에러는 아님 (Local Transform 사용)
	}

	UE_LOG("[FBX] Scene has %d poses", PoseCount);

	// 첫 번째 Bind Pose 사용 (보통 하나만 존재)
	FbxPose* BindPose = nullptr;

	for (int32 i = 0; i < PoseCount; i++)
	{
		FbxPose* Pose = Scene->GetPose(i);
		if (Pose && Pose->IsBindPose())
		{
			BindPose = Pose;
			UE_LOG("[FBX] Found bind pose");
			break;
		}
	}

	if (!BindPose)
	{
		UE_LOG("[FBX] Warning: No bind pose found, using local transforms");
		return true;
	}

	// Bind Pose의 각 Node에 대한 Transform 정보 추출
	int32 PoseNodeCount = BindPose->GetCount();
	UE_LOG("[FBX] Bind pose has %d nodes", PoseNodeCount);

	for (int32 i = 0; i < PoseNodeCount; i++)
	{
		FbxNode* Node = BindPose->GetNode(i);
		if (!Node)
			continue;

		FString NodeName = Node->GetName();

		// Skeleton에서 이 Bone 찾기
		int32 BoneIndex = OutSkeleton->FindBoneIndex(NodeName);
		if (BoneIndex < 0)
		{
			// Skeleton에 없는 노드는 무시 (Bone이 아닐 수 있음)
			continue;
		}

		// Bind Pose Matrix 가져오기
		// IMPORTANT: ConvertScene() 적용 후의 Transform 사용
		// BindPose->GetMatrix(i)는 변환 전 원본 데이터이므로 사용하지 않음
		// Node->EvaluateGlobalTransform()은 ConvertScene() 적용 후의 Global Transform
		FbxAMatrix FbxBindMatrix = Node->EvaluateGlobalTransform();

		// CRITICAL: JointPostConversionMatrix 적용 (Unreal Engine 방식)
		// bForceFrontXAxis = true일 때만 적용됨 (기본값 Identity)
		// -Y Forward → +X Forward 변환 (SkeletalMesh Bone Hierarchy 전용)
		FbxAMatrix JointPostMatrix = FFbxDataConverter::GetJointPostConversionMatrix();
		FbxBindMatrix = FbxBindMatrix * JointPostMatrix;

		// Inverse Bind Pose Matrix 계산
		// Skinning 시 Vertex를 Bone Space로 변환하는데 사용
		FbxAMatrix FbxInverseBindMatrix = FbxBindMatrix.Inverse();

		// FbxAMatrix를 Mundi FMatrix로 변환 (Row-Major)
		FMatrix InverseBindPoseMatrix = ConvertFbxMatrix(FbxMatrix(FbxInverseBindMatrix));

		// Skeleton에 Inverse Bind Pose Matrix 설정
		OutSkeleton->SetInverseBindPoseMatrix(BoneIndex, InverseBindPoseMatrix);

		UE_LOG("[FBX] Set inverse bind pose for bone [%d]: %s", BoneIndex, NodeName.c_str());
	}

	UE_LOG("[FBX] Bind pose extraction completed");
	return true;
}

bool FFbxImporter::ExtractMaterials(FbxScene* InScene, USkeletalMesh* OutSkeletalMesh)
{
	if (!InScene || !OutSkeletalMesh)
	{
		SetError("ExtractMaterials: Invalid parameters");
		return false;
	}

	UE_LOG("[FBX] Extracting materials and loading textures...");

	int32 materialCount = InScene->GetMaterialCount();

	if (materialCount == 0)
	{
		UE_LOG("[FBX] Warning: Mesh has no materials");
		return true; // 에러는 아니지만 Material이 없음
	}

	UE_LOG("[FBX] Found %d materials", materialCount);

	// FBX 파일의 디렉토리 경로 구하기 (Scene 파일 경로 기반)
	FbxString fbxFilePath = Scene->GetDocumentInfo()->Url.Get();
	std::filesystem::path fbxDirAbsolute = std::filesystem::path(fbxFilePath.Buffer()).parent_path();

	// ResolveAssetRelativePath 사용하여 Data/ 기준 상대 경로로 변환
	FString fbxDirStr = ResolveAssetRelativePath(fbxDirAbsolute.string(), GDataDir);
	UE_LOG("[FBX] FBX directory (relative to Data/): %s", fbxDirStr.c_str());

	// Helper lambda: 텍스처 경로를 Data/ 기준 상대 경로로 변환
	auto ResolveTexturePath = [&](FbxFileTexture* Texture) -> FString
	{
		if (!Texture) return "";

		const char* TexturePath = Texture->GetFileName();
		FString TexturePathStr = TexturePath;

		// 상대 경로로 변환 시도
		const char* RelativeFileName = Texture->GetRelativeFileName();
		if (RelativeFileName && strlen(RelativeFileName) > 0)
		{
			TexturePathStr = RelativeFileName;
		}

		// 경로 구분자 정규화 (Unix: / → Windows: \)
		std::replace(TexturePathStr.begin(), TexturePathStr.end(), '/', '\\');

		// 절대 경로로 변환
		std::filesystem::path TexturePathFs = TexturePathStr;

		// 상대 경로인 경우 FBX 디렉토리 기준으로 절대 경로 만들기
		if (TexturePathFs.is_relative())
		{
			TexturePathFs = fbxDirAbsolute / TexturePathFs;
			TexturePathFs = TexturePathFs.lexically_normal(); // 경로 정규화
		}

		// ResolveAssetRelativePath로 Data/ 기준 경로로 변환
		FString FinalPath = ResolveAssetRelativePath(TexturePathFs.string(), GDataDir);
		UE_LOG("[FBX]   Resolved texture path: %s", FinalPath.c_str());

		return FinalPath;
	};

	// 모든 Material 처리
	for (int32 matIndex = 0; matIndex < materialCount; matIndex++)
	{
		FbxSurfaceMaterial* fbxMaterial = InScene->GetMaterial(matIndex);
		if (!fbxMaterial)
		{
			UE_LOG("[FBX] Warning: Failed to get material at index %d", matIndex);
			continue;
		}

		FString materialName = fbxMaterial->GetName();
		UE_LOG("[FBX] Processing material[%d]: %s", matIndex, materialName.c_str());

		// Material 정보 구조체 생성
		FMaterialInfo materialInfo;
		materialInfo.MaterialName = materialName;

		// Diffuse Texture 추출
		FbxProperty diffuseProp = fbxMaterial->FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (diffuseProp.IsValid())
		{
			int32 textureCount = diffuseProp.GetSrcObjectCount<FbxTexture>();

			for (int32 i = 0; i < textureCount; i++)
			{
				FbxFileTexture* texture = diffuseProp.GetSrcObject<FbxFileTexture>(i);
				if (texture)
				{
					materialInfo.DiffuseTextureFileName = ResolveTexturePath(texture);
					UE_LOG("[FBX] - Diffuse texture: %s", materialInfo.DiffuseTextureFileName.c_str());
				}
			}
		}

		// Normal Map Texture 추출
		FbxProperty normalProp = fbxMaterial->FindProperty(FbxSurfaceMaterial::sNormalMap);
		if (normalProp.IsValid())
		{
			int32 textureCount = normalProp.GetSrcObjectCount<FbxTexture>();

			for (int32 i = 0; i < textureCount; i++)
			{
				FbxFileTexture* texture = normalProp.GetSrcObject<FbxFileTexture>(i);
				if (texture)
				{
					materialInfo.NormalTextureFileName = ResolveTexturePath(texture);
					UE_LOG("[FBX] - Normal texture: %s", materialInfo.NormalTextureFileName.c_str());
				}
			}
		}

		// Bump Map을 Normal Map으로 사용 (일부 FBX는 Bump에 저장)
		if (materialInfo.NormalTextureFileName.empty())
		{
			FbxProperty bumpProp = fbxMaterial->FindProperty(FbxSurfaceMaterial::sBump);
			if (bumpProp.IsValid())
			{
				int32 textureCount = bumpProp.GetSrcObjectCount<FbxTexture>();

				for (int32 i = 0; i < textureCount; i++)
				{
					FbxFileTexture* texture = bumpProp.GetSrcObject<FbxFileTexture>(i);
					if (texture)
					{
						materialInfo.NormalTextureFileName = ResolveTexturePath(texture);
						UE_LOG("[FBX] - Bump texture (as Normal): %s", materialInfo.NormalTextureFileName.c_str());
					}
				}
			}
		}

		// Diffuse Color 추출
		if (fbxMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* lambert = static_cast<FbxSurfaceLambert*>(fbxMaterial);
			FbxDouble3 diffuse = lambert->Diffuse.Get();
			materialInfo.DiffuseColor = FVector(
				static_cast<float>(diffuse[0]),
				static_cast<float>(diffuse[1]),
				static_cast<float>(diffuse[2])
			);
			UE_LOG("[FBX] - Diffuse color: (%.3f, %.3f, %.3f)",
				materialInfo.DiffuseColor.X,
				materialInfo.DiffuseColor.Y,
				materialInfo.DiffuseColor.Z);
		}

		// === Material 객체 생성 ===
		UE_LOG("[FBX] Creating Material: '%s'", materialInfo.MaterialName.c_str());
		UE_LOG("[FBX] - DiffuseTextureFileName: '%s'", materialInfo.DiffuseTextureFileName.c_str());
		UE_LOG("[FBX] - NormalTextureFileName: '%s'", materialInfo.NormalTextureFileName.c_str());

		// Material 동적 생성
		UMaterial* material = NewObject<UMaterial>();
		if (!material)
		{
			UE_LOG("[FBX] Warning: Failed to create Material object for '%s'", materialInfo.MaterialName.c_str());
			continue;
		}

		// MaterialInfo 설정 → 내부에서 ResolveTextures() 자동 호출
		material->SetMaterialInfo(materialInfo);

		// UberLit Shader 설정
		UShader* uberlitShader = UResourceManager::GetInstance().Load<UShader>("Shaders/Materials/UberLit.hlsl");
		if (uberlitShader)
		{
			material->SetShader(uberlitShader);
			UE_LOG("[FBX] UberLit shader set successfully");
		}
		else
		{
			UE_LOG("[FBX] Warning: Failed to load UberLit shader");
		}

		// ResourceManager에 등록
		UResourceManager::GetInstance().Add<UMaterial>(materialInfo.MaterialName, material);
		UE_LOG("[FBX] Material registered to ResourceManager: '%s'", materialInfo.MaterialName.c_str());

		// SkeletalMesh에 Material 이름 추가
		OutSkeletalMesh->AddMaterialName(materialInfo.MaterialName);
	}

	UE_LOG("[FBX] Material extraction completed: %d materials processed", materialCount);
	return true;
}

bool FFbxImporter::ExtractMaterialsFromScene(USkeletalMesh* OutSkeletalMesh)
{
	if (!Scene || !OutSkeletalMesh)
	{
		SetError("ExtractMaterialsFromScene: Invalid Scene or SkeletalMesh");
		return false;
	}

	// Scene에서 첫 번째 Mesh Node 찾기
	FbxNode* MeshNode = FindFirstMeshNode();
	if (!MeshNode)
	{
		SetError("ExtractMaterialsFromScene: No mesh node found in scene");
		return false;
	}

	// ExtractMaterials() 호출
	return ExtractMaterials(Scene, OutSkeletalMesh);
}

// Helper: FbxMatrix → FMatrix 변환 (Unreal Engine 스타일 - Y축 선택적 반전)
FMatrix FFbxImporter::ConvertFbxMatrixWithYAxisFlip(const FbxMatrix& FbxMatrix)
{
	FMatrix Matrix;

	// Unreal Engine ConvertMatrix 방식
	// Y축을 선택적으로 반전하여 Right-Handed → Left-Handed 변환
	// 이 변환이 Winding Order를 자동으로 올바르게 유지함
	for (int32 row = 0; row < 4; row++)
	{
		if (row == 1)  // Y축 Row만 특별 처리
		{
			Matrix.M[row][0] = -static_cast<float>(FbxMatrix.Get(row, 0));  // Y축 X 성분 반전
			Matrix.M[row][1] = static_cast<float>(FbxMatrix.Get(row, 1));   // Y축 Y 성분 유지
			Matrix.M[row][2] = -static_cast<float>(FbxMatrix.Get(row, 2));  // Y축 Z 성분 반전
			Matrix.M[row][3] = -static_cast<float>(FbxMatrix.Get(row, 3));  // Y축 W 성분 반전
		}
		else  // X, Z, W Row
		{
			Matrix.M[row][0] = static_cast<float>(FbxMatrix.Get(row, 0));   // X 성분 유지
			Matrix.M[row][1] = -static_cast<float>(FbxMatrix.Get(row, 1));  // Y 성분만 반전
			Matrix.M[row][2] = static_cast<float>(FbxMatrix.Get(row, 2));   // Z 성분 유지
			Matrix.M[row][3] = static_cast<float>(FbxMatrix.Get(row, 3));   // W 성분 유지
		}
	}

	return Matrix;
}

// Helper: FbxMatrix → FMatrix 변환 (기존 방식 - 직접 복사)
FMatrix FFbxImporter::ConvertFbxMatrix(const FbxMatrix& FbxMatrix)
{
	FMatrix Matrix;

	// FBX는 Row-Major, Mundi도 Row-Major이므로 직접 복사 가능
	for (int32 row = 0; row < 4; row++)
	{
		for (int32 col = 0; col < 4; col++)
		{
			Matrix.M[row][col] = static_cast<float>(FbxMatrix.Get(row, col));
		}
	}

	return Matrix;
}

// Helper: FbxVector4 Position → FVector 변환 (Y축 반전)
FVector FFbxImporter::ConvertFbxPosition(const FbxVector4& Pos)
{
	return FVector(
		static_cast<float>(Pos[0]),
		-static_cast<float>(Pos[1]),  // Y 반전
		static_cast<float>(Pos[2])
	);
}

// Helper: FbxVector4 Direction → FVector 변환 (Y축 반전)
FVector FFbxImporter::ConvertFbxDirection(const FbxVector4& Dir)
{
	FVector Result(
		static_cast<float>(Dir[0]),
		-static_cast<float>(Dir[1]),  // Y 반전
		static_cast<float>(Dir[2])
	);

	// Direction Vector는 정규화 필요
	float Length = std::sqrt(Result.X * Result.X + Result.Y * Result.Y + Result.Z * Result.Z);
	if (Length > 1e-8f)
	{
		Result.X /= Length;
		Result.Y /= Length;
		Result.Z /= Length;
	}

	return Result;
}

// Helper: FbxQuaternion → FQuat 변환 (Y, W 반전)
FQuat FFbxImporter::ConvertFbxQuaternion(const FbxQuaternion& Q)
{
	return FQuat(
		static_cast<float>(Q[0]),
		-static_cast<float>(Q[1]),  // Y 반전
		static_cast<float>(Q[2]),
		-static_cast<float>(Q[3])   // W 반전
	);
}

// Helper: FbxVector4 Scale → FVector 변환 (변환 없음)
FVector FFbxImporter::ConvertFbxScale(const FbxVector4& Scale)
{
	return FVector(
		static_cast<float>(Scale[0]),
		static_cast<float>(Scale[1]),
		static_cast<float>(Scale[2])
	);
}

// Helper: Odd Negative Scale 확인 (Unreal Engine 방식)
bool FFbxImporter::IsOddNegativeScale(const FbxAMatrix& TotalMatrix)
{
	FbxVector4 Scale = TotalMatrix.GetS();
	int32 NegativeNum = 0;

	if (Scale[0] < 0) NegativeNum++;
	if (Scale[1] < 0) NegativeNum++;
	if (Scale[2] < 0) NegativeNum++;

	return NegativeNum == 1 || NegativeNum == 3;
}

void FFbxImporter::SetError(const FString& Message)
{
	LastErrorMessage = Message;
	FString ErrorMsg = "[FBX ERROR] " + Message + "\n";
	OutputDebugStringA(ErrorMsg.c_str());
}

