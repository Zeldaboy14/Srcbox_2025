#ifndef UTLGRID_H
#define UTLGRID_H

#include <tier1/utlmap.h>


template < int S >
struct Cell
{
	int x, y;

	Cell()
		: x( 65535 )
		, y( 65535 )
	{
	}

	Cell( const Vector2D &point )
		: x( ( int )Round( point.x / S ) * S )
		, y( ( int )Round( point.y / S ) * S )
	{
	}

	Cell( int x, int y )
		: x( x ), y( y )
	{
	}


	bool operator ==( const Cell &rhs ) const
	{
		return x == rhs.x && y == rhs.y;
	}

	bool operator <( const Cell &rhs ) const
	{
		return x < rhs.x || ( x == rhs.x && y < rhs.y );
	}


	Vector2D Center() const
	{
		return Vector2D( x, y );
	}

	Cell<S> Top() const		{ return Cell<S>( x, y + S ); }
	Cell<S> Bottom() const	{ return Cell<S>( x, y - S); }
	Cell<S> Left() const	{ return Cell<S>( x - S, y ); }
	Cell<S> Right() const	{ return Cell<S>( x + S, y ); }
};


template < int S >
const CUtlVector< Cell< S > > &
RasterizeCircle( const Vector2D& point, float radius, CUtlVector< Cell< S > > &result )
{
	// TODO: неоптимально же!

	Cell< S > a( Vector2D( point.x - radius, point.y - radius ) );
	Cell< S > b( Vector2D( point.x + radius, point.y + radius ) );
	const float dist2 = ( radius + S / 2.f ) * ( radius + S / 2.f );

	for ( int y = a.y; y <= b.y; y += S )
	{
		for ( int x = a.x; x <= b.x; x += S )
		{
			Cell< S > cell( x, y );
			if ( point.DistToSqr( cell.Center() ) < dist2 )
			{
				result.AddToTail( cell );
			}
		}
	}

	return result;
}


//-----------------------------------------------------------------------------
// Grid with single node per cell.
//-----------------------------------------------------------------------------

template < int S >
class CUtlSimpleGrid
{
public:
	typedef Cell< S > Cell_t;
	typedef int Node_t;
	typedef CUtlMap< Cell_t, Node_t > Grid_t;


public:
	CUtlSimpleGrid();

	void Insert( const Vector2D &point, int item );
	void Remove( const Vector2D &point, int item );

	void Find( const Vector2D &point, float radius, CUtlVector< int > &result ) const;

	Node_t &operator []( const Cell_t &cell );

	void Purge();


private:
	Grid_t m_Grid;
};

template < int S >
inline
CUtlSimpleGrid< S >::CUtlSimpleGrid()
{
	m_Grid.SetLessFunc( DefLessFunc( Cell_t ) );
}

template < int S >
inline
void
CUtlSimpleGrid< S >::Insert( const Vector2D &point, int item )
{
	Cell_t c( point );

	int in = m_Grid.Find( c );

	Assert( !m_Grid.IsValidIndex( in ) );

	m_Grid.Insert( c, item );
}

template < int S >
inline
void
CUtlSimpleGrid< S >::Remove( const Vector2D &point, int item )
{
	Cell_t c( point );

	int in = m_Grid.Find( c );

	Assert( m_Grid.IsValidIndex( in ) );

	Nodes_t &nodes = m_Grid[ in ];
	nodes.FindAndRemove( item );
}

template < int S >
inline
void
CUtlSimpleGrid< S >::Find( const Vector2D &point, float radius, CUtlVector< int > &result ) const
{
	result.RemoveAll();

	Cell_t a( Vector2D( point.x - radius, point.y - radius ) );
	Cell_t b( Vector2D( point.x + radius, point.y + radius ) );

	for ( int y = a.y; y <= b.y; y += S )
	{
		for ( int x = a.x; x <= b.x; x += S )
		{
			int i = m_Grid.Find( Cell_t( x, y ) );

			if ( m_Grid.IsValidIndex( i ) )
			{
				const Nodes_t &nodes = m_Grid[ i ];

				result.AddToTail( nodes );
			}
		}
	}
}

template < int S >
inline
typename CUtlSimpleGrid< S >::Node_t &
CUtlSimpleGrid< S >::operator []( const Cell_t &cell )
{
	Grid_t::IndexType_t i = m_Grid.Find( cell );

	if ( !m_Grid.IsValidIndex( i ) )
	{
		i = m_Grid.Insert( cell, -1 );
	}

	return m_Grid[ i ];
}

