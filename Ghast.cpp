
#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#include "Ghast.h"
#include "../World.h"
#include "../Entities/GhastFireballEntity.h"
#include "../Chunk.h"
#include "../Entities/Player.h"


cGhast::cGhast(void) :
	super("Ghast", mtGhast, "entity.ghast.hurt", "entity.ghast.death", 4, 4)
{
	SetGravity(0.0f);
	SetAirDrag(0.20f);
	m_RelativeWalkSpeed = 0.6f;
}



void cGhast::GetDrops(cItems & a_Drops, cEntity * a_Killer)
{
	unsigned int LootingLevel = 0;
	if (a_Killer != nullptr)
	{
		LootingLevel = a_Killer->GetEquippedWeapon().m_Enchantments.GetLevel(cEnchantments::enchLooting);
	}
	AddRandomDropItem(a_Drops, 0, 2 + LootingLevel, E_ITEM_GUNPOWDER);
	AddRandomDropItem(a_Drops, 0, 1 + LootingLevel, E_ITEM_GHAST_TEAR);
}





bool cGhast::Attack(std::chrono::milliseconds a_Dt)
{
	if ((GetTarget() != nullptr) && (m_AttackCoolDownTicksLeft == 0))
	{
		// Setting this higher gives us more wiggle room for attackrate
		Vector3d Speed = GetLookVector() * 20;
		Speed.y = Speed.y + 1;

		auto GhastBall = cpp14::make_unique<cGhastFireballEntity>(this, GetPosition().addedY(1), Speed);
		auto GhastBallPtr = GhastBall.get();
		if (!GhastBallPtr->Initialize(std::move(GhastBall), *m_World))
		{
			return false;
		}

		ResetAttackCooldown();
		return true;
	}
	return false;
}

void cGhast::MoveToWayPoint(cChunk & a_Chunk)
{
	if ((m_NextWayPointPosition - GetPosition()).SqrLength() < WAYPOINT_RADIUS * WAYPOINT_RADIUS)
	{
		return;
	}
	Vector3d Distance = m_FinalDestination - GetPosition();
	if ((std::abs(Distance.x) > 0.05) || (std::abs(Distance.z) > 0.05))
	{
		Distance.Normalize();
		Distance *= m_RelativeWalkSpeed;
		AddSpeedX(Distance.x);
		AddSpeedZ(Distance.z);
		AddSpeedY(Distance.y);
	}
	else {
		StopMovingToPosition();
	}
}

void cGhast::InStateIdle(std::chrono::milliseconds a_Dt, cChunk & a_Chunk)
{
	if (m_PathfinderActivated)
	{
		return;  // Still getting there
	}

	m_IdleInterval += a_Dt;

	if (m_IdleInterval > std::chrono::seconds(1))
	{
		auto & Random = GetRandomProvider();

		// At this interval the results are predictable
		int rem = Random.RandInt(1, 7);
		m_IdleInterval -= std::chrono::seconds(1);  // So nothing gets dropped when the server hangs for a few seconds

		Vector3d Dist;
		Dist.x = static_cast<double>(Random.RandInt(-15, 15));
		Dist.z = static_cast<double>(Random.RandInt(-7, 7));
		Dist.y = static_cast<double>(Random.RandInt(-15, 15));

		if ((Dist.SqrLength() > 2) && (rem >= 3))
		{
			Vector3d Destination(GetPosX() + Dist.x, Dist.y + GetPosY(), GetPosZ() + Dist.z);
			cChunk * Chunk = a_Chunk.GetNeighborChunk(static_cast<int>(Destination.x), static_cast<int>(Destination.z));
			if ((Chunk == nullptr) || !Chunk->IsValid())
			{
				return;
			}

			BLOCKTYPE BlockType;
			NIBBLETYPE BlockMeta;
			int RelX = static_cast<int>(Destination.x) - Chunk->GetPosX() * cChunkDef::Width;
			int RelZ = static_cast<int>(Destination.z) - Chunk->GetPosZ() * cChunkDef::Width;
			int YBelowUs = static_cast<int>(Destination.y) - 1;
			if (YBelowUs >= 0)
			{
				Chunk->GetBlockTypeMeta(RelX, YBelowUs, RelZ, BlockType, BlockMeta);
				if (BlockType != E_BLOCK_STATIONARY_WATER)  // Idle mobs shouldn't enter water on purpose
				{
					MoveToPosition(Destination);
				}
			}
		}
	}
}

