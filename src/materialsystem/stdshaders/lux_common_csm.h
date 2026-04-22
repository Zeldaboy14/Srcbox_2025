//
//===================== File of the LUX Shader Project =====================//
//
//	Initial D.	:	24.01.2023 DMY
//	Last Change :	12.06.2025 DMY
//
//	Purpose of this File :	Shader Constant Register Declaration for
//							flashlight behaviour
//
//==========================================================================//

#ifndef LUX_COMMON_CSM_H_
#define LUX_COMMON_CSM_H_

//==========================================================================//
// PixelShader *Float* Constant Registers
//==========================================================================//

// We reuse these flashlight constants.
// Flashlight registers aren't used during the non-flashlight pass
// Therefore its safe to use these on all shaders.
const float4	g_CascadedResolutions			: register(c2);		// Resolutions on .xy, .zw --- !!! precomputed RCP values [Epsilon] !!! ---
const float4	g_CascadedPosition				: register(c13);	// Consider this a SunPos
const float4	g_CascadedStepData				: register(c14);
const float4x4	g_CascadedWorldToTexture		: register(c15);
//				g_CascadedWorldToTexture		: register(c16);
//				g_CascadedWorldToTexture		: register(c17);
//				g_CascadedWorldToTexture		: register(c18);

// Unused other flashlight registers :
const float4	g_CascadedColor					: register(c28);
// const float4 cFlashlightColor				: register(c28);
// const float4 cFlashlightScreenScale			: register(c31);

// Put on c60 to leave space for some other things
const float4x4	g_SnapshotWorldToTexture		: register(c60);
//				g_CascadedWorldToTexture		: register(c61);
//				g_CascadedWorldToTexture		: register(c62);
//				g_CascadedWorldToTexture		: register(c63);

// This is not functional
#define NVIDIA_PCF_POISSON	0
#define ATI_NOPCF			1
#define ATI_NO_PCF_FETCH4	2
#define NVIDIA_PCF_GAUSSIAN 3

//==========================================================================//
//	Declaring Samplers. We only have 16 on SM3.0. Ranging from 0-15
//	So we have to reuse them depending on what shader we are on and what features we require...
//	Note : Local definitions might be different. We don't always need to have clear names.
//==========================================================================//

 #define SNAPSHOTTING 1

// Alternate Layout 1-2 is supposed to be used with shaders that use 13 and 15 for other things
// For example : PBR uses 13 for a second cubemap ( cubemap lerp )
// We use 13 because thats the only sampler on all shaders that is usually only reserved by selfilluming Materials
// FIXME for CSM Refactor later: Make this a !defined() then let the Shader itself decide where to put these.
#if defined(CASCADED_LAYOUT1)

// This layout is intended for use with WVT
// It makes it impossible to have PhongWarp and PhongExponent with CSM
// We simply do not have enough samplers for regular WVT because of all th ergb stuff Valve did.
// Your other options would be using the lightwarp/parallaxmap sampler for Snapshotting,
// and still saying RIP to Phongwarp, which should be fine anyways
// Yeah you can already tell the issue with that, you might want parallax mapping.
sampler Sampler_CascadedDepth : register(s7);

	#if SNAPSHOTTING
	sampler Sampler_SnapshotDepth : register(s8);
	#endif
#elif defined(CASCADED_LAYOUT2)

// You can put your own here, if you need more just put CASCADED_LAYOUT3, 4... etc
/*
	sampler Sampler_CascadedDepth : register(s);

	#if SNAPSHOTTING
	sampler Sampler_SnapshotDepth : register(s);
	#endif
*/
#else // default layout

// Default layout goes with the assumption you are using a LUX shader
// s11 has the lightmap, then 12 gets a blendmodulate texture an its otherwise unused
// Due to this very consistent structure we can move SSS on s11
// Lightmapped Models weren't planned to have that either way.
// That way we can use 12 for depth on all models and regular brushes
// Regular LUX uses 12 only for $BlendModulateTexture, thus its perfect in every other scenario
// We need s9, its only used for $envmapmask2 on WVT. So ideal since it has a different layout anyways
sampler Sampler_CascadedDepth : register(s12);

	#if SNAPSHOTTING
	sampler Sampler_SnapshotDepth : register(s9);
	#endif
#endif

float2 vPoissonOffset[8] = {	float2( 0.3475f,	0.0042f),
								float2( 0.8806f,	0.3430f),
								float2(-0.0041f,   -0.6197f),
								float2( 0.0472f,	0.4964f),
								float2(-0.3730f,	0.0874f),
								float2(-0.9217f,   -0.3177f),
								float2(-0.6289f,	0.7388f),
								float2( 0.5744f,   -0.7741f)	};

//==========================================================================//
//	Stock SDK Shadow filters following :
//	The actual Cascaded functions follow...
//==========================================================================//

