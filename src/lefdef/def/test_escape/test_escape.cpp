#include <stdio.h>
#include <string.h>
#include "../def/defrReader.hpp"

FILE* fout;
int escapeCount = 0;

bool hasBackslash(const char* str) {
    return strchr(str, '\\') != NULL;
}

int compCallback(defrCallbackType_e c, defiComponent* co, defiUserData ud) {
    const char* id = co->id();
    if (hasBackslash(id) || strstr(id, "[") || strstr(id, "/")) {
        fprintf(fout, "Instance: '%s'\n", id);
        escapeCount++;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <def_file>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        printf("Cannot open file: %s\n", argv[1]);
        return 1;
    }

    fout = stdout;
    defrInit();
    defrSetComponentCbk(compCallback);
    defrSetProcessEscapeInTString(1);

    printf("=== Testing escape character handling (T_STRING enabled) ===\n\n");

    int result = defrRead(f, argv[1], NULL, 1);

    printf("\n--- Total escaped/special names found: %d ---\n", escapeCount);

    defrClear();
    fclose(f);

    return result;
}