#include <iostream>
#include <cstdio>
#include <cstring>
#include "defrReader.hpp"
#include "defiComponent.hpp"

static const char* getOverallPlacementType(defiPinPort* port) {
    if (port->isFixed()) return "FIXED";
    if (port->isCover()) return "COVER";
    if (port->isPlaced()) return "PLACED";
    return "UNKNOWN";
}

static const char* getOverallPinPlacementType(defiPin* pin) {
    if (pin->isFixed()) return "FIXED";
    if (pin->isCover()) return "COVER";
    if (pin->isPlaced()) return "PLACED";
    return "UNKNOWN";
}

static const char* placementTypeStr(int type) {
    switch (type) {
        case DEFI_COMPONENT_FIXED: return "FIXED";
        case DEFI_COMPONENT_COVER: return "COVER";
        case DEFI_COMPONENT_PLACED: return "PLACED";
        default: return "UNKNOWN";
    }
}

static void printPinInfo(defiPin* pin) {
    printf("Pin: %s\n", pin->pinName());
    printf("  NumPorts: %d\n", pin->numPorts());
    
    for (int i = 0; i < pin->numPorts(); i++) {
        defiPinPort* port = pin->pinPort(i);
        printf("  Port[%d]:\n", i);
        printf("    NumLayers: %d, NumPolygons: %d\n", port->numLayer(), port->numPolygons());
        
        if (port->hasPlacement()) {
            printf("    Overall: %s (%d %d) %s\n", 
                   getOverallPlacementType(port),
                   port->placementX(), port->placementY(),
                   port->orientStr());
        }
        
        for (int j = 0; j < port->numLayer(); j++) {
            printf("    Layer[%d]: %s\n", j, port->layer(j));
            if (port->hasLayerPlacement(j)) {
                printf("      Placement: %s (%d %d) %s\n",
                       placementTypeStr(port->layerPlacementType(j)),
                       port->layerPlacementX(j), port->layerPlacementY(j),
                       port->layerPlacementOrientStr(j));
            }
        }
        
        for (int j = 0; j < port->numPolygons(); j++) {
            printf("    Polygon[%d]: layer=%s\n", j, port->polygonName(j));
            if (port->hasPolygonPlacement(j)) {
                printf("      Placement: %s (%d %d) %s\n",
                       placementTypeStr(port->polygonPlacementType(j)),
                       port->polygonPlacementX(j), port->polygonPlacementY(j),
                       port->polygonPlacementOrientStr(j));
            }
        }
        
        for (int j = 0; j < port->numVias(); j++) {
            printf("    Via[%d]: %s (%d %d)\n", j, port->viaName(j), port->viaPtX(j), port->viaPtY(j));
        }
    }
    
    printf("  Direct NumLayers: %d, NumPolygons: %d\n", pin->numLayer(), pin->numPolygons());
    
    if (pin->hasPlacement()) {
        printf("  Overall Pin: %s (%d %d) %s\n",
               getOverallPinPlacementType(pin),
               pin->placementX(), pin->placementY(),
               pin->orientStr());
    }
    
    for (int j = 0; j < pin->numLayer(); j++) {
        printf("  Layer[%d]: %s\n", j, pin->layer(j));
        if (pin->hasLayerPlacement(j)) {
            printf("    Placement: %s (%d %d) %s\n",
                   placementTypeStr(pin->layerPlacementType(j)),
                   pin->layerPlacementX(j), pin->layerPlacementY(j),
                   pin->layerPlacementOrientStr(j));
        }
    }
    
    for (int j = 0; j < pin->numPolygons(); j++) {
        printf("  Polygon[%d]: layer=%s\n", j, pin->polygonName(j));
        if (pin->hasPolygonPlacement(j)) {
            printf("    Placement: %s (%d %d) %s\n",
                   placementTypeStr(pin->polygonPlacementType(j)),
                   pin->polygonPlacementX(j), pin->polygonPlacementY(j),
                   pin->polygonPlacementOrientStr(j));
        }
    }
    printf("\n");
}

static int pinCbk(defrCallbackType_e type, defiPin* pin, defiUserData data) {
    if (type != defrPinCbkType) return 0;
    
    const char* name = pin->pinName();
    if (strcmp(name, "P0") == 0 || strcmp(name, "P2") == 0 || 
        strcmp(name, "P8") == 0 || strcmp(name, "P9") == 0 ||
        strcmp(name, "P10") == 0) {
        printPinInfo(pin);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <def_file>\n", argv[0]);
        return 1;
    }
    
    defrInit();
    defrSetPinCbk(pinCbk);
    
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        printf("Cannot open file: %s\n", argv[1]);
        return 1;
    }
    
    int result = defrRead(f, argv[1], nullptr, 1);
    fclose(f);
    
    defrClear();
    return result;
}