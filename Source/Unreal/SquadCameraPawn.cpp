#include "SquadCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

ASquadCameraPawn::ASquadCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = ReferenceArmLength;
	SpringArm->SetRelativeRotation(FRotator(CameraPitch, 0.f, 0.f));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(FieldOfView);
}

void ASquadCameraPawn::Pan(const FVector2D& Input, float DeltaSeconds)
{
	if (Input.IsNearlyZero())
	{
		return;
	}
	const FRotator Yaw(0.f, GetActorRotation().Yaw, 0.f);
	const FVector Forward = Yaw.RotateVector(FVector::ForwardVector);
	const FVector Right = Yaw.RotateVector(FVector::RightVector);
	const float ZoomFactor = SpringArm->TargetArmLength / ReferenceArmLength;
	AddActorWorldOffset((Forward * Input.X + Right * Input.Y).GetClampedToMaxSize(1.f) * PanSpeed * ZoomFactor * DeltaSeconds);
}

void ASquadCameraPawn::Rotate(float Input, float DeltaSeconds)
{
	if (!FMath::IsNearlyZero(Input))
	{
		AddActorWorldRotation(FRotator(0.f, Input * RotateSpeed * DeltaSeconds, 0.f));
	}
}

void ASquadCameraPawn::Zoom(float WheelDelta)
{
	if (!FMath::IsNearlyZero(WheelDelta))
	{
		SpringArm->TargetArmLength = FMath::Clamp(SpringArm->TargetArmLength - WheelDelta * ZoomStep, MinZoom, MaxZoom);
	}
}