//==========================================================================//
// NVidia PCF Poisson :
// This Filter does not appear to run on all hardware from my observation
// Assumption : tex2Dproj causes problems on AMD hardware
//==========================================================================//
float Filter_Nvidia_PCF_Poisson(sampler DepthSampler, float f1ObjectDepth, float3 RMatTop, float3 RMatBottom)
{
	// Preparation
	float4 f4LightDepths = 0.0f;
	float4 f4Accumulated = 0.0f;
	float2 f2RotationOffset = 0.0f;

	// Start Filtering..
		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[0].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[0].xy) + RMatBottom.z;
		f4LightDepths.x += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[1].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[1].xy) + RMatBottom.z;
		f4LightDepths.y += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[2].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[2].xy) + RMatBottom.z;
		f4LightDepths.z += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[3].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[3].xy) + RMatBottom.z;
		f4LightDepths.w += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[4].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[4].xy) + RMatBottom.z;
		f4LightDepths.x += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[5].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[5].xy) + RMatBottom.z;
		f4LightDepths.y += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[6].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[6].xy) + RMatBottom.z;
		f4LightDepths.z += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

		f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[7].xy) + RMatTop.z;
		f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[7].xy) + RMatBottom.z;
		f4LightDepths.w += tex2Dproj(DepthSampler, float4(f2RotationOffset, f1ObjectDepth, 1)).x;

	// Return the average
	return dot(f4LightDepths, float4(0.25f, 0.25f, 0.25f, 0.25f));
}

//==========================================================================//
// ATI No PCF Fetch4 :
//==========================================================================//
float Filter_ATI_FETCH4(sampler DepthSampler, float f1ObjectDepth, float3 RMatTop, float3 RMatBottom)
{
	// Preparation
	float4 f4LightDepths = 0.0f;
	float4 f4Accumulated = 0.0f;
	float2 f2RotationOffset = 0.0f;

	// Start Filtering..
		// Original VALVE TODO :
		/*
		TODO: Fix this contact hardening stuff

		float flNumCloserSamples = 1;
		float flAccumulatedCloserSamples = f1ObjectDepth;
		float4 vBlockerDepths;

		// First, search for blockers
		for( int j=0; j<8; j++ )
		{
		f2RotationOffset.x = dot (RMatTop.xy,    vPoissonOffset[j].xy) + RMatTop.z;
		f2RotationOffset.y = dot (RMatBottom.xy, vPoissonOffset[j].xy) + RMatBottom.z;
		vBlockerDepths = tex2D( Sampler_ShadowDepth, f2RotationOffset.xy );

		// Which samples are closer than the pixel we're rendering?
		float4 vCloserSamples = (vBlockerDepths < f1ObjectDepth.xxxx );			// Binary comparison results
		flNumCloserSamples += dot( vCloserSamples, float4(1, 1, 1, 1) );		// How many samples are closer than receiver?
		flAccumulatedCloserSamples += dot (vCloserSamples, vBlockerDepths );	// Total depths from samples closer than receiver
		}

		float flBlockerDepth = flAccumulatedCloserSamples / flNumCloserSamples;
		float flContactHardeningScale = (f1ObjectDepth - flBlockerDepth) / flBlockerDepth;

		// Scale the kernel
		RMatTop.xy    *= flContactHardeningScale;
		RMatBottom.xy *= flContactHardeningScale;
		*/

		for (int i = 0; i<8; i++)
		{
			f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[i].xy) + RMatTop.z;
			f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[i].xy) + RMatBottom.z;
			f4LightDepths = tex2D(DepthSampler, f2RotationOffset.xy);
			f4Accumulated += (f4LightDepths > f1ObjectDepth.xxxx);
		}

	// Return the average
	return dot(f4Accumulated, float4(1.0f / 32.0f, 1.0f / 32.0f, 1.0f / 32.0f, 1.0f / 32.0f));
}

//==========================================================================//
// ATI No PCF Fetch4 :
//==========================================================================//
float Filter_ATI_NO_PCF(sampler DepthSampler, float f1ObjectDepth, float3 RMatTop, float3 RMatBottom)
{
	// Preparation
	float4 f4LightDepths = 0.0f;
	float4 f4Accumulated = 0.0f;
	float2 f2RotationOffset = 0.0f;

	// Start Filtering..
		for (int i = 0; i<2; i++)
		{
			f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[4 * i + 0].xy) + RMatTop.z;
			f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[4 * i + 0].xy) + RMatBottom.z;
			f4LightDepths.x = tex2D(DepthSampler, f2RotationOffset.xy).x;

			f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[4 * i + 1].xy) + RMatTop.z;
			f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[4 * i + 1].xy) + RMatBottom.z;
			f4LightDepths.y = tex2D(DepthSampler, f2RotationOffset.xy).x;

			f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[4 * i + 2].xy) + RMatTop.z;
			f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[4 * i + 2].xy) + RMatBottom.z;
			f4LightDepths.z = tex2D(DepthSampler, f2RotationOffset.xy).x;

			f2RotationOffset.x = dot(RMatTop.xy, vPoissonOffset[4 * i + 3].xy) + RMatTop.z;
			f2RotationOffset.y = dot(RMatBottom.xy, vPoissonOffset[4 * i + 3].xy) + RMatBottom.z;
			f4LightDepths.w = tex2D(DepthSampler, f2RotationOffset.xy).x;

			f4Accumulated += (f4LightDepths > f1ObjectDepth.xxxx);
		}

	return dot(f4Accumulated, float4(0.125, 0.125, 0.125, 0.125));
}

