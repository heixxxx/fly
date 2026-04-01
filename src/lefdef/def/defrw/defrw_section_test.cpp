#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

static int netCount = 0;
static int snetCount = 0;
static int netNameCount = 0;

static int nullVoidCbk(defrCallbackType_e, void*, defiUserData) { return 0; }
static int nullStringCbk(defrCallbackType_e, const char*, defiUserData) { return 0; }
static int nullIntCbk(defrCallbackType_e, int, defiUserData) { return 0; }
static int nullDoubleCbk(defrCallbackType_e, double, defiUserData) { return 0; }

static int netCbk(defrCallbackType_e, defiNet* net, defiUserData) {
    netCount++;
    return 0;
}

static int snetCbk(defrCallbackType_e, defiNet* net, defiUserData) {
    snetCount++;
    return 0;
}

static int netNameCbk(defrCallbackType_e, const char* name, defiUserData) {
    netNameCount++;
    return 0;
}

END_LEFDEF_PARSER_NAMESPACE

void printUsage(const char* prog) {
    printf("Usage: %s [-skipcomp] [-skipnets] [-skipsnets] [-netname] <def_file>\n", prog);
    printf("  -skipcomp  : Skip COMPONENTS section\n");
    printf("  -skipnets  : Skip NETS section\n");
    printf("  -skipsnets : Skip SPECIALNETS section\n");
    printf("  -netname   : Use NetNameCbk instead of NetCbk (lighter callback)\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    int skipComp = 0;
    int skipNets = 0;
    int skipSnets = 0;
    int useNetName = 0;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-skipcomp") == 0) {
            skipComp = 1;
        } else if (strcmp(argv[i], "-skipnets") == 0) {
            skipNets = 1;
        } else if (strcmp(argv[i], "-skipsnets") == 0) {
            skipSnets = 1;
        } else if (strcmp(argv[i], "-netname") == 0) {
            useNetName = 1;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    if (!filename) {
        printUsage(argv[0]);
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

    if (skipComp) defrSetSkipComponents(1);
    if (skipNets) defrSetSkipNets(1);
    if (skipSnets) defrSetSkipSpecialNets(1);

    if (!skipNets) {
        if (useNetName) {
            defrSetNetNameCbk(netNameCbk);
        } else {
            defrSetNetCbk(netCbk);
            defrSetNetStartCbk(nullIntCbk);
        }
    }
    
    if (!skipSnets) {
        defrSetSNetCbk(snetCbk);
        defrSetSNetStartCbk(nullIntCbk);
    }

    int result = defrRead(f, filename, NULL, 1);
    
    fclose(f);

    printf("Parse result: %d\n", result);
    printf("Nets parsed: %d\n", netCount);
    printf("Net names parsed: %d\n", netNameCount);
    printf("Special nets parsed: %d\n", snetCount);

    defrClear();

    return result;
}