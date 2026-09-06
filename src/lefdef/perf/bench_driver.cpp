// 无回显 DEF 解析基准 driver：回调只计数，测 parser 本身而非打印。
// 用法: bench_defrw <def file>
#include <stdio.h>
#include <time.h>
#include "defrReader.hpp"

static long compCount = 0, netCount = 0, snetCount = 0;
static long connCount = 0, netWirePaths = 0, snetWirePaths = 0;

static int compCbk(defrCallbackType_e, defiComponent*, defiUserData) {
    compCount++;
    return 0;
}

static int netCbk(defrCallbackType_e, defiNet* n, defiUserData) {
    netCount++;
    connCount += n->numConnections();
    for (int i = 0; i < n->numWires(); i++)
        netWirePaths += n->wire(i)->numPaths();
    return 0;
}

static int snetCbk(defrCallbackType_e, defiNet* n, defiUserData) {
    snetCount++;
    for (int i = 0; i < n->numWires(); i++)
        snetWirePaths += n->wire(i)->numPaths();
    return 0;
}

static int snetPathCbk(defrCallbackType_e, defiNet*, defiUserData) {
    return 0;  // partial path 已挂到 Net，无需处理
}

static void quietLog(const char*) {}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <def file>\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }

    defrInitSession(1);
    defrSetAllowVer60Plus(1);
    defrSetWarningLogFunction(quietLog);
    defrSetLogFunction(quietLog);
    defrSetComponentCbk(compCbk);
    defrSetNetCbk(netCbk);
    defrSetSNetCbk(snetCbk);
    defrSetSNetPartialPathCbk(snetPathCbk);
    defrSetNetPartialPathCbk(snetPathCbk);
    defrSetAddPathToNet();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int res = defrRead(f, argv[1], (void*)1, 1);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    fprintf(stderr,
            "status=%d components=%ld nets=%ld specialnets=%ld connections=%ld "
            "net_wirepaths=%ld snet_wirepaths=%ld time=%.0fms\n",
            res, compCount, netCount, snetCount, connCount, netWirePaths,
            snetWirePaths, ms);

    fclose(f);
    defrReleaseNResetMemory();
    return res != 0;
}
