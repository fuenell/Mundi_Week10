#include "pch.h"
#include "MeshLoader.h"
#include "SkeletalMesh.h"

// FBX SDK
#include <fbxsdk.h>

#ifdef IOS_REF
#undef IOS_REF
#define IOS_REF (*(pManager->GetIOSettings()))
#endif

IMPLEMENT_CLASS(UMeshLoader)

UMeshLoader& UMeshLoader::GetInstance()
{
    static UMeshLoader* Instance = nullptr;
    if (Instance == nullptr)
    {
        Instance = NewObject<UMeshLoader>();
    }
    return *Instance;
}

UMeshLoader::FFace UMeshLoader::ParseFaceBuffer(const FString& FaceBuffer)
{
    FFace Face = {};
    std::istringstream Tokenizer(FaceBuffer);
    FString IndexBuffer;

    // v (항상 존재)
    if (std::getline(Tokenizer, IndexBuffer, '/'))
        Face.IndexPosition = std::stoi(IndexBuffer);

    // vt (항상 존재)
    if (std::getline(Tokenizer, IndexBuffer, '/'))
    {
        if (!IndexBuffer.empty())
            Face.IndexTexCoord = std::stoi(IndexBuffer);
    }

    return Face;
}

UMeshLoader::~UMeshLoader()
{
    for (auto& it : MeshCache)
    {
        if (it.second)
        {
            delete it.second;
        }
        it.second = nullptr;
    }
    MeshCache.Empty();
}
FMeshData* UMeshLoader::LoadMesh(const std::filesystem::path& FilePath)
{
    auto it = MeshCache.find(FilePath.string());
    if (it != MeshCache.end())
    {
        return it->second;
    }

    if (!std::filesystem::exists(FilePath))
        return nullptr;

    std::ifstream File(FilePath);
    if (!File)
        return nullptr;

    TArray<FPosition> Positions;
    TArray<FNormal> Normals;
    TArray<FTexCoord> TexCoords;
    TArray<FFace> Faces;

    FString Line;
    while (std::getline(File, Line))
    {
        std::istringstream Tokenizer(Line);

        FString Prefix;
        Tokenizer >> Prefix;

        if (Prefix == "v") // position
        {
            FPosition Position;
            Tokenizer >> Position.x >> Position.y >> Position.z;
            Positions.push_back(Position);
        }
        else if (Prefix == "vn") // normal
        {
            FNormal Normal;
            Tokenizer >> Normal.x >> Normal.y >> Normal.z;
            Normals.push_back(Normal);
        }
        else if (Prefix == "vt") // uv
        {
            FTexCoord TexCoord;
            Tokenizer >> TexCoord.u >> TexCoord.v;
            TexCoords.push_back(TexCoord);
        }
        else if (Prefix == "f") // face
        {
            FString FaceBuffer;
            while (Tokenizer >> FaceBuffer)
            {
                Faces.push_back(ParseFaceBuffer(FaceBuffer));
            }
        }
        else if (Prefix == "l") // line (바운딩박스 OBJ용)
        {
            FString LineBuffer;
            while (Tokenizer >> LineBuffer)
            {
                Faces.push_back(ParseFaceBuffer(LineBuffer));
            }
        }
    }
    FMeshData* MeshData = new FMeshData();
    TMap<FVertexKey, uint32> UniqueVertexMap;

    for (const auto& Face : Faces)
    {
        const FPosition& Pos = Positions[Face.IndexPosition - 1];
        FVertexKey key{ Pos.x, Pos.y, Pos.z };

        auto it = UniqueVertexMap.find(key);
        if (it != UniqueVertexMap.end())
        {
            MeshData->Indices.push_back(it->second);
        }
        else
        {
            uint32 newIndex = static_cast<uint32>(MeshData->Vertices.size());

            // 위치 데이터 추가
            MeshData->Vertices.push_back(FVector(Pos.x, Pos.y, Pos.z));
            
            // 랜덤 색상 추가
            MeshData->Color.push_back(FVector4(
                static_cast<float>(rand()) / RAND_MAX,
                static_cast<float>(rand()) / RAND_MAX,
                static_cast<float>(rand()) / RAND_MAX,
                1.0f
            ));
            
            /*if (Face.IndexNormal > 0 && Face.IndexNormal <= Normals.size())
            {
                const FNormal& Normal = Normals[Face.IndexNormal - 1];
                MeshData->Normal.push_back(FVector(Normal.x, Normal.y, Normal.z));
            }*/

            MeshData->Indices.push_back(newIndex);
            UniqueVertexMap[key] = newIndex;
        }
    }

    MeshCache[FilePath.string()] = MeshData;

    return MeshData;
}

