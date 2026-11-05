//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Weapons Resource implementation
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "history_resource.h"
#include <vgui_controls/Controls.h>
#include <vgui/ISurface.h>
#include "c_baseplayer.h"
#include "hud.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

WeaponsResource gWR;

#ifdef HL2MP
static ConVar cl_hud_show_unowned_ammo( "cl_hud_show_unowned_ammo", "0", FCVAR_ARCHIVE,
	"Show ammo pickup icons even without a weapon that uses that ammo.", true, 0, true, 1 );
#endif

void FreeHudTextureList( CUtlDict< CHudTexture *, int >& list );

static CHudTexture *FindHudTextureInDict( CUtlDict< CHudTexture *, int >& list, const char *psz )
{
	int idx = list.Find( psz );
	if ( idx == list.InvalidIndex() )
		return NULL;

	return list[ idx ];
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : 
//-----------------------------------------------------------------------------
WeaponsResource::WeaponsResource( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : 
//-----------------------------------------------------------------------------
WeaponsResource::~WeaponsResource( void )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WeaponsResource::Init( void )
{
	Reset();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void WeaponsResource::Reset( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Load all the sprites needed for all registered weapons
//-----------------------------------------------------------------------------
void WeaponsResource::LoadAllWeaponSprites( void )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		if ( player->GetWeapon(i) )
		{
			LoadWeaponSprites( player->GetWeapon(i)->GetWeaponFileInfoHandle() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void WeaponsResource::LoadWeaponSprites( WEAPON_FILE_INFO_HANDLE hWeaponFileInfo )
{
	// WeaponsResource is a friend of C_BaseCombatWeapon
	FileWeaponInfo_t *pWeaponInfo = GetFileWeaponInfoFromHandle( hWeaponFileInfo );

	if ( !pWeaponInfo )
		return;

	// Already parsed the hud elements?
	if ( pWeaponInfo->bLoadedHudElements )
		return;

	pWeaponInfo->bLoadedHudElements = true;

	pWeaponInfo->iconActive = NULL;
	pWeaponInfo->iconInactive = NULL;
	pWeaponInfo->iconAmmo = NULL;
	pWeaponInfo->iconAmmo2 = NULL;
	pWeaponInfo->iconCrosshair = NULL;
	pWeaponInfo->iconAutoaim = NULL;
	pWeaponInfo->iconSmall = NULL;

	char sz[128];
	Q_snprintf(sz, sizeof( sz ), "scripts/%s", pWeaponInfo->szClassName);

	CUtlDict< CHudTexture *, int > tempList;

	LoadHudTextures( tempList, sz, g_pGameRules->GetEncryptionKey() );

	if ( !tempList.Count() )
	{
		// no sprite description file for weapon, use default small blocks
		pWeaponInfo->iconActive = gHUD.GetIcon( "selection" );
		pWeaponInfo->iconInactive = gHUD.GetIcon( "selection" );
		pWeaponInfo->iconAmmo = gHUD.GetIcon( "bucket1" );
		return;
	}

	CHudTexture *p;
	p = FindHudTextureInDict( tempList, "crosshair" );
	if ( p )
	{
		pWeaponInfo->iconCrosshair = gHUD.AddUnsearchableHudIconToList( *p );
	}

	p = FindHudTextureInDict( tempList, "autoaim" );
	if ( p )
	{
		pWeaponInfo->iconAutoaim = gHUD.AddUnsearchableHudIconToList( *p );
	}

	p = FindHudTextureInDict( tempList, "zoom" );
	if ( p )
	{
		pWeaponInfo->iconZoomedCrosshair = gHUD.AddUnsearchableHudIconToList( *p );
	}
	else
	{
		pWeaponInfo->iconZoomedCrosshair = pWeaponInfo->iconCrosshair; //default to non-zoomed crosshair
	}

	p = FindHudTextureInDict( tempList, "zoom_autoaim" );
	if ( p )
	{
		pWeaponInfo->iconZoomedAutoaim = gHUD.AddUnsearchableHudIconToList( *p );
	}
	else
	{
		pWeaponInfo->iconZoomedAutoaim = pWeaponInfo->iconZoomedCrosshair;  //default to zoomed crosshair
	}

	CHudHistoryResource *pHudHR = GET_HUDELEMENT( CHudHistoryResource );	
	if( pHudHR )
	{
		p = FindHudTextureInDict( tempList, "weapon" );
		if ( p )
		{
			pWeaponInfo->iconInactive = gHUD.AddUnsearchableHudIconToList( *p );
			if ( pWeaponInfo->iconInactive )
			{
				pWeaponInfo->iconInactive->Precache();
				pHudHR->SetHistoryGap( pWeaponInfo->iconInactive->Height() );
			}
		}

		p = FindHudTextureInDict( tempList, "weapon_s" );
		if ( p )
		{
			pWeaponInfo->iconActive = gHUD.AddUnsearchableHudIconToList( *p );
			if ( pWeaponInfo->iconActive )
			{
				pWeaponInfo->iconActive->Precache();
			}
		}

		p = FindHudTextureInDict( tempList, "weapon_small" );
		if ( p )
		{
			pWeaponInfo->iconSmall = gHUD.AddUnsearchableHudIconToList( *p );
			if ( pWeaponInfo->iconSmall )
			{
				pWeaponInfo->iconSmall->Precache();
			}
		}

		p = FindHudTextureInDict( tempList, "ammo" );
		if ( p )
		{
			pWeaponInfo->iconAmmo = gHUD.AddUnsearchableHudIconToList( *p );
			if ( pWeaponInfo->iconAmmo )
			{
				pWeaponInfo->iconAmmo->Precache();
				pHudHR->SetHistoryGap( pWeaponInfo->iconAmmo->Height() );
			}
		}

		p = FindHudTextureInDict( tempList, "ammo2" );
		if ( p )
		{
			pWeaponInfo->iconAmmo2 = gHUD.AddUnsearchableHudIconToList( *p );
			if ( pWeaponInfo->iconAmmo2 )
			{
				pWeaponInfo->iconAmmo2->Precache();
				pHudHR->SetHistoryGap( pWeaponInfo->iconAmmo2->Height() );
			}
		}
	}

	FreeHudTextureList( tempList );
}

//-----------------------------------------------------------------------------
// Purpose: Helper function to return a Ammo pointer from id
//-----------------------------------------------------------------------------
CHudTexture *WeaponsResource::GetAmmoIconFromWeapon( int iAmmoId )
{
	if ( iAmmoId < 0 )
		return NULL;

	C_BasePlayer *player = GetHudPlayer();
	if ( !player )
		return NULL;

	for ( int i = 0; i < MAX_WEAPONS; i++ )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( !weapon )
			continue;

		if ( weapon->GetPrimaryAmmoType() == iAmmoId && weapon->GetWpnData().iconAmmo )
		{
			return weapon->GetWpnData().iconAmmo;
		}
		else if ( weapon->GetSecondaryAmmoType() == iAmmoId && weapon->GetWpnData().iconAmmo2 )
		{
			return weapon->GetWpnData().iconAmmo2;
		}
	}

	// Ammo can be collected before its weapon, or remain after that weapon is
	// dropped. Its icon belongs to the weapon definition, not a live entity.
#ifdef HL2MP
	if ( !cl_hud_show_unowned_ammo.GetBool() )
		return NULL;
#endif
	for ( int i = 0; i < GetWeaponInfoCount(); ++i )
	{
		const WEAPON_FILE_INFO_HANDLE handle = (WEAPON_FILE_INFO_HANDLE)i;
		FileWeaponInfo_t *info = GetFileWeaponInfoFromHandle( handle );
		if ( !info->bParsedScript || ( info->iAmmoType != iAmmoId && info->iAmmo2Type != iAmmoId ) )
			continue;
		LoadWeaponSprites( handle );
		CHudTexture *icon = info->iAmmoType == iAmmoId ? info->iconAmmo : info->iconAmmo2;
		if ( icon )
			return icon;
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Get a pointer to a weapon using this ammo
//-----------------------------------------------------------------------------
const FileWeaponInfo_t *WeaponsResource::GetWeaponFromAmmo( int iAmmoId )
{
	if ( iAmmoId < 0 )
		return NULL;

	C_BasePlayer *player = GetHudPlayer();
	if ( !player )
		return NULL;

	for ( int i = 0; i < MAX_WEAPONS; i++ )
	{
		C_BaseCombatWeapon *weapon = player->GetWeapon( i );
		if ( !weapon )
			continue;

		if ( weapon->GetPrimaryAmmoType() == iAmmoId )
		{
			return &weapon->GetWpnData();
		}
		else if ( weapon->GetSecondaryAmmoType() == iAmmoId )
		{
			return &weapon->GetWpnData();
		}
	}

	for ( int i = 0; i < GetWeaponInfoCount(); ++i )
	{
		const FileWeaponInfo_t *info = GetFileWeaponInfoFromHandle( (WEAPON_FILE_INFO_HANDLE)i );
		if ( info->bParsedScript && ( info->iAmmoType == iAmmoId || info->iAmmo2Type == iAmmoId ) )
			return info;
	}
	return NULL;
}

