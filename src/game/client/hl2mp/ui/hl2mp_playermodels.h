#ifndef HL2MP_PLAYERMODELS_H
#define HL2MP_PLAYERMODELS_H

// Keep this roster aligned with CHL2MP_Player::ValidatePlayerModel.
struct HL2MPPlayerModel { const char *name; const char *path; };
static const HL2MPPlayerModel s_PlayerModels[] = {
    { "COMBINE SOLDIER", "models/combine_soldier.mdl" },
    { "PRISON GUARD", "models/combine_soldier_prisonguard.mdl" },
    { "COMBINE ELITE", "models/combine_super_soldier.mdl" },
    { "METROPOLICE", "models/police.mdl" },
    { "REBEL / MALE 01", "models/humans/group03/male_01.mdl" },
    { "REBEL / MALE 02", "models/humans/group03/male_02.mdl" },
    { "REBEL / MALE 03", "models/humans/group03/male_03.mdl" },
    { "REBEL / MALE 04", "models/humans/group03/male_04.mdl" },
    { "REBEL / MALE 05", "models/humans/group03/male_05.mdl" },
    { "REBEL / MALE 06", "models/humans/group03/male_06.mdl" },
    { "REBEL / MALE 07", "models/humans/group03/male_07.mdl" },
    { "REBEL / MALE 08", "models/humans/group03/male_08.mdl" },
    { "REBEL / MALE 09", "models/humans/group03/male_09.mdl" },
    { "REBEL / FEMALE 01", "models/humans/group03/female_01.mdl" },
    { "REBEL / FEMALE 02", "models/humans/group03/female_02.mdl" },
    { "REBEL / FEMALE 03", "models/humans/group03/female_03.mdl" },
    { "REBEL / FEMALE 04", "models/humans/group03/female_04.mdl" },
    { "REBEL / FEMALE 06", "models/humans/group03/female_06.mdl" },
    { "REBEL / FEMALE 07", "models/humans/group03/female_07.mdl" }
};

#endif
