#ifndef _C_FUNC_SIMPLELADDER_H_
#define _C_FUNC_SIMPLELADDER_H_
#ifdef _WIN32
#pragma once
#endif

#include "c_baseentity.h"

class C_FuncSimpleLadder : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_FuncSimpleLadder, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	virtual void Spawn( void );
	virtual void Activate( void );
	//virtual int DrawModel( int flags, const RenderableInstance_t &instance );
	virtual int DrawModel( int flags );

	virtual int DrawDebugTextOverlays( void );
	virtual void DrawDebugGeometryOverlays( void );
	
	Vector m_climbableNormal;
};

#endif // _C_FUNC_SIMPLELADDER_H_