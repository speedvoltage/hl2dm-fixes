//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "hl2mp_hud_chat.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "text_message.h"
#include "vguicenterprint.h"
#include "vgui/Cursor.h"
#include "vgui/IInput.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/RichText.h"
#include "vgui_controls/TextEntry.h"
#include "c_team.h"
#include "c_playerresource.h"
#include "c_hl2mp_player.h"
#include "hl2mp_gamerules.h"
#include "ihudlcd.h"
#include <time.h>

DECLARE_HUDELEMENT( CHudChat );

DECLARE_HUD_MESSAGE( CHudChat, SayText );
DECLARE_HUD_MESSAGE( CHudChat, SayText2 );
DECLARE_HUD_MESSAGE( CHudChat, TextMsg );

ConVar cl_chat_timestamps( "cl_chat_timestamps", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Show local timestamps in text chat." );
ConVar cl_chatbox_style( "cl_chatbox_style", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Chat presentation style: 0 = classic, 1 = modern.", true, 0, true, 1 );
ConVar cl_chat_window_x( "cl_chat_window_x", "-1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Normalized horizontal position of the chat window." );
ConVar cl_chat_window_y( "cl_chat_window_y", "-1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Normalized vertical position of the chat window." );
ConVar cl_chat_window_w( "cl_chat_window_w", "-1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Normalized width of the chat window." );
ConVar cl_chat_window_h( "cl_chat_window_h", "-1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Normalized height of the chat window." );

//=====================
//CHudChatLine
//=====================

void CHudChatLine::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings( pScheme );
}

//=====================
//CHudChatInputLine
//=====================

void CHudChatInputLine::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
}

//=====================
//CHudChat
//=====================

CHudChat::CHudChat( const char *pElementName ) : BaseClass( pElementName )
{
	m_iLastScreenWide = 0;
	m_iLastScreenTall = 0;
	m_bLastActive = false;
	m_bWindowBoundsInitialized = false;
	m_iWindowDragMode = WINDOW_DRAG_NONE;
	m_iDragStartCursorX = 0;
	m_iDragStartCursorY = 0;
	m_iDragStartX = 0;
	m_iDragStartY = 0;
	m_iDragStartWide = 0;
	m_iDragStartTall = 0;
	m_iWindowMargin = 12;
	m_iMinWindowWide = 440;
	m_iMinWindowTall = 260;
	m_iHeaderTall = 26;
	m_iResizeGripSize = 7;
	m_iLastStyle = cl_chatbox_style.GetInt();

	m_clrPanel = Color( 18, 19, 18, 238 );
	m_clrPanelBorder = Color( 92, 88, 78, 190 );
	m_clrAccent = Color( 232, 112, 32, 255 );
	m_clrText = Color( 232, 232, 226, 255 );
	m_clrSecondaryText = Color( 168, 171, 165, 255 );

	m_pTitleLabel = new vgui::Label( this, "ChatWindowTitle", "CHAT" );
	m_pTitleLabel->SetMouseInputEnabled( false );
	m_pTitleLabel->SetKeyBoardInputEnabled( false );
	m_pTitleLabel->SetVisible( false );

	m_pFilterTitle = new vgui::Label( m_pFilterPanel, "ChatFilterTitle", "CHAT FILTERS" );
	m_pFilterTitle->SetMouseInputEnabled( false );
	m_pFilterTitle->SetKeyBoardInputEnabled( false );

	m_pFilterAccent = new vgui::Panel( m_pFilterPanel, "ChatFilterAccent" );
	m_pFilterAccent->SetMouseInputEnabled( false );
	m_pFilterAccent->SetKeyBoardInputEnabled( false );
}

void CHudChat::CreateChatInputLine( void )
{
	m_pChatInput = new CHudChatInputLine( this, "ChatInputLine" );
	m_pChatInput->SetVisible( false );
}

void CHudChat::CreateChatLines( void )
{
	m_ChatLine = new CHudChatLine( this, "ChatLine1" );
	m_ChatLine->SetVisible( false );	
}

