#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "tier3/tier3.h"
#include "base64.h"
#include "lzma/lzma.h"
#include <lzmaDecoder.h>
#include "utlbuffer.h"

#include "tier0/valve_minmax_off.h"
// This is wrapped by minmax_off due to Valve making a macro for min and max...
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
// Now we can unwrap
#include "tier0/valve_minmax_on.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace rapidjson;

LUA_REGISTRATION_INIT( Serializers )

//
// CRC32
//

LUA_BINDING_BEGIN( Serializers, Crc32, "library", "Compute the CRC32 of a string" )
{
    const char *pszString = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "string" );

    lua_pushinteger( L, CRC32_ProcessSingleBuffer( pszString, Q_strlen( pszString ) ) );
    return 1;
}
LUA_BINDING_END( "integer", "CRC32 of the string." )

//
// Base64
//

LUA_BINDING_BEGIN( Serializers, Base64Encode, "library", "Encode a string to base64" )
{
    // Srcbox: taken whole, so binary data ( pictures, compressed data ) encodes correctly
    size_t length = 0;
    const char *pszString = luaL_checklstring( L, 1, &length );
    bool bDisableRfc2045Compliance = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 2, false, "disableRfc2045Compliance" );

    std::string input( pszString, length );
    std::string encodedString;

    if ( bDisableRfc2045Compliance )
        encodedString = base64_encode( input, false );
    else
        encodedString = base64_encode_mime( input );

    lua_pushstring( L, encodedString.c_str() );

    return 1;
}
LUA_BINDING_END( "string", "Base64 encoded string." )

LUA_BINDING_BEGIN( Serializers, Base64Decode, "library", "Decode a base64 encoded string" )
{
    // Srcbox: the result can be binary ( zero bytes ), so it's returned whole
    size_t length = 0;
    const char *pszString = luaL_checklstring( L, 1, &length );
    std::string decodedString = base64_decode( std::string( pszString, length ) );

    lua_pushlstring( L, decodedString.data(), decodedString.size() );

    return 1;
}
LUA_BINDING_END( "string", "Decoded string." )

//
// JSON
//

// TODO: Test and expand this for all datatypes.
static void WriteLuaTableToJson( lua_State *L, PrettyWriter< StringBuffer > &writer, int index )
{
    lua_pushnil( L );

    while ( lua_next( L, index ) != 0 )
    {
        if ( lua_type( L, -2 ) == LUA_TSTRING )
        {
            writer.Key( lua_tostring( L, -2 ) );
        }
        else
        {
            writer.Key( "" );
        }

        switch ( lua_type( L, -1 ) )
        {
            case LUA_TNUMBER:
                writer.Double( lua_tonumber( L, -1 ) );
                break;
            case LUA_TSTRING:
                writer.String( lua_tostring( L, -1 ) );
                break;
            case LUA_TBOOLEAN:
                writer.Bool( lua_toboolean( L, -1 ) );
                break;
            case LUA_TTABLE:
                writer.StartObject();
                WriteLuaTableToJson( L, writer, lua_gettop( L ) );
                writer.EndObject();
                break;
            default:
                DevWarning( "Unsupported type %s\n", lua_typename( L, lua_type( L, -1 ) ) );
                writer.Null();
                break;
        }

        lua_pop( L, 1 );  // Pop the value, keep the key
    }
}

LUA_BINDING_BEGIN( Serializers, JsonEncode, "library", "Encode a lua table to JSON" )
{
    luaL_checktype( L, 1, LUA_TTABLE );

    StringBuffer buffer;
    buffer.Clear();
    PrettyWriter< StringBuffer > writer( buffer );

    WriteLuaTableToJson( L, writer, 1 );

    lua_pushstring( L, buffer.GetString() );
    return 1;
}
LUA_BINDING_END( "string", "JSON encoded string." )

//
// LZMA
//

// Srcbox: these use Garry's Mod's format ( util.Compress / util.Decompress ): the 5 LZMA property
// bytes, the uncompressed size as an 8-byte little endian integer, then the LZMA stream. That's
// what GMod's .dupe files and anything else GMod compressed hold, so those open here and what's
// compressed here opens in GMod. The engine's own format ( an lzma_header_t, "LZMA" first ) is
// still read. Strings are taken whole ( compressed data has zero bytes in it ), and failing to
// decompress gives nil, like GMod.
#define GMOD_LZMA_HEADER_SIZE ( 5 + 8 )