void UMeshLoader::AddMeshData(const FString& InName, FMeshData* InMeshData)
{
    if (MeshCache.Contains(InName))
    {
        delete MeshCache[InName];
    }
    MeshCache[InName] = InMeshData;
}

const TMap<FString, FMeshData*>* UMeshLoader::GetMeshCache()
{
    return &MeshCache;
}

// Helper: FbxMatrix를 FMatrix로 변환 (Row-major)
FMatrix FbxMatrixToFMatrix(const FbxAMatrix& FbxMat)
{
    FMatrix Result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            Result.M[row][col] = static_cast<float>(FbxMat.Get(row, col));
        }
    }
    return Result;
}

// Helper: 본 계층 구조 재귀 추출
void ProcessSkeletonHierarchy(FbxNode* InNode, int32 ParentIndex, FReferenceSkeleton& OutSkeleton, TMap<FbxNode*, int32>& NodeToBoneIndexMap)
{
    if (!InNode)
        return;

    FbxNodeAttribute* Attr = InNode->GetNodeAttribute();

    // Skeleton 또는 Mesh 노드만 처리
    if (Attr && (Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton ||
                 Attr->GetAttributeType() == FbxNodeAttribute::eMesh))
    {
        FBoneInfo BoneInfo;
        BoneInfo.BoneName = InNode->GetName();
        BoneInfo.ParentIndex = ParentIndex;

        // Local Transform (부모 기준)
        FbxAMatrix LocalTransform = InNode->EvaluateLocalTransform();
        BoneInfo.LocalTransform = FbxMatrixToFMatrix(LocalTransform);

        // Bind Pose (Global Transform)
        FbxAMatrix GlobalTransform = InNode->EvaluateGlobalTransform();
        FMatrix BindPose = FbxMatrixToFMatrix(GlobalTransform);

        // InverseBindPose
        BoneInfo.InverseBindPose = BindPose.InverseAffine();

        int32 BoneIndex = OutSkeleton.Bones.Num();
        OutSkeleton.Bones.Add(BoneInfo);
        NodeToBoneIndexMap.Add(InNode, BoneIndex);

        UE_LOG("  [Bone %d] Name: %s, Parent: %d", BoneIndex, BoneInfo.BoneName.c_str(), ParentIndex);

        // 자식 재귀 처리
        for (int i = 0; i < InNode->GetChildCount(); ++i)
        {
            ProcessSkeletonHierarchy(InNode->GetChild(i), BoneIndex, OutSkeleton, NodeToBoneIndexMap);
        }
    }
    else
    {
        // Skeleton이 아닌 노드도 자식 탐색
        for (int i = 0; i < InNode->GetChildCount(); ++i)
        {
            ProcessSkeletonHierarchy(InNode->GetChild(i), ParentIndex, OutSkeleton, NodeToBoneIndexMap);
        }
    }
}