void CHudChat::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	m_bWindowBoundsInitialized = false;

	if ( cl_chatbox_style.GetBool() )
	{
		SetPaintBackgroundType( 0 );
		SetPaintBackgroundEnabled( true );
		SetPaintBorderEnabled( true );
		SetBorder( NULL );
		SetBgColor( Color( 0, 0, 0, 0 ) );
		ApplyModernStyle();
	}
	else
	{
		if ( m_ChatLine )
		{
			m_ChatLine->SetPaintBackgroundEnabled( true );
			m_ChatLine->SetPaintBorderEnabled( true );
		}
		if ( m_pChatInput )
		{
			m_pChatInput->SetPaintBackgroundEnabled( true );
			m_pChatInput->SetPaintBorderEnabled( true );
		}
		if ( GetChatHistory() )
		{
			GetChatHistory()->SetPaintBackgroundEnabled( true );
			GetChatHistory()->SetPaintBorderEnabled( true );
		}
		m_pTitleLabel->SetVisible( false );
		m_pFilterTitle->SetVisible( false );
		m_pFilterAccent->SetVisible( false );
		if ( m_pFiltersButton )
		{
			m_pFiltersButton->SetText( "Filters" );
			m_pFiltersButton->SetPaintBackgroundEnabled( true );
			m_pFiltersButton->SetPaintBorderEnabled( true );
		}
		if ( m_pFilterPanel )
		{
			m_pFilterPanel->SetPaintBackgroundType( 2 );
			m_pFilterPanel->SetPaintBackgroundEnabled( true );
			m_pFilterPanel->SetPaintBorderEnabled( true );
		}
	}
	UpdateLayout();
}

void CHudChat::PerformLayout( void )
{
	BaseClass::PerformLayout();
	UpdateLayout();
}

void CHudChat::PaintBackground( void )
{
	if ( !cl_chatbox_style.GetBool() )
	{
		BaseClass::PaintBackground();
		return;
	}

	if ( GetMessageMode() == MM_NONE )
		return;

	int wide, tall;
	GetSize( wide, tall );

	vgui::surface()->DrawSetColor( m_clrPanel );
	vgui::surface()->DrawFilledRect( 0, 0, wide, tall );
	vgui::surface()->DrawSetColor( Color( 24, 25, 23, 245 ) );
	vgui::surface()->DrawFilledRect( 1, 2, wide - 1, m_iHeaderTall );

	vgui::surface()->DrawSetColor( m_clrAccent );
	vgui::surface()->DrawFilledRect( 0, 0, wide, 2 );

	vgui::surface()->DrawSetColor( m_clrPanelBorder );
	vgui::surface()->DrawFilledRect( 1, m_iHeaderTall, wide - 1, m_iHeaderTall + 1 );
}

void CHudChat::PaintBorder( void )
{
	if ( !cl_chatbox_style.GetBool() )
	{
		BaseClass::PaintBorder();
		return;
	}

	if ( GetMessageMode() == MM_NONE )
		return;

	int wide, tall;
	GetSize( wide, tall );

	vgui::surface()->DrawSetColor( m_clrPanelBorder );
	vgui::surface()->DrawOutlinedRect( 0, 0, wide, tall );
	vgui::surface()->DrawLine( wide - 14, tall - 3, wide - 3, tall - 14 );
	vgui::surface()->DrawLine( wide - 9, tall - 3, wide - 3, tall - 9 );
}

void CHudChat::Init( void )
{
	BaseClass::Init();

	HOOK_HUD_MESSAGE( CHudChat, SayText );
	HOOK_HUD_MESSAGE( CHudChat, SayText2 );
	HOOK_HUD_MESSAGE( CHudChat, TextMsg );
}

//-----------------------------------------------------------------------------
// Purpose: Overrides base reset to not cancel chat at round restart
//-----------------------------------------------------------------------------
void CHudChat::Reset( void )
{
}

