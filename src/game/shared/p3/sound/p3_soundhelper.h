#ifndef P3_SOUNDHELPER_H
#define P3_SOUNDHELPER_H

#include "soundenvelope.h"

//-----------------------------------------------------------------------------
// P3_SoundHelper -- вспомогательные функции для проигрывания звуков
//-----------------------------------------------------------------------------

struct P3_SoundHelper
{
	static CSoundPatch*	CreateSound( const char* name, CBaseEntity* pOwner );
	static void			DestroySound( CSoundPatch* pSound );

	static bool			GetSoundParameters( const char* name, CSoundParameters& params );
	static bool			GetSoundParameters( CSoundPatch* pSound, CSoundParameters& params );
	static void			PlaySound( CSoundPatch* pSound );
	static void			StopSound( CSoundPatch* pSound );
	static bool			IsPlaying( CSoundPatch* pSound );

	static void			FadeIn( CSoundPatch* pSound, float flTime );
	static void			FadeOut( CSoundPatch* pSound, float flTime );
	static void			CrossFade( CSoundPatch* pSound1, CSoundPatch* pSound2, float flFadeOut, float flFadeIn );
	static void			ReplaceSound( CSoundPatch* pSound1, CSoundPatch* pSound2 );

	static void			SetVolume( CSoundPatch* pSound, float flVolume );
	static void			ChangeVolume( CSoundPatch* pSound, float flVolume, float flTime );
	static float		GetVolume( CSoundPatch* pSound );

	static void			SetPitch( CSoundPatch* pSound, float flPitch );
	static void			ChangePitch( CSoundPatch* pSound, float flVolume, float flPitch );
	static float		GetPitch( CSoundPatch* pSound );
};

//-----------------------------------------------------------------------------
// P3_IdleSound -- проигрывает звук в рандомные промежутки времени
//-----------------------------------------------------------------------------

class P3_IdleSound
{
public:
	P3_IdleSound();

	void				Init( CBaseEntity* pOwner, const char* name );
	void				SetInterval( float flMin, float flMax );

	void				Enable( bool bEnable );

	void				Precache();
	void				Update();

private:
	CBaseEntity*		m_pOwner;
	char				m_szSoundName[64];

	float				m_flIntervalMin;
	float				m_flIntervalMax;

	bool				m_bEnabled;

	float				m_flNextSoundTime;
};

#endif