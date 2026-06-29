#include "cbase.h"
#include "p3_util_strings.h"
//////////////////////////////////////////////////////////////////////////

const char* FindFirstInString(const char* s, const char* sps, bool bFindEndOfLine)
{
	int near_idx = 1000000;
	for (int i=0; i < Q_strlen(sps); i++)
	{
		char sp = sps[i]; 
		const char *tmp = Q_strnchr(s,sp,Q_strlen(s));
		if (!tmp) 
			continue;
		int dest = tmp-s;
		if (dest<near_idx)
			near_idx = dest;
	}

	if (near_idx > Q_strlen(s)) return bFindEndOfLine ? s + Q_strlen(s) : 0;

	return s+near_idx;
}

void TrimSpacesAndTabs(const char* s, char* result, bool leave_one)
{
	char toTrim[] = " \t";
	int TrimCount = strlen(toTrim);

	int idx = 0;
	for( const char* p =s; *p; p++)
	{
		if( (Q_strnchr(toTrim, *p, TrimCount) == 0) ||
			(leave_one && (idx == 0 || Q_strnchr(toTrim, result[idx-1], TrimCount) == 0)) )
		{
			result[idx++] = *p;
		}
	}
	result[idx] = '\0';
}

const char* VectorToString( const Vector& v )
{
	static char buf[64];
	Q_snprintf( buf, sizeof buf, "%f %f %f", v.x, v.y, v.z );
	return buf;
}

Vector StringToVector( const char* s )
{
	Vector v;
	UTIL_StringToVector( v.Base(), s );
	return v;
}