void CHudChat::OnTick( void )
{
	BaseClass::OnTick();

	const int style = cl_chatbox_style.GetInt();
	if ( style != m_iLastStyle )
	{
		FinishWindowDrag( true );
		m_iLastStyle = style;
		InvalidateLayout( true, true );
		UpdateMessageModePrompt();
		UpdateLayout();
	}

	int screenWide, screenTall;
	vgui::surface()->GetScreenSize( screenWide, screenTall );

	const bool active = GetMessageMode() != MM_NONE;
	if ( screenWide != m_iLastScreenWide || screenTall != m_iLastScreenTall || active != m_bLastActive )
	{
		if ( screenWide != m_iLastScreenWide || screenTall != m_iLastScreenTall )
		{
			m_bWindowBoundsInitialized = false;
		}

		m_iLastScreenWide = screenWide;
		m_iLastScreenTall = screenTall;
		m_bLastActive = active;
		UpdateLayout();
	}

	if ( m_pFiltersButton )
	{
		m_pFiltersButton->SetVisible( active );
	}
}

void CHudChat::StartMessageMode( int iMessageModeType )
{
	BaseClass::StartMessageMode( iMessageModeType );
	UpdateMessageModePrompt();
	if ( cl_chatbox_style.GetBool() )
	{
		ApplyModernStyle();
	}
	UpdateLayout();

	if ( GetChatHistory() )
	{
		int cursorX, cursorY;
		GetChatHistory()->GetSize( cursorX, cursorY );
		cursorX /= 2;
		cursorY /= 2;
		GetChatHistory()->LocalToScreen( cursorX, cursorY );
		vgui::input()->SetCursorPos( cursorX, cursorY );
	}
}

void CHudChat::StopMessageMode( void )
{
	FinishWindowDrag( true );
	BaseClass::StopMessageMode();

	if ( m_pChatInput )
	{
		m_pChatInput->SetVisible( false );
	}

	if ( m_pFiltersButton )
	{
		m_pFiltersButton->SetVisible( false );
	}

	UpdateLayout();
}

void CHudChat::OnCursorMoved( int x, int y )
{
	if ( m_iWindowDragMode != WINDOW_DRAG_NONE )
	{
		if ( !vgui::input()->IsMouseDown( MOUSE_LEFT ) )
		{
			FinishWindowDrag( true );
			return;
		}

		int cursorX, cursorY;
		vgui::input()->GetCursorPos( cursorX, cursorY );
		const int deltaX = cursorX - m_iDragStartCursorX;
		const int deltaY = cursorY - m_iDragStartCursorY;
		int screenWide, screenTall;
		vgui::surface()->GetScreenSize( screenWide, screenTall );

		int left = m_iDragStartX;
		int top = m_iDragStartY;
		int right = m_iDragStartX + m_iDragStartWide;
		int bottom = m_iDragStartY + m_iDragStartTall;

		if ( m_iWindowDragMode == WINDOW_DRAG_MOVE )
		{
			left = clamp( m_iDragStartX + deltaX, m_iWindowMargin, screenWide - m_iWindowMargin - m_iDragStartWide );
			top = clamp( m_iDragStartY + deltaY, m_iWindowMargin, screenTall - m_iWindowMargin - m_iDragStartTall );
			right = left + m_iDragStartWide;
			bottom = top + m_iDragStartTall;
		}
		else
		{
			if ( m_iWindowDragMode & WINDOW_DRAG_LEFT )
				left = clamp( m_iDragStartX + deltaX, m_iWindowMargin, right - m_iMinWindowWide );
			if ( m_iWindowDragMode & WINDOW_DRAG_RIGHT )
				right = clamp( m_iDragStartX + m_iDragStartWide + deltaX, left + m_iMinWindowWide, screenWide - m_iWindowMargin );
			if ( m_iWindowDragMode & WINDOW_DRAG_TOP )
				top = clamp( m_iDragStartY + deltaY, m_iWindowMargin, bottom - m_iMinWindowTall );
			if ( m_iWindowDragMode & WINDOW_DRAG_BOTTOM )
				bottom = clamp( m_iDragStartY + m_iDragStartTall + deltaY, top + m_iMinWindowTall, screenTall - m_iWindowMargin );
		}

		SetBounds( left, top, right - left, bottom - top );
		UpdateLayout();
		Repaint();
		return;
	}

	int cursorX, cursorY;
	vgui::input()->GetCursorPos( cursorX, cursorY );
	ScreenToLocal( cursorX, cursorY );
	SetWindowCursor( GetWindowDragMode( cursorX, cursorY ) );
	BaseClass::OnCursorMoved( x, y );
}