LUA_BINDING_BEGIN( Serializers, LzmaCompress, "library", "Compress a string using LZMA" )
{
    size_t inputLength = 0;
    const char *inputString = luaL_checklstring( L, 1, &inputLength );

    unsigned int valveSize = 0;
    unsigned char *valveData = LZMA_Compress( ( unsigned char * )inputString, ( unsigned int )inputLength, &valveSize );

    if ( !valveData || valveSize < sizeof( lzma_header_t ) )
    {
        if ( valveData )
            free( valveData );

        lua_pushnil( L );
        return 1;
    }

    // Engine format -> GMod format: same properties and stream, a different header
    const lzma_header_t *pHeader = ( const lzma_header_t * )valveData;
    const unsigned int streamSize = valveSize - sizeof( lzma_header_t );

    CUtlBuffer output;
    output.EnsureCapacity( GMOD_LZMA_HEADER_SIZE + streamSize );
    output.Put( pHeader->properties, 5 );

    unsigned long long actualSize = pHeader->actualSize;
    for ( int i = 0; i < 8; i++ )
        output.PutUnsignedChar( ( unsigned char )( actualSize >> ( 8 * i ) ) );

    output.Put( valveData + sizeof( lzma_header_t ), streamSize );
    free( valveData );

    lua_pushlstring( L, ( const char * )output.Base(), output.TellPut() );
    return 1;
}
LUA_BINDING_END( "string", "Compressed string." )

LUA_BINDING_BEGIN( Serializers, LzmaDecompress, "library", "Decompress a string using LZMA" )
{
    size_t compressedLength = 0;
    const char *compressedString = luaL_checklstring( L, 1, &compressedLength );
    const lua_Integer maxSize = luaL_optinteger( L, 2, 0 );

    // Anything bigger than this isn't a real decompressed size ( and wouldn't fit the engine's header )
    const unsigned long long sizeLimit = maxSize > 0 ? ( unsigned long long )maxSize : 0x7FFFFFFFull;

    CUtlBuffer valve;
    const unsigned char *input = ( const unsigned char * )compressedString;

    if ( compressedLength >= sizeof( lzma_header_t ) && LZMA_IsCompressed( ( unsigned char * )input ) )
    {
        // Already the engine's format
        if ( ( ( const lzma_header_t * )input )->actualSize > sizeLimit )
        {
            lua_pushnil( L );
            return 1;
        }

        valve.Put( input, ( int )compressedLength );
    }
    else if ( compressedLength > GMOD_LZMA_HEADER_SIZE )
    {
        unsigned long long actualSize = 0;
        for ( int i = 0; i < 8; i++ )
            actualSize |= ( unsigned long long )input[5 + i] << ( 8 * i );

        if ( actualSize > sizeLimit )
        {
            lua_pushnil( L );
            return 1;
        }

        // GMod format -> engine format, for LZMA_Uncompress
        lzma_header_t header;
        header.id = LZMA_ID;
        header.actualSize = ( unsigned int )actualSize;
        header.lzmaSize = ( unsigned int )( compressedLength - GMOD_LZMA_HEADER_SIZE );
        memcpy( header.properties, input, 5 );

        valve.Put( &header, sizeof( header ) );
        valve.Put( input + GMOD_LZMA_HEADER_SIZE, ( int )( compressedLength - GMOD_LZMA_HEADER_SIZE ) );
    }
    else
    {
        lua_pushnil( L );
        return 1;
    }

    if ( ( ( const lzma_header_t * )valve.Base() )->actualSize == 0 )
    {
        lua_pushstring( L, "" );
        return 1;
    }

    unsigned char *outputString = NULL;
    unsigned int outUncompressedSize = 0;

    if ( !LZMA_Uncompress( ( unsigned char * )valve.Base(), &outputString, &outUncompressedSize ) )
    {
        if ( outputString )
            free( outputString );

        lua_pushnil( L );
        return 1;
    }

    lua_pushlstring( L, ( const char * )outputString, outUncompressedSize );
    free( outputString );
    return 1;
}
LUA_BINDING_END( "string", "Decompressed string." )

//
// Open the library
//

LUALIB_API int luaopen_Serializers( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Serializers );
    return 1;
}
