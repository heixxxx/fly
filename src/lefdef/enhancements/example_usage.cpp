#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include "defrReader.hpp"

static int partial_callback_count = 0;
static int net_callback_count = 0;
static long total_paths_processed = 0;
static long peak_memory_kb = 0;
static long last_report_paths = 0;

static long get_memory_kb(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

static void update_peak(void) {
    long cur = get_memory_kb();
    if (cur > peak_memory_kb) peak_memory_kb = cur;
}

static int partialPathCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    partial_callback_count++;
    update_peak();
    
    int num_wires = net->numWires();
    int total_paths = 0;
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        total_paths += wire->numPaths();
    }
    
    total_paths_processed += total_paths;
    
    fprintf(stderr, "[Partial #%d] Net: %s, Wires: %d, Paths in this batch: %d, Total paths: %ld, Peak: %ld KB\n",
            partial_callback_count, net->name(), num_wires, total_paths, 
            total_paths_processed, peak_memory_kb);
    
    return 0;
}

static int netCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    net_callback_count++;
    update_peak();
    
    int num_wires = net->numWires();
    int total_paths = 0;
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        total_paths += wire->numPaths();
    }
    
    fprintf(stderr, "[Net #%d] Net: %s, Wires: %d, Paths: %d, Pins: %d, Peak: %ld KB\n",
            net_callback_count, net->name(), num_wires, total_paths,
            net->numConnections(), peak_memory_kb);
    
    return 0;
}

static int netEndCallback(defrCallbackType_e type, void*, defiUserData data) {
    update_peak();
    fprintf(stderr, "\n=== NETS Section End ===\n");
    fprintf(stderr, "Total nets: %d\n", net_callback_count);
    fprintf(stderr, "Total partial callbacks: %d\n", partial_callback_count);
    fprintf(stderr, "Total paths processed: %ld\n", total_paths_processed);
    fprintf(stderr, "Peak memory: %ld KB (%.2f MB)\n", peak_memory_kb, peak_memory_kb/1024.0);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <def_file>\n", argv[0]);
        return 1;
    }
    
    const char* def_file = argv[1];
    fprintf(stderr, "=== Testing NetPartialPathCbk ===\n");
    fprintf(stderr, "File: %s\n", def_file);
    
    defrInit();
    
    // Register callbacks
    defrSetNetCbk(netCallback);
    defrSetNetPartialPathCbk(partialPathCallback);
    defrSetNetEndCbk(netEndCallback);
    
    clock_t start = clock();
    
    FILE* f = fopen(def_file, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", def_file);
        return 1;
    }
    
    int result = defrRead(f, def_file, NULL, 0);
    fclose(f);
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    fprintf(stderr, "\n=== Final Results ===\n");
    fprintf(stderr, "Parse time: %.2f seconds\n", elapsed);
    fprintf(stderr, "Parse result: %d\n", result);
    fprintf(stderr, "Net callbacks: %d\n", net_callback_count);
    fprintf(stderr, "Partial path callbacks: %d\n", partial_callback_count);
    fprintf(stderr, "Peak memory: %ld KB (%.2f MB)\n", peak_memory_kb, peak_memory_kb/1024.0);
    
    defrClear();
    
    return result;
}
