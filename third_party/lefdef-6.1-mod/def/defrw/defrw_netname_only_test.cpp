#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

static int netNameCount = 0;

static int nullVoidCbk(defrCallbackType_e, void*, defiUserData) { return 0; }
static int nullStringCbk(defrCallbackType_e, const char*, defiUserData) { return 0; }
static int nullIntCbk(defrCallbackType_e, int, defiUserData) { return 0; }
static int nullDoubleCbk(defrCallbackType_e, double, defiUserData) { return 0; }

static int netNameCbk(defrCallbackType_e, const char* name, defiUserData) {
    netNameCount++;
    return 0;
}

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
    
    defrSetNetNameCbk(netNameCbk);
    
    defrSetNetNameOnly(1);
    printf("defrSetNetNameOnly(1) enabled\n");
    printf("This enables: SkipComponents + SkipNetDetails + SkipSNetDetails\n");
    
    int result = defrRead(f, filename, NULL, 1);
    
    fclose(f);
    
    printf("Parse result: %d\n", result);
    printf("Total net names: %d\n", netNameCount);
    
    defrClear();
    
    return result;
}