#ifndef LOADSAVEMERCPROFILE_H
#define LOADSAVEMERCPROFILE_H

#include "Soldier_Profile_Type.h"

#define MERC_PROFILE_SIZE               (716)           /**< Merc profile size on disk */

/**
* Extract merc profile from the binary data. */
void ExtractMercProfile(BYTE const* const Src, MERCPROFILESTRUCT& p, UINT32 *checksum, bool const isCorrectlyEncoded);

/** Calculates soldier profile checksum. */
UINT32 SoldierProfileChecksum(MERCPROFILESTRUCT const& p);

/** Extract IMP merc profile from file.
* If saved checksum is not correct, exception will be thrown. */
void ExtractImpProfileFromFile(SGPFile *file, INT32 *iProfileId, INT32 *iPortraitNumber, MERCPROFILESTRUCT& p);

void InjectMercProfile(BYTE* Dst, MERCPROFILESTRUCT const&);
void InjectMercProfileIntoFile(HWFILE, MERCPROFILESTRUCT const&);

#endif
