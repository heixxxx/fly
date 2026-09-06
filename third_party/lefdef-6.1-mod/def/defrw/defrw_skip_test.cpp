#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

static int netCount = 0;
static int snetCount = 0;

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

END_LEFDEF_PARSER_NAMESPACE

void printUsage(const char* prog) {
    printf("Usage: %s [-skip] <def_file>\n", prog);
    printf("  -skip   : Skip COMPONENTS section for performance test\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    int skipComponents = 0;
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-skip") == 0) {
            skipComponents = 1;
        } else {
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

    // Set callbacks for header info
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

    // Set callbacks for NETS
    defrSetNetCbk(netCbk);
    defrSetNetStartCbk(nullIntCbk);
    
    // Set callbacks for SPECIALNETS
    defrSetSNetCbk(snetCbk);
    defrSetSNetStartCbk(nullIntCbk);

    // Skip COMPONENTS if requested
    if (skipComponents) {
        defrSetSkipComponents(1);
        printf("Skipping COMPONENTS section\n");
    }

    int result = defrRead(f, filename, NULL, 1);
    
    fclose(f);

    printf("Parse result: %d\n", result);
    printf("Nets parsed: %d\n", netCount);
    printf("Special nets parsed: %d\n", snetCount);

    defrClear();

    return result;
}