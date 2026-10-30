//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: sheet (.sht) loading for particle sprite sequences
//
// Reconstructed from psheet.obj in the Alien Swarm SDK's bitmap.lib (CSheet only;
// CSheetExtended is not used by the particle library and is left out).
//
// Needed when building the Alien Swarm particle code against an SDK whose bitmap
// library has no CSheet (e.g. the TF2 SDK). Do NOT add this to a build that also
// links the Alien Swarm bitmap.lib, which already contains these functions.
//
//===========================================================================//

#include "bitmap/psheet.h"
#include "tier0/dbg.h"
#include "tier1/utlbuffer.h"
#include "mathlib/mathlib.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


CSheet::CSheet( void )
{
}

CSheet::CSheet( CUtlBuffer &buf )
{
	// lets read a sheet
	int nVersion = buf.GetInt();
	int nNumCoordsPerFrame = ( nVersion ) ? MAX_IMAGES_PER_FRAME_ON_DISK : 1;

	int nNumSequences = buf.GetInt();
	int nNumToAllocate = nNumSequences;

	// The old hardcoded arrays were 64. Allocate at least that many so that content
	// using sequence numbers past the last sequence in the sheet keeps working.
	if ( nNumToAllocate < 64 )
		nNumToAllocate = 64;

	m_SheetInfo.EnsureCapacity( nNumSequences );
	for ( int i = 0; i < nNumToAllocate; ++i )
	{
		m_SheetInfo.AddToTail();
		m_SheetInfo[i].m_pSamples = NULL;
		m_SheetInfo[i].m_SeqFlags = 0;
		m_SheetInfo[i].m_bSequenceIsCopyOfAnotherSequence = false;
		m_SheetInfo[i].m_nNumFrames = 0;
		m_SheetInfo[i].m_flFrameSpan = 0.0f;
	}

	while ( nNumSequences-- )
	{
		int nSequenceNumber = buf.GetInt();
		if ( nSequenceNumber < 0 )
		{
			Warning( "Invalid sequence number (%d)!!!\n", nSequenceNumber );
			return;
		}

		// grow the table to hold this sequence
		while ( m_SheetInfo.Count() <= nSequenceNumber )
		{
			int i = m_SheetInfo.AddToTail();
			m_SheetInfo[i].m_pSamples = NULL;
			m_SheetInfo[i].m_SeqFlags = 0;
			m_SheetInfo[i].m_bSequenceIsCopyOfAnotherSequence = false;
			m_SheetInfo[i].m_nNumFrames = 0;
			m_SheetInfo[i].m_flFrameSpan = 0.0f;
		}

		m_SheetInfo[nSequenceNumber].m_SeqFlags = (uint8)buf.GetInt();
		int nFrameCount = buf.GetInt();
		m_SheetInfo[nSequenceNumber].m_nNumFrames = (int16)nFrameCount;

		// single frame sequences only need one sample
		int nTimeSamples = ( nFrameCount == 1 ) ? 1 : SEQUENCE_SAMPLE_COUNT;
		// NOTE: the shipped code doesn't free samples of a sequence number that appears twice
		m_SheetInfo[nSequenceNumber].m_pSamples = new SheetSequenceSample_t[ nTimeSamples ];

		int nTotalSequenceTime = (int)buf.GetFloat();
		float flOOTotalSequenceTime = 1.0f / (float)nTotalSequenceTime;

		float InterpKnot[SEQUENCE_SAMPLE_COUNT];
		float InterpValue[SEQUENCE_SAMPLE_COUNT];
		SheetSequenceSample_t Samples[SEQUENCE_SAMPLE_COUNT];

		float fCurTime = 0.0f;
		for ( int nFrm = 0; nFrm < nFrameCount; nFrm++ )
		{
			float fThisDuration = buf.GetFloat();
			InterpValue[nFrm] = (float)nFrm;
			InterpKnot[nFrm] = ( flOOTotalSequenceTime * fCurTime ) * SEQUENCE_SAMPLE_COUNT;

			SheetSequenceSample_t &seq = Samples[nFrm];
			seq.m_fBlendFactor = 0.0f;
			for ( int nImage = 0; nImage < nNumCoordsPerFrame; nImage++ )
			{
				// NOTE: files store up to 4 images per frame but only 2 are kept in memory.
				// The shipped code wrote the extra ones past the end of the sample (into data
				// that is overwritten or never read); they are read and dropped here instead.
				SequenceSampleTextureCoords_t dummy;
				SequenceSampleTextureCoords_t &s = ( nImage < MAX_IMAGES_PER_FRAME_IN_MEMORY ) ? seq.m_TextureCoordData[nImage] : dummy;
				s.m_fLeft_U0 = buf.GetFloat();
				s.m_fTop_V0 = buf.GetFloat();
				s.m_fRight_U0 = buf.GetFloat();
				s.m_fBottom_V0 = buf.GetFloat();
			}
			if ( nNumCoordsPerFrame == 1 )
			{
				seq.CopyFirstFrameToOthers();
			}

			fCurTime += fThisDuration;
			m_SheetInfo[nSequenceNumber].m_flFrameSpan = fCurTime;
		}

		// now, fill in the whole table
		for ( int nIdx = 0; nIdx < nTimeSamples; nIdx++ )
		{
			float flIdxA, flIdxB, flInterp;
			GetInterpolationData( InterpKnot, InterpValue, nFrameCount, SEQUENCE_SAMPLE_COUNT, (float)nIdx,
				!( m_SheetInfo[nSequenceNumber].m_SeqFlags & SEQ_FLAG_CLAMP ), &flIdxA, &flIdxB, &flInterp );

			SheetSequenceSample_t sA = Samples[(int)flIdxA];
			SheetSequenceSample_t sB = Samples[(int)flIdxB];
			SheetSequenceSample_t &oseq = m_SheetInfo[nSequenceNumber].m_pSamples[nIdx];

			oseq.m_fBlendFactor = flInterp;
			for ( int nImage = 0; nImage < MAX_IMAGES_PER_FRAME_IN_MEMORY; nImage++ )
			{
				SequenceSampleTextureCoords_t &src0 = sA.m_TextureCoordData[nImage];
				SequenceSampleTextureCoords_t &src1 = sB.m_TextureCoordData[nImage];
				SequenceSampleTextureCoords_t &o = oseq.m_TextureCoordData[nImage];

				o.m_fLeft_U0 = src0.m_fLeft_U0;
				o.m_fTop_V0 = src0.m_fTop_V0;
				o.m_fRight_U0 = src0.m_fRight_U0;
				o.m_fBottom_V0 = src0.m_fBottom_V0;

				o.m_fLeft_U1 = src1.m_fLeft_U0;
				o.m_fTop_V1 = src1.m_fTop_V0;
				o.m_fRight_U1 = src1.m_fRight_U0;
				o.m_fBottom_V1 = src1.m_fBottom_V0;
			}
		}
	}

	// now, fill in all unseen sequences with copies of the first seen sequence to prevent crashes
	// while editing
	int nFirstSequence = -1;
	for ( int i = 0; i < m_SheetInfo.Count(); i++ )
	{
		if ( m_SheetInfo[i].m_pSamples )
		{
			nFirstSequence = i;
			break;
		}
	}

	if ( nFirstSequence != -1 )
	{
		for ( int i = 0; i < m_SheetInfo.Count(); i++ )
		{
			if ( m_SheetInfo[i].m_pSamples == NULL )
			{
				m_SheetInfo[i].m_pSamples = m_SheetInfo[nFirstSequence].m_pSamples;
				m_SheetInfo[i].m_SeqFlags = m_SheetInfo[nFirstSequence].m_SeqFlags;
				m_SheetInfo[i].m_nNumFrames = m_SheetInfo[nFirstSequence].m_nNumFrames;
				m_SheetInfo[i].m_bSequenceIsCopyOfAnotherSequence = true;
			}
		}
	}
}

