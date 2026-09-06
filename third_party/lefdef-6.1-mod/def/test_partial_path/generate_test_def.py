#!/usr/bin/env python3
import sys

def generate_def_with_many_paths(num_nets, paths_per_net, output_file):
    """Generate a DEF file with many nets containing many paths"""
    
    with open(output_file, 'w') as f:
        f.write("VERSION 5.8 ;\n")
        f.write("DESIGN test_design ;\n")
        f.write("DIVIDER \"/\" ;\n")
        f.write("BUSBITCHARS \"[]\" ;\n")
        f.write("UNITS 1000 MICRONS 1 ;\n")
        
        # Die area
        f.write("DIEAREA ( 0 0 ) ( 10000 10000 ) ;\n")
        
        # Components section
        f.write("\nCOMPONENTS 2 ;\n")
        f.write("- inst1 PAD_IN + PLACED ( 0 0 ) N ;\n")
        f.write("- inst2 PAD_OUT + PLACED ( 10000 10000 ) N ;\n")
        f.write("END COMPONENTS\n")
        
        # Nets section
        f.write("\nNETS %d ;\n" % num_nets)
        
        for net_id in range(1, num_nets + 1):
            net_name = "net_%d" % net_id
            f.write("- %s\n" % net_name)
            f.write("  ( inst1 pin1 ) ( inst2 pin2 )\n")
            
            # Generate many ROUTED statements
            for path_id in range(1, paths_per_net + 1):
                layer_num = (path_id % 3) + 1
                layer = "M%d" % layer_num
                width = 140
                x1 = path_id * 100
                y1 = net_id * 100
                x2 = x1 + 1000
                y2 = y1 + 1000
                
                f.write("  + ROUTED %s %d ( %d %d ) ( %d %d )\n" % (layer, width, x1, y1, x2, y2))
                if path_id % 100 == 0:
                    f.write("  NEW %s %d ( %d %d ) ( %d %d )\n" % (layer, width, x2, y2, x2 + 500, y2 + 500))
            
            f.write("  ;\n")
        
        f.write("END NETS\n")
        f.write("\nEND DESIGN\n")
    
    print("Generated %s" % output_file)
    print("  Nets: %d" % num_nets)
    print("  Paths per net: ~%d" % (paths_per_net + paths_per_net // 100))
    print("  Total paths: ~%d" % (num_nets * (paths_per_net + paths_per_net // 100)))

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: %s <num_nets> <paths_per_net> <output_file>" % sys.argv[0])
        print("Example: %s 100 50 test.def" % sys.argv[0])
        sys.exit(1)
    
    num_nets = int(sys.argv[1])
    paths_per_net = int(sys.argv[2])
    output_file = sys.argv[3]
    
    generate_def_with_many_paths(num_nets, paths_per_net, output_file)
