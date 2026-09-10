#include "cbase.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "tier1/KeyValues.h"
#include "tier1/utldict.h"

#include "tier0/memdbgon.h"

ConVar cl_parallax_cubemaps( "cl_parallax_cubemaps", "1", FCVAR_ARCHIVE, "Apply per-map parallax cubemap settings." );

class CHL2MPParallaxCubemaps : public CAutoGameSystemPerFrame
{
public:
	CHL2MPParallaxCubemaps() : CAutoGameSystemPerFrame( "HL2MPParallaxCubemaps" ), m_bActive( false ), m_bEnabled( false ), m_bReload( false ) {}

	bool Init() OVERRIDE
	{
		materials->AddRestoreFunc( OnMaterialRestore );
		return true;
	}

	void Shutdown() OVERRIDE
	{
		LevelShutdownPreEntity();
		materials->RemoveRestoreFunc( OnMaterialRestore );
	}

	void LevelInitPostEntity() OVERRIDE
	{
		m_bActive = true;
		Reload();
	}

	void LevelShutdownPreEntity() OVERRIDE
	{
		m_bActive = false;
		m_bReload = false;
		Clear();
	}

	void PreRender() OVERRIDE
	{
		if ( m_bActive && ( m_bReload || m_bEnabled != cl_parallax_cubemaps.GetBool() ) )
			Reload();
	}

	void Reload();
	static void OnMaterialRestore( int nChangeFlags );

private:
	struct Cubemap_t
	{
		Vector origin;
		matrix3x4_t worldToBox;
	};

	struct Material_t
	{
		IMaterial *pMaterial;
		bool bEnabledDefined;
		int nEnabled;
		int nVectorSizes[4];
		float vectors[4][4];
	};

	void Clear();
	CUtlVector< Material_t > m_Materials;
	bool m_bActive;
	bool m_bEnabled;
	bool m_bReload;
	static const char *s_pParams[5];
};

static CHL2MPParallaxCubemaps g_ParallaxCubemaps;

const char *CHL2MPParallaxCubemaps::s_pParams[5] =
{
	"$envmapparallax", "$envmaporigin", "$envmapparallaxobb1", "$envmapparallaxobb2", "$envmapparallaxobb3"
};

void CHL2MPParallaxCubemaps::OnMaterialRestore( int nChangeFlags )
{
	g_ParallaxCubemaps.m_bReload = true;
}

void CHL2MPParallaxCubemaps::Clear()
{
	if ( !m_Materials.Count() )
		return;

	MaterialLock_t lock = materials->Lock();
	FOR_EACH_VEC( m_Materials, i )
	{
		Material_t &entry = m_Materials[i];
		for ( int j = 0; j < (int)ARRAYSIZE( s_pParams ); ++j )
		{
			bool bFound = false;
			IMaterialVar *pVar = entry.pMaterial->FindVar( s_pParams[j], &bFound, false );
			if ( !bFound )
				continue;

			if ( j == 0 )
			{
				if ( entry.bEnabledDefined )
					pVar->SetIntValue( entry.nEnabled );
				else
					pVar->SetUndefined();
			}
			else if ( entry.nVectorSizes[j - 1] )
				pVar->SetVecValue( entry.vectors[j - 1], entry.nVectorSizes[j - 1] );
			else
				pVar->SetUndefined();
		}
		entry.pMaterial->RecomputeStateSnapshots();
		entry.pMaterial->DecrementReferenceCount();
	}
	m_Materials.Purge();
	materials->Unlock( lock );
}

