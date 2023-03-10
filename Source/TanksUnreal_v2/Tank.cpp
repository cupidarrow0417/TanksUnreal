// GPL


#include "Tank.h"
#include "Kismet/KismetMathLibrary.h"
#include "TankController.h"
#include "Materials/MaterialInstanceDynamic.h"

#define FPSSCALE deltaTime / evalNormal

// Sets default values
ATank::ATank()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	const int boxextent = 80;

	CollisionRoot = CreateDefaultSubobject<UBoxComponent>("Root");
	RootComponent = CollisionRoot;
	CollisionRoot->SetBoxExtent(FVector(boxextent, boxextent, boxextent));
	CollisionRoot->SetRelativeLocation(FVector(0,0, boxextent/2.0));
	CollisionRoot->BodyInstance.bLockXRotation = true;
	CollisionRoot->BodyInstance.bLockYRotation = true;
	CollisionRoot->SetSimulatePhysics(true);

	ChargeShotBar = CreateDefaultSubobject<UWidgetComponent>("ChargeShot");
	ChargeShotBar->SetupAttachment(CollisionRoot);
	ChargeShotBar->SetRelativeRotation(FQuat::MakeFromEuler(FVector(0,90,180)));
	ChargeShotBar->SetRelativeLocation(FVector(0,100,-60));

	//heath HUD will always face the camera
	HealthInfoHUD = CreateDefaultSubobject<UWidgetComponent>("Info HUD");
	HealthInfoHUD->SetupAttachment(CollisionRoot);
	HealthInfoHUD->SetWidgetSpace(EWidgetSpace::Screen);
	HealthInfoHUD->SetRelativeLocation(FVector(0,0,100));

	BulletSpawnpoint = CreateDefaultSubobject<UChildActorComponent>("Bullet Spawnpoint");
	BulletSpawnpoint->SetupAttachment(CollisionRoot);

}

FString ATank::GetName()
{
	auto c = GetController();
	ITankController* controller = Cast<ITankController>(c);
	if (controller == nullptr) {
		//use default GetName if controller is not present or has the wrong type
		return Super::GetName();
	}
	else {
		return controller->Execute_GetName(c);
	}
}

// Called when the game starts or when spawned
void ATank::BeginPlay()
{
	Super::BeginPlay();

	//enable delegate
	CollisionRoot->OnComponentBeginOverlap.AddDynamic(this, &ATank::BeginOverlap);

	SetupTank();
}

// Called every frame
void ATank::Tick(float DeltaTime)
{
	deltaTime = DeltaTime;
	Super::Tick(DeltaTime);

	//play effects
	if (abs(GetVelocity().Length()) > 0.5) {
		if (!isMoving) {
			isMoving = true;
			MovingAction();
		}
	}
	else {
		if (isMoving) {
			isMoving = false;
			StopMovingAction();
		}
	}
}

/**
 * Move the tank. Called by the player input axis
 * @param amount the amount to move each scaled tick
 */
void ATank::Move(float amount)
{
	if (controlEnabled) {
		CollisionRoot->AddImpulse(GetActorRightVector() * amount * 50 * pow(FPSSCALE, 1.3));
	}
}

/**
 * Turn the tank. Called by the player input axis
 * @param amount the rate to turn each scaled tick
 */
void ATank::Turn(float amount)
{
	if (controlEnabled) {
		CollisionRoot->AddAngularImpulseInRadians(GetActorUpVector() * amount * 1000 * pow(FPSSCALE,2));
	}
}

/**
 * Charge up the shot. 
 * @param speed the rate to charge the shot each scaled tick
 */
void ATank::ChargeShot(float speed)
{
	if (controlEnabled && speed > 0.1) {
		ChargeShotBar->SetVisibility(true);
		currentPercent += speed * chargeRate * FPSSCALE;
		//update progress bar
		if (currentPercent >= 1) {
			Fire();
		}
		SetChargeBar(currentPercent);
	}
}

/**
 * Fire a bullet based on the charge amount.
 */
void ATank::Fire()
{
	if (controlEnabled) {
		SapwnBullet(UKismetMathLibrary::MapRangeClamped(currentPercent, 0, 1, minMaxBulletSpeed.X, minMaxBulletSpeed.Y));

		//reset charge bar
		SetChargeBar(currentPercent = 0);
		ChargeShotBar->SetVisibility(false);
	}
}

/**
 * Damage this tank based on a tank damager
 */
void ATank::Damage(ATankDamager* damagingActor)
{
	if (!IsAlive) {
		return;
	}
	//calculate distance
	auto distance = FVector::Dist(this->GetActorLocation(),damagingActor->GetActorLocation());
	auto damageTaken = UKismetMathLibrary::MapRangeClamped(distance,damageDistMinMax.X,damageDistMinMax.Y,1.0,0.0) * damagingActor->damageMultiplier;
	
	//take damage
	currentHealth -= damageTaken;

	//update health bar
	SetHealthBar(currentHealth);

	//knockback
	auto dirvec =  damagingActor->GetActorLocation() - GetActorLocation();
	dirvec.Z = 50;
	dirvec = dirvec.RotateAngleAxis(180, FVector(0, 0, 1));
	dirvec.Normalize();
	CollisionRoot->AddImpulse(dirvec * damagingActor->knockbackStrength * CollisionRoot->GetMass() * FPSSCALE);

	//dead? play explosion effect
	if (currentHealth < 0) {
		Die();
	}
}

void ATank::BeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	//is the overlapping actor something that can damage the tank?
	ATankDamager* td = Cast<ATankDamager>(OtherActor);
	if (td != nullptr) {
		Damage(td);
	}
}

/**
 Mark the tank as dead. The tank does not get deallocated.
 This will invoke the DieEffect which the blueprint defines.
 */
void ATank::Die()
{
	IsAlive = controlEnabled = false;
	CollisionRoot->SetVisibility(false, true);
	CollisionRoot->SetSimulatePhysics(false);
	CollisionRoot->SetGenerateOverlapEvents(false);
	DieEffect();
	SetActorTickEnabled(false);
}

/**
 * Sets the color of the tank. This generates a new material instance.
 * @param color the color to set the material to
 */
void ATank::SetColor(const FColor& color)
{
	//get all the static mesh components
	TArray<UStaticMeshComponent*> StaticMeshes;
	this->GetComponents<UStaticMeshComponent>(StaticMeshes);

	if (StaticMeshes.Num() > 0) {
		//create a dynamic material instance and set the color
		auto material = UMaterialInstanceDynamic::Create(StaticMeshes[0]->GetMaterial(0), StaticMeshes[0]);
		material->SetVectorParameterValue(FName("TankColor"), color);

		//assign the material
		for (auto& sm : StaticMeshes) {
			sm->SetMaterial(0, material);
		}
	}
}

/**
 * Reset this tank
 */
void ATank::SetupTank()
{
	IsAlive = true;
	currentHealth = 1;

	//update health
	SetHealthBar(currentHealth);

	//hide shot bar
	CollisionRoot->SetVisibility(true, true);
	ChargeShotBar->SetVisibility(false);

	//enable physics
	CollisionRoot->SetSimulatePhysics(true);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionRoot->SetEnableGravity(true);
	CollisionRoot->SetGenerateOverlapEvents(true);

	SetActorTickEnabled(true);

	StopMovingAction();

	UpdateName();
}