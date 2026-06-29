#ifndef P3_REGISTRY_H
#define P3_REGISTRY_H

//-----------------------------------------------------------------------------
// Функции для получения значения переменных из реестра
//-----------------------------------------------------------------------------

float		P3_Registry_GetFloat( const char* key, float defaultValue = 0 );
int			P3_Registry_GetInt( const char* key, int defaultValue = 0 );
const char*	P3_Registry_GetString( const char* key, const char* defaultValue = "" );

#endif // P3_REGISTRY_H

