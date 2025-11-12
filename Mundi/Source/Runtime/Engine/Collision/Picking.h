#pragma once
#include "Vector.h"
#include "InputManager.h"
#include "UEContainer.h"
#include "Enums.h"

class UStaticMeshComponent;
class AGizmoActor;
class USkeletalMeshComponent;
// Forward Declarations
class AActor;
class ACameraActor;
class FViewport;
// Unreal-style simple ray type
struct alignas(16) FRay
{
    FVector Origin;
    FVector Direction; // Normalized
};

// Build A world-space ray from the current mouse position and camera/projection info.
// - InView: view matrix (row-major, row-vector convention; built by LookAtLH)
// - InProj: projection matrix created by PerspectiveFovLH in this project
FRay MakeRayFromMouse(const FMatrix& InView,
                      const FMatrix& InProj);

// Improved version: directly use camera world position and orientation
FRay MakeRayFromMouseWithCamera(const FMatrix& InView,
                                const FMatrix& InProj,
                                const FVector& CameraWorldPos,
                                const FVector& CameraRight,
                                const FVector& CameraUp,
                                const FVector& CameraForward);

// Viewport-specific ray generation for multi-viewport picking
FRay MakeRayFromViewport(const FMatrix& InView,
                         const FMatrix& InProj,
                         const FVector& CameraWorldPos,
                         const FVector& CameraRight,
                         const FVector& CameraUp,
                         const FVector& CameraForward,
                         const FVector2D& ViewportMousePos,
                         const FVector2D& ViewportSize,
                         const FVector2D& ViewportOffset = FVector2D(0.0f, 0.0f));

// Ray-sphere intersection.
// Returns true and the closest positive T if the ray hits the sphere.
bool IntersectRaySphere(const FRay& InRay, const FVector& InCenter, float InRadius, float& OutT);

// Möller–Trumbore ray-triangle intersection.
// Returns true if hit and outputs closest positive T along the ray.
bool IntersectRayTriangleMT(const FRay& InRay,
                            const FVector& InA,
                            const FVector& InB,
                            const FVector& InC,
                            float& OutT);

// Calculate distance from point to ray
// Returns the minimum distance and the parametric T value on the ray
float CalculatePointToRayDistance(const FVector& Point, const FRay& Ray, float& OutT);

// Ray-Octahedron intersection for bone picking
// Returns true if ray intersects the octahedron (bone shape)
bool IntersectRayOctahedron(const FRay& Ray,
                            const FVector& StartPoint,
                            const FVector& EndPoint,
                            float Scale,
                            float& OutT);

/**
 * Bone Picking Result
 * - 본 피킹 결과를 담는 구조체
 */
struct FBonePicking
{
    // Picking Type
    enum class EPickingType
    {
        None,       // No picking
        Joint,      // Joint (Sphere) picked
        Bone        // Bone (Octahedron) picked
    };

    // Picked bone index (-1 if no bone picked)
    int32 BoneIndex = -1;

    // Picking type
    EPickingType PickingType = EPickingType::None;

    // Picking location in world space
    FVector PickingLocation = FVector::Zero();

    // Distance along the ray
    float Distance = FLT_MAX;

    // Check if picking is valid
    bool IsValid() const { return BoneIndex >= 0 && PickingType != EPickingType::None; }
};

/**
 * PickingSystem
 * - 액터 피킹 관련 로직을 담당하는 클래스
 */
class CPickingSystem
{
public:
    /** === 피킹 실행 === */
    static AActor* PerformPicking(const TArray<AActor*>& Actors, ACameraActor* Camera);

    // Viewport-specific picking for multi-viewport scenarios
    static AActor* PerformViewportPicking(const TArray<AActor*>& Actors,
                                          ACameraActor* Camera,
                                          const FVector2D& ViewportMousePos,
                                          const FVector2D& ViewportSize,
                                          const FVector2D& ViewportOffset = FVector2D(0.0f, 0.0f));

    // Viewport-specific picking with custom aspect ratio
    static AActor* PerformViewportPicking(const TArray<AActor*>& Actors,
                                          ACameraActor* Camera,
                                          const FVector2D& ViewportMousePos,
                                          const FVector2D& ViewportSize,
                                          const FVector2D& ViewportOffset,
                                          float ViewportAspectRatio, FViewport* Viewport);

    // 뷰포트 정보를 명시적으로 받는 기즈모 호버링 검사
    static uint32 IsHoveringGizmoForViewport(AGizmoActor* GizmoActor, const ACameraActor* Camera,
                                             const FVector2D& ViewportMousePos,
                                             const FVector2D& ViewportSize,
                                             const FVector2D& ViewportOffset,FViewport*Viewport,
                                             FVector& OutImpactPoint);
    
    // 기즈모 드래그로 액터를 이동시키는 함수
   // static void DragActorWithGizmo(AActor* Actor, AGizmoActor* GizmoActor, uint32 GizmoAxis, const FVector2D& MouseDelta, const ACameraActor* Camera, EGizmoMode InGizmoMode);

    /** === 헬퍼 함수들 === */
    static bool CheckActorPicking(const AActor* Actor, const FRay& Ray, float& OutDistance);

    /** === 본 피킹 === */
    // Perform bone picking on a skeletal mesh component
    static FBonePicking PerformBonePicking(USkeletalMeshComponent* SkeletalMeshComponent,
                                           const FRay& Ray,
                                           float JointRadius = 1.0f,
                                           float BoneScale = 0.2f);

    static uint32 GetPickCount() { return TotalPickCount; }
    static uint64 GetLastPickTime() { return LastPickTime; }
    static uint64 GetTotalPickTime() { return TotalPickTime; }
private:
    /** === 내부 헬퍼 함수들 === */
    static bool CheckGizmoComponentPicking(UStaticMeshComponent* Component, const FRay& Ray, 
                                           float ViewWidth, float ViewHeight, const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
                                           float& OutDistance, FVector& OutImpactPoint);

    static uint32 TotalPickCount;
    static uint64 LastPickTime;
    static uint64 TotalPickTime;
};
