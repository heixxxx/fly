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
        fprintf(stderr, "Usage: %s [-skip] <def_file>\n", argv[0]);
        return 1;
    }

    int skipDetails = 0;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-skip") == 0) {
            skipDetails = 1;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [-skip] <def_file>\n", argv[0]);
        return 1;
    }

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

    defrSetSkipComponents(1);
    
    if (skipDetails) {
        defrSetSkipNetDetails(1);
        defrSetSkipSNetDetails(1);
        printf("Skip net/snet details enabled\n");
    }

    defrSetNetNameCbk(netNameCbk);

    int result = defrRead(f, filename, NULL, 1);
    
    fclose(f);

    printf("Parse result: %d\n", result);
    printf("Total net/snet names: %d\n", netNameCount);

    defrClear();

    return result;
}