//==========================================================================//
// Nvidia PCF 5x5 Gaussian
// Taken from the S++ repository
// This does not appear to work with all hardware...
//==========================================================================//
float Filter_NVIDIA_PCF_5x5_Gaussian(sampler DepthSampler, float3 f3ClosePosition, float2 f2Epsilon)
{
	float2	f2ShadowMapCenter	= f3ClosePosition.xy;
	float	f1ObjectDepth		= f3ClosePosition.z;

	float f1EpsilonX = f2Epsilon.x;
	float f1TwoEpsilonX = 2.0f * f1EpsilonX;
	float f1EpsilonY = f2Epsilon.y;
	float f1TwoEpsilonY = 2.0f * f1EpsilonY;

	float4 vOneTaps;
	vOneTaps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1TwoEpsilonX, f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vOneTaps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1TwoEpsilonX, f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vOneTaps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1TwoEpsilonX, -f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vOneTaps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1TwoEpsilonX, -f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	float flOneTaps = dot(vOneTaps, float4(1.0f / 331.0f, 1.0f / 331.0f, 1.0f / 331.0f, 1.0f / 331.0f));

	float4 vSevenTaps;
	vSevenTaps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1TwoEpsilonX, 0), f1ObjectDepth, 1)).x;
	vSevenTaps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1TwoEpsilonX, 0), f1ObjectDepth, 1)).x;
	vSevenTaps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0, f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vSevenTaps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0, -f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	float flSevenTaps = dot(vSevenTaps, float4(7.0f / 331.0f, 7.0f / 331.0f, 7.0f / 331.0f, 7.0f / 331.0f));

	float4 vFourTapsA, vFourTapsB;
	vFourTapsA.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1TwoEpsilonX, f1EpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsA.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1EpsilonX, f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsA.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1EpsilonX, f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsA.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1TwoEpsilonX, f1EpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsB.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1TwoEpsilonX, -f1EpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsB.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1EpsilonX, -f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsB.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1EpsilonX, -f1TwoEpsilonY), f1ObjectDepth, 1)).x;
	vFourTapsB.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1TwoEpsilonX, -f1EpsilonY), f1ObjectDepth, 1)).x;
	float flFourTapsA = dot(vFourTapsA, float4(4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f));
	float flFourTapsB = dot(vFourTapsB, float4(4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f));

	float4 v20Taps;
	v20Taps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1EpsilonX, f1EpsilonY), f1ObjectDepth, 1)).x;
	v20Taps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1EpsilonX, f1EpsilonY), f1ObjectDepth, 1)).x;
	v20Taps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1EpsilonX, -f1EpsilonY), f1ObjectDepth, 1)).x;
	v20Taps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1EpsilonX, -f1EpsilonY), f1ObjectDepth, 1)).x;
	float fl20Taps = dot(v20Taps, float4(20.0f / 331.0f, 20.0f / 331.0f, 20.0f / 331.0f, 20.0f / 331.0f));

	float4 v33Taps;
	v33Taps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f1EpsilonX, 0), f1ObjectDepth, 1)).x;
	v33Taps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f1EpsilonX, 0), f1ObjectDepth, 1)).x;
	v33Taps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0, f1EpsilonY), f1ObjectDepth, 1)).x;
	v33Taps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0, -f1EpsilonY), f1ObjectDepth, 1)).x;
	float fl33Taps = dot(v33Taps, float4(33.0f / 331.0f, 33.0f / 331.0f, 33.0f / 331.0f, 33.0f / 331.0f));

	float flCenterTap = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x * (55.0f / 331.0f);

	// Sum all 25 Taps
	return flOneTaps + flSevenTaps + flFourTapsA + flFourTapsB + fl20Taps + fl33Taps + flCenterTap;
}

//==========================================================================//
// Cheap 4 Taps Filter
//==========================================================================//
float Filter_4Tap(sampler DepthSampler, float3 f3ClosePosition, float2 f2Epsilon)
{
	float2 f2ShadowMapCenter = f3ClosePosition.xy;
	float f1ObjectDepth = f3ClosePosition.z;

	float4 f4Taps;
	f4Taps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2( f2Epsilon.x,  f2Epsilon.y), f1ObjectDepth, 1)).x;
	f4Taps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f2Epsilon.x,  f2Epsilon.y), f1ObjectDepth, 1)).x;
	f4Taps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2( f2Epsilon.x, -f2Epsilon.y), f1ObjectDepth, 1)).x;
	f4Taps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f2Epsilon.x, -f2Epsilon.y), f1ObjectDepth, 1)).x;

	float f1Tap = dot(f4Taps, float4(1.0f / 8.0f, 1.0f / 8.0f, 1.0f / 8.0f, 1.0f / 8.0f));
	float f1TapCenter = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x * float(1.0f / 8.0f);

	return f1Tap + f1TapCenter;
}

float Filter_Box(sampler DepthSampler, float3 f3ClosePosition, float2 f2Epsilon)
{
	float2 f2ShadowMapCenter = f3ClosePosition.xy;
	float f1ObjectDepth = f3ClosePosition.z;

	float4 f4Taps;
	f4Taps.x = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f2Epsilon.x, 0.0f), f1ObjectDepth, 1)).x;
	f4Taps.y = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f2Epsilon.x, f2Epsilon.y), f1ObjectDepth, 1)).x;
	f4Taps.z = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0.0f, f2Epsilon.y), f1ObjectDepth, 1)).x;
	f4Taps.w = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x;

	return dot(f4Taps, float4(0.25f, 0.25f, 0.25f, 0.25f));
}

