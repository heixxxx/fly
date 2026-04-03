#!/usr/bin/env python3

with open('/root/project/lefdef6.1/def/TEST/test_escape_large.def', 'w') as f:
    f.write("""VERSION 5.8 ;
NAMESCASESENSITIVE ON ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN test_escape ;
UNITS DISTANCE MICRONS 1000 ;

""")

    f.write("COMPONENTS 10000000 ;\n")
    for i in range(1, 10000001):
        if i % 2 == 0:
            f.write(f"- inst_{i}\\[1\\]/module_{i}\\[2\\] MACRO ;\n")
        else:
            f.write(f"- inst_{i} MACRO ;\n")
    f.write("END COMPONENTS\n\n")

    f.write("NETS 10000000 ;\n")
    for i in range(1, 10000001):
        if i % 2 == 0:
            f.write(f"- net_{i}\\[1\\]/subnet_{i}\\[2\\] ;\n")
        else:
            f.write(f"- net_{i} ;\n")
    f.write("END NETS\n\n")

    f.write("END DESIGN\n")

print("Generated test_escape_large.def with 10M components and 10M nets")