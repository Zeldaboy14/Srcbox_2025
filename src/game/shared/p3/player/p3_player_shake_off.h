#ifndef P3_PLAYER_SHAKE_OFF_H
#define P3_PLAYER_SHAKE_OFF_H


#include "p3/p3_player_shared.h"

class CP3PlayerShakeOffController
{
	DECLARE_SIMPLE_DATADESC();
private:
	class CP3_Player *m_pPlayer;

	int attachment;
	int stage;
	int tick_count;

public:
	CP3PlayerShakeOffController( class CP3_Player *player )
		: m_pPlayer( player )
		, attachment( -1 )
		, stage( -1 )
		, tick_count( -1 )
	{
	}

	void Reset( int att = -1 );

	Activity TranslateActivity( Activity idealActivity );

	void HandleAnimationEvent( int ae );
	void HandleInputEvent( CUserCmd *ucmd );

	bool JustStarted() const { return stage == 1; }
	bool MoveCompleted() const { return stage == 10; }
};

#endif // P3_PLAYER_SHAKE_OFF_H
