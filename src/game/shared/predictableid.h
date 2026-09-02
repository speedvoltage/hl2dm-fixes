//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef PREDICTABLEID_H
#define PREDICTABLEID_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"

#if !defined( NO_ENTITY_PREDICTION )
//-----------------------------------------------------------------------------
// Purpose: Wraps 32bit predictID to allow access and creation
//-----------------------------------------------------------------------------
class CPredictableId
{
public:
	enum
	{
		NETWORKED_BITS = 32,
	};

	// Construction
					CPredictableId( void );

	static void		ResetInstanceCounters( void );

	// Is the Id being used
	bool			IsActive( void ) const;

	// Call this to set from data
	void			Init( int player, int command, const char *classname, const char *module, int line );

	// Get player index
	int				GetPlayer( void ) const;
	// Get hash value
	int				GetHash( void ) const;
	// Get index number
	int				GetInstanceNumber( void ) const;
	// Get command number
	int				GetCommandNumber( void ) const;

	// Check command number
//	bool			IsCommandNumberEqual( int testNumber ) const;

	// Client only
	void			SetAcknowledged( bool ack );
	bool			GetAcknowledged( void ) const;

	// For conversion to/from integer
	uint32			GetRaw( void ) const;
	uint32			GetNetworkedRaw( void ) const;
	void			SetRaw( uint32 raw );
	void			SetNetworkedRaw( uint32 raw );

	char const		*Describe( void ) const;

	// Equality test
	bool operator ==( const CPredictableId& other ) const;
	bool operator !=( const CPredictableId& other ) const;
private:
	static uint32	GetField( uint32 value, uint32 mask, int shift );
	static void		SetField( uint32 &value, uint32 mask, int shift, uint32 field );

	void			SetCommandNumber( int commandNumber );
	void			SetPlayer( int playerIndex );
	void			SetHash( uint32 hash );
	void			SetInstanceNumber( int counter );

	uint32			m_PredictableID;
};

COMPILE_TIME_ASSERT( sizeof( CPredictableId ) == sizeof( uint32 ) );
COMPILE_TIME_ASSERT( CPredictableId::NETWORKED_BITS == sizeof( uint32 ) * 8 );

// This can be empty, the class has a proper constructor
FORCEINLINE void NetworkVarConstruct( CPredictableId &x ) {}

#endif

#endif // PREDICTABLEID_H
