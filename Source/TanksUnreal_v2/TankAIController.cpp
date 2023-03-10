// GPL


#include "TankAIController.h"
#include "Math/UnrealMathUtility.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystemTypes.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"
#include "TanksGameMode.h"

#define SET_TIMER(method,seconds) GetWorld()->GetTimerManager().SetTimer(handle, this, method, seconds, false)
#define facing_angle(a,b) AngleBetweenDirections(a,b) - PI / 2
#define invert_mode() state = state == State::Fleeing ? State::Fighting : State::Fleeing

#define NOW GetWorld()->GetTimeSeconds()

uint8 ATankAIController::staticPlayerNum = 0;

//constructor - set the number
ATankAIController::ATankAIController() {
	playerNum = ++staticPlayerNum;
	PrimaryActorTick.bCanEverTick = true;
}

//interface - get the player name
FString ATankAIController::GetName_Implementation() {
	return FString::Printf(TEXT("COM %d"), playerNum);
}

//track the pawn that this controller controls
void ATankAIController::OnPossess(APawn* other)
{
	//the Super call is necessary to ensure the possess completes correctly
	Super::OnPossess(other);
	tank = Cast<ATank>(other);
	AITick();
}
void ATankAIController::OnUnPossess()
{
	//the Super call is necessary to ensure the unpossess completes correctly
	Super::OnUnPossess();
	tank = nullptr;

	//cancel the timer
	handle.Invalidate();
}

/**
 * Calculate the angle between two directional FVectors
 * @param a First vector
 * @param b Second vector
 * @returns the angle between a and b
 */
float ATankAIController::AngleBetweenDirections(FVector& a, FVector& b)
{
	a.Normalize();
	b.Normalize();

	return FMath::Acos(FVector::DotProduct(a,b));
}

/**
 * Rotate the tank to face the target position. This invokes Turn() on the tank
 * @param pos the position to face
 * @return true if the position is directly ahead, false otherwise
 */
bool ATankAIController::RotateToFacePos(const FVector& b)
{
	FVector a = tank->GetActorLocation();
	FVector dir = a - b;
	FVector tdir = tank->GetActorForwardVector();

	auto angle = facing_angle(tdir, dir);

	//turn in the appropriate direction
	//TODO: change the amount based on the angle
	if (FMath::Abs(angle) > 0.05) {
		if (angle > 0) {
			tank->Turn(-1);
		}
		else {
			tank->Turn(1);
		}
		return false;
	}
	else {
		return true;
	}
}

/**
 Tick the decision making of this AI. Runs on a timer.
 */
void ATankAIController::AITick()
{
	//GLog->Logf(TEXT("%s"), AController::GetPawn());

	//choose a new place to drive
	chassisTargetPos = FVector(FMath::RandRange(-4000,4000), FMath::RandRange(-4000, 4000),0);

	//invert state
	invert_mode();

	//whether to pursue or go to a random position on the board
	pursueInDriving = FMath::RandRange(0, 100) < 80;

	SET_TIMER(&ATankAIController::AITick, FMath::RandRange(5,10));
}

/**
 * Find the closest tank on the board
 * @returns a pointer to the tank closest to this tank
 */
ATank* ATankAIController::GetClosestTank()
{
	auto gamemode = Cast<ATanksGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
	ATank* closest = nullptr;
	float closestDist = 10000;
	auto tanks = gamemode->GetActiveTanks();
	for (const auto& t : tanks) {
		if (t == tank) {
			continue;
		}
		auto distance = FVector::Distance(tank->GetActorLocation(),t->GetActorLocation());
		if (distance < closestDist) {
			closest = t;
		}
	}

	return closest;
}

/**
 * Request to fire a shot. This may not fire a shot.
 * @param pos the position to shoot at
 */
void ATankAIController::Fire(const FVector& pos)
{
	auto now = NOW;
	if (now - LastShotTime > minShotDelay) {
		//set fire strength
		tank->currentPercent = FMath::GetMappedRangeValueClamped(distanceToPower,FVector2D(0.1,0.83),FVector::Distance(tank->GetActorLocation(),pos));

		//fire and mark time
		tank->Fire();
		LastShotTime = now;

		//shot timing variance
		minShotDelay = FMath::FRandRange(FMath::GetMappedRangeValueClamped(FVector2D(0.25,1),FVector2D(1.5,0.5),tank->currentPercent), 1.5);
	}
}

/**
 * Angles the tank to face the closest tank on the board
 */
void ATankAIController::DefenseTick()
{
	auto closest = GetClosestTank();
	if (closest == nullptr) {
		return;
	}
	
	//figure out what side of the pawn the target is on
	auto closeEnough = RotateToFacePos(closest->GetActorLocation());
	if (closeEnough) {
		Fire(closest->GetActorLocation());
	}

	tank->Move(-0.5);
}

/**
 * Tick the driving logic once. Calls Move() and Turn() on the tank to drive it.
 */
void ATankAIController::DriveTick()
{

	// configure navigation request
	auto navsys = UNavigationSystemV1::GetNavigationSystem(GetWorld());
	auto navdata = navsys->GetNavDataForProps(GetNavAgentPropertiesRef());
	auto pos = tank->GetActorLocation();
	auto closest = GetClosestTank();
	FVector end;
	if (closest == nullptr || !pursueInDriving) {
		end = chassisTargetPos;
	}
	else {
		end = closest->GetActorLocation();
		// too close to target?
		if (FVector::Distance(end, tank->GetActorLocation()) < 1000) {
			//either invert mode or run away
			invert_mode();
		}
	}
	FPathFindingQuery query(this,*navdata,pos,end);

	//perform navigation calculation
	auto result = navsys->FindPathSync(query, EPathFindingMode::Regular);

	//if failed, go into fighting mode
	if (result.Result != ENavigationQueryResult::Success) {
		state = State::Fighting;
		return;
	}

	auto path = result.Path->GetPathPoints();
	if (path.Num() > 1) {
		//drive towards path[1] with automatic speed
		RotateToFacePos(path[1]);
		auto dist = FVector::Distance(pos, path[1]);
		tank->Move(FMath::GetMappedRangeValueClamped(FVector2D(0,2000),FVector2D(0.8,1),dist));

		//at end of path? switch states
		if (path.Num() == 2 && dist < 200) {
			invert_mode();
		}
	}

	//if angled at another tank, shoot at it
	if (closest == nullptr) {
		return;
	}
	auto target = closest->GetActorLocation();
	auto forward = tank->GetActorForwardVector();
	auto tdir = pos - target;
	if (FMath::Abs(facing_angle(forward, tdir)) < 0.05) {
		Fire(target);
	}

	/*for (int i = 0; i < path.Num() - 1; i++) {
		DrawDebugLine(GetWorld(), path[i], path[i + 1], FColor::Green, 0.0f, 0.0f, 5);
	}*/
}

void ATankAIController::Tick(float deltaTime) {

	//don't run if no pawn is attached
	if (tank == nullptr) {
		return;
	}

	switch (state) {
	case State::Fleeing:
		//drive towards the chassis target pos
		DriveTick();
		break;
	case State::Fighting:
		DefenseTick();
		break;
	}
}