FSkeletalMeshAsset* UMeshLoader::LoadFBXSkeletalMesh(const FString& FilePath)
{
    UE_LOG("========================================");
    UE_LOG("[FBX LOADER] Loading: %s", FilePath.c_str());
    UE_LOG("========================================");

    // 1. FBX SDK 초기화
    UE_LOG("[Step 1] Initializing FBX SDK...");
    FbxManager* SdkManager = FbxManager::Create();
    if (!SdkManager)
    {
        UE_LOG("ERROR: Unable to create FBX Manager!");
        return nullptr;
    }
    UE_LOG("  OK: FBX Manager created");

    // FBX 파일을 어떻게 읽을지 설정하는 옵션 매뉴얼
    FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
    SdkManager->SetIOSettings(ios);
    UE_LOG("  OK: IO Settings configured");

    // 2. Scene 생성 및 파일 로드
    UE_LOG("[Step 2] Loading FBX file...");
    FbxScene* Scene = FbxScene::Create(SdkManager, "MyScene");
    FbxImporter* Importer = FbxImporter::Create(SdkManager, "");

    if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
    {
        UE_LOG("ERROR: FBX Importer initialization failed!");
        UE_LOG("  Reason: %s", Importer->GetStatus().GetErrorString());
        Importer->Destroy();
        Scene->Destroy();
        SdkManager->Destroy();
        return nullptr;
    }
    UE_LOG("  OK: Importer initialized");

    if (!Importer->Import(Scene))
    {
        UE_LOG("ERROR: Failed to import FBX scene!");
        Importer->Destroy();
        Scene->Destroy();
        SdkManager->Destroy();
        return nullptr;
    }
    UE_LOG("  OK: Scene imported successfully");
    Importer->Destroy();

    // 3. Scene을 DirectX 좌표계로 변환
    UE_LOG("[Step 3] Converting coordinate system...");
    FbxAxisSystem::DirectX.ConvertScene(Scene);
    UE_LOG("  OK: Converted to DirectX coordinate system");

    // 삼각형 메시로 변환
    UE_LOG("[Step 4] Triangulating mesh...");
    FbxGeometryConverter GeomConverter(SdkManager);
    GeomConverter.Triangulate(Scene, true);
    UE_LOG("  OK: Mesh triangulated");

    // 4. SkeletalMeshAsset 생성
    FSkeletalMeshAsset* Asset = new FSkeletalMeshAsset();
    Asset->PathFileName = FilePath;

    // 5. Skeleton 추출
    UE_LOG("[Step 5] Extracting skeleton hierarchy...");
    TMap<FbxNode*, int32> NodeToBoneIndexMap;
    FbxNode* RootNode = Scene->GetRootNode();

    if (RootNode)
    {
        for (int i = 0; i < RootNode->GetChildCount(); ++i)
        {
            ProcessSkeletonHierarchy(RootNode->GetChild(i), -1, Asset->RefSkeleton, NodeToBoneIndexMap);
        }
    }

    UE_LOG("  OK: Skeleton extracted - %d bones found", Asset->RefSkeleton.Bones.Num());

    // 6. 첫 번째 메시 찾기
    UE_LOG("[Step 6] Searching for mesh node...");
    FbxNode* MeshNode = nullptr;
    std::function<FbxNode*(FbxNode*)> FindMeshNode = [&](FbxNode* Node) -> FbxNode* {
        if (Node->GetNodeAttribute() &&
            Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            return Node;
        }
        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            FbxNode* Found = FindMeshNode(Node->GetChild(i));
            if (Found) return Found;
        }
        return nullptr;
    };
    MeshNode = FindMeshNode(RootNode);

    if (!MeshNode)
    {
        UE_LOG("ERROR: No mesh found in FBX file!");
        delete Asset;
        Scene->Destroy();
        SdkManager->Destroy();
        return nullptr;
    }
    UE_LOG("  OK: Mesh node found - %s", MeshNode->GetName());

    FbxMesh* Mesh = MeshNode->GetMesh();
    if (!Mesh)
    {
        UE_LOG("ERROR: Failed to get mesh data from node!");
        delete Asset;
        Scene->Destroy();
        SdkManager->Destroy();
        return nullptr;
    }
    UE_LOG("  OK: Mesh data retrieved");

    // 7. 정점 데이터 추출
    UE_LOG("[Step 7] Extracting vertex data...");
    int VertexCount = Mesh->GetControlPointsCount();
    FbxVector4* ControlPoints = Mesh->GetControlPoints();
    UE_LOG("  Control points: %d", VertexCount);

    // 8. Skin 데이터 추출
    UE_LOG("[Step 8] Extracting skin deformer (bone weights)...");
    TMap<int32, TArray<TPair<int32, float>>> VertexBoneWeights; // VertexIndex -> [(BoneIndex, Weight)]

    int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    UE_LOG("  Skin deformers found: %d", SkinCount);

    if (SkinCount > 0)
    {
        FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin);
        int ClusterCount = Skin->GetClusterCount();
        UE_LOG("  Bone clusters: %d", ClusterCount);

        for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            FbxNode* LinkNode = Cluster->GetLink();
            if (!LinkNode)
                continue;

            // Bone Index 찾기
            int32 BoneIndex = -1;
            if (NodeToBoneIndexMap.Contains(LinkNode))
            {
                BoneIndex = NodeToBoneIndexMap[LinkNode];
            }

            if (BoneIndex == -1)
                continue;

            // Weights 추출
            int* Indices = Cluster->GetControlPointIndices();
            double* Weights = Cluster->GetControlPointWeights();
            int IndexCount = Cluster->GetControlPointIndicesCount();

            UE_LOG("    Cluster[%d]: Bone '%s' (Index: %d) affects %d vertices",
                ClusterIndex, LinkNode->GetName(), BoneIndex, IndexCount);

            for (int i = 0; i < IndexCount; ++i)
            {
                int32 VertexIndex = Indices[i];
                float Weight = static_cast<float>(Weights[i]);

                if (!VertexBoneWeights.Contains(VertexIndex))
                {
                    VertexBoneWeights.Add(VertexIndex, TArray<TPair<int32, float>>());
                }
                VertexBoneWeights[VertexIndex].Add(TPair<int32, float>(BoneIndex, Weight));
            }
        }
        UE_LOG("  OK: Bone weights extracted for %d vertices", VertexBoneWeights.Num());
    }
    else
    {
        UE_LOG("  WARNING: No skin deformer found - will use rigid binding to root bone");
    }

    // 9. 삼각형 데이터 추출
    UE_LOG("[Step 9] Building triangle mesh...");
    int PolygonCount = Mesh->GetPolygonCount();
    UE_LOG("  Polygons: %d", PolygonCount);

    TMap<FString, int32> VertexMap; // 중복 제거용

    for (int PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
    {
        int PolySize = Mesh->GetPolygonSize(PolyIndex);
        if (PolySize != 3)
            continue; // 삼각형만 처리

        for (int VertIndex = 0; VertIndex < 3; ++VertIndex)
        {
            FSkinnedVertex Vertex;

            // Position
            int ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, VertIndex);
            FbxVector4 Position = ControlPoints[ControlPointIndex];
            Vertex.Position = FVector(
                static_cast<float>(Position[0]),
                static_cast<float>(Position[1]),
                static_cast<float>(Position[2])
            );

            // Normal
            FbxVector4 Normal;
            Mesh->GetPolygonVertexNormal(PolyIndex, VertIndex, Normal);
            Vertex.Normal = FVector(
                static_cast<float>(Normal[0]),
                static_cast<float>(Normal[1]),
                static_cast<float>(Normal[2])
            ).GetNormalized();

            // UV
            FbxVector2 UV(0, 0);
            bool bUnmapped = false;
            if (Mesh->GetElementUVCount() > 0)
            {
                FbxGeometryElementUV* UVElement = Mesh->GetElementUV(0);
                int UVIndex = (UVElement->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    ? ControlPointIndex
                    : Mesh->GetTextureUVIndex(PolyIndex, VertIndex);
                UV = UVElement->GetDirectArray().GetAt(UVIndex);
            }
            Vertex.UV = FVector2D(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1])); // V 반전

            // Bone Weights
            for (int i = 0; i < 4; ++i)
            {
                Vertex.BoneIndices[i] = 0;
                Vertex.BoneWeights[i] = 0.0f;
            }

            if (VertexBoneWeights.Contains(ControlPointIndex))
            {
                TArray<TPair<int32, float>>& Weights = VertexBoneWeights[ControlPointIndex];

                // 가중치 정규화 및 상위 4개만 선택
                float TotalWeight = 0.0f;
                for (auto& W : Weights)
                    TotalWeight += W.second;

                // 가중치 큰 순서로 정렬
                Weights.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) {
                    return A.second > B.second;
                });

                // 상위 4개 본만 사용
                int Count = FMath::Min(4, (int)Weights.Num());
                float NormalizedTotal = 0.0f;
                for (int i = 0; i < Count; ++i)
                {
                    Vertex.BoneIndices[i] = Weights[i].first;
                    Vertex.BoneWeights[i] = Weights[i].second / TotalWeight;
                    NormalizedTotal += Vertex.BoneWeights[i];
                }

                // 가중치 합이 1.0이 되도록 보정
                if (NormalizedTotal > 0.0f)
                {
                    for (int i = 0; i < Count; ++i)
                    {
                        Vertex.BoneWeights[i] /= NormalizedTotal;
                    }
                }
            }
            else
            {
                // Skin이 없으면 루트 본에 고정
                Vertex.BoneIndices[0] = 0;
                Vertex.BoneWeights[0] = 1.0f;
            }

            // 인덱스 추가
            Asset->Vertices.Add(Vertex);
            Asset->Indices.Add(static_cast<uint32>(Asset->Vertices.Num() - 1));
        }
    }

    UE_LOG("  OK: Mesh built - %d vertices, %d indices (triangles: %d)",
        Asset->Vertices.Num(), Asset->Indices.Num(), Asset->Indices.Num() / 3);

    // 샘플 정점 정보 출력 (처음 3개)
    if (Asset->Vertices.Num() > 0)
    {
        UE_LOG("  Sample vertices (first 3):");
        int SampleCount = FMath::Min(3, (int)Asset->Vertices.Num());
        for (int i = 0; i < SampleCount; ++i)
        {
            const FSkinnedVertex& V = Asset->Vertices[i];
            UE_LOG("    Vertex[%d]: Pos(%.2f, %.2f, %.2f), Bones[%d,%d,%d,%d], Weights[%.3f,%.3f,%.3f,%.3f]",
                i, V.Position.X, V.Position.Y, V.Position.Z,
                V.BoneIndices[0], V.BoneIndices[1], V.BoneIndices[2], V.BoneIndices[3],
                V.BoneWeights[0], V.BoneWeights[1], V.BoneWeights[2], V.BoneWeights[3]);
        }
    }

    // 10. Material Group 기본 설정
    UE_LOG("[Step 10] Setting up material groups...");
    if (Asset->Indices.Num() > 0)
    {
        FGroupInfo Group;
        Group.StartIndex = 0;
        Group.IndexCount = Asset->Indices.Num();
        Asset->GroupInfos.Add(Group);
        Asset->bHasMaterial = false;
        UE_LOG("  OK: 1 material group created (all triangles)");
    }

    // Cleanup
    Scene->Destroy();
    SdkManager->Destroy();

    UE_LOG("========================================");
    UE_LOG("[FBX LOADER] SUCCESS!");
    UE_LOG("  File: %s", FilePath.c_str());
    UE_LOG("  Bones: %d", Asset->RefSkeleton.Bones.Num());
    UE_LOG("  Vertices: %d", Asset->Vertices.Num());
    UE_LOG("  Indices: %d", Asset->Indices.Num());
    UE_LOG("  Triangles: %d", Asset->Indices.Num() / 3);
    UE_LOG("========================================");
    return Asset;
}

UMeshLoader::UMeshLoader()
{
}