CSheet::~CSheet( void )
{
	for ( int i = 0; i < m_SheetInfo.Count(); i++ )
	{
		if ( m_SheetInfo[i].m_pSamples && ( !m_SheetInfo[i].m_bSequenceIsCopyOfAnotherSequence ) )
		{
			delete[] m_SheetInfo[i].m_pSamples;
		}
	}
}

const SheetSequenceSample_t *CSheet::GetSampleForSequence( float flAge, float flAgeScale, int nSequence, bool bForceLoop )
{
	if ( m_SheetInfo[nSequence].m_nNumFrames == 1 )
		return (const SheetSequenceSample_t *) &m_SheetInfo[nSequence].m_pSamples[0];

	int nIndex = (int)( flAge * flAgeScale );
	if ( ( m_SheetInfo[nSequence].m_SeqFlags & SEQ_FLAG_CLAMP ) && !bForceLoop )
	{
		// (an unsigned compare in the shipped code, so negative indices clamp to the end too)
		if ( (unsigned)nIndex >= SEQUENCE_SAMPLE_COUNT - 1 )
			nIndex = SEQUENCE_SAMPLE_COUNT - 1;
	}
	else
	{
		nIndex &= ( SEQUENCE_SAMPLE_COUNT - 1 );
	}
	return (const SheetSequenceSample_t *) &m_SheetInfo[nSequence].m_pSamples[nIndex];
}
