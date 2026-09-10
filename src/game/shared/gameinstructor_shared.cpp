#include "cbase.h"

#include "tier0/memdbgon.h"

ConVar sv_gameinstructor_disable( "sv_gameinstructor_disable", "0", FCVAR_REPLICATED | FCVAR_NOTIFY,
    "Disable game instructor hints for all clients." );
