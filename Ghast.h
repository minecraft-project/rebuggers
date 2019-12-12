
#pragma once

#include "AggressiveMonster.h"





class cGhast :
	public cAggressiveMonster
{
	typedef cAggressiveMonster super;

public:
	cGhast(void);

	CLASS_PROTODEF(cGhast)

	virtual void GetDrops(cItems & a_Drops, cEntity * a_Killer = nullptr) override;
	virtual bool Attack(std::chrono::milliseconds a_Dt) override;
	bool IsCharging(void) const {return false; }
	void MoveToWayPoint(cChunk & a_Chunk) override;
	void InStateIdle(std::chrono::milliseconds a_Dt, cChunk & a_Chunk) override;
	void InStateChasing(std::chrono::milliseconds a_Dt, cChunk & a_Chunk) override;
	void Tick(std::chrono::milliseconds a_Dt, cChunk & a_Chunk) override;
	void SetPitchAndYawFromDestination(bool a_IsFollowingPath) override;
} ;




