#ifndef P3_UTIL_STRINGS
#define P3_UTIL_STRINGS

//////////////////////////////////////////////////////////////////////////

const char* FindFirstInString(const char* s, const char* sps, bool bFindEndOfLine = false);
void TrimSpacesAndTabs(const char* s, char* result, bool leave_one = false);
//-----
template<int Nstr, int MaxW>
int SplitStr(const char* src, const char* sps, char (&sParts)[Nstr][MaxW])
{
	if ( !src || !src[0] )
		return 0;

	int nParts = 0;
	const char *s = 0;
	/*while ( sp ) 
	{
	s = Q_strstr(src,&sp[0]);
	if (s)
	break;
	sp++;
	}*/

	s = FindFirstInString(src,sps);
	if( s )
	{
		const char* bs = src;
		while(bs)
		{
			if( nParts >= Nstr )
			{
				AssertMsg(0,"Buffer too small");
				Warning("Error: Buffer overflow. Parse string(%s), max tokens = %i \n", src, Nstr);
				return Nstr;
			}
			int part_len;
			if (s)
				part_len = (int)(s - bs + 1);
			else
				part_len = Q_strlen(bs)+1;
			sParts[nParts][0] = '\0';
			if( MaxW < part_len ) 
			{
				AssertMsg(0,"Buffer too small");
				char temp[1024];
				Q_strncpy(temp, bs, part_len);
				Warning("Error: Buffer too small. Parse string(%s), token(%s[%i]), max token size[%i]\n", src, temp, part_len, MaxW);
			}
			if (part_len > 1) // empty string check
			{
				Q_strncpy(sParts[nParts], bs, part_len);
				nParts++;
			}
			
			if (s) 
				bs = s+1;
			else
				break;
			s = FindFirstInString(s+1,sps);
		}
	}
	else
	{
		if( MaxW < strlen(src) ) 
		{
			AssertMsg(0,"Buffer too small");
			Warning("Error: Buffer too small. Parse string(%s), token(%s[%i]), max token size[%i]\n", src, src, strlen(src), MaxW);
		}
		Q_strcpy(sParts[0], src);
		nParts = 1;
	}

	return nParts;
}

//-----------------------------------------------------------------------------
// FieldsIterator -- итератор по подстрокам, разделенным символом-разделителем
//-----------------------------------------------------------------------------

template< const int MAX_FIELD_LEN = 32 >
class FieldsIterator
{
public:
	FieldsIterator()
	{
		Init( 0, ' ' );
	}

	FieldsIterator( const char* buffer, char separator )
	{
		Init( buffer, separator );
	}

	void Init( const char* buffer, char separator )
	{
		m_bufferPointer = buffer;
		m_fieldSeparator = separator;
		m_currentField[0] = 0;

		Next();
	}

	bool IsDone() const
	{
		// [ укзатель равен 0 ] или [ достигнут конец буффера и не было больше токена ]
		return ( !m_bufferPointer || ( !*m_bufferPointer && !m_currentField[0] ) );
	}

	void Next()
	{
		if ( !IsDone() )
		{
			SkipSeparators();

			if ( !IsDone() )
			{
				const char* start = m_bufferPointer++;
				SkipNonSeparators();

				int size = min( MAX_FIELD_LEN, m_bufferPointer-start+1 );
				Q_strncpy( m_currentField, start, size );
				TrimSpacesAndTabs( m_currentField, m_currentField );

				SkipSeparators();
			}
			else
			{
				m_currentField[0] = 0; // done
			}
		}
	}

	const char* GetCurrent() const
	{
		return m_currentField;
	}

	const char* operator*() const
	{
		return GetCurrent();
	}

	const char* GetBufferPointer ()
	{
		return m_bufferPointer;
	}

private:
	void SkipSeparators()
	{
		while ( *m_bufferPointer && *m_bufferPointer == m_fieldSeparator  ) m_bufferPointer++;
	}

	void SkipNonSeparators()
	{
		while ( *m_bufferPointer && *m_bufferPointer != m_fieldSeparator ) m_bufferPointer++;
	}

	const char*	m_bufferPointer;
	char		m_fieldSeparator;
	char		m_currentField[MAX_FIELD_LEN];
};

#define FOR_EACH_FIELD_EX( BUFFER, SEPARATOR, I, MAXLEN ) \
	for ( FieldsIterator<MAXLEN> I( BUFFER,  SEPARATOR ); !I.IsDone(); I.Next() )

#define FOR_EACH_FIELD( BUFFER, SEPARATOR, I ) FOR_EACH_FIELD_EX( BUFFER, SEPARATOR, I, 32 ) 


const char* VectorToString( const Vector& v );
Vector StringToVector( const char* s );

#endif	// P3_UTIL_STRINGS