//==========================================================================//
// Experimental Filter
// Thank you https://github.com/TheRealMJP/Shadows for this code implementation!
// Moment Shadow Mapping
//==========================================================================//
float4 GetOptimizedMoments(in float depth)
{
	float square = depth * depth;
	float4 moments = float4(depth, square, square * depth, square * square);
	float4 optimized = mul(moments, float4x4(-2.07224649f, 13.7948857237f, 0.105877704f, 9.7924062118f,
		32.23703778f, -59.4683975703f, -1.9077466311f, -33.7652110555f,
		-68.571074599f, 82.0359750338f, 9.3496555107f, 47.9456096605f,
		39.3703274134f, -35.364903257f, -6.6543490743f, -23.9728048165f));
	optimized[0] += 0.035955884801f;
	return optimized;
}

float4 ConvertOptimizedMoments(in float4 optimizedMoments)
{
	optimizedMoments[0] -= 0.035955884801f;
	return mul(optimizedMoments, float4x4(0.2227744146f, 0.1549679261f, 0.1451988946f, 0.163127443f,
		0.0771972861f, 0.1394629426f, 0.2120202157f, 0.2591432266f,
		0.7926986636f, 0.7963415838f, 0.7258694464f, 0.6539092497f,
		0.0319417555f, -0.1722823173f, -0.2758014811f, -0.3376131734f));
}

// Hamburger looks better now
float ComputeMSMHamburger(in float4 moments, in float fragmentDepth, in float depthBias, in float momentBias)
{
	// Bias input data to avoid artifacts
	float4 b = lerp(moments, float4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
	float3 z;
	z[0] = fragmentDepth - depthBias;

	// Compute a Cholesky factorization of the Hankel matrix B storing only non-
	// trivial entries or related products
	float L32D22 = mad(-b[0], b[1], b[2]);
	float D22 = mad(-b[0], b[0], b[1]);
	float squaredDepthVariance = mad(-b[1], b[1], b[3]);
	float D33D22 = dot(float2(squaredDepthVariance, -L32D22), float2(D22, L32D22));
	float InvD22 = 1.0f / D22;
	float L32 = L32D22 * InvD22;

	// Obtain a scaled inverse image of bz = (1,z[0],z[0]*z[0])^T
	float3 c = float3(1.0f, z[0], z[0] * z[0]);

	// Forward substitution to solve L*c1=bz
	c[1] -= b.x;
	c[2] -= b.y + L32 * c[1];

	// Scaling to solve D*c2=c1
	c[1] *= InvD22;
	c[2] *= D22 / D33D22;

	// Backward substitution to solve L^T*c3=c2
	c[1] -= L32 * c[2];
	c[0] -= dot(c.yz, b.xy);

	// Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions
	// z[1] and z[2]
	float p = c[1] / c[2];
	float q = c[0] / c[2];
	float D = (p * p * 0.25f) - q;
	float r = sqrt(D);
	z[1] = -p * 0.5f - r;
	z[2] = -p * 0.5f + r;

	// Compute the shadow intensity by summing the appropriate weights
	float4 switchVal = (z[2] < z[0]) ? float4(z[1], z[0], 1.0f, 1.0f) :
		((z[1] < z[0]) ? float4(z[0], z[1], 0.0f, 1.0f) :
		float4(0.0f, 0.0f, 0.0f, 0.0f));
	float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1]) / ((z[2] - switchVal[1]) * (z[0] - z[1]));
	float shadowIntensity = switchVal[2] + switchVal[3] * quotient;
	return 1.0f - saturate(shadowIntensity);
}

float ComputeMSMHausdorff(in float4 moments, in float fragmentDepth, in float depthBias, in float momentBias)
{
	// Bias input data to avoid artifacts
	float4 b = lerp(moments, float4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
	float3 z;
	z[0] = fragmentDepth - depthBias;

	// Compute a Cholesky factorization of the Hankel matrix B storing only non-
	// trivial entries or related products
	float L32D22 = mad(-b[0], b[1], b[2]);
	float D22 = mad(-b[0], b[0], b[1]);
	float squaredDepthVariance = mad(-b[1], b[1], b[3]);
	float D33D22 = dot(float2(squaredDepthVariance, -L32D22), float2(D22, L32D22));
	float InvD22 = 1.0f / D22;
	float L32 = L32D22 * InvD22;

	// Obtain a scaled inverse image of bz=(1,z[0],z[0]*z[0])^T
	float3 c = float3(1.0f, z[0], z[0] * z[0]);

	// Forward substitution to solve L*c1=bz
	c[1] -= b.x;
	c[2] -= b.y + L32 * c[1];

	// Scaling to solve D*c2=c1
	c[1] *= InvD22;
	c[2] *= D22 / D33D22;

	// Backward substitution to solve L^T*c3=c2
	c[1] -= L32 * c[2];
	c[0] -= dot(c.yz, b.xy);

	// Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions z[1]
	// and z[2]
	float p = c[1] / c[2];
	float q = c[0] / c[2];
	float D = ((p * p) / 4.0f) - q;
	float r = sqrt(D);
	z[1] = -(p / 2.0f) - r;
	z[2] = -(p / 2.0f) + r;

	float shadowIntensity = 1.0f;

	// Use a solution made of four deltas if the solution with three deltas is invalid
	if (z[1] < 0.0f || z[2] > 1.0f)
	{
		float zFree = ((b[2] - b[1]) * z[0] + b[2] - b[3]) / ((b[1] - b[0]) * z[0] + b[1] - b[2]);
		float w1Factor = (z[0] > zFree) ? 1.0f : 0.0f;
		shadowIntensity = (b[1] - b[0] + (b[2] - b[0] - (zFree + 1.0f) * (b[1] - b[0])) * (zFree - w1Factor - z[0])
			/ (z[0] * (z[0] - zFree))) / (zFree - w1Factor) + 1.0f - b[0];
	}
	// Use the solution with three deltas
	else{
		float4 switchVal = (z[2] < z[0]) ? float4(z[1], z[0], 1.0f, 1.0f) :
			((z[1] < z[0]) ? float4(z[0], z[1], 0.0f, 1.0f) :
			float4(0.0f, 0.0f, 0.0f, 0.0f));
		float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1]) / ((z[2] - switchVal[1]) * (z[0] - z[1]));
		shadowIntensity = switchVal[2] + switchVal[3] * quotient;
	}

	return 1.0f - saturate(shadowIntensity);
}

