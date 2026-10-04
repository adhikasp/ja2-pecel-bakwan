#ifndef LOADSAVETACTICALSTATUSTYPE_H
#define LOADSAVETACTICALSTATUSTYPE_H

#include "Types.h"


#define TACTICAL_STATUS_TYPE_SIZE               (316)


void ExtractTacticalStatusTypeFromFile(HWFILE);
void InjectTacticalStatusTypeIntoFile(HWFILE);

#endif