template < int S >
inline
void
CUtlSimpleGrid< S >::Purge()
{
	m_Grid.Purge();
}


//-----------------------------------------------------------------------------
// Grid with multiple nodes per cell.
//-----------------------------------------------------------------------------

template < int S >
class CUtlGrid
{
public:
	typedef Cell< S > Cell_t;
	typedef CCopyableUtlVector< int > Nodes_t;
	typedef CUtlMap< Cell_t, Nodes_t > Grid_t;
	typedef typename Grid_t::KeyType_t KeyType_t;
	typedef typename Grid_t::ElemType_t ElemType_t;
	typedef typename Grid_t::IndexType_t IndexType_t;


public:
	CUtlGrid();

	// Max "size" of the vector
	IndexType_t  MaxElement() const							{ return m_Grid.MaxElement(); }

	// Checks if a node is valid and in the map
	bool  IsValidIndex( IndexType_t i ) const				{ return m_Grid.IsValidIndex( i ); }

	static int Size() { return S; }

	ElemType_t &		Element( IndexType_t i )			{ return m_Grid.Element( i ); }
	const ElemType_t &	Element( IndexType_t i ) const		{ return m_Grid.Element( i ); }
	ElemType_t &		operator[]( IndexType_t i )			{ return m_Grid.Element( i ); }
	const ElemType_t &	operator[]( IndexType_t i ) const	{ return m_Grid.Element( i ); }
	KeyType_t &			Key( IndexType_t i )				{ return m_Grid.Key( i ); }
	const KeyType_t &	Key( IndexType_t i ) const			{ return m_Grid.Key( i ); }

	void Insert( const Vector2D &point, int item );
	void Remove( const Vector2D &point, int item );

	void Find( const Vector2D &point, float radius, CUtlVector< int > &result ) const;

	Nodes_t &operator []( const Cell_t &cell );

	void Purge();


private:
	Grid_t m_Grid;
};

template < int S >
inline
CUtlGrid< S >::CUtlGrid()
{
	m_Grid.SetLessFunc( DefLessFunc( Cell_t ) );
}

template < int S >
inline
void
CUtlGrid< S >::Insert( const Vector2D &point, int item )
{
	Cell_t c( point );

	int in = m_Grid.Find( c );

	if ( !m_Grid.IsValidIndex( in ) )
	{
		Nodes_t nodes;
		nodes.AddToTail( item );
		m_Grid.Insert( c, nodes );
	}
	else
	{
		Nodes_t &nodes = m_Grid[ in ];
		nodes.AddToTail( item );
	}
}

template < int S >
inline
void
CUtlGrid< S >::Remove( const Vector2D &point, int item )
{
	Cell_t c( point );

	int in = m_Grid.Find( c );

	Assert( m_Grid.IsValidIndex( in ) );

	Nodes_t &nodes = m_Grid[ in ];
	nodes.FindAndRemove( item );
}

template < int S >
inline
void
CUtlGrid< S >::Find( const Vector2D &point, float radius, CUtlVector< int > &result ) const
{
	result.RemoveAll();

	Cell_t a( Vector2D( point.x - radius, point.y - radius ) );
	Cell_t b( Vector2D( point.x + radius, point.y + radius ) );

	for ( int y = a.y; y <= b.y; y += S )
	{
		for ( int x = a.x; x <= b.x; x += S )
		{
			int i = m_Grid.Find( Cell_t( x, y ) );

			if ( m_Grid.IsValidIndex( i ) )
			{
				const Nodes_t &nodes = m_Grid[ i ];

				result.AddVectorToTail( nodes );
			}
		}
	}
}

template < int S >
inline
typename CUtlGrid< S >::Nodes_t &
CUtlGrid< S >::operator []( const Cell_t &cell )
{
	Grid_t::IndexType_t i = m_Grid.Find( cell );

	if ( !m_Grid.IsValidIndex( i ) )
	{
		i = m_Grid.Insert( cell, Nodes_t() );
	}

	return m_Grid[ i ];
}

template < int S >
inline
void
CUtlGrid< S >::Purge()
{
	m_Grid.Purge();
}

#endif // UTLGRID_H
