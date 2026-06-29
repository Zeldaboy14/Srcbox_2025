#ifndef UTLGRAPH_H
#define UTLGRAPH_H

#ifdef _WIN32
#pragma once
#endif


#include "tier1/utlmap.h"
#include "tier1/utlvector.h"


#define FOR_EACH_NODE( graphName, iteratorName ) \
	for ( int iteratorName = 0; iteratorName < graphName.MaxElement(); iteratorName++ ) if ( !graphName.IsValidIndex( iteratorName ) ) continue; else

#define FOR_EACH_NODE_L( graphName, iteratorName ) \
	for ( int iteratorName = 0; iteratorName < graphName.MaxElement(); iteratorName++ ) if ( !graphName.IsValidIndex( iteratorName ) ) continue; else


template < typename T >
struct Edge
{
	T a, b;

	Edge()
		: a( -1 ), b( -1 )
	{
	}

	Edge( T a, T b )
		: a( a ), b( b )
	{
	}
};

template < typename T >
int EdgeUniCompare( const Edge< T > *r, const Edge< T > *l )
{
	T ra = min( r->a, r->b );
	T rb = max( r->a, r->b );
	T la = min( l->a, l->b );
	T lb = max( l->a, l->b );
	return ( ( ra == la ) && ( rb == lb ) ) ? 0
		:  ( ( ra < la ) || ( rb < lb ) ) ? -1
		:  1; // ( ( ra > la ) || ( rb > lb ) )
}


template < typename T >
class CUtlGraphLinked
{
public:
	typedef CCopyableUtlVector< T > Nodes_t;
	typedef CUtlMap< T, Nodes_t > LinkedNodes_t;
	typedef typename LinkedNodes_t::IndexType_t Index_t;
	typedef CUtlVector< Index_t > Indices_t;

public:
	CUtlGraphLinked();

	const Nodes_t &Nodes() const;
	const Indices_t &Indices() const;

	T &Node( Index_t i );
	const T &Node( Index_t i ) const;
	const Nodes_t &operator[]( Index_t i ) const;
	void Nodes( Index_t i, Nodes_t &nodes ) const;

	int Count() const;
	bool IsValidIndex( Index_t i ) const;
	Index_t MaxElement() const							{ return m_LinkedNodes.MaxElement(); }

	void Link( Index_t a, Index_t b );
	void Unlink( Index_t a, Index_t b );
	bool AreLinked( Index_t a, Index_t b ) const;

	Index_t Add( const T &node );
	void Add( const T &a, const T &b );

	void Remove( const T &node );
	void Remove( const T &a, const T &b );

	Index_t Find( const T &node ) const;
	bool HasNode( const T &node ) const;

	void Purge();

private:
	LinkedNodes_t m_LinkedNodes;
};

template < typename T >
inline
CUtlGraphLinked< T >::CUtlGraphLinked()
{
	m_LinkedNodes.SetLessFunc( DefLessFunc( T ) );
}

template < typename T >
inline
const typename CUtlGraphLinked< T >::Nodes_t &
CUtlGraphLinked< T >::Nodes() const
{
	static Nodes_t result;
	result.RemoveAll();
	result.EnsureCapacity( m_LinkedNodes.Count() );

	FOR_EACH_MAP_FAST( m_LinkedNodes, i )
	{
		result.AddToTail( m_LinkedNodes.Key( i ) );
	}

	return result;
}

template < typename T >
inline
const typename CUtlGraphLinked< T >::Indices_t &
CUtlGraphLinked< T >::Indices() const
{
	static Indices_t result;
	result.RemoveAll();
	result.EnsureCapacity( m_LinkedNodes.Count() );

	FOR_EACH_MAP_FAST( m_LinkedNodes, i )
	{
		result.AddToTail( i );
	}

	return result;
}

template < typename T >
inline
T &
CUtlGraphLinked< T >::Node( Index_t i )
{
	Assert( m_LinkedNodes.IsValidIndex( i ) );

	return m_LinkedNodes.Key( i );
}

template < typename T >
inline
const T &
CUtlGraphLinked< T >::Node( Index_t i ) const
{
	Assert( m_LinkedNodes.IsValidIndex( i ) );

	return m_LinkedNodes.Key( i );
}