// VSM

float ChebyshevUpperBound(float2 moments, float mean, float minVariance,
	float lightBleedingReduction)
{
	// Compute variance
	float variance = moments.y - (moments.x * moments.x);
	variance = max(variance, minVariance);

	// Compute probabilistic upper bound
	float d = mean - moments.x;
	float pMax = variance / (variance + (d * d));

	pMax = saturate((pMax - 0.1f) / (1.0f - 0.1f));

	// One-tailed Chebyshev
	return (mean <= moments.x ? 1.0f : pMax);
}

float2 ComputeReceiverPlaneDepthBias(float3 texCoordDX, float3 texCoordDY)
{
	float2 biasUV;
	biasUV.x = texCoordDY.y * texCoordDX.z - texCoordDX.y * texCoordDY.z;
	biasUV.y = texCoordDX.x * texCoordDY.z - texCoordDY.x * texCoordDX.z;
	biasUV *= 1.0f / ((texCoordDX.x * texCoordDY.y) - (texCoordDX.y * texCoordDY.x));
	return biasUV;
}

float LUX_DoShadowNvidiaPCF5x5Gaussian(sampler Sampler_ShadowDepth, const float3 shadowMapPos, float2 f2Epsilon)
{
	float2 f2TwoEpsilon = 2.0f * f2Epsilon;
	float fEpsilonX = f2Epsilon.x;
	float fEpsilonY = f2Epsilon.y;
	float fTwoEpsilonX = f2TwoEpsilon.x;
	float fTwoEpsilonY = f2TwoEpsilon.y;

	float3 shadowMapCenter_objDepth = shadowMapPos;

	float2 shadowMapCenter = shadowMapCenter_objDepth.xy;			// Center of shadow filter
	float objDepth = shadowMapCenter_objDepth.z;					// Object depth in shadow space

	float4 vOneTaps;
	vOneTaps.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fTwoEpsilonX, fTwoEpsilonY), objDepth, 1)).x;
	vOneTaps.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fTwoEpsilonX, fTwoEpsilonY), objDepth, 1)).x;
	vOneTaps.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fTwoEpsilonX, -fTwoEpsilonY), objDepth, 1)).x;
	vOneTaps.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fTwoEpsilonX, -fTwoEpsilonY), objDepth, 1)).x;
	float flOneTaps = dot(vOneTaps, float4(1.0f / 331.0f, 1.0f / 331.0f, 1.0f / 331.0f, 1.0f / 331.0f));

	float4 vSevenTaps;
	vSevenTaps.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fTwoEpsilonX, 0), objDepth, 1)).x;
	vSevenTaps.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fTwoEpsilonX, 0), objDepth, 1)).x;
	vSevenTaps.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(0, fTwoEpsilonY), objDepth, 1)).x;
	vSevenTaps.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(0, -fTwoEpsilonY), objDepth, 1)).x;
	float flSevenTaps = dot(vSevenTaps, float4(7.0f / 331.0f, 7.0f / 331.0f, 7.0f / 331.0f, 7.0f / 331.0f));

	float4 vFourTapsA, vFourTapsB;
	vFourTapsA.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fTwoEpsilonX, fEpsilonY), objDepth, 1)).x;
	vFourTapsA.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fEpsilonX, fTwoEpsilonY), objDepth, 1)).x;
	vFourTapsA.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fEpsilonX, fTwoEpsilonY), objDepth, 1)).x;
	vFourTapsA.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fTwoEpsilonX, fEpsilonY), objDepth, 1)).x;
	vFourTapsB.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fTwoEpsilonX, -fEpsilonY), objDepth, 1)).x;
	vFourTapsB.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fEpsilonX, -fTwoEpsilonY), objDepth, 1)).x;
	vFourTapsB.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fEpsilonX, -fTwoEpsilonY), objDepth, 1)).x;
	vFourTapsB.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fTwoEpsilonX, -fEpsilonY), objDepth, 1)).x;
	float flFourTapsA = dot(vFourTapsA, float4(4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f));
	float flFourTapsB = dot(vFourTapsB, float4(4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f, 4.0f / 331.0f));

	float4 v20Taps;
	v20Taps.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fEpsilonX, fEpsilonY), objDepth, 1)).x;
	v20Taps.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fEpsilonX, fEpsilonY), objDepth, 1)).x;
	v20Taps.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fEpsilonX, -fEpsilonY), objDepth, 1)).x;
	v20Taps.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fEpsilonX, -fEpsilonY), objDepth, 1)).x;
	float fl20Taps = dot(v20Taps, float4(20.0f / 331.0f, 20.0f / 331.0f, 20.0f / 331.0f, 20.0f / 331.0f));

	float4 v33Taps;
	v33Taps.x = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(fEpsilonX, 0), objDepth, 1)).x;
	v33Taps.y = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(-fEpsilonX, 0), objDepth, 1)).x;
	v33Taps.z = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(0, fEpsilonY), objDepth, 1)).x;
	v33Taps.w = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter + float2(0, -fEpsilonY), objDepth, 1)).x;
	float fl33Taps = dot(v33Taps, float4(33.0f / 331.0f, 33.0f / 331.0f, 33.0f / 331.0f, 33.0f / 331.0f));

	float flCenterTap = tex2Dproj(Sampler_ShadowDepth, float4(shadowMapCenter, objDepth, 1)).x * (55.0f / 331.0f);

	// Sum all 25 Taps
	return flOneTaps + flSevenTaps + flFourTapsA + flFourTapsB + fl20Taps + fl33Taps + flCenterTap;
}