void CHudChat::OnMousePressed( vgui::MouseCode code )
{
	if ( code != MOUSE_LEFT || GetMessageMode() == MM_NONE )
	{
		BaseClass::OnMousePressed( code );
		return;
	}

	int cursorX, cursorY;
	vgui::input()->GetCursorPos( cursorX, cursorY );
	m_iDragStartCursorX = cursorX;
	m_iDragStartCursorY = cursorY;
	ScreenToLocal( cursorX, cursorY );
	m_iWindowDragMode = GetWindowDragMode( cursorX, cursorY );

	if ( m_iWindowDragMode == WINDOW_DRAG_NONE )
	{
		BaseClass::OnMousePressed( code );
		return;
	}

	GetBounds( m_iDragStartX, m_iDragStartY, m_iDragStartWide, m_iDragStartTall );
	vgui::input()->SetMouseCapture( GetVPanel() );
	SetWindowCursor( m_iWindowDragMode );
}

void CHudChat::OnMouseReleased( vgui::MouseCode code )
{
	if ( code == MOUSE_LEFT && m_iWindowDragMode != WINDOW_DRAG_NONE )
	{
		FinishWindowDrag( true );
		return;
	}

	BaseClass::OnMouseReleased( code );
}

void CHudChat::OnMouseCaptureLost( void )
{
	FinishWindowDrag( false );
	BaseClass::OnMouseCaptureLost();
}

void CHudChat::ChatPrintf( int iPlayerIndex, int iFilter, const char *fmt, ... )
{
	va_list marker;
	char msg[4096];

	va_start( marker, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, marker );
	va_end( marker );

	char output[4096];
	output[0] = 0;
	const bool useFilterColor = ( iFilter & CHAT_FILTER_SERVERMSG ) != 0;

	if ( useFilterColor )
	{
		SetCustomColor( cl_chatbox_style.GetBool() ? m_clrAccent : g_ColorYellow );
	}

	if ( cl_chat_timestamps.GetBool() )
	{
		time_t now = time( NULL );
		struct tm localTime;
		bool validTime = false;

#ifdef _WIN32
		validTime = localtime_s( &localTime, &now ) == 0;
#else
		validTime = localtime_r( &now, &localTime ) != NULL;
#endif

		if ( validTime )
		{
			Q_snprintf( output, sizeof( output ), "%c[%02d:%02d] %c%s", COLOR_NORMAL, localTime.tm_hour, localTime.tm_min, useFilterColor ? COLOR_CUSTOM : COLOR_NORMAL, msg );
		}
	}

	if ( !output[0] )
	{
		if ( useFilterColor )
		{
			Q_snprintf( output, sizeof( output ), "%c%s", COLOR_CUSTOM, msg );
		}
		else
		{
			Q_strncpy( output, msg, sizeof( output ) );
		}
	}

	BaseClass::ChatPrintf( iPlayerIndex, iFilter, "%s", output );
}

int CHudChat::GetChatInputOffset( void )
{
	if ( m_pChatInput->IsVisible() )
	{
		return m_iFontHeight;
	}

		return 0;
}

Color CHudChat::GetDefaultTextColor( void )
{
	return cl_chatbox_style.GetBool() ? m_clrText : BaseClass::GetDefaultTextColor();
}

