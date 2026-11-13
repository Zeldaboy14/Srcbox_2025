//===== Reconstructed from particles.lib debug info (src/common/blobulator/smartarray.h) =====//
//
// Minimal growable array used throughout the blobulator.
//   T                 element type (copied bitwise)
//   MEMALIGN          1: storage is 16-byte aligned and allocated through g_pMemAlloc
//                     0: storage comes from the CRT malloc/realloc/free
//   INITIAL_CAPACITY  number of elements allocated by the constructor (0: no allocation)
//=============================================================================================//
#ifndef BLOB_SMARTARRAY_H
#define BLOB_SMARTARRAY_H
#pragma once

#include <stdlib.h>
#include <string.h>
#include "tier0/memalloc.h"

template < class T, int MEMALIGN = 0, int INITIAL_CAPACITY = 16 >
class SmartArray
{
public:
	SmartArray()
	{
		size = 0;
		if ( INITIAL_CAPACITY > 0 )
		{
			capacity = INITIAL_CAPACITY;
			if ( MEMALIGN )
				a = (T *)MemAlloc_AllocAligned( capacity * sizeof( T ), 16 );
			else
				a = (T *)malloc( capacity * sizeof( T ) );
		}
		else
		{
			capacity = 0;
			a = NULL;
		}
	}

	~SmartArray()
	{
		if ( MEMALIGN )
			MemAlloc_FreeAligned( a );
		else
			free( a );
	}

	void ensureCapacity( int n )
	{
		if ( capacity < n )
			resize( n * 2 );
	}

	void ensureAdditionalCapacity( int n )
	{
		int newSize = size + n;
		if ( capacity < newSize )
		{
			capacity = newSize * 2;
			if ( MEMALIGN )
			{
				T *newA = (T *)MemAlloc_AllocAligned( capacity * sizeof( T ), 16 );
				memcpy( newA, a, size * sizeof( T ) );
				MemAlloc_FreeAligned( a );
				a = newA;
			}
			else
			{
				a = (T *)realloc( a, capacity * sizeof( T ) );
			}
		}
	}

	void resize( int n )
	{
		capacity = n;
		if ( MEMALIGN )
		{
			T *newA = (T *)MemAlloc_AllocAligned( capacity * sizeof( T ), 16 );
			memcpy( newA, a, size * sizeof( T ) );
			MemAlloc_FreeAligned( a );
			a = newA;
		}
		else
		{
			a = (T *)realloc( a, capacity * sizeof( T ) );
		}
	}

	T &operator[]( int i )
	{
		return a[i];
	}

	void put( int i, const T &x )
	{
		a[i] = x;
	}

	void push( const T &x )
	{
		a[size] = x;
		size++;
	}

	T &push()
	{
		return a[size++];
	}

	T &interlockedPush();
	int interlockedIncrementSize();

	void push2( const T &x, const T &y )
	{
		a[size] = x;
		a[size + 1] = y;
		size += 2;
	}

	void push2AutoSize( const T &x, const T &y )
	{
		if ( size + 1 >= capacity )
		{
			capacity = size * 2 + 4;
			a = (T *)realloc( a, capacity * sizeof( T ) );
		}
		a[size] = x;
		a[size + 1] = y;
		size += 2;
	}

	void pushAutoSize( const T &x )
	{
		if ( size >= capacity )
		{
			capacity = size * 2 + 2;
			a = (T *)realloc( a, capacity * sizeof( T ) );
		}
		a[size] = x;
		size++;
	}

	T &pushAutoSize()
	{
		if ( size >= capacity )
		{
			capacity = size * 2 + 2;
			a = (T *)realloc( a, capacity * sizeof( T ) );
		}
		return a[size++];
	}

	T &pop()
	{
		return a[--size];
	}

	T &interlockedPop();
	int interlockedDecrementSize();

	void pop2( T &x, T &y )
	{
		y = a[--size];
		x = a[--size];
	}

	void remove( int i )
	{
		a[i] = a[--size];
	}

	T removeAndReturn( int i )
	{
		T r = a[i];
		a[i] = a[--size];
		return r;
	}

	int find( const T &x )
	{
		for ( int i = 0; i < size; i++ )
		{
			if ( a[i] == x )
				return i;
		}
		return -1;
	}

	bool findAndRemove( const T &x )
	{
		int i = find( x );
		if ( i < 0 )
			return false;
		remove( i );
		return true;
	}

	void findAndRemoveAll( const T &x )
	{
		for ( int i = size - 1; i >= 0; i-- )
		{
			if ( a[i] == x )
				remove( i );
		}
	}

	T &last()
	{
		return a[size - 1];
	}

	void swap( int i, int j )
	{
		T t = a[i];
		a[i] = a[j];
		a[j] = t;
	}

	// true if no adjacent pair is out of order according to C::gt
	template < class C >
	bool testSort()
	{
		for ( int i = 0; i < size - 1; i++ )
		{
			if ( C::gt( a[i], a[i + 1] ) )
				return false;
		}
		return true;
	}

	template < class C >
	void sort()
	{
		if ( size == 2 )
		{
			if ( C::gt( a[0], a[1] ) )
				swap( 0, 1 );
		}
		else if ( size > 2 )
		{
			sort< C >( 0, size - 1 );
		}
	}

	// median-of-three quicksort; recurses on the left part and loops on the right part
	template < class C >
	void sort( int lo0, int hi0 )
	{
		for ( ;; )
		{
			int lo = lo0;
			int hi = hi0;
			int midIndex = ( lo0 + hi0 ) / 2;

			if ( C::gt( a[lo], a[hi] ) )
				swap( lo, hi );
			if ( C::gt( a[lo], a[midIndex] ) )
				swap( lo, midIndex );
			if ( C::gt( a[midIndex], a[hi] ) )
				swap( midIndex, hi );

			if ( hi0 - lo0 <= 2 )
				return;

			// park the pivot at the end
			T mid = a[midIndex];
			a[midIndex] = a[hi0];

			lo = lo0 - 1;
			hi = hi0;
			for ( ;; )
			{
				while ( C::gt( mid, a[++lo] ) )
					;
				while ( C::gt( a[--hi], mid ) )
					;
				if ( lo >= hi )
					break;
				swap( lo, hi );
			}

			a[hi0] = a[lo];
			a[lo] = mid;

			int leftHi = lo - 1;
			int rightLo = lo + 1;

			int n = leftHi - lo0;
			if ( n == 1 )
			{
				if ( C::gt( a[lo0], a[leftHi] ) )
					swap( lo0, leftHi );
			}
			else if ( n > 1 )
			{
				sort< C >( lo0, leftHi );
			}

			n = hi0 - rightLo;
			if ( n == 1 )
			{
				if ( C::gt( a[rightLo], a[hi0] ) )
					swap( rightLo, hi0 );
				return;
			}
			if ( n <= 1 )
				return;
			lo0 = rightLo;
		}
	}

	void swapData( SmartArray &o )
	{
		T *ta = a;
		a = o.a;
		o.a = ta;

		int ts = size;
		size = o.size;
		o.size = ts;

		int tc = capacity;
		capacity = o.capacity;
		o.capacity = tc;
	}

	T *a;
	int size;
	int capacity;
};

#endif // BLOB_SMARTARRAY_H