float Filter_ShiroExperimental(sampler DepthSampler, float3 f3ClosePosition, float2 f2Epsilon)
{
	float2 f2ShadowMapCenter = f3ClosePosition.xy;
	float f1ObjectDepth = f3ClosePosition.z;

//	float3 shadowPosDX = ddx(f3ClosePosition);
//	float3 shadowPosDY = ddy(f3ClosePosition);

//	float f1CurrentDepth = tex2D(DepthSampler, f3ClosePosition.xy).x;

	// this doesn't help.
	/*
	float2 receiverPlaneDepthBias = ComputeReceiverPlaneDepthBias(shadowPosDX, shadowPosDY);

	// Static depth biasing to make up for incorrect fractional sampling on the shadow map grid
	float fractionalSamplingError = dot(float2(1.0f, 1.0f) * f2Epsilon, abs(receiverPlaneDepthBias));
	f1ObjectDepth -= min(fractionalSamplingError, 0.01f);
	*/
	
	// Fallback
#if !defined(CSM_FILTER)
#define CSM_FILTER 0
#endif
	
#if (CSM_FILTER == 0)
	
	// fuck + tap, I want 1 tap
//	float f1FinalShadow = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x;

	// Nvidia Percentage Closer Filter + tap
//	float f1FinalShadow = Filter_4Tap(DepthSampler, float3(f3ClosePosition.xy, f1ObjectDepth), f2Epsilon);

	// 5x5
//	float f1FinalShadow = Filter_NVIDIA_PCF_5x5_Gaussian(DepthSampler, f3ClosePosition, f2Epsilon);

	// 5x5 Gaussian
	float f1FinalShadow = LUX_DoShadowNvidiaPCF5x5Gaussian(DepthSampler, f3ClosePosition, f2Epsilon);
#elif (CSM_FILTER == 1)
	
	// Nvidia Percentage Closer Filter 5x5
	float f1FinalShadow = Filter_NVIDIA_PCF_5x5_Gaussian(DepthSampler, f3ClosePosition, f2Epsilon);
#elif (CSM_FILTER == 2)
	
	// Get current depth
	float f1CurrentDepth = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x;
	
	float4 f4Moments;
	// We don't use a format that allows for packing of moments
	f4Moments = GetOptimizedMoments(f1CurrentDepth);
//	f1CurrentDepth = ChebyshevUpperBound(f4Moments.xy, f1ObjectDepth, g_CascadedResolutions.z, );

	// We use a 16 bit framebuffer format so we MUST do this
	f4Moments = ConvertOptimizedMoments(f4Moments);

	// Moment Shadow Mapping ( Hamburger )
	float f1FinalShadow = ComputeMSMHamburger(f4Moments, f1ObjectDepth, g_CascadedResolutions.z, g_CascadedResolutions.w); // depth bias, moment bias
#elif (CSM_FILTER == 3)
	
	//==========================================================================//
	//	f1x0y2		f1x1y2		f1x2y2
	//
	//	f1x0y1		f1x1y1		f1x2y1
	//
	//	f1x0y0		f1x1y0		f1x2y0
	//
	// Filter 3x3 square.
	// TODO: Clean this up into float4's then use dot(x, float4(1/4))
	//==========================================================================//
	/*
	float f1x0y0 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f2Epsilon.x,-f2Epsilon.y), f1ObjectDepth, 1)).x;
	float f1x1y0 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0.0f,-f2Epsilon.y), f1ObjectDepth, 1)).x;
	float f1x2y0 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f2Epsilon.x,-f2Epsilon.y), f1ObjectDepth, 1)).x;

	float f1x0y1 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f2Epsilon.x, 0.0f), f1ObjectDepth, 1)).x;
//	float f1x1y1 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0.0f, 0.0f), f1ObjectDepth, 1)).x;
	float f1x2y1 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f2Epsilon.x, 0.0f), f1ObjectDepth, 1)).x;

	float f1x0y2 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(-f2Epsilon.x,f2Epsilon.y), f1ObjectDepth, 1)).x;
	float f1x1y2 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(0.0f,f2Epsilon.y), f1ObjectDepth, 1)).x;
	float f1x2y2 = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter + float2(f2Epsilon.x,f2Epsilon.y), f1ObjectDepth, 1)).x;

	//==========================================================================//
	// Combine into 3 floats
	//==========================================================================//
	float f1Scale4 = 1.0f / 4.0f;
	float f1Scale3 = 1.0f / 3.0f;
	float f1DepthMain;
	float f1DepthSide;
	float f1DepthAngle;

	f1DepthMain = f1x1y1;
	f1DepthSide = f1x0y1 + f1x1y2 + f1x2y1 + f1x1y0;
	f1DepthAngle = f1x0y2 + f1x2y2 + f1x0y0 + f1x2y0;

	f1DepthSide	 *= f1Scale4;
	f1DepthAngle *= f1Scale4;
	*/

	float4 f4Moments;
	//==========================================================================//
	// Calculate Moments and Filter the shit out of this.
	//==========================================================================//
	
	float f1DepthMain = Filter_NVIDIA_PCF_5x5_Gaussian(DepthSampler, f3ClosePosition, f2Epsilon);

	f4Moments = GetOptimizedMoments(f1DepthMain);
	f4Moments = ConvertOptimizedMoments(f4Moments); // We use a 16 bit framebuffer format so we MUST do this

	float f1FinalShadow = ComputeMSMHamburger(f4Moments, f1ObjectDepth, 0.0001f, 0.0001f); // depth bias, moment bias
//
//	// Do it again. different depth
//	//==========================================================================//
//
//	f4Moments = GetOptimizedMoments(f1DepthSide);
//	f4Moments = ConvertOptimizedMoments(f4Moments); // We use a 16 bit framebuffer format so we MUST do this
//
//	f1FinalShadow += g_CascadedResolutions.z * ComputeMSMHamburger(f4Moments, f1ObjectDepth, 0.0001f, 0.0001f); // depth bias, moment bias
//
//	// Do it again. different depth
//	//==========================================================================//
//
//	f4Moments = GetOptimizedMoments(f1DepthAngle);
//	f4Moments = ConvertOptimizedMoments(f4Moments); // We use a 16 bit framebuffer format so we MUST do this

	f1FinalShadow += g_CascadedResolutions.w * ComputeMSMHamburger(f4Moments, f1ObjectDepth, 0.0001f, 0.0001f); // depth bias, moment bias

	// Finalise
	//==========================================================================//
//	f1FinalShadow *= 1.0f / 3.0f;
#endif
	
	// 5x5
//	float f1FinalShadow = Filter_NVIDIA_PCF_5x5_Gaussian(DepthSampler, f3ClosePosition, f2Epsilon);

	// 3x3 - 4
//	float f1FinalShadow = Filter_4Tap(DepthSampler, float3(f3ClosePosition.xy, f1ObjectDepth), f2Epsilon);

	// 2x2 box
//	float f1FinalShadow = Filter_Box(DepthSampler, float3(f3ClosePosition.xy, f1ObjectDepth), f2Epsilon);

	// 1 tap
//	float f1FinalShadow = tex2Dproj(DepthSampler, float4(f2ShadowMapCenter, f1ObjectDepth, 1)).x;

	return f1FinalShadow;
}