Color CHudChat::GetClientColor( int clientIndex )
{
	if ( clientIndex == 0 ) // console msg
	{
		return cl_chatbox_style.GetBool() ? m_clrAccent : g_ColorYellow;
	}
	else if( g_PR )
	{
		switch ( g_PR->GetTeam( clientIndex ) )
		{
		case TEAM_COMBINE:
			return g_ColorBlue;
		case TEAM_REBELS:
			return g_ColorRed;
		default:
			return cl_chatbox_style.GetBool() ? Color( 224, 154, 82, 255 ) : g_ColorYellow;
		}
	}

	return cl_chatbox_style.GetBool() ? m_clrAccent : g_ColorYellow;
}
void CHudChat::UpdateLayout( void )
{
	if ( !m_pChatInput || !GetChatHistory() )
		return;

	int screenWide, screenTall;
	vgui::surface()->GetScreenSize( screenWide, screenTall );

	const float scale = MAX( 0.65f, screenTall / 1080.0f );
	const int pad = MAX( 8, (int)( 10 * scale ) );
	const int gap = MAX( 5, (int)( 6 * scale ) );
	const int inputTall = MAX( 26, (int)( 32 * scale ) );
	const int filterWide = MAX( 68, (int)( 82 * scale ) );
	const int filterTall = MAX( 22, (int)( 26 * scale ) );
	const int filterPad = MAX( 8, (int)( 10 * scale ) );
	const int filterTitleTall = MAX( 24, (int)( 28 * scale ) );
	const int filterRowGap = MAX( 1, (int)( 2 * scale ) );
	const int minFilterRowTall = MAX( 18, (int)( 20 * scale ) );
	const bool modern = cl_chatbox_style.GetBool();
	const int filterContentTop = modern ? filterTitleTall : filterPad;
	const int minFilterPanelTall = filterContentTop + filterPad + filterRowGap * 5 + minFilterRowTall * 6;
	m_iWindowMargin = MAX( 12, (int)( 22 * scale ) );
	m_iMinWindowWide = MIN( screenWide - m_iWindowMargin * 2, (int)( 440 * scale ) );
	m_iHeaderTall = MAX( 22, (int)( 26 * scale ) );
	m_iResizeGripSize = MAX( 6, (int)( 7 * scale ) );
	m_iMinWindowTall = MIN( screenTall - m_iWindowMargin * 2, MAX( (int)( 260 * scale ), m_iHeaderTall + pad + inputTall + gap * 2 + minFilterPanelTall ) );

	if ( !m_bWindowBoundsInitialized )
	{
		RestoreWindowBounds( screenWide, screenTall, scale );
	}

	int panelWide, panelTall;
	GetSize( panelWide, panelTall );

	const bool active = GetMessageMode() != MM_NONE;
	m_pTitleLabel->SetVisible( active && modern );
	m_pTitleLabel->SetBounds( pad, 2, panelWide - pad * 2, m_iHeaderTall - 2 );
	if ( active )
	{
		const int historyY = m_iHeaderTall + gap;
		const int historyTall = panelTall - historyY - pad - inputTall - gap;
		GetChatHistory()->SetBounds( pad, historyY, panelWide - pad * 2, MAX( 1, historyTall ) );
		m_pChatInput->SetBounds( pad, panelTall - pad - inputTall, panelWide - pad * 2 - filterWide - gap, inputTall );

		if ( m_pFiltersButton )
		{
			m_pFiltersButton->SetBounds( panelWide - pad - filterWide, panelTall - pad - filterTall, filterWide, filterTall );
		}

		if ( m_pFilterPanel )
		{
			const int filterPanelWide = MIN( panelWide - pad * 2, MAX( 210, (int)( 230 * scale ) ) );
			const int filterPanelTall = MIN( panelTall - m_iHeaderTall - pad - inputTall - gap * 2, MAX( minFilterPanelTall, (int)( 190 * scale ) ) );
			const int filterRowTall = MAX( minFilterRowTall, ( filterPanelTall - filterContentTop - filterPad - filterRowGap * 5 ) / 6 );
			m_pFilterPanel->SetBounds( panelWide - pad - filterPanelWide, panelTall - pad - inputTall - gap - filterPanelTall, filterPanelWide, filterPanelTall );
			m_pFilterPanel->SetZPos( 100 );

			m_pFilterAccent->SetVisible( modern );
			m_pFilterTitle->SetVisible( modern );
			if ( modern )
			{
				m_pFilterAccent->SetBounds( 0, 0, filterPanelWide, 2 );
				m_pFilterTitle->SetBounds( filterPad, 2, filterPanelWide - filterPad * 2, filterTitleTall - 2 );
			}

			int filterRow = 0;
			for ( int i = 0; i < m_pFilterPanel->GetChildCount(); ++i )
			{
				vgui::CheckButton *button = dynamic_cast< vgui::CheckButton * >( m_pFilterPanel->GetChild( i ) );
				if ( !button )
					continue;

				button->SetBounds( filterPad, filterContentTop + filterRow * ( filterRowTall + filterRowGap ), filterPanelWide - filterPad * 2, filterRowTall );
				++filterRow;
			}
		}
	}
	else
	{
		GetChatHistory()->SetBounds( 0, 0, panelWide, panelTall );
	}
}

