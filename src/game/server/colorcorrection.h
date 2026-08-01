//========= Copyright Valve Corporation, All rights reserved. ============//  
//  
// Purpose: Color correction entity.  
//  
// $NoKeywords: $  
//===========================================================================//  

#ifndef COLORCORRECTION_H  
#define COLORCORRECTION_H  

#include "cbase.h"  

#define COLOR_CORRECTION_ENT_THINK_RATE TICK_INTERVAL  

class CColorCorrection : public CBaseEntity
{
    DECLARE_CLASS( CColorCorrection, CBaseEntity );
public:
    DECLARE_SERVERCLASS();
    DECLARE_DATADESC();

    CColorCorrection();

    void Spawn( void );
    int  UpdateTransmitState();
    void Activate( void );

    virtual int ObjectCaps( void ) { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

    // Inputs  
    void InputEnable( inputdata_t &inputdata );
    void InputDisable( inputdata_t &inputdata );
    void InputSetFadeInDuration( inputdata_t &inputdata );
    void InputSetFadeOutDuration( inputdata_t &inputdata );
    void SetMaxWeight( float weight ) { m_flMaxWeight = weight; }
    float GetMaxWeight() const { return m_flMaxWeight; }
    void SetEnabled( bool e ) { m_bEnabled = e; }
    bool IsEnabled() const { return m_bEnabled; }

private:
    void FadeIn( void );
    void FadeOut( void );

    void FadeInThink( void );    // Fades lookup weight from Cur->MaxWeight   
    void FadeOutThink( void );   // Fades lookup weight from CurWeight->0.0  

    float m_flFadeInDuration;        // Duration for a full 0->MaxWeight transition  
    float m_flFadeOutDuration;       // Duration for a full Max->0 transition  
    float m_flStartFadeInWeight;
    float m_flStartFadeOutWeight;
    float m_flTimeStartFadeIn;
    float m_flTimeStartFadeOut;

    float m_flMaxWeight;

    bool m_bStartDisabled;
    CNetworkVar( bool, m_bEnabled );

    CNetworkVar( float, m_MinFalloff );
    CNetworkVar( float, m_MaxFalloff );
    CNetworkVar( float, m_flCurWeight );
    CNetworkString( m_netlookupFilename, MAX_PATH );

    string_t m_lookupFilename;
};

#endif // COLORCORRECTION_H