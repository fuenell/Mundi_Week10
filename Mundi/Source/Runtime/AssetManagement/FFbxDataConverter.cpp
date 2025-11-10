#include "pch.h"
#include "FFbxDataConverter.h"

// Static 멤버 초기화
FbxAMatrix FFbxDataConverter::AxisConversionMatrix;
FbxAMatrix FFbxDataConverter::AxisConversionMatrixInv;
bool FFbxDataConverter::bIsInitialized = false;

FbxAMatrix FFbxDataConverter::JointPostConversionMatrix;
bool FFbxDataConverter::bIsJointMatrixInitialized = false;

void FFbxDataConverter::SetAxisConversionMatrix(const FbxAMatrix& Matrix)
{
	AxisConversionMatrix = Matrix;
	AxisConversionMatrixInv = Matrix.Inverse();
	bIsInitialized = true;
}

const FbxAMatrix& FFbxDataConverter::GetAxisConversionMatrix()
{
	if (!bIsInitialized)
	{
		AxisConversionMatrix.SetIdentity();
	}
	return AxisConversionMatrix;
}

const FbxAMatrix& FFbxDataConverter::GetAxisConversionMatrixInv()
{
	if (!bIsInitialized)
	{
		AxisConversionMatrixInv.SetIdentity();
	}
	return AxisConversionMatrixInv;
}

void FFbxDataConverter::SetJointPostConversionMatrix(const FbxAMatrix& Matrix)
{
	JointPostConversionMatrix = Matrix;
	bIsJointMatrixInitialized = true;
}

const FbxAMatrix& FFbxDataConverter::GetJointPostConversionMatrix()
{
	if (!bIsJointMatrixInitialized)
	{
		JointPostConversionMatrix.SetIdentity();
	}
	return JointPostConversionMatrix;
}

// ========================================
// 좌표 변환 함수 구현
// ========================================

FVector FFbxDataConverter::ConvertPos(const FbxVector4& Vector)
{
	return FVector(
		static_cast<float>(Vector[0]),
		static_cast<float>(-Vector[1]),  // Y축 반전 (RH → LH)
		static_cast<float>(Vector[2])
	);
}

FVector FFbxDataConverter::ConvertDir(const FbxVector4& Vector)
{
	FVector result(
		static_cast<float>(Vector[0]),
		static_cast<float>(-Vector[1]),  // Y축 반전
		static_cast<float>(Vector[2])
	);
	result.Normalize();
	return result;
}

FQuat FFbxDataConverter::ConvertRotToQuat(const FbxQuaternion& Quaternion)
{
	return FQuat(
		static_cast<float>(Quaternion[0]),    // X
		static_cast<float>(-Quaternion[1]),   // Y 반전
		static_cast<float>(Quaternion[2]),    // Z
		static_cast<float>(-Quaternion[3])    // W 반전
	);
}

FVector FFbxDataConverter::ConvertScale(const FbxVector4& Vector)
{
	return FVector(
		static_cast<float>(Vector[0]),
		static_cast<float>(Vector[1]),
		static_cast<float>(Vector[2])
	);
}

FTransform FFbxDataConverter::ConvertTransform(const FbxAMatrix& Matrix)
{
	FTransform result;

	// Position
	result.Translation = ConvertPos(Matrix.GetT());

	// Rotation
	result.Rotation = ConvertRotToQuat(Matrix.GetQ());

	// Scale
	result.Scale3D = ConvertScale(Matrix.GetS());

	return result;
}

FMatrix FFbxDataConverter::ConvertMatrix(const FbxMatrix& fbxMatrix)
{
	FMatrix result;

	// 행렬 복사
	for (int row = 0; row < 4; row++)
	{
		for (int col = 0; col < 4; col++)
		{
			result.M[row][col] = static_cast<float>(fbxMatrix.Get(row, col));
		}
	}

	// Y축 관련 요소 반전 (Right-Handed → Left-Handed)
	result.M[1][0] = -result.M[1][0];
	result.M[1][1] = -result.M[1][1];
	result.M[1][2] = -result.M[1][2];
	result.M[1][3] = -result.M[1][3];  // Translation Y

	return result;
}
