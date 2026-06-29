#ifndef UTLINDEXBUFFER_H
#define UTLINDEXBUFFER_H

#ifdef _WIN32
#pragma once
#endif


#include "tier1/utlqueue.h"


#define FOR_EACH_INDEX( indexName, iteratorName ) \
	for( int iteratorName = 0; iteratorName < indexName.MaxIndex(); iteratorName++ ) if ( !indexName.IsIndexValid( iteratorName ) ) continue; else

#define FOR_EACH_VEC_INDEXED( vecName, indexName, iteratorName ) \
	FOR_EACH_VEC( vecName, iteratorName ) if ( !indexName.IsIndexValid( iteratorName ) ) continue; else


class CUtlIndexBuffer
{
public:
	typedef unsigned int Index_t;

public:
	CUtlIndexBuffer();

	// Allocate, deallocate indeces
	Index_t AddIndex();
	void RemoveIndex( Index_t i );
	Index_t LastIndex() const				{ Assert( IsIndexValid( m_nLastIndex ) ); return m_nLastIndex; }

	// Is a index valid?
	bool IsIndexValid( Index_t i ) const;

	int Count() const;
	int MaxIndex() const;

	void Purge();

private:
	Index_t m_nMaxIndex;
	Index_t m_nLastIndex;
	CUtlQueue< Index_t > m_unused;
};


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
inline
CUtlIndexBuffer::CUtlIndexBuffer()
	: m_nMaxIndex( 0 )
	, m_nLastIndex( 65535 )
{
}


//-----------------------------------------------------------------------------
// Allocate, deallocate handles
//-----------------------------------------------------------------------------
inline
CUtlIndexBuffer::Index_t
CUtlIndexBuffer::AddIndex()
{
	m_nLastIndex = ( m_unused.Count() > 0 ) ? m_unused.RemoveAtHead() : m_nMaxIndex++;
	return m_nLastIndex;
}

inline
void
CUtlIndexBuffer::RemoveIndex( Index_t i )
{
	Assert( i < m_nMaxIndex );

	m_unused.Insert( i );
	m_nLastIndex = 65535;

	if ( m_unused.Count() == (int)m_nMaxIndex ) {
		m_unused.RemoveAll();
		m_nMaxIndex = 0;
	}
}


//-----------------------------------------------------------------------------
// Is a index valid?
//-----------------------------------------------------------------------------
inline
bool
CUtlIndexBuffer::IsIndexValid( Index_t i ) const
{
	return i < m_nMaxIndex && !m_unused.Check( i );
}

inline
int
CUtlIndexBuffer::Count() const
{
	return m_nMaxIndex - m_unused.Count();
}

inline
int
CUtlIndexBuffer::MaxIndex() const
{
	return m_nMaxIndex;
}

inline
void
CUtlIndexBuffer::Purge()
{
	m_nMaxIndex = 0;
	m_nLastIndex = 65535;
	m_unused.Purge();
}


//-----------------------------------------------------------------------------
// Reference to IndexBuffer delegates all counting to referenced IndexBuffer.
// Does not add or remove any indeces from referenced IndexBuffer.
// Calls to AddIndex and RemoveIndex should be synchronized with referenced IndexBuffer.
//-----------------------------------------------------------------------------
class CUtlIndexBufferRef
{
public:
	typedef CUtlIndexBuffer::Index_t Index_t;

public:
	CUtlIndexBufferRef( const CUtlIndexBuffer &ref );

	// Allocate, deallocate indeces
	Index_t AddIndex();
	void RemoveIndex( Index_t i )			{}
	Index_t LastIndex() const				{ return m_Ref.LastIndex(); }

	// Is a index valid?
	bool IsIndexValid( Index_t i ) const	{ return m_Ref.IsIndexValid( i ); }

	int Count() const						{ return m_Ref.Count(); }
	int MaxIndex() const					{ return m_Ref.MaxIndex(); }

	void Purge();

private:
	const CUtlIndexBuffer &m_Ref;
#if defined( DEBUG )
	Index_t m_nLastAddedIndex;
//	Index_t m_nLastRemovedIndex;
#endif
};

inline
CUtlIndexBufferRef::CUtlIndexBufferRef( const CUtlIndexBuffer &ref )
	: m_Ref( ref )
#if defined( DEBUG )
	, m_nLastAddedIndex( 65535 )
//	, m_nLastRemovedIndex( 65535 )
#endif
{
}

inline
CUtlIndexBufferRef::Index_t
CUtlIndexBufferRef::AddIndex()
{
	Index_t i = m_Ref.LastIndex();
#if defined( DEBUG )
	Assert( m_nLastAddedIndex != i );
	m_nLastAddedIndex = i;
#endif
	return i;
}

inline
void
CUtlIndexBufferRef::Purge()
{
#if defined( DEBUG )
	m_nLastAddedIndex = 65535;
#endif
}

#endif // UTLINDEXBUFFER_H
