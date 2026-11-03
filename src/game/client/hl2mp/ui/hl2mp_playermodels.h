#ifndef HL2MP_PLAYERMODELS_H
#define HL2MP_PLAYERMODELS_H

// Keep this roster aligned with CHL2MP_Player::ValidatePlayerModel.
struct HL2MPPlayerModel { const char *name; const char *path; };
static const HL2MPPlayerModel s_PlayerModels[] = {
    { "#HL2MP_UI_c6c23ebeb9c3", "models/combine_soldier.mdl" },
    { "#HL2MP_UI_fc6a085ccdc0", "models/combine_soldier_prisonguard.mdl" },
    { "#HL2MP_UI_faed69cdde12", "models/combine_super_soldier.mdl" },
    { "#HL2MP_UI_f0c03bb77035", "models/police.mdl" },
    { "#HL2MP_UI_057bff8b8959", "models/humans/group03/male_01.mdl" },
    { "#HL2MP_UI_b9e697bc19e0", "models/humans/group03/male_02.mdl" },
    { "#HL2MP_UI_aad656bb6e8f", "models/humans/group03/male_03.mdl" },
    { "#HL2MP_UI_1f95117ee4f3", "models/humans/group03/male_04.mdl" },
    { "#HL2MP_UI_e7df3af51093", "models/humans/group03/male_05.mdl" },
    { "#HL2MP_UI_d1c40fe43425", "models/humans/group03/male_06.mdl" },
    { "#HL2MP_UI_75631726e0e9", "models/humans/group03/male_07.mdl" },
    { "#HL2MP_UI_566a21d4264d", "models/humans/group03/male_08.mdl" },
    { "#HL2MP_UI_94692f297e0b", "models/humans/group03/male_09.mdl" },
    { "#HL2MP_UI_d05c0f4c00fb", "models/humans/group03/female_01.mdl" },
    { "#HL2MP_UI_f87f3edea40a", "models/humans/group03/female_02.mdl" },
    { "#HL2MP_UI_7c38943c47b6", "models/humans/group03/female_03.mdl" },
    { "#HL2MP_UI_2a326b14e241", "models/humans/group03/female_04.mdl" },
    { "#HL2MP_UI_e031a9531886", "models/humans/group03/female_06.mdl" },
    { "#HL2MP_UI_022ecfb248bc", "models/humans/group03/female_07.mdl" }
};

#endif
