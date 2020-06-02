// GPL

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "TankController.h"
#include "Tank.h"
#include "TankAIController.generated.h"

/**
 * 
 */
UCLASS()
class TANKSUNREAL_V2_API ATankAIController : public AAIController, public ITankController
{
	GENERATED_BODY()

public:
	ATankAIController();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
		FString GetName();
	virtual FString GetName_Implementation() override;

protected:
	ATank* tank = nullptr;

	void OnPossess(APawn* other) override;
	void OnUnPossess() override;

	float AngleBetweenDirections(FVector& A, FVector& B);
	void RotateToFacePos(const FVector& pos);

	enum State {
		Fleeing, Fighting
	};

	State state;
	FVector chassisTargetPos = FVector(0, 0, 0);

	void AITick();
	ATank* GetClosestTank();

private:
	void Tick(float deltaTime) override;

	void DefenseTick();
	void DriveTick();

	FTimerHandle handle;

	static uint8 staticPlayerNum;
	uint8 playerNum;
};