void CHudChat::RestoreWindowBounds( int screenWide, int screenTall, float scale )
{
	const int availableWide = MAX( 1, screenWide - m_iWindowMargin * 2 );
	const int availableTall = MAX( 1, screenTall - m_iWindowMargin * 2 );
	const int defaultWide = clamp( (int)( screenWide * 0.36f ), m_iMinWindowWide, MIN( availableWide, (int)( 680 * scale ) ) );
	const int defaultTall = clamp( (int)( screenTall * 0.34f ), m_iMinWindowTall, MIN( availableTall, (int)( 380 * scale ) ) );
	const bool hasSavedBounds = cl_chat_window_x.GetFloat() >= 0.0f && cl_chat_window_y.GetFloat() >= 0.0f && cl_chat_window_w.GetFloat() > 0.0f && cl_chat_window_h.GetFloat() > 0.0f;

	int wide = defaultWide;
	int tall = defaultTall;
	int x = m_iWindowMargin;
	int y = MAX( m_iWindowMargin, screenTall - tall - MAX( 82, (int)( 118 * scale ) ) );

	if ( hasSavedBounds )
	{
		wide = clamp( (int)( screenWide * cl_chat_window_w.GetFloat() + 0.5f ), m_iMinWindowWide, availableWide );
		tall = clamp( (int)( screenTall * cl_chat_window_h.GetFloat() + 0.5f ), m_iMinWindowTall, availableTall );
		x = m_iWindowMargin + (int)( ( availableWide - wide ) * clamp( cl_chat_window_x.GetFloat(), 0.0f, 1.0f ) + 0.5f );
		y = m_iWindowMargin + (int)( ( availableTall - tall ) * clamp( cl_chat_window_y.GetFloat(), 0.0f, 1.0f ) + 0.5f );
	}

	SetBounds( x, y, wide, tall );
	m_bWindowBoundsInitialized = true;
}

void CHudChat::SaveWindowBounds( void )
{
	int screenWide, screenTall;
	vgui::surface()->GetScreenSize( screenWide, screenTall );
	int x, y, wide, tall;
	GetBounds( x, y, wide, tall );
	const int availableWide = MAX( 1, screenWide - m_iWindowMargin * 2 );
	const int availableTall = MAX( 1, screenTall - m_iWindowMargin * 2 );
	const int moveWide = MAX( 1, availableWide - wide );
	const int moveTall = MAX( 1, availableTall - tall );

	cl_chat_window_x.SetValue( clamp( (float)( x - m_iWindowMargin ) / moveWide, 0.0f, 1.0f ) );
	cl_chat_window_y.SetValue( clamp( (float)( y - m_iWindowMargin ) / moveTall, 0.0f, 1.0f ) );
	cl_chat_window_w.SetValue( clamp( (float)wide / screenWide, 0.0f, 1.0f ) );
	cl_chat_window_h.SetValue( clamp( (float)tall / screenTall, 0.0f, 1.0f ) );
}

int CHudChat::GetWindowDragMode( int x, int y )
{
	int wide, tall;
	GetSize( wide, tall );
	int dragMode = WINDOW_DRAG_NONE;

	if ( x < m_iResizeGripSize )
		dragMode |= WINDOW_DRAG_LEFT;
	else if ( x >= wide - m_iResizeGripSize )
		dragMode |= WINDOW_DRAG_RIGHT;

	if ( y < m_iResizeGripSize )
		dragMode |= WINDOW_DRAG_TOP;
	else if ( y >= tall - m_iResizeGripSize )
		dragMode |= WINDOW_DRAG_BOTTOM;

	if ( dragMode == WINDOW_DRAG_NONE && y < m_iHeaderTall )
		dragMode = WINDOW_DRAG_MOVE;

	return dragMode;
}

