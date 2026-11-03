#ifndef HL2MP_COMMUNITY_H
#define HL2MP_COMMUNITY_H
class CBasePlayer;
class KeyValues;
void HL2MPCommunityRequest( CBasePlayer *player, KeyValues *request );
bool HL2MPCommunityIsProtected( CBasePlayer *player );
#endif