//==========================================================================//
//	Computes Cascaded Shadow from Depth
//==========================================================================//
float FilterCascadedShadow(float3 f3ShadowMapPos, const int nShadowFilter = 0)
{
	// Main Projection ( non-snapshot )
	// ========================================

	// This is 1.0f / Resolution. ( Epsilon )
	// Size of a single pixel. The depth texture is not square. So we need one for X and Y respectively.
	// the rcp() value is precomputed on the CPU so we don't do it every pixel!
	float2 f2Epsilon = g_CascadedResolutions.xy;

	// This doesn't work for some reason
//	float f1ObjectDepth = f3ShadowMapPos.z;
//	float f2ShadowMapCenter = f3ShadowMapPos.xy;

	// NVIDIA_PCF_POISSON	0
	// ATI_NOPCF			1
	// ATI_NO_PCF_FETCH4	2
	// NVIDIA_PCF_GAUSSIAN	3
	float f1Shadow = 1.0f;

	// TODO: Implement other shadow filters than the Gaussian 5x5.
	// ISSUE: Other filters require the randomrotation sampler
	// See: https://web.archive.org/web/20140224205853/http://obge.paradice-insight.us/wiki/Includes_(Effects)
	// for a way to compute a "random" number based on project position without requiring a sampler...
	// NOTE: This might a bit expensive but the other shadow filters aren't as HQ so it might be a relative loss
	// And for not requiring a sample that'd be ok I guess...
	// Also: there is already a perlin noise function for sm3.0, why not use that? Is it more expensive?
	/*
	if (nShadowFilter == NVIDIA_PCF_POISSON)
	{
		// Can't do this rn because no Random Number
		f1Shadow = Filter_Nvidia_PCF_Poisson(Sampler_CascadedDepth, f1ObjectDepth, float3 RMatTop, float3 RMatBottom);
	}
	else if (nShadowFilter == ATI_NOPCF)
	{
		// Can't do this rn because no Random Number
		f1Shadow = Filter_ATI_NO_PCF(Sampler_CascadedDepth, f1ObjectDepth, RMatTop, RMatBottom);
	}
	else if (nShadowFilter == ATI_NO_PCF_FETCH4)
	{
		// Can't do this rn because no Random Number
		f1Shadow = Filter_ATI_FETCH4(Sampler_CascadedDepth, f1ObjectDepth, RMatTop, RMatBottom);
	}
	else if (nShadowFilter == NVIDIA_PCF_GAUSSIAN)
	*/
	{
//		f1Shadow = Filter_NVIDIA_PCF_5x5_Gaussian(Sampler_CascadedDepth, f3ShadowMapPos, f2Epsilon);
	}
//	else if (nShadowFilter == This filter doesn't have a name yet) {}
	// Very cheap filtering :
//	f1Shadow = Filter_4Tap(Sampler_CascadedDepth, f3ShadowMapPos, f2Epsilon);

	f1Shadow = Filter_ShiroExperimental(Sampler_CascadedDepth, f3ShadowMapPos, f2Epsilon);

	return f1Shadow;
}

