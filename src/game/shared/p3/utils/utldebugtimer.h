#ifndef UTLDEBUGTIMER_H
#define UTLDEBUGTIMER_H


class CUtlDebugTimer
{
public:
	CUtlDebugTimer()
		: duration( 1.f )
	{
	}

	CUtlDebugTimer( float duration )
		: duration( duration )
	{
	}

	float Duration() const { return duration; }
	bool Expired() const { return gpGlobals->curtime - last_time > duration * 0.9f; }

	void Reset() { last_time = gpGlobals->curtime; }

private:
	float last_time;
	float duration;
};

extern CUtlDebugTimer g_DebugTimer;

#endif // UTLDEBUGTIMER_H