template < typename T >
inline
const typename CUtlGraphLinked< T >::Nodes_t &
CUtlGraphLinked< T >::operator[]( Index_t i ) const
{
	Assert( m_LinkedNodes.IsValidIndex( i ) );

	return m_LinkedNodes[ i ];
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Nodes( Index_t i, Nodes_t &nodes ) const
{
	Assert( m_LinkedNodes.IsValidIndex( i ) );

	nodes.AddVectorToTail( m_LinkedNodes[ i ] );
}

template < typename T >
inline
int
CUtlGraphLinked< T >::Count() const
{
	return m_LinkedNodes.Count();
}

template < typename T >
inline
bool
CUtlGraphLinked< T >::IsValidIndex( Index_t i ) const
{
	return m_LinkedNodes.IsValidIndex( i );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Link( Index_t a, Index_t b )
{
	Assert( m_LinkedNodes.IsValidIndex( a ) );
	Assert( m_LinkedNodes.IsValidIndex( b ) );

	T &kb = m_LinkedNodes.Key( b );
	Nodes_t &na = m_LinkedNodes[ a ];

	Assert( !na.IsValidIndex( na.Find( kb ) ) );

	na.AddToTail( kb );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Unlink( Index_t a, Index_t b )
{
	Assert( m_LinkedNodes.IsValidIndex( a ) );
	Assert( m_LinkedNodes.IsValidIndex( b ) );

	T &kb = m_LinkedNodes.Key( b );
	Nodes_t &na = m_LinkedNodes[ a ];

	int ina = na.Find( kb );

	Assert( na.IsValidIndex( ina ) );

	na.Remove( ina );
}

template < typename T >
inline
bool
CUtlGraphLinked< T >::AreLinked( Index_t a, Index_t b ) const
{
	Assert( m_LinkedNodes.IsValidIndex( a ) );
	Assert( m_LinkedNodes.IsValidIndex( b ) );

	Nodes_t &nodes = m_LinkedNodes[ a ];
	T &kb = m_LinkedNodes.Key( b );

	return nodes.Find( kb );
}

template < typename T >
inline
typename CUtlGraphLinked< T >::Index_t
CUtlGraphLinked< T >::Add( const T &node )
{
	Assert( !HasNode( node ) );
	return m_LinkedNodes.Insert( node, Nodes_t() );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Add( const T &a, const T &b )
{
	LinkedNodes_t::IndexType_t ia = m_LinkedNodes.Find( a );
	LinkedNodes_t::IndexType_t ib = m_LinkedNodes.Find( b );

	Assert( m_LinkedNodes.IsValidIndex( ia ) && m_LinkedNodes.IsValidIndex( ib ) );

	Link( ia, ib );
	Link( ib, ia );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Remove( const T &node )
{
	LinkedNodes_t::IndexType_t i = m_LinkedNodes.Find( node );

	Assert( m_LinkedNodes.IsValidIndex( i ) );

	Nodes_t &nodes = m_LinkedNodes[ i ];

	FOR_EACH_VEC( nodes, in )
	{
		LinkedNodes_t::IndexType_t inn = m_LinkedNodes.Find( nodes[ in ] );
		Nodes_t &nn = m_LinkedNodes[ inn ];
		nn.Remove( nn.Find( node ) );
	}

	m_LinkedNodes.Remove( node );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Remove( const T &a, const T &b )
{
	LinkedNodes_t::IndexType_t ia = m_LinkedNodes.Find( a );
	LinkedNodes_t::IndexType_t ib = m_LinkedNodes.Find( b );

	Assert( m_LinkedNodes.IsValidIndex( ia ) && m_LinkedNodes.IsValidIndex( ib ) );

	Unlink( ia, ib );
	Unlink( ib, ia );
}

template < typename T >
inline
typename CUtlGraphLinked< T >::Index_t
CUtlGraphLinked< T >::Find( const T &node ) const
{
	return m_LinkedNodes.Find( node );
}

template < typename T >
inline
bool
CUtlGraphLinked< T >::HasNode( const T &node ) const
{
	return m_LinkedNodes.IsValidIndex( m_LinkedNodes.Find( node ) );
}

template < typename T >
inline
void
CUtlGraphLinked< T >::Purge()
{
	m_LinkedNodes.Purge();
}


#define FOR_EACH_NODE_M( graphName, iteratorName ) \
	for ( int iteratorName = 0; iteratorName < graphName.Count(); iteratorName++ )

template < typename T >
class CUtlGraphMatrix
{
public:
	typedef int Index_t;
	typedef CUtlVector< T > Nodes_t;
	typedef CUtlVector< bool > LinkedNodes_t;

public:
	CUtlGraphMatrix();

	void Init( int size );

	T &Node( Index_t i );
	const T &Node( Index_t i ) const;
	const Nodes_t &operator[]( Index_t i ) const;

	int Count() const;
	bool IsValidIndex( Index_t i ) const;

	void Link( Index_t a, Index_t b );
	void Unlink( Index_t a, Index_t b );
	bool AreLinked( Index_t a, Index_t b ) const;

	Index_t Add( const T &node );
	void Add( const T &a, const T &b );

	void Remove( const T &node );
	void Remove( const T &a, const T &b );

	Index_t Find( const T &node ) const;
	Index_t FindOrAdd( const T &node );
	bool HasNode( const T &node ) const;

	void Purge();

private:
	Index_t GetNodeIndex( const T &node ) const;
	Index_t GetLinkIndex( Index_t a, Index_t b ) const;

private:
	int m_nSize;
	Nodes_t m_Nodes;
	LinkedNodes_t m_LinkedNodes;
};

template < typename T >
inline
CUtlGraphMatrix< T >::CUtlGraphMatrix()
	: m_nSize( 0 )
{
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Init( int size )
{
	Assert( !m_nSize );

	m_nSize = size;
	m_Nodes.EnsureCapacity( size );
	m_LinkedNodes.SetCount( size * size );

	memset( m_Nodes.Base(), 0, size * sizeof( T ) );
	memset( m_LinkedNodes.Base(), 0, size * size * sizeof( bool ) );
}

template < typename T >
inline
T &
CUtlGraphMatrix< T >::Node( Index_t i )
{
	Assert( m_Nodes.IsValidIndex( i ) );

	return m_Nodes[ i ];
}

template < typename T >
inline
const T &
CUtlGraphMatrix< T >::Node( Index_t i ) const
{
	Assert( m_Nodes.IsValidIndex( i ) );

	return m_Nodes[ i ];
}

template < typename T >
inline
const typename CUtlGraphMatrix< T >::Nodes_t &
CUtlGraphMatrix< T >::operator[]( Index_t i ) const
{
	static Nodes_t result;

	result.RemoveAll();

	Assert( m_Nodes.IsValidIndex( i ) );

	for ( int in = 0 ; in < m_Nodes.Count(); in++ )
	{
		if ( m_LinkedNodes[ GetLinkIndex( i, in ) ] )
		{
			result.AddToTail( m_Nodes[ in ] );
		}
	}

	return result;
}

template < typename T >
inline
int
CUtlGraphMatrix< T >::Count() const
{
	return m_Nodes.Count();
}

template < typename T >
inline
bool
CUtlGraphMatrix< T >::IsValidIndex( Index_t i ) const
{
	return m_Nodes.IsValidIndex( i );
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Link( int a, int b )
{
	Assert( a != b );

	m_LinkedNodes[ GetLinkIndex( a, b ) ] = true;
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Unlink( int a, int b )
{
	Assert( a != b );

	m_LinkedNodes[ GetLinkIndex( a, b ) ] = false;
}

template < typename T >
inline
bool
CUtlGraphMatrix< T >::AreLinked( Index_t a, Index_t b ) const
{
	Assert( a != b );

	return m_LinkedNodes[ GetLinkIndex( a, b ) ];
}

template < typename T >
inline
typename CUtlGraphMatrix< T >::Index_t
CUtlGraphMatrix< T >::Add( const T &node )
{
	Assert( m_Nodes.Count() < m_nSize );

	return m_Nodes.AddToTail( node );
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Add( const T &a, const T &b )
{
	Index_t ia = GetNodeIndex( a );
	Index_t ib = GetNodeIndex( b );

	Link( ia, ib );
	Link( ib, ia );
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Remove( const T &node )
{
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Remove( const T &a, const T &b )
{
	Index_t ia = GetNodeIndex( a );
	Index_t ib = GetNodeIndex( b );

	Unlink( ia, ib );
	Unlink( ib, ia );
}

template < typename T >
inline
typename CUtlGraphMatrix< T >::Index_t
CUtlGraphMatrix< T >::Find( const T &node ) const
{
	return m_Nodes.Find( node );
}

template < typename T >
inline
typename CUtlGraphMatrix< T >::Index_t
CUtlGraphMatrix< T >::FindOrAdd( const T &node )
{
	Index_t i = m_Nodes.Find( node );

	if ( m_Nodes.IsValidIndex( i ) )
	{
		return i;
	}

	return Add( node );
}

template < typename T >
inline
bool
CUtlGraphMatrix< T >::HasNode( const T &node ) const
{
	return m_Nodes.IsValidIndex( m_Nodes.Find( node ) );
}

template < typename T >
inline
void
CUtlGraphMatrix< T >::Purge()
{
	m_Nodes.Purge();
	m_LinkedNodes.Purge();
}

template < typename T >
inline
typename CUtlGraphMatrix< T >::Index_t
CUtlGraphMatrix< T >::GetNodeIndex( const T &node ) const
{
	return m_Nodes.Find( node );
}

template < typename T >
inline
typename CUtlGraphMatrix< T >::Index_t
CUtlGraphMatrix< T >::GetLinkIndex( Index_t a, Index_t b ) const
{
	return a + b * m_nSize;
}

#endif // UTLGRAPH_H