//-------------------------------------------------------------------------------------------------
// Calculates the offset to use for sampling the shadow map, based on the surface normal
//-------------------------------------------------------------------------------------------------
float3 GetShadowPosOffset(in float nDotL, in float3 normal)
{
	// We have a different texel size because of the cascades...
	float3 f3TexelSize = float3(g_CascadedResolutions.xy, 0.0f);

	float nmlOffsetScale = saturate(1.0f - nDotL);
	return f3TexelSize * nmlOffsetScale * normal;
}

float ComputeCascadedShadow(float3 f3WorldPos, float3 f3WorldNormal)
{
	float	f1Result = 0.0f; // prepare..
	float4	f4temp;

	// The f3WorldNormal * Bias, helps with aliasing issues on slopes
	float f1NdL = saturate(dot(f3WorldNormal, -g_CascadedPosition.xyz));

	// Don't want to do any of this IF the surface faces away from the light
	if (f1NdL > 0.0f)
	{
		//	float3 f3Offset = GetShadowPosOffset(f1NdL, f3WorldNormal);

		// + f3Offset
		//	float4	f4temp = mul(float4(f3WorldPos + f3Offset, 1.0f), g_CascadedWorldToTexture);

		f4temp = mul(float4(f3WorldPos + f3WorldNormal * 0.2f, 1.0f), g_CascadedWorldToTexture);
		float3	f3CascadedPos = f4temp.xyz;

		// computes a box that is projected *from* the suns location, indicating where which cascades should be drawn
		// NOTE : No longer works
		//	float	f1CascadeArea = step(f3CascadedPos.x, 0.49) * step(0.01, f3CascadedPos.x) *
		//							step(f3CascadedPos.y, 0.99) * step(0.01, f3CascadedPos.y);
		float	f1CascadeArea = step(f3CascadedPos.x, 1.0f) * step(0.01, f3CascadedPos.x) *
			step(f3CascadedPos.y, 1.0f) * step(0.01, f3CascadedPos.y);

		// Offset the texture coordinate to the far away cascade
		// but only if outside the area of the main cascade
		//	f3CascadedPos.xy = lerp(f3CascadedPos.xy * g_CascadedStepData.x + g_CascadedStepData.yz, f3CascadedPos.xy, f1CascadeArea);

		// Filters the depth sampler for our desired shadow
		f1Result = FilterCascadedShadow(f3CascadedPos);

#if SNAPSHOTTING
		// Sample Cascade ( Snapshot )
		f4temp = mul(float4(f3WorldPos, 1.0f), g_SnapshotWorldToTexture);
		f3CascadedPos = f4temp.xyz;

		float	f1SnapshotShadow = Filter_4Tap(Sampler_SnapshotDepth, float3(f3CascadedPos.xy, f3CascadedPos.z), g_CascadedResolutions.zw);

		//			float	f1SnapshotShadow = tex2Dproj(Sampler_SnapshotDepth, float4(f3CascadedPos.xy, f3CascadedPos.z, 1)).x;

		f1Result = lerp(f1SnapshotShadow, f1Result, f1CascadeArea);
		f1Result = min(f1NdL, f1Result); // if NdL is lower, use that instead
		//			f1Result = min(f1SnapshotShadow, f1Result);
#endif
	}

	// Fade out the shadow at the borders a little
	// Otherwise you get a hard cutoff between cascades
	// this is very important for our forward offset
//	float	weight =	lerp(saturate(saturate(abs(f3CascadedPos.x * 4.0 - 3.0) - 0.9) * 10.0 +
//						saturate(abs(f3CascadedPos.y * 2.0 - 1.0) - 0.9) * 10.0), 0.0f, f1CascadeArea);
//	f1Result = lerp(f1Result, 1.0, weight);

	// Return the shadow
	return	f1Result;
}
#endif // End of LUX_COMMON_CSM_H_