void CHL2MPParallaxCubemaps::Reload()
{
	Clear();
	m_bReload = false;
	m_bEnabled = cl_parallax_cubemaps.GetBool();
	if ( !m_bActive || !m_bEnabled )
		return;

	char szMap[MAX_PATH];
	const char *pMapName = engine->GetLevelName();
	if ( !pMapName || V_strlen( pMapName ) >= (int)sizeof( szMap ) )
		return;
	V_StripExtension( pMapName, szMap, sizeof( szMap ) );
	V_FixSlashes( szMap, '/' );
	V_strlower( szMap );
	if ( V_strncmp( szMap, "maps/", 5 ) || !szMap[5] || V_strstr( szMap, ".." ) || V_strchr( szMap, ':' ) )
		return;

	char szFile[MAX_PATH + 16];
	V_snprintf( szFile, sizeof( szFile ), "%s_parallax.txt", szMap );
	KeyValues *pSettings = new KeyValues( "ParallaxCubemaps" );
	KeyValues::AutoDelete settings( pSettings );
	if ( !pSettings->LoadFromFile( filesystem, szFile, "GAME" ) )
		return;

	if ( pSettings->FindKey( "mapversion" ) && pSettings->GetInt( "mapversion" ) != engine->GetLevelVersion() )
	{
		Warning( "Parallax cubemap settings %s do not match this map revision.\n", szFile );
		return;
	}

	KeyValues *pBoxes = pSettings->FindKey( "boxes" );
	if ( !pBoxes )
	{
		Warning( "Missing boxes in %s.\n", szFile );
		return;
	}

	CUtlDict< Cubemap_t, int > cubemaps;
	for ( KeyValues *pBox = pBoxes->GetFirstTrueSubKey(); pBox; pBox = pBox->GetNextTrueSubKey() )
	{
		Vector values[4];
		const char *pKeys[] = { "mins", "maxs", "origin", "angles" };
		bool bValid = true;
		for ( int i = 0; i < (int)ARRAYSIZE( pKeys ); ++i )
		{
			char extra;
			const char *pValue = pBox->GetString( pKeys[i], i < 2 ? "" : "0 0 0" );
			bValid = bValid && sscanf( pValue, "%f %f %f %c", &values[i].x, &values[i].y, &values[i].z, &extra ) == 3;
			for ( int j = 0; bValid && j < 3; ++j )
				bValid = IsFinite( values[i][j] ) && fabsf( values[i][j] ) <= MAX_COORD_FLOAT;
		}
		for ( int i = 0; bValid && i < 3; ++i )
			bValid = values[1][i] - values[0][i] >= 0.01f;
		KeyValues *pProbes = pBox->FindKey( "cubemaps" );
		if ( !bValid || !pProbes || !pProbes->GetFirstValue() )
		{
			Warning( "Invalid parallax cubemap box %s in %s.\n", pBox->GetName(), szFile );
			return;
		}

		matrix3x4_t boxToWorld, worldToBox;
		AngleMatrix( QAngle( values[3].x, values[3].y, values[3].z ), values[2], boxToWorld );
		MatrixInvert( boxToWorld, worldToBox );
		for ( int i = 0; i < 3; ++i )
		{
			worldToBox[i][3] -= values[0][i];
			for ( int j = 0; j < 4; ++j )
				worldToBox[i][j] /= values[1][i] - values[0][i];
		}

		for ( KeyValues *pProbe = pProbes->GetFirstValue(); pProbe; pProbe = pProbe->GetNextValue() )
		{
			Cubemap_t probe;
			char extra;
			bValid = sscanf( pProbe->GetName(), "c%f_%f_%f%c", &probe.origin.x, &probe.origin.y, &probe.origin.z, &extra ) == 3;
			for ( int i = 0; bValid && i < 3; ++i )
				bValid = IsFinite( probe.origin[i] ) && fabsf( probe.origin[i] ) <= MAX_COORD_FLOAT;
			char szProbe[64] = {};
			if ( bValid )
				V_snprintf( szProbe, sizeof( szProbe ), "c%d_%d_%d", (int)probe.origin.x, (int)probe.origin.y, (int)probe.origin.z );
			bValid = bValid && !V_strcmp( szProbe, pProbe->GetName() );
			Vector local;
			if ( bValid )
			{
				VectorTransform( probe.origin, worldToBox, local );
				for ( int i = 0; bValid && i < 3; ++i )
					bValid = local[i] > 0.0f && local[i] < 1.0f;
			}
			if ( !bValid || cubemaps.Find( szProbe ) != cubemaps.InvalidIndex() )
			{
				Warning( "Invalid or duplicate parallax cubemap %s in %s.\n", pProbe->GetName(), szFile );
				return;
			}
			MatrixCopy( worldToBox, probe.worldToBox );
			cubemaps.Insert( szProbe, probe );
		}
	}

	char szPrefix[MAX_PATH + 2];
	V_snprintf( szPrefix, sizeof( szPrefix ), "%s/", szMap );
	const int nPrefixLength = V_strlen( szPrefix );
	MaterialLock_t lock = materials->Lock();
	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMaterial = materials->GetMaterial( h );
		if ( !pMaterial || V_strnicmp( pMaterial->GetName(), szPrefix, nPrefixLength ) || pMaterial->IsErrorMaterial() )
			continue;
		const char *pShader = pMaterial->GetShaderName();
		if ( !pShader || ( V_stricmp( pShader, "LightmappedGeneric" ) && V_stricmp( pShader, "LightmappedGeneric_DX9" ) &&
			 V_stricmp( pShader, "WorldVertexTransition" ) && V_stricmp( pShader, "WorldVertexTransition_DX9" ) ) )
			continue;

		bool bFound = false;
		IMaterialVar *pEnvmap = pMaterial->FindVar( "$envmap", &bFound, false );
		if ( !bFound || !pEnvmap->IsTexture() )
			continue;
		ITexture *pTexture = pEnvmap->GetTextureValue();
		if ( !pTexture || pTexture->IsError() || !pTexture->IsCubeMap() || V_strnicmp( pTexture->GetName(), szPrefix, nPrefixLength ) )
			continue;

		char szProbe[64];
		V_strncpy( szProbe, pTexture->GetName() + nPrefixLength, sizeof( szProbe ) );
		int nLength = V_strlen( szProbe );
		if ( nLength > 4 && !V_stricmp( szProbe + nLength - 4, ".hdr" ) )
			szProbe[nLength - 4] = 0;
		int nProbe = cubemaps.Find( szProbe );
		if ( nProbe == cubemaps.InvalidIndex() )
			continue;

		IMaterialVar *pVars[5];
		bool bValid = true;
		Material_t entry = {};
		entry.pMaterial = pMaterial;
		for ( int i = 0; bValid && i < (int)ARRAYSIZE( pVars ); ++i )
		{
			pVars[i] = pMaterial->FindVar( s_pParams[i], &bFound, false );
			bValid = bFound;
			if ( !bValid || !pVars[i]->IsDefined() )
				continue;
			if ( i == 0 )
			{
				bValid = pVars[i]->GetType() == MATERIAL_VAR_TYPE_INT && pVars[i]->GetIntValue() == 0;
				entry.bEnabledDefined = true;
				entry.nEnabled = pVars[i]->GetIntValue();
			}
			else
			{
				bValid = pVars[i]->GetType() == MATERIAL_VAR_TYPE_VECTOR && pVars[i]->VectorSize() >= 2 && pVars[i]->VectorSize() <= 4;
				if ( bValid )
				{
					entry.nVectorSizes[i - 1] = pVars[i]->VectorSize();
					pVars[i]->GetVecValue( entry.vectors[i - 1], entry.nVectorSizes[i - 1] );
				}
			}
		}
		if ( !bValid )
			continue;

		pMaterial->IncrementReferenceCount();
		m_Materials.AddToTail( entry );
		Cubemap_t &probe = cubemaps[nProbe];
		pVars[1]->SetVecValue( probe.origin.Base(), 3 );
		for ( int i = 0; i < 3; ++i )
			pVars[i + 2]->SetVecValue( probe.worldToBox[i], 4 );
		pVars[0]->SetIntValue( 1 );
		pMaterial->RecomputeStateSnapshots();
	}
	materials->Unlock( lock );
	DevMsg( "Applied %s to %d materials.\n", szFile, m_Materials.Count() );
}

CON_COMMAND_F( cl_parallax_cubemaps_reload, "Reload the current map's parallax cubemap settings.", FCVAR_CHEAT )
{
	g_ParallaxCubemaps.Reload();
}
