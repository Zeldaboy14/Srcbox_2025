#ifndef P3_CORE_PARSER_H
#define P3_CORE_PARSER_H

#include <ctype.h>

template <int S>
int SplitParams( const char *param_string, char (&params)[S][32] )
{
	char trimmed[1024];

	TrimSpacesAndTabs( param_string, trimmed );

	return SplitStr( trimmed, ",", params );
}

inline bool IsNumber( const char *token )
{
	Assert( token );

	if ( *token == '-' )
		token++;

	for ( const char *p = token; *p; p++ )
	{
		if ( !isdigit( *p ) )
			return false;
	}

	return true;
}


//////////////////////////////////////////////////////////////////////////
template <int BUFFER_SIZE>
int ParseToken( const char *token, const char tokens[][BUFFER_SIZE], int tokens_num, int def = -1 )
{
	for ( int i = 0; i < tokens_num; i++ )
	{
		if ( Q_stricmp( token, tokens[i] ) == 0 )
		{
			return i;
		}
	}

	return def;
}

#endif // P3_CORE_PARSER
