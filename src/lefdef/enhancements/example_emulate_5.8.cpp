#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

// 状态追踪（不修改 parser 代码）
static int processed_paths = 0;
static int total_paths = 0;
static int current_wire_idx = 0;
static int partial_cbk_count = 0;

// 模拟 5.8 的单个 path 处理（访问顺序完全一致）
static void processPathLike58(defiPath* path, int path_idx) {
    printf("    === Path #%d (emulating 5.8) ===\n", path_idx);
    
    // ⭐ 与 5.8 完全一致：initTraverse + next 循环
    path->initTraverse();
    
    int type;
    int point_count = 0;
    
    while ((type = path->next()) != DEFIPATH_DONE) {
        // ⭐ 与 5.8 的访问顺序完全一致
        switch (type) {
            case DEFIPATH_LAYER: {
                const char* layer = path->getLayer();
                printf("      [Layer] %s\n", layer);
                break;
            }
            case DEFIPATH_WIDTH: {
                int width = path->getWidth();
                printf("      [Width] %d\n", width);
                break;
            }
            case DEFIPATH_POINT: {
                int x, y;
                path->getPoint(&x, &y);
                printf("      [Point] #%d (%d, %d)\n", ++point_count, x, y);
                break;
            }
            case DEFIPATH_FLUSHPOINT: {
                int x, y, ext;
                path->getFlushPoint(&x, &y, &ext);
                printf("      [FlushPoint] #%d (%d, %d) ext=%d\n", ++point_count, x, y, ext);
                break;
            }
            case DEFIPATH_VIA: {
                const char* via = path->getVia();
                printf("      [Via] %s\n", via);
                break;
            }
            case DEFIPATH_VIAROTATION: {
                int rot = path->getViaRotation();
                printf("      [ViaRotation] %d\n", rot);
                break;
            }
            case DEFIPATH_TAPER: {
                printf("      [Taper] yes\n");
                break;
            }
            case DEFIPATH_SHAPE: {
                const char* shape = path->getShape();
                printf("      [Shape] %s\n", shape);
                break;
            }
            default:
                printf("      [Unknown] type=%d\n", type);
                break;
        }
    }
    
    printf("    === Path #%d done (points: %d) ===\n", path_idx, point_count);
    total_paths++;
}

static int partialPathCallback(defrCallbackType_e type, defiNet* net, void*) {
    partial_cbk_count++;
    printf("\n[Partial #%d] Net: %s, processing NEW paths...\n", 
           partial_cbk_count, net->name());
    
    int num_wires = net->numWires();
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        int num_paths = wire->numPaths();
        
        printf("  Wire #%d: total=%d paths, already processed=%d\n",
               w, num_paths, processed_paths);
        
        for (int p = processed_paths; p < num_paths; p++) {
            defiPath* path = wire->path(p);
            processPathLike58(path, total_paths + 1);
            processed_paths++;
        }
    }
    
    printf("[Partial #%d] done. Total processed so far: %d\n\n", 
           partial_cbk_count, total_paths);
    
    processed_paths = 0;
    
    return 0;
}

// SNet callback：处理剩余 path
static int snetCallback(defrCallbackType_e type, defiNet* net, void*) {
    printf("\n[SNet] Net: %s (final callback)\n", net->name());
    
    int num_wires = net->numWires();
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        int num_paths = wire->numPaths();
        
        printf("  Wire #%d: remaining paths=%d (already done=%d)\n",
               w, num_paths - processed_paths, processed_paths);
        
        // ⭐ 处理剩余未处理的 path
        for (int p = processed_paths; p < num_paths; p++) {
            defiPath* path = wire->path(p);
            processPathLike58(path, p + 1);
            processed_paths++;
        }
    }
    
    printf("[SNet] Total paths for this net: %d\n", total_paths);
    
    // ⭐ 重置计数器（为下一个 net）
    processed_paths = 0;
    current_wire_idx = 0;
    
    return 0;
}

// SNet end callback
static int snetEndCallback(defrCallbackType_e type, void*, void*) {
    printf("\n=== SPECIALNETS Section End ===\n");
    printf("Total partial callbacks: %d\n", partial_cbk_count);
    printf("Total paths processed: %d\n", total_paths);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <def_file>\n", argv[0]);
        fprintf(stderr, "Note: This program emulates 5.8 PathCbk behavior without modifying parser code\n");
        return 1;
    }
    
    printf("=== Emulating 5.8 PathCbk (no parser modification) ===\n");
    printf("File: %s\n\n", argv[1]);
    
    defrInit();
    
    // 注册 callbacks
    defrSetSNetCbk(snetCallback);
    defrSetSNetPartialPathCbk(partialPathCallback);
    defrSetSNetEndCbk(snetEndCallback);
    
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", argv[1]);
        return 1;
    }
    
    int result = defrRead(f, argv[1], NULL, 0);
    fclose(f);
    
    printf("\n=== Final Summary ===\n");
    printf("Parse result: %d\n", result);
    printf("Total partial callbacks: %d\n", partial_cbk_count);
    printf("Total paths processed: %d\n", total_paths);
    
    defrClear();
    
    return result;
}
