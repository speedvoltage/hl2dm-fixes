//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Draws the normal TF2 or HL2 HUD.
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "clientmode_hl2mpnormal.h"
#include "hl2mp_integrity.h"
#include "vgui_int.h"
#include "hud.h"
#include <vgui/IInput.h>
#include <vgui/IPanel.h>
#include <vgui/ISurface.h>
#include <vgui_controls/AnimationController.h>
#include "iinput.h"
#include "hl2mpclientscoreboard.h"
#include "hl2mptextwindow.h"
#include "ienginevgui.h"
#include "c_hl2mp_player.h"
#include "glow_outline_effect.h"
#include "hl2mp_gamerules.h"
#include "clienteffectprecachesystem.h"
#include "hl2mp_mainmenu.h"
#include "hl2mp_loading.h"
#include "hl2mp_messages.h"
#include "hl2mp_community.h"
#include "hl2mp_vote.h"
#include "hl2mp_hud_customization.h"
#include "view_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CLIENTEFFECT_REGISTER_BEGIN( PrecacheHL2MPSpectatorGlowEffects )
	CLIENTEFFECT_MATERIAL( "dev/glow_blur_x" )
	CLIENTEFFECT_MATERIAL( "dev/glow_blur_y" )
	CLIENTEFFECT_MATERIAL( "dev/glow_color" )
	CLIENTEFFECT_MATERIAL( "dev/glow_downsample" )
	CLIENTEFFECT_MATERIAL( "dev/halo_add_to_screen" )
CLIENTEFFECT_REGISTER_END()

static ConVar mm_menu_camera_pitch( "mm_menu_camera_pitch", "9", FCVAR_CHEAT, "Preview the background camera pitch." );
static ConVar mm_menu_camera_yaw( "mm_menu_camera_yaw", "210", FCVAR_CHEAT, "Preview the background camera yaw." );

void ClientModeHL2MPNormal::OverrideView( CViewSetup *view )
{
	BaseClass::OverrideView( view );
	if ( engine->IsLevelMainMenuBackground() )
	{
		view->angles.Init( mm_menu_camera_pitch.GetFloat(), mm_menu_camera_yaw.GetFloat(), 0 );
		if ( ConVarRef( "mm_menu_motion" ).GetBool() ) view->angles.y += sinf( float( Plat_FloatTime() * 0.08 ) ) * 0.8f;
		view->fov = 75;
	}
}

bool ClientModeHL2MPNormal::ShouldDrawViewModel()
{
	return !engine->IsLevelMainMenuBackground() && BaseClass::ShouldDrawViewModel();
}

bool ClientModeHL2MPNormal::ShouldDrawCrosshair()
{
	return !engine->IsLevelMainMenuBackground() && BaseClass::ShouldDrawCrosshair();
}
//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
vgui::HScheme g_hVGuiCombineScheme = 0;
ConVar fov_desired( "fov_desired", "90", FCVAR_ARCHIVE | FCVAR_USERINFO, "Sets the base field-of-view.", true, 70.0, true, 110.0 );

ConVar cl_spectator_team_outlines( "cl_spectator_team_outlines", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Show team-colored player outlines while spectating or watching SourceTV." );


// Instance the singleton and expose the interface to it.
IClientMode *GetClientModeNormal()
{
	static ClientModeHL2MPNormal g_ClientModeNormal;
	return &g_ClientModeNormal;
}

ClientModeHL2MPNormal* GetClientModeHL2MPNormal()
{
	Assert( dynamic_cast< ClientModeHL2MPNormal* >( GetClientModeNormal() ) );

	return static_cast< ClientModeHL2MPNormal* >( GetClientModeNormal() );
}

//-----------------------------------------------------------------------------
// Purpose: this is the viewport that contains all the hud elements
//-----------------------------------------------------------------------------
class CHudViewport : public CBaseViewport
{
private:
	DECLARE_CLASS_SIMPLE( CHudViewport, CBaseViewport );

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		gHUD.InitColors( pScheme );

		SetPaintBackgroundEnabled( false );
	}

	virtual IViewPortPanel *CreatePanelByName( const char *szPanelName );
};