void CHudChat::SetWindowCursor( int dragMode )
{
	vgui::HCursor cursor = vgui::dc_arrow;
	if ( dragMode == ( WINDOW_DRAG_LEFT | WINDOW_DRAG_TOP ) || dragMode == ( WINDOW_DRAG_RIGHT | WINDOW_DRAG_BOTTOM ) )
		cursor = vgui::dc_sizenwse;
	else if ( dragMode == ( WINDOW_DRAG_RIGHT | WINDOW_DRAG_TOP ) || dragMode == ( WINDOW_DRAG_LEFT | WINDOW_DRAG_BOTTOM ) )
		cursor = vgui::dc_sizenesw;
	else if ( dragMode & ( WINDOW_DRAG_LEFT | WINDOW_DRAG_RIGHT ) )
		cursor = vgui::dc_sizewe;
	else if ( dragMode & ( WINDOW_DRAG_TOP | WINDOW_DRAG_BOTTOM ) )
		cursor = vgui::dc_sizens;
	else if ( dragMode == WINDOW_DRAG_MOVE )
		cursor = vgui::dc_sizeall;

	SetCursor( cursor );
}

void CHudChat::FinishWindowDrag( bool releaseCapture )
{
	if ( m_iWindowDragMode == WINDOW_DRAG_NONE )
		return;

	m_iWindowDragMode = WINDOW_DRAG_NONE;
	if ( releaseCapture )
		vgui::input()->SetMouseCapture( NULL );
	SaveWindowBounds();
	SetWindowCursor( WINDOW_DRAG_NONE );

	if ( m_pChatInput && GetMessageMode() != MM_NONE )
		m_pChatInput->RequestFocus();
}

void CHudChat::UpdateMessageModePrompt( void )
{
	if ( !m_pChatInput )
		return;

	if ( !cl_chatbox_style.GetBool() )
	{
		const wchar_t *prompt = NULL;
		switch ( GetMessageMode() )
		{
		case MM_SAY:
			prompt = g_pVGuiLocalize->Find( "#chat_say" );
			m_pChatInput->SetPrompt( prompt ? prompt : L"Say :" );
			break;
		case MM_SAY_TEAM:
			prompt = g_pVGuiLocalize->Find( "#chat_say_team" );
			m_pChatInput->SetPrompt( prompt ? prompt : L"Say (TEAM) :" );
			break;
		case MM_SAY_PARTY:
			prompt = g_pVGuiLocalize->Find( "#chat_say_party" );
			m_pChatInput->SetPrompt( prompt ? prompt : L"Say (PARTY) :" );
			break;
		}
		return;
	}

	switch ( GetMessageMode() )
	{
	case MM_SAY_TEAM:
		m_pChatInput->SetPrompt( L"TEAM" );
		break;
	case MM_SAY_PARTY:
		m_pChatInput->SetPrompt( L"PARTY" );
		break;
	default:
		m_pChatInput->SetPrompt( L"ALL" );
		break;
	}
}

