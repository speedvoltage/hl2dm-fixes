//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "checksum_crc.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if !defined( NO_ENTITY_PREDICTION )
namespace
{
	const int PREDICTABLE_ID_ACK_SHIFT = 0;
	const int PREDICTABLE_ID_PLAYER_SHIFT = 1;
	const int PREDICTABLE_ID_COMMAND_SHIFT = 6;
	const int PREDICTABLE_ID_HASH_SHIFT = 16;
	const int PREDICTABLE_ID_INSTANCE_SHIFT = 28;

	const uint32 PREDICTABLE_ID_ACK_MASK = 0x00000001u;
	const uint32 PREDICTABLE_ID_PLAYER_MASK = 0x0000003eu;
	const uint32 PREDICTABLE_ID_COMMAND_MASK = 0x0000ffc0u;
	const uint32 PREDICTABLE_ID_HASH_MASK = 0x0fff0000u;
	const uint32 PREDICTABLE_ID_INSTANCE_MASK = 0xf0000000u;
	COMPILE_TIME_ASSERT( ( PREDICTABLE_ID_ACK_MASK | PREDICTABLE_ID_PLAYER_MASK | PREDICTABLE_ID_COMMAND_MASK | PREDICTABLE_ID_HASH_MASK | PREDICTABLE_ID_INSTANCE_MASK ) == 0xffffffffu );

	uint32 GetPredictableIdField( uint32 value, uint32 mask, int shift )
	{
		return ( value & mask ) >> shift;
	}

	void SetPredictableIdField( uint32 &value, uint32 mask, int shift, uint32 field )
	{
		value = ( value & ~mask ) | ( ( field << shift ) & mask );
	}

	const char *FindStableModulePath( const char *module )
	{
		const char *stablePath = module;
		int moduleLength = Q_strlen( module );

		for ( int i = 0; i + 5 <= moduleLength; ++i )
		{
			bool segmentStart = i == 0 || module[ i - 1 ] == '/' || module[ i - 1 ] == '\\';
			bool segmentEnd = module[ i + 4 ] == '/' || module[ i + 4 ] == '\\';
			if ( segmentStart && segmentEnd && !Q_strnicmp( module + i, "game", 4 ) )
			{
				stablePath = module + i;
			}
		}

		return stablePath;
	}

