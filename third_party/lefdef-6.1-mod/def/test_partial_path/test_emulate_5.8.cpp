#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"

// 全局状态，模拟 5.8 的逐个 path 处理
static int processed_path_count = 0;
static int total_paths_count = 0;
static int partial_callback_count = 0;

// 处理单个 path（完全模仿 5.8 的访问方式）
static void processSinglePath(defiPath* path) {
    printf("  --- Processing single path ---\n");
    
    // ⭐ 与 5.8 的数据访问完全一致
    path->initTraverse();
    int type;
    int point_count = 0;
    
    while ((type = path->next()) != DEFIPATH_DONE) {
        switch (type) {
            case DEFIPATH_LAYER: {
                const char* layer = path->getLayer();
                printf("    Layer: %s\n", layer);
                break;
            }
            case DEFIPATH_WIDTH: {
                int width = path->getWidth();
                printf("    Width: %d\n", width);
                break;
            }
            case DEFIPATH_POINT: {
                int x, y;
                path->getPoint(&x, &y);
                printf("    Point #%d: (%d, %d)\n", ++point_count, x, y);
                break;
            }
            case DEFIPATH_FLUSHPOINT: {
                int x, y, ext;
                path->getFlushPoint(&x, &y, &ext);
                printf("    FlushPoint #%d: (%d, %d) ext=%d\n", ++point_count, x, y, ext);
                break;
            }
            case DEFIPATH_VIA: {
                const char* via = path->getVia();
                printf("    Via: %s\n", via);
                break;
            }
            case DEFIPATH_VIAROTATION: {
                int rot = path->getViaRotation();
                printf("    ViaRotation: %d (%s)\n", rot, path->getViaRotationStr());
                break;
            }
            case DEFIPATH_TAPER: {
                printf("    Taper: yes\n");
                break;
            }
            case DEFIPATH_SHAPE: {
                const char* shape = path->getShape();
                printf("    Shape: %s\n", shape);
                break;
            }
        }
    }
    
    printf("  --- Path done (total points: %d) ---\n", point_count);
}

// Partial path callback（模拟 5.8 的 PathCbk）
static int partialPathCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    partial_callback_count++;
    printf("\n[Partial #%d] Net: %s\n", partial_callback_count, net->name());
    
    int num_wires = net->numWires();
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        int num_paths = wire->numPaths();
        
        printf("  Wire #%d: %d paths (already processed: %d)\n", 
               w, num_paths, processed_path_count);
        
        // ⭐ 只处理尚未处理的 path（模拟 5.8 的逐个处理）
        for (int p = processed_path_count; p < num_paths; p++) {
            defiPath* path = wire->path(p);
            
            // ⭐ 完全模仿 5.8：逐个处理每个 path
            processSinglePath(path);
            
            processed_path_count++;
            total_paths_count++;
        }
    }
    
    printf("[Partial #%d] Done. Total processed paths: %d\n\n", 
           partial_callback_count, total_paths_count);
    
    return 0;
}

// SNet callback
static int snetCallback(defrCallbackType_e type, defiNet* net, defiUserData data) {
    printf("\n[SNet Final] Net: %s\n", net->name());
    
    int num_wires = net->numWires();
    
    for (int w = 0; w < num_wires; w++) {
        defiWire* wire = net->wire(w);
        int num_paths = wire->numPaths();
        
        printf("  Wire #%d: %d remaining paths (already processed: %d)\n",
               w, num_paths, processed_path_count);
        
        // 处理剩余的 path
        for (int p = processed_path_count; p < num_paths; p++) {
            defiPath* path = wire->path(p);
            processSinglePath(path);
            processed_path_count++;
            total_paths_count++;
        }
    }
    
    printf("[SNet] Final total paths: %d\n", total_paths_count);
    
    // 重置计数器（为下一个 net）
    processed_path_count = 0;
    
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <def_file>\n", argv[0]);
        return 1;
    }
    
    printf("=== Emulating 5.8 PathCbk using 6.1 SNetPartialPathCbk ===\n");
    printf("File: %s\n\n", argv[1]);
    
    defrInit();
    
    // 注册 callbacks
    defrSetSNetCbk(snetCallback);
    defrSetSNetPartialPathCbk(partialPathCallback);
    
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", argv[1]);
        return 1;
    }
    
    int result = defrRead(f, argv[1], NULL, 0);
    fclose(f);
    
    printf("\n=== Final Summary ===\n");
    printf("Parse result: %d\n", result);
    printf("Total partial callbacks: %d\n", partial_callback_count);
    printf("Total paths processed: %d\n", total_paths_count);
    
    defrClear();
    
    return result;
}
