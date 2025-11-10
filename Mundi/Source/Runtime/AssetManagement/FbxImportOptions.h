#pragma once

#include <cstdint>

/**
 * FBX Import 타입 정의
 * 현재는 SkeletalMesh만 지원, 향후 StaticMesh와 Animation 추가 예정
 */
enum class EFbxImportType : uint8_t
{
	SkeletalMesh,	// Skeletal Mesh (본 포함)
	StaticMesh,		// Static Mesh (미구현)
	Animation		// Animation (미구현)
};

/**
 * FBX Import 옵션 구조체
 * Unreal Engine의 UFbxImportUI와 유사한 구조
 */
struct FFbxImportOptions
{
	// Import 타입
	EFbxImportType ImportType = EFbxImportType::SkeletalMesh;

	// === 공통 옵션 ===

	// Import 스케일 배율 (FBX 단위 변환용)
	float ImportScale = 1.0f;

	// ========================================
	// 좌표계 변환 옵션 (Unreal Engine 호환)
	// ========================================

	/**
	 * Scene 좌표계 변환 여부
	 *
	 * true:  FBX Scene을 Unreal-style 좌표계로 변환 (기본)
	 *        Z-Up, -Y-Forward, Right-Handed → Y-Flip → Left-Handed
	 *
	 * false: FBX 원본 좌표계 유지 + Y-Flip만 적용
	 *        (Axis Conversion Matrix = Identity)
	 */
	bool bConvertScene = true;

	/**
	 * Front Axis를 +X로 강제
	 *
	 * false: -Y Forward (기본, Maya/Max 호환)
	 * true:  +X Forward (직관적, 일부 툴 호환)
	 *
	 * bConvertScene = true일 때만 유효
	 * Skeletal Mesh Bone Hierarchy에 JointOrientationMatrix 적용
	 */
	bool bForceFrontXAxis = true;

	/**
	 * Scene 단위를 m (meter)로 변환
	 *
	 * true:  FBX 단위 → Meter (m) 단위로 변환
	 * false: 원본 단위 유지 (기본)
	 *
	 * NOTE: Blender는 기본적으로 meter 단위를 사용합니다.
	 * FBX 파일이 cm 단위인 경우 이 옵션을 true로 설정하면 1/100 크기로 축소됩니다.
	 */
	bool bConvertSceneUnit = true;

	// 중복 버텍스 병합 여부
	bool bRemoveDegenerates = true;

	// === SkeletalMesh 전용 옵션 ===

	// Skeleton Asset 생성 여부
	bool bImportSkeleton = true;

	// Morph Target (BlendShape) import 여부 (미구현)
	bool bImportMorphTargets = false;

	// LOD (Level of Detail) import 여부 (미구현)
	bool bImportLODs = false;

	// === StaticMesh 전용 옵션 (미구현) ===

	// Collision 생성 여부
	bool bGenerateCollision = false;

	// === Animation 전용 옵션 (미구현) ===

	// Animation import 여부
	bool bImportAnimations = false;

	// 기본 생성자
	FFbxImportOptions() = default;
};