	void CanonicalizeModulePath( const char *module, char *buffer, int bufferSize )
	{
		Q_strncpy( buffer, FindStableModulePath( module ), bufferSize );
		Q_FixSlashes( buffer, '/' );
		Q_strlower( buffer );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Helper class for resetting instance numbers, etc.
//-----------------------------------------------------------------------------
class CPredictableIdHelper
{
public:
	CPredictableIdHelper()
	{
		Reset( -1 );
	}

	void	Reset( int command )
	{
		m_nCurrentCommand = command;
		m_nCount = 0;
		memset( m_Entries, 0, sizeof( m_Entries ) );
	}

	int		AddEntry( int command, int hash )
	{
		// Clear list if command number changes
		if ( command != m_nCurrentCommand )
		{
			Reset( command );
		}

		entry *e = FindOrAddEntry( hash );
		if ( !e )
			return 0;
		Assert( e->count < 16 );
		e->count++;
		return e->count-1;
	}

private:

	enum
	{
		MAX_ENTRIES = 256,
	};

	struct entry
	{
		int		hash;
		int		count;
	};

	entry			*FindOrAddEntry( int hash )
	{
		int i;
		for ( i = 0; i < m_nCount; i++ )
		{
			entry *e = &m_Entries[ i ];
			if ( e->hash == hash )
				return e;
		}

		if ( m_nCount >= MAX_ENTRIES )
		{
			Assert( m_nCount < MAX_ENTRIES );
			return NULL;
		}

		entry *e = &m_Entries[ m_nCount++ ];
		e->hash = hash;
		e->count = 0;
		return e;
	}

	int				m_nCurrentCommand;
	int				m_nCount;
	entry			m_Entries[ MAX_ENTRIES ];
};

static CPredictableIdHelper g_Helper;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CPredictableId::CPredictableId( void ) :
	m_PredictableID( 0 )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPredictableId::ResetInstanceCounters( void )
{
	g_Helper.Reset( -1 );
}

//-----------------------------------------------------------------------------
// Purpose: Is the Id being used
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPredictableId::IsActive( void ) const
{
	return m_PredictableID != 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : playerIndex - 
//-----------------------------------------------------------------------------
void CPredictableId::SetPlayer( int playerIndex )
{
	Assert( playerIndex >= 0 );
	Assert( playerIndex <= 31 );
	SetPredictableIdField( m_PredictableID, PREDICTABLE_ID_PLAYER_MASK, PREDICTABLE_ID_PLAYER_SHIFT, (uint32)playerIndex );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CPredictableId::GetPlayer( void ) const
{
	return (int)GetPredictableIdField( m_PredictableID, PREDICTABLE_ID_PLAYER_MASK, PREDICTABLE_ID_PLAYER_SHIFT );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CPredictableId::GetCommandNumber( void ) const
{
	return (int)GetPredictableIdField( m_PredictableID, PREDICTABLE_ID_COMMAND_MASK, PREDICTABLE_ID_COMMAND_SHIFT );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : commandNumber - 
//-----------------------------------------------------------------------------
void CPredictableId::SetCommandNumber( int commandNumber )
{
	Assert( commandNumber >= 0 );
	SetPredictableIdField( m_PredictableID, PREDICTABLE_ID_COMMAND_MASK, PREDICTABLE_ID_COMMAND_SHIFT, (uint32)commandNumber );
}

/*
bool CPredictableId::IsCommandNumberEqual( int testNumber ) const
{
	if ( ( testNumber & ((1<<10) - 1) ) == m_PredictableID.command )
		return true;

	return false;
}
*/

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *classname - 
//			*module - 
//			line - 
// Output : static int
//-----------------------------------------------------------------------------
static uint32 ClassFileLineHash( const char *classname, const char *module, int line )
{
	CRC32_t retval;

	CRC32_Init( &retval );

	char tempbuffer[ 512 ];
	Assert( classname );
	Assert( module );
	
	// ACK, have to go lower case due to issues with .dsp having different cases of drive
	//  letters, etc.!!!
	Q_strncpy( tempbuffer, classname ? classname : "", sizeof( tempbuffer ) );
	Q_strlower( tempbuffer );
	CRC32_ProcessBuffer( &retval, (void *)tempbuffer, Q_strlen( tempbuffer ) );
	
	CanonicalizeModulePath( module ? module : "", tempbuffer, sizeof( tempbuffer ) );
	CRC32_ProcessBuffer( &retval, (void *)tempbuffer, Q_strlen( tempbuffer ) );
	
	CRC32_ProcessBuffer( &retval, (void *)&line, sizeof( int ) );

	CRC32_Final( &retval );

	return retval;
}

//-----------------------------------------------------------------------------
// Purpose: Create a predictable id of the specified parameter set
// Input  : player - 
//			command - 
//			*classname - 
//			*module - 
//			line - 
//-----------------------------------------------------------------------------
void CPredictableId::Init( int player, int command, const char *classname, const char *module, int line )
{
	SetPlayer( player );
	SetCommandNumber( command );

	SetHash( ClassFileLineHash( classname, module, line ) );

	// Use helper to determine instance number this command
	int instance = g_Helper.AddEntry( command, GetHash() );

	// Set appropriate instance number
	SetInstanceNumber( instance );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CPredictableId::GetHash( void ) const
{
	return (int)GetPredictableIdField( m_PredictableID, PREDICTABLE_ID_HASH_MASK, PREDICTABLE_ID_HASH_SHIFT );
}

void CPredictableId::SetHash( uint32 hash )
{
	SetPredictableIdField( m_PredictableID, PREDICTABLE_ID_HASH_MASK, PREDICTABLE_ID_HASH_SHIFT, hash );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : counter - 
//-----------------------------------------------------------------------------
void CPredictableId::SetInstanceNumber( int counter )
{
	Assert( counter >= 0 );
	Assert( counter <= 15 );
	SetPredictableIdField( m_PredictableID, PREDICTABLE_ID_INSTANCE_MASK, PREDICTABLE_ID_INSTANCE_SHIFT, (uint32)counter );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int CPredictableId::GetInstanceNumber( void ) const
{
	return (int)GetPredictableIdField( m_PredictableID, PREDICTABLE_ID_INSTANCE_MASK, PREDICTABLE_ID_INSTANCE_SHIFT );
}

// Client only
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : ack - 
//-----------------------------------------------------------------------------
void CPredictableId::SetAcknowledged( bool ack )
{
	SetPredictableIdField( m_PredictableID, PREDICTABLE_ID_ACK_MASK, PREDICTABLE_ID_ACK_SHIFT, ack ? 1u : 0u );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPredictableId::GetAcknowledged( void ) const
{
	return GetPredictableIdField( m_PredictableID, PREDICTABLE_ID_ACK_MASK, PREDICTABLE_ID_ACK_SHIFT ) != 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
uint32 CPredictableId::GetRaw( void ) const
{
	return m_PredictableID;
}

uint32 CPredictableId::GetNetworkedRaw( void ) const
{
	return m_PredictableID & ~PREDICTABLE_ID_ACK_MASK;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : raw - 
//-----------------------------------------------------------------------------
void CPredictableId::SetRaw( uint32 raw )
{
	m_PredictableID = raw;
}

void CPredictableId::SetNetworkedRaw( uint32 raw )
{
	uint32 previousIdentity = GetNetworkedRaw();
	bool preserveAcknowledged = GetAcknowledged() && previousIdentity == ( raw & ~PREDICTABLE_ID_ACK_MASK );
	m_PredictableID = raw & ~PREDICTABLE_ID_ACK_MASK;
	SetAcknowledged( preserveAcknowledged );
}

//-----------------------------------------------------------------------------
// Purpose: Determine if one id is == another, ignores Acknowledged state
// Input  : other - 
// Output : bool CPredictableId::operator
//-----------------------------------------------------------------------------
bool CPredictableId::operator ==( const CPredictableId& other ) const
{
	if ( this == &other )
		return true;

	if ( GetPlayer() != other.GetPlayer() )
		return false;
	if ( GetCommandNumber() != other.GetCommandNumber() )
		return false;
	if ( GetHash() != other.GetHash() )
		return false;
	if ( GetInstanceNumber() != other.GetInstanceNumber() )
		return false;
	return true;
}

bool CPredictableId::operator !=( const CPredictableId& other ) const
{
	return !(*this == other);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char const
//-----------------------------------------------------------------------------
const char *CPredictableId::Describe( void ) const
{
	static char desc[ 128 ];

	Q_snprintf( desc, sizeof( desc ), "pl(%i) cmd(%i) hash(%i) inst(%i) ack(%s)",
		GetPlayer(),
		GetCommandNumber(),
		GetHash(),
		GetInstanceNumber() ,
		GetAcknowledged() ? "true" : "false" );

	return desc;
}
#endif
