#include "cbase.h"
#include "hl2mp_rendertargets.h"
#include "materialsystem/imaterialsystem.h"

#include "tier0/memdbgon.h"

ITexture *CHL2MPRenderTargets::CreateProjectileCameraTexture( IMaterialSystem *pMaterialSystem )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_ProjectileCamera",
		1024, 576, RT_SIZE_DEFAULT,
		pMaterialSystem->GetBackBufferFormat(),
		MATERIAL_RT_DEPTH_SHARED,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
		CREATERENDERTARGETFLAGS_HDR );
}

void CHL2MPRenderTargets::InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );
	m_ProjectileCameraTexture.Init( CreateProjectileCameraTexture( pMaterialSystem ) );
}

void CHL2MPRenderTargets::ShutdownClientRenderTargets()
{
	m_ProjectileCameraTexture.Shutdown();
	BaseClass::ShutdownClientRenderTargets();
}

static CHL2MPRenderTargets g_HL2MPRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHL2MPRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_HL2MPRenderTargets );
