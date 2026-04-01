#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

static int nullVoidCbk(defrCallbackType_e, void*, defiUserData) { return 0; }
static int nullStringCbk(defrCallbackType_e, const char*, defiUserData) { return 0; }
static int nullIntCbk(defrCallbackType_e, int, defiUserData) { return 0; }
static int nullDoubleCbk(defrCallbackType_e, double, defiUserData) { return 0; }

END_LEFDEF_PARSER_NAMESPACE

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <def_file>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return 1;
    }

    using namespace LefDefParser;
    
    defrInit();

    defrSetDesignCbk(nullStringCbk);
    defrSetTechnologyCbk(nullStringCbk);
    defrSetDesignEndCbk(nullVoidCbk);
    defrSetPropCbk((defrPropCbkFnType)nullVoidCbk);
    defrSetPropDefStartCbk(nullVoidCbk);
    defrSetPropDefEndCbk(nullVoidCbk);
    defrSetArrayNameCbk(nullStringCbk);
    defrSetFloorPlanNameCbk(nullStringCbk);
    defrSetUnitsCbk(nullDoubleCbk);
    defrSetVersionCbk(nullDoubleCbk);
    defrSetVersionStrCbk(nullStringCbk);
    defrSetDividerCbk(nullStringCbk);
    defrSetBusBitCbk(nullStringCbk);
    defrSetCaseSensitiveCbk(nullIntCbk);
    defrSetHistoryCbk(nullStringCbk);
    defrSetExtensionCbk(nullStringCbk);

    defrSetSiteCbk((defrSiteCbkFnType)nullVoidCbk);
    defrSetCanplaceCbk((defrSiteCbkFnType)nullVoidCbk);
    defrSetCannotOccupyCbk((defrSiteCbkFnType)nullVoidCbk);
    defrSetDieAreaCbk((defrBoxCbkFnType)nullVoidCbk);
    defrSetPinCapCbk((defrPinCapCbkFnType)nullVoidCbk);
    defrSetDefaultCapCbk(nullIntCbk);
    defrSetStartPinsCbk(nullIntCbk);
    defrSetPinEndCbk(nullVoidCbk);
    defrSetPinCbk((defrPinCbkFnType)nullVoidCbk);
    defrSetPinPropCbk((defrPinPropCbkFnType)nullVoidCbk);
    defrSetPinPropStartCbk(nullIntCbk);
    defrSetPinPropEndCbk(nullVoidCbk);
    defrSetRowCbk((defrRowCbkFnType)nullVoidCbk);
    defrSetTrackCbk((defrTrackCbkFnType)nullVoidCbk);
    defrSetGcellGridCbk((defrGcellGridCbkFnType)nullVoidCbk);
    defrSetViaCbk((defrViaCbkFnType)nullVoidCbk);
    defrSetViaStartCbk(nullIntCbk);
    defrSetViaEndCbk(nullVoidCbk);
    defrSetRegionCbk((defrRegionCbkFnType)nullVoidCbk);
    defrSetRegionStartCbk(nullIntCbk);
    defrSetRegionEndCbk(nullVoidCbk);
    defrSetComponentCbk((defrComponentCbkFnType)nullVoidCbk);
    defrSetComponentStartCbk(nullIntCbk);
    defrSetComponentEndCbk(nullVoidCbk);
    defrSetComponentMaskShiftLayerCbk((defrComponentMaskShiftLayerCbkFnType)nullVoidCbk);
    defrSetNetCbk((defrNetCbkFnType)nullVoidCbk);
    defrSetNetNameCbk(nullStringCbk);
    defrSetNetStartCbk(nullIntCbk);
    defrSetNetEndCbk(nullVoidCbk);
    defrSetNetSubnetNameCbk(nullStringCbk);
    defrSetNetNonDefaultRuleCbk(nullStringCbk);
    defrSetNetPartialPathCbk((defrNetCbkFnType)nullVoidCbk);
    defrSetSNetCbk((defrNetCbkFnType)nullVoidCbk);
    defrSetSNetStartCbk(nullIntCbk);
    defrSetSNetEndCbk(nullVoidCbk);
    defrSetSNetPartialPathCbk((defrNetCbkFnType)nullVoidCbk);
    defrSetSNetWireCbk((defrNetCbkFnType)nullVoidCbk);
    defrSetGroupsStartCbk(nullIntCbk);
    defrSetGroupsEndCbk(nullVoidCbk);
    defrSetGroupNameCbk(nullStringCbk);
    defrSetGroupMemberCbk(nullStringCbk);
    defrSetGroupCbk((defrGroupCbkFnType)nullVoidCbk);
    defrSetScanchainCbk((defrScanchainCbkFnType)nullVoidCbk);
    defrSetScanchainsStartCbk(nullIntCbk);
    defrSetScanchainsEndCbk(nullVoidCbk);
    defrSetIOTimingCbk((defrIOTimingCbkFnType)nullVoidCbk);
    defrSetIOTimingsStartCbk(nullIntCbk);
    defrSetIOTimingsEndCbk(nullVoidCbk);
    defrSetFPCCbk((defrFPCCbkFnType)nullVoidCbk);
    defrSetFPCStartCbk(nullIntCbk);
    defrSetFPCEndCbk(nullVoidCbk);
    defrSetTimingDisableCbk((defrTimingDisableCbkFnType)nullVoidCbk);
    defrSetTimingDisablesStartCbk(nullIntCbk);
    defrSetTimingDisablesEndCbk(nullVoidCbk);
    defrSetPartitionCbk((defrPartitionCbkFnType)nullVoidCbk);
    defrSetPartitionsStartCbk(nullIntCbk);
    defrSetPartitionsEndCbk(nullVoidCbk);
    defrSetBlockageCbk((defrBlockageCbkFnType)nullVoidCbk);
    defrSetBlockageStartCbk(nullIntCbk);
    defrSetBlockageEndCbk(nullVoidCbk);
    defrSetSlotCbk((defrSlotCbkFnType)nullVoidCbk);
    defrSetSlotStartCbk(nullIntCbk);
    defrSetSlotEndCbk(nullVoidCbk);
    defrSetFillCbk((defrFillCbkFnType)nullVoidCbk);
    defrSetFillStartCbk(nullIntCbk);
    defrSetFillEndCbk(nullVoidCbk);
    defrSetNonDefaultCbk((defrNonDefaultCbkFnType)nullVoidCbk);
    defrSetNonDefaultStartCbk(nullIntCbk);
    defrSetNonDefaultEndCbk(nullVoidCbk);
    defrSetStylesCbk((defrStylesCbkFnType)nullVoidCbk);
    defrSetStylesStartCbk(nullIntCbk);
    defrSetStylesEndCbk(nullVoidCbk);
    defrSetAssertionCbk((defrAssertionCbkFnType)nullVoidCbk);
    defrSetAssertionsStartCbk(nullIntCbk);
    defrSetAssertionsEndCbk(nullVoidCbk);
    defrSetConstraintCbk((defrAssertionCbkFnType)nullVoidCbk);
    defrSetConstraintsStartCbk(nullIntCbk);
    defrSetConstraintsEndCbk(nullVoidCbk);

    defrSetComponentExtCbk(nullStringCbk);
    defrSetPinExtCbk(nullStringCbk);
    defrSetViaExtCbk(nullStringCbk);
    defrSetNetConnectionExtCbk(nullStringCbk);
    defrSetNetExtCbk(nullStringCbk);
    defrSetGroupExtCbk(nullStringCbk);
    defrSetScanChainExtCbk(nullStringCbk);
    defrSetIoTimingsExtCbk(nullStringCbk);
    defrSetPartitionsExtCbk(nullStringCbk);

    int status = defrRead(f, filename, NULL, 1);

    fclose(f);
    defrClear();

    return status;
}