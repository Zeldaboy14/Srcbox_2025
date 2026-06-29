//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_EFFECTS_H
#define C_EFFECTS_H
#ifdef _WIN32
#pragma once
#endif


// Draw rain effects.
void DrawPrecipitation();

//-----------------------------------------------------------------------------
// Precipitation blocker entity
//-----------------------------------------------------------------------------
class C_PrecipitationBlocker : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_PrecipitationBlocker, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_PrecipitationBlocker();
	virtual ~C_PrecipitationBlocker();
};

#endif // C_EFFECTS_H
