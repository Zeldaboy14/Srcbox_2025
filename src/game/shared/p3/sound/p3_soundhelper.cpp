#include "cbase.h"
#include "p3_soundhelper.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define	ENVELOPE_CONTROLLER (CSoundEnvelopeController::GetController())

//-----------------------------------------------------------------------------
// P3_SoundHelper
//-----------------------------------------------------------------------------

CSoundPatch* P3_SoundHelper::CreateSound( const char* name, CBaseEntity* pOwner )
{
	CSoundParameters params;
	if ( !GetSoundParameters( name, params ) )
	{
		return NULL;
	}

	EmitSound_t es;
	es.m_nFlags = SND_SHOULDPAUSE;
	es.m_pSoundName = name;
	es.m_nChannel = params.channel;
	es.m_SoundLevel = params.soundlevel;
	es.m_flVolume = params.volume;
	es.m_nPitch = params.pitch;

	CPASAttenuationFilter filter( pOwner );
	CSoundPatch* pSound = ENVELOPE_CONTROLLER.SoundCreate( filter, pOwner->entindex(), es );

	return pSound;
}

void P3_SoundHelper::DestroySound( CSoundPatch* pSound )
{
	if ( pSound )
	{
		ENVELOPE_CONTROLLER.SoundDestroy( pSound );
	}
}

bool P3_SoundHelper::GetSoundParameters( const char* name, CSoundParameters& params )
{
	Assert( name && name[0] );

	if ( !CBaseEntity::GetParametersForSound( name, params, NULL ) )
	{
		DevWarning( "GetSoundParameters: can't get sound parameters: %s\n", name );
		params = CSoundParameters();
		return false;
	}

	return true;
}

bool P3_SoundHelper::GetSoundParameters( CSoundPatch* pSound, CSoundParameters& params )
{	
	//const char* name = STRING( ENVELOPE_CONTROLLER.SoundGetScriptName( pSound ) );
	//return GetSoundParameters( name, params );
}

void P3_SoundHelper::PlaySound( CSoundPatch* pSound )
{
	Assert( pSound );

	CSoundParameters params;
	if ( !GetSoundParameters( pSound, params ) )
	{
		return;
	}

	ENVELOPE_CONTROLLER.Play( pSound, params.volume, params.pitch );
}

void P3_SoundHelper::StopSound( CSoundPatch* pSound )
{
	if ( pSound )
	{
		ENVELOPE_CONTROLLER.Shutdown( pSound );
	}
}

bool P3_SoundHelper::IsPlaying( CSoundPatch* pSound )
{
	Assert( pSound );
	//return ENVELOPE_CONTROLLER.IsPlaying( pSound );
	return 0;
}

void P3_SoundHelper::FadeIn( CSoundPatch* pSound, float flTime )
{
	Assert( pSound );
	
	CSoundParameters params;
	GetSoundParameters( pSound, params );

	ENVELOPE_CONTROLLER.SoundChangeVolume( pSound, params.volume, flTime ); 
}

void P3_SoundHelper::FadeOut( CSoundPatch* pSound, float flTime )
{
	Assert( pSound );
	ENVELOPE_CONTROLLER.SoundFadeOut( pSound, flTime, false );
}

void P3_SoundHelper::CrossFade( CSoundPatch* pSound1, CSoundPatch* pSound2, float flFadeOut, float flFadeIn )
{
	Assert( pSound1 );
	Assert( pSound2 );

	FadeOut( pSound1, flFadeOut );
	PlaySound( pSound2 );
	SetVolume( pSound2, 0 );
	FadeIn( pSound2, flFadeIn );
}

void P3_SoundHelper::ReplaceSound( CSoundPatch* pSound1, CSoundPatch* pSound2 )
{
	Assert( pSound1 );
	Assert( pSound2 );

	StopSound( pSound1 );
	StopSound( pSound2 );
	PlaySound( pSound2 );
}

void P3_SoundHelper::SetVolume( CSoundPatch* pSound, float flVolume )
{
	ChangeVolume( pSound, flVolume, 0 );
}

void P3_SoundHelper::ChangeVolume( CSoundPatch* pSound, float flVolume, float flTime )
{
	Assert( pSound );
	ENVELOPE_CONTROLLER.SoundChangeVolume( pSound, flVolume, flTime );
}

float P3_SoundHelper::GetVolume( CSoundPatch* pSound )
{
	Assert( pSound );
	return ENVELOPE_CONTROLLER.SoundGetVolume( pSound );
}

void P3_SoundHelper::SetPitch( CSoundPatch* pSound, float flPitch )
{
	ChangePitch( pSound, flPitch, 0 );
}

void P3_SoundHelper::ChangePitch( CSoundPatch* pSound, float flPitch, float flTime )
{
	Assert( pSound );
	ENVELOPE_CONTROLLER.SoundChangeVolume( pSound, flPitch, flTime );
}

float P3_SoundHelper::GetPitch( CSoundPatch* pSound )
{
	Assert( pSound );
	return ENVELOPE_CONTROLLER.SoundGetPitch( pSound );
}

//-----------------------------------------------------------------------------
// P3_IdleSound
//-----------------------------------------------------------------------------

P3_IdleSound::P3_IdleSound()
{
	m_pOwner = NULL;
	m_szSoundName[0] = '\0';

	m_flIntervalMin = 1;
	m_flIntervalMax = 3;

	m_bEnabled = true;

	m_flNextSoundTime = 0;
}

void P3_IdleSound::Init( CBaseEntity* pOwner, const char* name )
{
	Assert( m_pOwner );

	m_pOwner = pOwner;
	Q_strcpy( m_szSoundName, name );
}

void P3_IdleSound::SetInterval( float flMin, float flMax )
{
	m_flIntervalMin = flMin;
	m_flIntervalMax = flMax;
}

void P3_IdleSound::Enable( bool bEnable )
{
	m_bEnabled = bEnable;
}

void P3_IdleSound::Precache()
{
	CBaseEntity::PrecacheScriptSound( m_szSoundName );
}

void P3_IdleSound::Update()
{
	Assert( m_pOwner );

	if ( m_bEnabled && gpGlobals->curtime > m_flNextSoundTime && gpGlobals->curtime > 3 )
	{
		m_pOwner->EmitSound( m_szSoundName );
		m_flNextSoundTime = gpGlobals->curtime;

		if ( m_flIntervalMin == m_flIntervalMax )
		{
			m_flNextSoundTime += m_flIntervalMin;
		}
		else
		{
			m_flNextSoundTime += RandomFloat( m_flIntervalMin, m_flIntervalMax );
		}
	}
}