int ClientModeHL2MPNormal::GetDeathMessageStartHeight( void )
{
	return m_pViewport->GetDeathMessageStartHeight();
}

IViewPortPanel* CHudViewport::CreatePanelByName( const char *szPanelName )
{
	IViewPortPanel* newpanel = NULL;

	if ( Q_strcmp( PANEL_SCOREBOARD, szPanelName) == 0 )
	{
		newpanel = new CHL2MPClientScoreBoardDialog( this );
		return newpanel;
	}
	else if ( Q_strcmp(PANEL_INFO, szPanelName) == 0 )
	{
		newpanel = new CHL2MPTextWindow( this );
		return newpanel;
	}
	else if ( Q_strcmp(PANEL_SPECGUI, szPanelName) == 0 )
	{
		newpanel = new CHL2MPSpectatorGUI( this );	
		return newpanel;
	}

	
	return BaseClass::CreatePanelByName( szPanelName ); 
}

//-----------------------------------------------------------------------------
// ClientModeHLNormal implementation
//-----------------------------------------------------------------------------
ClientModeHL2MPNormal::ClientModeHL2MPNormal()
{
	m_pViewport = new CHudViewport();
	m_pViewport->Start( gameuifuncs, gameeventmanager );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeHL2MPNormal::~ClientModeHL2MPNormal()
{
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeHL2MPNormal::Init()
{
	HL2MPMessages_Init();
	BaseClass::Init();
	// Base HUD initialization can load stock English files; restore our selected
	// language before creating controls or resolving weapon/chat labels.
	HL2MPMessages_Init();
	HL2MPMainMenu_Create();
	HL2MPCommunity_Init();
	HL2MPIntegrity_Init();
	HL2MPHudCustomization_Init();
	HL2MPVoteUI_Init();

	// Load up the combine control panel scheme
	g_hVGuiCombineScheme = vgui::scheme()->LoadSchemeFromFileEx( enginevgui->GetPanel( PANEL_CLIENTDLL ), "resource/CombinePanelScheme.res", "CombineScheme" );
	if (!g_hVGuiCombineScheme)
	{
		Warning( "Couldn't load combine panel scheme!\n" );
	}
}

void ClientModeHL2MPNormal::LevelInit( const char *map )
{
	HL2MPVoteUI_Reset();
	HL2MPLoading_LevelInit( map );
	BaseClass::LevelInit( map );
}

int ClientModeHL2MPNormal::KeyInput( int down, ButtonCode_t keynum, const char *binding )
{
	if ( HL2MPScoreboardMouseInput( down, keynum ) ) return 0;
	return BaseClass::KeyInput( down, keynum, binding );
}

void ClientModeHL2MPNormal::Shutdown()
{
	HL2MPMainMenu_Destroy();
	BaseClass::Shutdown();
}

bool ClientModeHL2MPNormal::DoPostScreenSpaceEffects( const CViewSetup *pSetup )
{
	if ( !BaseClass::DoPostScreenSpaceEffects( pSetup ) )
		return false;

	C_HL2MP_Player *pLocalPlayer = C_HL2MP_Player::GetLocalHL2MPPlayer();
	bool bViewerCanSeeOutlines = cl_spectator_team_outlines.GetBool() &&
		( engine->IsHLTV() || ( pLocalPlayer && pLocalPlayer->GetTeamNumber() == TEAM_SPECTATOR ) );

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		C_HL2MP_Player *pPlayer = ToHL2MPPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer )
		{
			pPlayer->UpdateSpectatorGlow( bViewerCanSeeOutlines );
		}
	}

	if ( bViewerCanSeeOutlines )
	{
		g_GlowObjectManager.RenderGlowEffects( pSetup, 0 );
	}

	return true;
}

