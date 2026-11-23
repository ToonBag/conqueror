#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SquadCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/** Angled top-down camera. Panned, rotated and zoomed by ASquadPlayerController. */
UCLASS()
class UNREAL_API ASquadCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ASquadCameraPawn();

	UPROPERTY(EditAnywhere, Category = "Camera")
	float PanSpeed = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float RotateSpeed = 90.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float ZoomStep = 200.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinZoom = 600.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxZoom = 4000.f;

	/** Camera pitch (degrees, looking down) and horizontal field of view. The AI bot simulates the same view. */
	static constexpr float CameraPitch = -60.f;
	static constexpr float FieldOfView = 60.f;
	/** Arm length the pan speed is tuned for: panning scales with zoom relative to this. */
	static constexpr float ReferenceArmLength = 1800.f;

	/** Input in camera-relative space: X = forward, Y = right. */
	void Pan(const FVector2D& Input, float DeltaSeconds);
	void Rotate(float Input, float DeltaSeconds);
	void Zoom(float WheelDelta);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;
};
