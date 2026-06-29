#ifndef UTLUNIONFIND_H
#define UTLUNIONFIND_H

#include <tier1/utlrbtree.h>
#include "utils/utlgraph.h"

struct CUtlDisjointSetNode
{
	int begin;
	int end;

	CUtlDisjointSetNode()
	{
	}

	CUtlDisjointSetNode( int begin, int end )
		: begin( begin )
		, end( end )
	{
	}
};

class CUtlDisjointSet
{
public:
	typedef CUtlVector< int > Nodes_t;
	typedef CUtlMemory< int > NodeSet_t;
	typedef CUtlVector< CUtlDisjointSetNode > Sets_t;


public:
	CUtlDisjointSet( int capacity = 0 )
	{
		nodes_.EnsureCapacity( capacity );
	}

	const Nodes_t &Nodes() const { return nodes_; }
	Nodes_t &Nodes() { return nodes_; }
	const Sets_t &Sets() const { return sets_; }
	Sets_t &Sets() { return sets_; }

	const NodeSet_t &operator[]( int index ) const;

	void RemoveAll();
	void Purge();


private:
	Nodes_t nodes_;
	Sets_t sets_;
};

inline
const
CUtlDisjointSet::NodeSet_t &
CUtlDisjointSet::operator[]( int index ) const
{
	static NodeSet_t result( nodes_.Base(), nodes_.Count() );

	Assert( sets_.IsValidIndex( index ) );

	const CUtlDisjointSetNode &set = sets_[ index ];
	NodeSet_t interval( nodes_.Base() + set.begin, set.end - set.begin );

	result.Swap( interval );

	return result;
}

inline
void
CUtlDisjointSet::RemoveAll()
{
	nodes_.RemoveAll();
	sets_.RemoveAll();
}

inline
void
CUtlDisjointSet::Purge()
{
	nodes_.Purge();
	sets_.Purge();
}

template < typename G >
CUtlDisjointSet &
UtlUnionFind( const G &graph, const CUtlVector< int > &nodes, CUtlDisjointSet &set )
{
	typedef CUtlRBTree< int > NodeSet_t;
	set.Nodes().EnsureCapacity( nodes.Count() );

	NodeSet_t node_set( DefLessFunc( int ) );
	node_set.Insert( nodes.Base(), nodes.Count() );

	while ( node_set.Count() )
	{
		CUtlDisjointSetNode set_node;
		int node = node_set[ node_set.Root() ];
		node_set.RemoveAt( node_set.Root() );
		int last_node = set_node.begin = set.Nodes().AddToTail( node );

		do
		{
			Assert( last_node < set.Nodes().Count() );

			if ( last_node != set.Nodes().Count() - 1 )
			{
				last_node++;
			}

			const typename G::Nodes_t &links = graph[ graph.Find( set.Nodes()[ last_node ] ) ];

			FOR_EACH_VEC( links, il )
			{
				int node_index = node_set.Find( links[ il ] );
				if ( node_set.IsValidIndex( node_index ) )
				{
					set.Nodes().AddToTail( links[ il ] );
					node_set.RemoveAt( node_index );
				}
			}
		}
		while ( set.Nodes().Count() != last_node + 1 );

		set_node.end = last_node + 1;  // past end like end()
		set.Sets().AddToTail( set_node );
	}

	return set;
}

template < typename G, typename C >
CUtlDisjointSet &
UtlUnionGroup( const G &graph, const C &comparator, CUtlDisjointSet &set )
{
	typedef CUtlRBTree< int > NodeSet_t;
	set.Nodes().EnsureCapacity( graph.Count() );

	NodeSet_t node_set( DefLessFunc( int ) );
	NodeSet_t deferred_set( DefLessFunc( int ) );
	deferred_set.EnsureCapacity( graph.Count() );

	FOR_EACH_NODE( graph, i )
	{
		int node = graph.Node( i );
		if ( !comparator.skip( node ) )
		{
			deferred_set.Insert( node );
		}
	}

	while ( deferred_set.Count() )
	{
		CUtlDisjointSetNode set_node;
		int node = deferred_set[ deferred_set.Root() ];
		deferred_set.RemoveAt( deferred_set.Root() );
		int last_node = set_node.begin = set.Nodes().AddToTail( node );

		do
		{
			Assert( last_node < set.Nodes().Count() );

			if ( last_node != set.Nodes().Count() - 1 )
			{
				last_node++;
			}

			const typename G::Nodes_t &links = graph[ graph.Find( set.Nodes()[ last_node ] ) ];

			FOR_EACH_VEC( links, il )
			{
				int iil = deferred_set.Find( links[ il ] );
				if ( comparator.skip( links[ il ] ) || !deferred_set.IsValidIndex( iil ) )
				{
					continue;
				}

				if ( comparator( set.Nodes()[ last_node ], links[ il ] ) )
				{
					set.Nodes().AddToTail( links[ il ] );
					deferred_set.RemoveAt( iil );
				}
			}
		}
		while ( set.Nodes().Count() != last_node + 1 );

		set_node.end = last_node + 1;  // past end like end()
		set.Sets().AddToTail( set_node );
	}

	return set;
}

/*
template < typename G, typename I >
CUtlDisjointSet &
UtlUnionFilter( const G &graph, const I &indicator, CUtlDisjointSet &set )
{
	typedef CUtlRBTree< int > NodeSet_t;
	set.Nodes().EnsureCapacity( graph.Count() );

	NodeSet_t node_set( DefLessFunc( int ) );
	NodeSet_t deferred_set( DefLessFunc( int ) );
	deferred_set.EnsureCapacity( graph.Count() );

	FOR_EACH_NODE( graph, i )
	{
		int node = graph.Node( i );
		if ( !comparator.skip( node ) )
		{
			deferred_set.Insert( node );
		}
	}

	while ( deferred_set.Count() )
	{
		CUtlDisjointSetNode set_node;
		int node = deferred_set[ deferred_set.Root() ];
		deferred_set.RemoveAt( deferred_set.Root() );
		int last_node = set_node.begin = set.Nodes().AddToTail( node );

		do
		{
			Assert( last_node < set.Nodes().Count() );

			if ( last_node != set.Nodes().Count() - 1 )
			{
				last_node++;
			}

			const typename G::Nodes_t &links = graph[ graph.Find( set.Nodes()[ last_node ] ) ];

			if ( comparator( set.Nodes()[ last_node ], links[ il ] ) )
			{
				set.Nodes().AddToTail( links[ il ] );
				deferred_set.RemoveAt( iil );
			}
		}
		while ( set.Nodes().Count() != last_node + 1 );

		set_node.end = last_node + 1;  // past end like end()
		set.Sets().AddToTail( set_node );
	}

	return set;
}
*/

#endif // UTLUNIONFIND_H