void CHudChat::ApplyModernStyle( void )
{
	if ( m_ChatLine )
	{
		m_ChatLine->SetPaintBackgroundEnabled( false );
		m_ChatLine->SetPaintBorderEnabled( false );
		m_ChatLine->SetBorder( NULL );
		m_ChatLine->SetFgColor( m_clrText );
	}

	if ( m_pChatInput )
	{
		m_pChatInput->SetPaintBackgroundEnabled( false );
		m_pChatInput->SetPaintBorderEnabled( false );
		m_pChatInput->SetBorder( NULL );

		vgui::Label *prompt = m_pChatInput->GetPrompt();
		prompt->SetPaintBackgroundEnabled( false );
		prompt->SetFgColor( m_clrAccent );
		prompt->SetContentAlignment( vgui::Label::a_center );
		prompt->SetTextInset( 0, 0 );

		vgui::Panel *input = m_pChatInput->GetInputPanel();
		input->SetPaintBackgroundEnabled( true );
		input->SetPaintBorderEnabled( false );
		input->SetBorder( NULL );
		input->SetBgColor( Color( 8, 9, 8, 215 ) );
		input->SetFgColor( Color( 238, 238, 232, 255 ) );
	}

	if ( GetChatHistory() )
	{
		GetChatHistory()->SetPaintBackgroundEnabled( false );
		GetChatHistory()->SetPaintBorderEnabled( false );
		GetChatHistory()->SetBorder( NULL );
		GetChatHistory()->SetFgColor( m_clrText );
	}

	if ( m_pFiltersButton )
	{
		m_pFiltersButton->SetText( "FILTERS" );
		m_pFiltersButton->SetPaintBackgroundEnabled( true );
		m_pFiltersButton->SetPaintBorderEnabled( false );
		m_pFiltersButton->SetBorder( NULL );
		m_pFiltersButton->SetDefaultColor( m_clrSecondaryText, Color( 35, 36, 34, 235 ) );
		m_pFiltersButton->SetArmedColor( m_clrText, Color( 49, 50, 47, 245 ) );
		m_pFiltersButton->SetDepressedColor( m_clrAccent, Color( 28, 29, 27, 250 ) );
		m_pFiltersButton->SetVisible( GetMessageMode() != MM_NONE );
	}

	m_pTitleLabel->SetPaintBackgroundEnabled( false );
	m_pTitleLabel->SetPaintBorderEnabled( false );
	m_pTitleLabel->SetBorder( NULL );
	m_pTitleLabel->SetFgColor( m_clrSecondaryText );
	m_pTitleLabel->SetContentAlignment( vgui::Label::a_west );
	m_pTitleLabel->SetTextInset( 0, 0 );
	if ( m_pFiltersButton )
		m_pTitleLabel->SetFont( m_pFiltersButton->GetFont() );

	if ( m_pFilterPanel )
	{
		m_pFilterPanel->SetPaintBackgroundType( 0 );
		m_pFilterPanel->SetPaintBackgroundEnabled( true );
		m_pFilterPanel->SetPaintBorderEnabled( false );
		m_pFilterPanel->SetBorder( NULL );
		m_pFilterPanel->SetBgColor( Color( 20, 21, 20, 252 ) );

		m_pFilterAccent->SetPaintBackgroundEnabled( true );
		m_pFilterAccent->SetBgColor( m_clrAccent );
		m_pFilterTitle->SetPaintBackgroundEnabled( false );
		m_pFilterTitle->SetPaintBorderEnabled( false );
		m_pFilterTitle->SetBorder( NULL );
		m_pFilterTitle->SetFgColor( m_clrSecondaryText );
		m_pFilterTitle->SetContentAlignment( vgui::Label::a_west );
		m_pFilterTitle->SetTextInset( 0, 0 );
		if ( m_pFiltersButton )
			m_pFilterTitle->SetFont( m_pFiltersButton->GetFont() );

		for ( int i = 0; i < m_pFilterPanel->GetChildCount(); ++i )
		{
			vgui::CheckButton *button = dynamic_cast< vgui::CheckButton * >( m_pFilterPanel->GetChild( i ) );
			if ( !button )
				continue;

			button->SetPaintBackgroundEnabled( true );
			button->SetPaintBorderEnabled( false );
			button->SetBorder( NULL );
			button->SetDefaultColor( m_clrText, Color( 29, 30, 28, 245 ) );
			button->SetArmedColor( m_clrText, Color( 43, 44, 41, 250 ) );
			button->SetDepressedColor( m_clrAccent, Color( 36, 37, 34, 252 ) );
			button->SetSelectedColor( m_clrText, Color( 32, 33, 30, 248 ) );
			button->GetCheckImage()->SetColor( m_clrAccent );
			button->GetCheckImage()->_bgColor = Color( 12, 13, 12, 255 );
		}
	}
}
