//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: Used to calculate the player's view in the vehicle
//
//=============================================================================

#include "cbase.h"
#include "vehicles\p3_vehicle_viewblend_shared.h"

#ifdef CLIENT_DLL

// Client includes
#include "p3\vehicles\p3_c_prop_vehicle.h"
#include "view.h"

#else
// Server include
#include "p3\vehicles\p3_vehicle_base.h"

#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL

extern ConVar default_fov;

#define CP3_PropVehicleDriveable C_P3_PropVehicleDriveable

#endif // CLIENT_DLL

extern ConVar r_VehicleViewDampen;

BEGIN_SIMPLE_DATADESC( P3_ViewSmoothingData_t )
	DEFINE_FIELD( vecAnglesSaved, FIELD_VECTOR ),
	DEFINE_FIELD( vecOriginSaved, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( vecAngleDiffSaved, FIELD_VECTOR ),
	DEFINE_FIELD( vecAngleDiffMin, FIELD_VECTOR ),
	DEFINE_FIELD( bRunningEnterExit, FIELD_BOOLEAN ),
	DEFINE_FIELD( bWasRunningAnim, FIELD_BOOLEAN ),
	DEFINE_FIELD( flEnterExitStartTime, FIELD_FLOAT ),
	DEFINE_FIELD( flEnterExitDuration, FIELD_FLOAT ),
	DEFINE_FIELD( flFOV, FIELD_FLOAT ),

	// These are filled out in the vehicle's constructor:
	//CBaseAnimating	*pVehicle;
	//bool	bClampEyeAngles;
	//float	flPitchCurveZero;
	//float	flPitchCurveLinear;
	//float	flRollCurveZero;
	//float	flRollCurveLinear;
	//ViewLockData_t pitchLockData;
	//ViewLockData_t rollLockData;
	//bool bDampenEyePosition;
END_DATADESC()

// remaps an angular variable to a 3 band function:
// 0 <= t < start :		f(t) = 0
// start <= t <= end :	f(t) = end * spline(( t-start) / (end-start) )  // s curve between clamped and linear
// end < t :			f(t) = t
float RemapAngleRange( float startInterval, float endInterval, float value, P3_RemapAngleRange_CurvePart_t *peCurvePart )
{
	// Fixup the roll
	value = AngleNormalize( value );
	float absAngle = fabs(value);

	// beneath cutoff?
	if ( absAngle < startInterval )
	{
		if ( peCurvePart )
		{
			*peCurvePart = P3_RemapAngleRange_CurvePart_Zero;
		}
		value = 0;
	}
	// in spline range?
	else if ( absAngle <= endInterval )
	{
		float newAngle = SimpleSpline( (absAngle - startInterval) / (endInterval-startInterval) ) * endInterval;

		// grab the sign from the initial value
		if ( value < 0 )
		{
			newAngle *= -1;
		}

		if ( peCurvePart )
		{
			*peCurvePart = P3_RemapAngleRange_CurvePart_Spline;
		}
		value = newAngle;
	}
	// else leave it alone, in linear range
	else if ( peCurvePart )
	{
		*peCurvePart = P3_RemapAngleRange_CurvePart_Linear;
	}

	return value;
}

//-----------------------------------------------------------------------------
// Purpose: For a given degree of freedom, blends between the raw and clamped
//			view depending on this vehicle's preferences. When vehicles wreck
//			catastrophically, it's often better to lock the view for a little
//			while until things settle down than to keep trying to clamp/flatten
//			the view artificially because we can never really catch up with
//			the chaotic flipping.
//-----------------------------------------------------------------------------
float ApplyViewLocking( float flAngleRaw, float flAngleClamped, P3_ViewLockData_t &lockData, P3_RemapAngleRange_CurvePart_t eCurvePart )
{
	// If we're set up to never lock this degree of freedom, return the clamped value.
	if ( lockData.flLockInterval == 0 )
		return flAngleClamped;

	float flAngleOut = flAngleClamped;

	// Lock the view if we're in the linear part of the curve, and keep it locked
	// until some duration after we return to the flat (zero) part of the curve.
	if ( ( eCurvePart == P3_RemapAngleRange_CurvePart_Linear ) ||
		( lockData.bLocked && ( eCurvePart == P3_RemapAngleRange_CurvePart_Spline ) ) )
	{
		//Msg( "LOCKED\n" );
		lockData.bLocked = true;
		lockData.flUnlockTime = gpGlobals->curtime + lockData.flLockInterval;
		flAngleOut = flAngleRaw;
	}
	else
	{
		if ( ( lockData.bLocked ) && ( gpGlobals->curtime > lockData.flUnlockTime ) )
		{
			lockData.bLocked = false;
			if ( lockData.flUnlockBlendInterval > 0 )
			{
				lockData.flUnlockTime = gpGlobals->curtime;
			}
			else
			{
				lockData.flUnlockTime = 0;
			}
		}

		if ( !lockData.bLocked )
		{
			if ( lockData.flUnlockTime != 0 )
			{
				// Blend out from the locked raw view (no remapping) to a remapped view.
				float flBlend = RemapValClamped( gpGlobals->curtime - lockData.flUnlockTime, 0, lockData.flUnlockBlendInterval, 0, 1 );
				//Msg( "BLEND %f\n", flBlend );

				flAngleOut = Lerp( flBlend, flAngleRaw, flAngleClamped );
				if ( flBlend >= 1.0f )
				{
					lockData.flUnlockTime = 0;
				}
			}
			else
			{
				// Not blending out from a locked view to a remapped view.
				//Msg( "CLAMPED\n" );
				flAngleOut = flAngleClamped;
			}
		}
		else
		{
			//Msg( "STILL LOCKED\n" );
			flAngleOut = flAngleRaw;
		}
	}

	return flAngleOut;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pData - 
//			vehicleEyeAngles - 
//-----------------------------------------------------------------------------
void RemapViewAngles( P3_ViewSmoothingData_t *pData, QAngle &vehicleEyeAngles )
{
	QAngle vecEyeAnglesRemapped;

	// Clamp pitch.
	P3_RemapAngleRange_CurvePart_t ePitchCurvePart;
	vecEyeAnglesRemapped.x = RemapAngleRange( pData->flPitchCurveZero, pData->flPitchCurveLinear, vehicleEyeAngles.x, &ePitchCurvePart );

	vehicleEyeAngles.z = vecEyeAnglesRemapped.z = AngleNormalize( vehicleEyeAngles.z );

	// Blend out the roll dampening as our pitch approaches 90 degrees, to avoid gimbal lock problems.
	float flBlendRoll = 1.0;
	if ( fabs( vehicleEyeAngles.x ) > 60 )
	{
		flBlendRoll = RemapValClamped( fabs( vecEyeAnglesRemapped.x ), 60, 80, 1, 0);
	}

	P3_RemapAngleRange_CurvePart_t eRollCurvePart;
	float flRollDamped = RemapAngleRange( pData->flRollCurveZero, pData->flRollCurveLinear, vecEyeAnglesRemapped.z, &eRollCurvePart );
	vecEyeAnglesRemapped.z = Lerp( flBlendRoll, vecEyeAnglesRemapped.z, flRollDamped );

	//Msg("PITCH ");
	vehicleEyeAngles.x = ApplyViewLocking( vehicleEyeAngles.x, vecEyeAnglesRemapped.x, pData->pitchLockData, ePitchCurvePart );

	//Msg("ROLL ");
	vehicleEyeAngles.z = ApplyViewLocking( vehicleEyeAngles.z, vecEyeAnglesRemapped.z, pData->rollLockData, eRollCurvePart );
}

//-----------------------------------------------------------------------------
// Purpose: Vehicle dampening shared between server and client
//-----------------------------------------------------------------------------
void P3_SharedVehicleViewSmoothing(CBasePlayer *pPlayer, 
								Vector *pAbsOrigin, QAngle *pAbsAngles, 
								bool bEnterAnimOn, bool bExitAnimOn, 
								const Vector &vecEyeExitEndpoint, 
								P3_ViewSmoothingData_t *pData, 
								float *pFOV )
{	
	int eyeAttachmentIndex = pData->pViewBody->LookupAttachment( "vehicle_driver_eyes" );
	matrix3x4_t vehicleEyePosToWorld;
	Vector vehicleEyeOrigin;
	QAngle vehicleEyeAngles;
	pData->pViewBody->GetAttachment( eyeAttachmentIndex, vehicleEyeOrigin, vehicleEyeAngles );
	//AngleMatrix( vehicleEyeAngles, vehicleEyePosToWorld );

	// Dampen the eye positional change as we drive around.
	*pAbsAngles = pPlayer->EyeAngles();

	if ( r_VehicleViewDampen.GetInt() && pData->bDampenEyePosition )
	{
		CP3_PropVehicleDriveable *pDriveable = assert_cast<CP3_PropVehicleDriveable*>(pData->pVehicle);
		pDriveable->DampenEyePosition( vehicleEyeOrigin, vehicleEyeAngles );
	}

	// Started running an entry or exit anim?
	bool bRunningAnim = ( bEnterAnimOn || bExitAnimOn );
	if ( bRunningAnim && !pData->bWasRunningAnim )
	{
		pData->bRunningEnterExit = true;
		pData->flEnterExitStartTime = gpGlobals->curtime;
		pData->flEnterExitDuration = pData->pViewBody->SequenceDuration( pData->pVehicle->GetSequence() );

#ifdef CLIENT_DLL
		pData->vecOriginSaved = PrevMainViewOrigin();
		pData->vecAnglesSaved = PrevMainViewAngles();
#endif

		// Save our initial angular error, which we will blend out over the length of the animation.
		pData->vecAngleDiffSaved.x = AngleDiff( vehicleEyeAngles.x, pData->vecAnglesSaved.x );
		pData->vecAngleDiffSaved.y = AngleDiff( vehicleEyeAngles.y, pData->vecAnglesSaved.y );
		pData->vecAngleDiffSaved.z = AngleDiff( vehicleEyeAngles.z, pData->vecAnglesSaved.z );

		pData->vecAngleDiffMin = pData->vecAngleDiffSaved;
	}

	pData->bWasRunningAnim = bRunningAnim;

	float frac = 0;
	float flFracFOV = 0;

	// If we're in an enter/exit animation, blend the player's eye angles to the attachment's
	if ( bRunningAnim || pData->bRunningEnterExit )
	{
		*pAbsAngles = vehicleEyeAngles;

		// Forward integrate to determine the elapsed time in this entry/exit anim.
		frac = ( gpGlobals->curtime - pData->flEnterExitStartTime ) / pData->flEnterExitDuration;
		frac = clamp( frac, 0.0f, 1.0f );

		flFracFOV = ( gpGlobals->curtime - pData->flEnterExitStartTime ) / ( pData->flEnterExitDuration * 0.85f );
		flFracFOV = clamp( flFracFOV, 0.0f, 1.0f );

		//Msg("Frac: %f\n", frac );

		if ( frac < 1.0 )
		{
			// Blend to the desired vehicle eye origin
			//Vector vecToView = (vehicleEyeOrigin - PrevMainViewOrigin());
			//vehicleEyeOrigin = PrevMainViewOrigin() + (vecToView * SimpleSpline(frac));
			//debugoverlay->AddBoxOverlay( vehicleEyeOrigin, -Vector(1,1,1), Vector(1,1,1), vec3_angle, 0,255,255, 64, 10 );
		}
		else 
		{
			pData->bRunningEnterExit = false;

			// Enter animation has finished, align view with the eye attachment point
			// so they can start mouselooking around.
			if ( !bExitAnimOn )
			{
				Vector localEyeOrigin;
				QAngle localEyeAngles;

				pData->pViewBody->GetAttachmentLocal( eyeAttachmentIndex, localEyeOrigin, localEyeAngles );
#ifdef CLIENT_DLL
				engine->SetViewAngles( localEyeAngles );
#endif
			}
		}
	}

	//// Damp out some of the vehicle motion (neck/head would do this)
	if ( pData->bClampEyeAngles )
	{
		RemapViewAngles( pData, vehicleEyeAngles );
	}

	*pAbsOrigin = vehicleEyeOrigin;

	// If we're playing an entry or exit animation...
	if ( bRunningAnim || pData->bRunningEnterExit )
	{
		float flSplineFrac = clamp( SimpleSpline( frac ), 0, 1 );

		// Blend out the error between the player's initial eye angles and the animation's initial
		// eye angles over the duration of the animation. 
		QAngle vecAngleDiffBlend = ( ( 1 - flSplineFrac ) * pData->vecAngleDiffSaved );

		// If our current error is less than the error amount that we're blending 
		// out, use that. This lets the angles converge as quickly as possible.
		QAngle vecAngleDiffCur;
		vecAngleDiffCur.x = AngleDiff( vehicleEyeAngles.x, pData->vecAnglesSaved.x );
		vecAngleDiffCur.y = AngleDiff( vehicleEyeAngles.y, pData->vecAnglesSaved.y );
		vecAngleDiffCur.z = AngleDiff( vehicleEyeAngles.z, pData->vecAnglesSaved.z );

		// In either case, never increase the error, so track the minimum error and clamp to that.
		for (int i = 0; i < 3; i++)
		{
			if ( fabs(vecAngleDiffCur[i] ) < fabs( pData->vecAngleDiffMin[i] ) )
			{
				pData->vecAngleDiffMin[i] = vecAngleDiffCur[i];
			}

			if ( fabs(vecAngleDiffBlend[i] ) < fabs( pData->vecAngleDiffMin[i] ) )
			{
				pData->vecAngleDiffMin[i] = vecAngleDiffBlend[i];
			}
		}

		// Add the error to the animation's eye angles.
		*pAbsAngles -= pData->vecAngleDiffMin;

		// Use this as the basis for the next error calculation.
		pData->vecAnglesSaved = *pAbsAngles;

		//if ( gpGlobals->frametime )
		//{
		//	Msg("Angle : %.2f %.2f %.2f\n", target.x, target.y, target.z );
		//}
		//Msg("Prev: %.2f %.2f %.2f\n", pData->vecAnglesSaved.x, pData->vecAnglesSaved.y, pData->vecAnglesSaved.z );

		Vector vecAbsOrigin = *pAbsOrigin;

		// If we're exiting, our desired position is the server-sent exit position
		if ( bExitAnimOn )
		{
			//debugoverlay->AddBoxOverlay( vecEyeExitEndpoint, -Vector(1,1,1), Vector(1,1,1), vec3_angle, 255,255,255, 64, 10 );

			// Blend to the exit position
			*pAbsOrigin = Lerp( flSplineFrac, vecAbsOrigin, vecEyeExitEndpoint );
			
			if ( pFOV != NULL )
			{
				float flDefaultFOV;
#ifdef CLIENT_DLL
				flDefaultFOV = default_fov.GetFloat();
#else
				flDefaultFOV = pPlayer->GetDefaultFOV();
#endif
				*pFOV = Lerp( flFracFOV, pData->flFOV, flDefaultFOV );
			}
		}
		else
		{
			// Blend from our starting position to the desired origin
			*pAbsOrigin = Lerp( flSplineFrac, pData->vecOriginSaved, vecAbsOrigin );
			
			if ( pFOV != NULL )
			{
				float flDefaultFOV;
#ifdef CLIENT_DLL
				flDefaultFOV = default_fov.GetFloat();
#else
				flDefaultFOV = pPlayer->GetDefaultFOV();
#endif
				*pFOV = Lerp( flFracFOV, flDefaultFOV, pData->flFOV );
			}
		}
	}
	else if ( pFOV != NULL )
	{
		// Not running an entry/exit anim. Just use the vehicle's FOV.
		*pFOV = pData->flFOV;
	}
}