/*
	until path finding algorithm for flying is made, the ghast fly at random locations
	ghast just move to final destination
*/
void cGhast::InStateChasing(std::chrono::milliseconds a_Dt, cChunk & a_Chunk)
{
	super::InStateChasing(a_Dt, a_Chunk);
}

void cGhast::Tick(std::chrono::milliseconds a_Dt, cChunk & a_Chunk)
{
	super::Tick(a_Dt, a_Chunk);
	if (!IsTicking())
	{
		// The base class tick destroyed us
		return;
	}
	GET_AND_VERIFY_CURRENT_CHUNK(Chunk, POSX_TOINT, POSZ_TOINT);

	ASSERT((GetTarget() == nullptr) || (GetTarget()->IsPawn() && (GetTarget()->GetWorld() == GetWorld())));
	if (m_AttackCoolDownTicksLeft > 0)
	{
		m_AttackCoolDownTicksLeft -= 1;
	}

	if (m_Health <= 0)
	{
		// The mob is dead, but we're still animating the "puff" they leave when they die
		m_DestroyTimer += a_Dt;
		if (m_DestroyTimer > std::chrono::seconds(1))
		{
			Destroy(true);
		}
		return;
	}

	if (m_TicksSinceLastDamaged < 100)
	{
		++m_TicksSinceLastDamaged;
	}
	if ((GetTarget() != nullptr))
	{
		ASSERT(GetTarget()->IsTicking());

		if (GetTarget()->IsPlayer())
		{
			if (!static_cast<cPlayer *>(GetTarget())->CanMobsTarget())
			{
				SetTarget(nullptr);
				m_EMState = IDLE;
			}
		}
	}

	bool a_IsFollowingPath = false;
	if (m_PathfinderActivated)
	{
		if (ReachedFinalDestination() || (m_LeashToPos != nullptr))
		{
			StopMovingToPosition();  // Simply sets m_PathfinderActivated to false.
		}
		a_IsFollowingPath = true;
		MoveToWayPoint(*Chunk);
	}

	SetPitchAndYawFromDestination(a_IsFollowingPath);

	switch (m_EMState)
	{
	case IDLE:
	{
		// If enemy passive we ignore checks for player visibility.
		InStateIdle(a_Dt, a_Chunk);
		break;
	}
	case CHASING:
	{
		// If we do not see a player anymore skip chasing action.
		InStateChasing(a_Dt, a_Chunk);
		break;
	}
	case ESCAPING:
	{
		InStateEscaping(a_Dt, a_Chunk);
		break;
	}
	case ATTACKING: break;
	}  // switch (m_EMState)

	// Leash calculations
	//cMonster::CalcLeashActions(a_Dt);

	BroadcastMovementUpdate();

	if (m_AgingTimer > 0)
	{
		m_AgingTimer--;
		if ((m_AgingTimer <= 0) && IsBaby())
		{
			SetAge(1);
			m_World->BroadcastEntityMetadata(*this);
		}
	}
}

void cGhast::SetPitchAndYawFromDestination(bool a_IsFollowingPath)
{
	Vector3d BodyDistance;
	if (!a_IsFollowingPath && (GetTarget() != nullptr))
	{
		BodyDistance = GetTarget()->GetPosition() - GetPosition();
	}
	else
	{
		BodyDistance = m_FinalDestination - GetPosition();
	}
	double BodyRotation, BodyPitch;
	BodyDistance.Normalize();
	VectorToEuler(BodyDistance.x, BodyDistance.y, BodyDistance.z, BodyRotation, BodyPitch);
	SetYaw(BodyRotation);

	Vector3d HeadDistance;
	if (GetTarget() != nullptr)
	{
		if (GetTarget()->IsPlayer())  // Look at a player
		{
			HeadDistance = GetTarget()->GetPosition() - GetPosition();
		}
		else  // Look at some other entity
		{
			HeadDistance = GetTarget()->GetPosition() - GetPosition();
			// HeadDistance.y = GetTarget()->GetPosY() + GetHeight();
		}
	}
	else  // Look straight
	{
		HeadDistance = BodyDistance;
		HeadDistance.y = 0;
	}

	double HeadRotation, HeadPitch;
	HeadDistance.Normalize();
	VectorToEuler(HeadDistance.x, HeadDistance.y, HeadDistance.z, HeadRotation, HeadPitch);
	if ((std::abs(BodyRotation - HeadRotation) < 70) && (std::abs(HeadPitch) < 60))
	{
		SetHeadYaw(HeadRotation);
		SetPitch(-HeadPitch);
	}
	else
	{
		SetHeadYaw(BodyRotation);
		SetPitch(0);
	}
}
