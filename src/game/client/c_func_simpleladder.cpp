#include "cbase.h"
#include "c_func_simpleladder.h"
#include "model_types.h"
#ifdef HL2MP
#include "HL2MP_Gamerules.h"
#endif

IMPLEMENT_CLIENTCLASS_DT( C_FuncSimpleLadder, DT_FuncSimpleLadder, CFuncSimpleLadder )
	RecvPropVector( RECVINFO( m_climbableNormal ) )
END_RECV_TABLE()

void C_FuncSimpleLadder::Spawn( void )
{

}

void C_FuncSimpleLadder::Activate( void )
{
	BaseClass::Activate();
}

//int C_FuncSimpleLadder::DrawModel( int flags, const RenderableInstance_t &instance )
int C_FuncSimpleLadder::DrawModel( int flags )
{
	// This is a real ladder brush (ladder texture), so it has to be drawn.
	// Earlier versions of this class were an invisible ladder volume and
	// skipped the normal render pass.
	return BaseClass::DrawModel( flags );
}

int C_FuncSimpleLadder::DrawDebugTextOverlays( void )
{
	return 0;
}

void C_FuncSimpleLadder::DrawDebugGeometryOverlays( void )
{

}