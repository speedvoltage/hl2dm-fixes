#ifndef HL2MP_FOOTSTEPS_H
#define HL2MP_FOOTSTEPS_H

enum HL2MPFootstepStyle { FOOTSTEPS_MODEL, FOOTSTEPS_COMBINE, FOOTSTEPS_POLICE, FOOTSTEPS_REBEL, FOOTSTEPS_MATERIAL };
inline int HL2MPFootstepStyleValue( int value ) { return value >= FOOTSTEPS_MODEL && value <= FOOTSTEPS_MATERIAL ? value : FOOTSTEPS_MODEL; }
inline int HL2MPRemoteFootstepStyle( bool community, bool bright, bool ally, int teammates, int enemies )
{
    return community && bright ? HL2MPFootstepStyleValue( ally ? teammates : enemies ) : FOOTSTEPS_MODEL;
}
// Sound bank order is citizen, Combine, police, followed by surface sounds.
inline int HL2MPFootstepBank( int style, int modelBank )
{
    switch ( HL2MPFootstepStyleValue(style) )
    {
    case FOOTSTEPS_COMBINE: return 1;
    case FOOTSTEPS_POLICE: return 2;
    case FOOTSTEPS_REBEL: return 0;
    case FOOTSTEPS_MATERIAL: return 3;
    default: return modelBank >= 0 && modelBank < 3 ? modelBank : 0;
    }
}
#endif
