#ifndef _FUNC_SIMPLELADDER_H_
#define _FUNC_SIMPLELADDER_H_
#ifdef _WIN32
#pragma once
#endif

class CFuncSimpleLadder : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncSimpleLadder, CBaseEntity );
	DECLARE_SERVERCLASS();

	virtual void Spawn( void );
	virtual void Activate( void );
	virtual bool KeyValue( const char *szKeyName, const char *szValue );

	virtual int DrawDebugTextOverlays( void );
	virtual void DrawDebugGeometryOverlays( void );
	
	CNetworkVector( m_climbableNormal );
};

#endif // _FUNC_SIMPLELADDER_H_