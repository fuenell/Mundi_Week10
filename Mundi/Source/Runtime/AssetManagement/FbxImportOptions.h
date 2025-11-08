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

	// 원본 좌표계를 Mundi 좌표계로 변환할지 여부
	// Mundi는 Z-Up, X-Forward, Y-Right, Left-Handed 사용
	bool bConvertScene = true;

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
