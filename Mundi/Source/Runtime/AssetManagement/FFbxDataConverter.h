#pragma once

#include "pch.h"
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
