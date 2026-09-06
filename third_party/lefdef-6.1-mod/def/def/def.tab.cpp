/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1


/* Substitute the variable and function names.  */
#define yyparse         defyyparse
#define yylex           defyylex
#define yyerror         defyyerror
#define yydebug         defyydebug
#define yynerrs         defyynerrs

/* First part of user prologue.  */
#line 58 "def.y"

#include <stdlib.h>
#include <string.h>
#include "defrReader.hpp"
#include "defiUser.hpp"
#include "defrCallBacks.hpp"
#include "lex.h"

#define DEF_MAX_INT 2147483647
#define YYDEBUG 1     // this is temp fix for pcr 755132 
// TX_DIR:TRANSLATION ON


#include "defrData.hpp"
#include "defrSettings.hpp"
#include "defrCallBacks.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

// Macros to describe how we handle a callback.
// If the function was set then call it.
// If the function returns non zero then there was an error
// so call the error routine and exit.

#define PROCESS_CALLBACK_RETVAL() \
     if (defData->defRetVal == PARSE_OK) { \
     } else if (defData->defRetVal == STOP_PARSE) { \
         return defData->defRetVal; \
     } else { \
         defData->defError(6010, "An error has been reported in callback."); \
         return defData->defRetVal; \
     }

#define CALLBACK(func, typ, data) \
     if (func && !defData->errors) { \
         defData->defRetVal = (*func)(typ, data, defData->session->UserData); \
         PROCESS_CALLBACK_RETVAL(); \
     }

#define CALLBACK2(func, typ, data1, data2) \
     if (func && !defData->errors) { \
         defData->defRetVal = (*func)(typ, data1, data2, defData->session->UserData); \
         PROCESS_CALLBACK_RETVAL(); \
     }

#define CALLBACK3(func, typ, data1, data2, data3) \
     if (func && !defData->errors) { \
         defData->defRetVal = (*func)(typ, data1, data2, data3, defData->session->UserData); \
         PROCESS_CALLBACK_RETVAL(); \
     }

#define CHKERR() \
    if (defData->checkErrors()) { \
      return 1; \
    }

#define CHKPROPTYPE(propType, propName, name) \
    if (propType == 'N') { \
       defData->warningMsg = (char*)malloc(strlen(propName)+strlen(name)+40); \
       sprintf(defData->warningMsg, "The PropName %s is not defined for %s.", \
               propName, name); \
       defData->defWarning(7010, defData->warningMsg); \
       free(defData->warningMsg); \
    }

int yylex(YYSTYPE *pYylval, defrData *defData)
{
    return defData->defyylex(pYylval);
}


void yyerror(defrData *defData, const char *s)
{
    return defData->defyyerror(s);
}




#define FIXED 1
#define COVER 2
#define PLACED 3
#define UNPLACED 4

#line 161 "def.tab.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "def.tab.hpp"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_QSTRING = 3,                    /* QSTRING  */
  YYSYMBOL_T_STRING = 4,                   /* T_STRING  */
  YYSYMBOL_SITE_PATTERN = 5,               /* SITE_PATTERN  */
  YYSYMBOL_NUMBER = 6,                     /* NUMBER  */
  YYSYMBOL_K_HISTORY = 7,                  /* K_HISTORY  */
  YYSYMBOL_K_NAME = 8,                     /* K_NAME  */
  YYSYMBOL_K_NAMESCASESENSITIVE = 9,       /* K_NAMESCASESENSITIVE  */
  YYSYMBOL_K_DESIGN = 10,                  /* K_DESIGN  */
  YYSYMBOL_K_VIAS = 11,                    /* K_VIAS  */
  YYSYMBOL_K_TECH = 12,                    /* K_TECH  */
  YYSYMBOL_K_UNITS = 13,                   /* K_UNITS  */
  YYSYMBOL_K_ARRAY = 14,                   /* K_ARRAY  */
  YYSYMBOL_K_FLOORPLAN = 15,               /* K_FLOORPLAN  */
  YYSYMBOL_K_SITE = 16,                    /* K_SITE  */
  YYSYMBOL_K_CANPLACE = 17,                /* K_CANPLACE  */
  YYSYMBOL_K_CANNOTOCCUPY = 18,            /* K_CANNOTOCCUPY  */
  YYSYMBOL_K_DIEAREA = 19,                 /* K_DIEAREA  */
  YYSYMBOL_K_PINS = 20,                    /* K_PINS  */
  YYSYMBOL_K_PINSHAPE = 21,                /* K_PINSHAPE  */
  YYSYMBOL_K_DEFAULTCAP = 22,              /* K_DEFAULTCAP  */
  YYSYMBOL_K_MINPINS = 23,                 /* K_MINPINS  */
  YYSYMBOL_K_WIRECAP = 24,                 /* K_WIRECAP  */
  YYSYMBOL_K_TRACKS = 25,                  /* K_TRACKS  */
  YYSYMBOL_K_GCELLGRID = 26,               /* K_GCELLGRID  */
  YYSYMBOL_K_DO = 27,                      /* K_DO  */
  YYSYMBOL_K_BY = 28,                      /* K_BY  */
  YYSYMBOL_K_STEP = 29,                    /* K_STEP  */
  YYSYMBOL_K_LAYER = 30,                   /* K_LAYER  */
  YYSYMBOL_K_ROW = 31,                     /* K_ROW  */
  YYSYMBOL_K_RECT = 32,                    /* K_RECT  */
  YYSYMBOL_K_COMPS = 33,                   /* K_COMPS  */
  YYSYMBOL_K_COMP_GEN = 34,                /* K_COMP_GEN  */
  YYSYMBOL_K_SOURCE = 35,                  /* K_SOURCE  */
  YYSYMBOL_K_WEIGHT = 36,                  /* K_WEIGHT  */
  YYSYMBOL_K_EEQMASTER = 37,               /* K_EEQMASTER  */
  YYSYMBOL_K_FIXED = 38,                   /* K_FIXED  */
  YYSYMBOL_K_COVER = 39,                   /* K_COVER  */
  YYSYMBOL_K_UNPLACED = 40,                /* K_UNPLACED  */
  YYSYMBOL_K_PLACED = 41,                  /* K_PLACED  */
  YYSYMBOL_K_FOREIGN = 42,                 /* K_FOREIGN  */
  YYSYMBOL_K_REGION = 43,                  /* K_REGION  */
  YYSYMBOL_K_REGIONS = 44,                 /* K_REGIONS  */
  YYSYMBOL_K_NETS = 45,                    /* K_NETS  */
  YYSYMBOL_K_START_NET = 46,               /* K_START_NET  */
  YYSYMBOL_K_MUSTJOIN = 47,                /* K_MUSTJOIN  */
  YYSYMBOL_K_ORIGINAL = 48,                /* K_ORIGINAL  */
  YYSYMBOL_K_USE = 49,                     /* K_USE  */
  YYSYMBOL_K_STYLE = 50,                   /* K_STYLE  */
  YYSYMBOL_K_PATTERN = 51,                 /* K_PATTERN  */
  YYSYMBOL_K_PATTERNNAME = 52,             /* K_PATTERNNAME  */
  YYSYMBOL_K_ESTCAP = 53,                  /* K_ESTCAP  */
  YYSYMBOL_K_ROUTED = 54,                  /* K_ROUTED  */
  YYSYMBOL_K_NEW = 55,                     /* K_NEW  */
  YYSYMBOL_K_SNETS = 56,                   /* K_SNETS  */
  YYSYMBOL_K_SHAPE = 57,                   /* K_SHAPE  */
  YYSYMBOL_K_WIDTH = 58,                   /* K_WIDTH  */
  YYSYMBOL_K_VOLTAGE = 59,                 /* K_VOLTAGE  */
  YYSYMBOL_K_SPACING = 60,                 /* K_SPACING  */
  YYSYMBOL_K_NONDEFAULTRULE = 61,          /* K_NONDEFAULTRULE  */
  YYSYMBOL_K_NONDEFAULTRULES = 62,         /* K_NONDEFAULTRULES  */
  YYSYMBOL_K_NOFLOPS = 63,                 /* K_NOFLOPS  */
  YYSYMBOL_K_N = 64,                       /* K_N  */
  YYSYMBOL_K_S = 65,                       /* K_S  */
  YYSYMBOL_K_E = 66,                       /* K_E  */
  YYSYMBOL_K_W = 67,                       /* K_W  */
  YYSYMBOL_K_FN = 68,                      /* K_FN  */
  YYSYMBOL_K_FE = 69,                      /* K_FE  */
  YYSYMBOL_K_FS = 70,                      /* K_FS  */
  YYSYMBOL_K_FW = 71,                      /* K_FW  */
  YYSYMBOL_K_GROUPS = 72,                  /* K_GROUPS  */
  YYSYMBOL_K_GROUP = 73,                   /* K_GROUP  */
  YYSYMBOL_K_SOFT = 74,                    /* K_SOFT  */
  YYSYMBOL_K_MAXX = 75,                    /* K_MAXX  */
  YYSYMBOL_K_MAXY = 76,                    /* K_MAXY  */
  YYSYMBOL_K_MAXHALFPERIMETER = 77,        /* K_MAXHALFPERIMETER  */
  YYSYMBOL_K_CONSTRAINTS = 78,             /* K_CONSTRAINTS  */
  YYSYMBOL_K_NET = 79,                     /* K_NET  */
  YYSYMBOL_K_PATH = 80,                    /* K_PATH  */
  YYSYMBOL_K_SUM = 81,                     /* K_SUM  */
  YYSYMBOL_K_DIFF = 82,                    /* K_DIFF  */
  YYSYMBOL_K_SCANCHAINS = 83,              /* K_SCANCHAINS  */
  YYSYMBOL_K_START = 84,                   /* K_START  */
  YYSYMBOL_K_FLOATING = 85,                /* K_FLOATING  */
  YYSYMBOL_K_ORDERED = 86,                 /* K_ORDERED  */
  YYSYMBOL_K_STOP = 87,                    /* K_STOP  */
  YYSYMBOL_K_IN = 88,                      /* K_IN  */
  YYSYMBOL_K_OUT = 89,                     /* K_OUT  */
  YYSYMBOL_K_RISEMIN = 90,                 /* K_RISEMIN  */
  YYSYMBOL_K_RISEMAX = 91,                 /* K_RISEMAX  */
  YYSYMBOL_K_FALLMIN = 92,                 /* K_FALLMIN  */
  YYSYMBOL_K_FALLMAX = 93,                 /* K_FALLMAX  */
  YYSYMBOL_K_WIREDLOGIC = 94,              /* K_WIREDLOGIC  */
  YYSYMBOL_K_MAXDIST = 95,                 /* K_MAXDIST  */
  YYSYMBOL_K_ASSERTIONS = 96,              /* K_ASSERTIONS  */
  YYSYMBOL_K_DISTANCE = 97,                /* K_DISTANCE  */
  YYSYMBOL_K_MICRONS = 98,                 /* K_MICRONS  */
  YYSYMBOL_K_NDR = 99,                     /* K_NDR  */
  YYSYMBOL_K_END = 100,                    /* K_END  */
  YYSYMBOL_K_POWERDOMAIN = 101,            /* K_POWERDOMAIN  */
  YYSYMBOL_K_HINSTS = 102,                 /* K_HINSTS  */
  YYSYMBOL_K_IOTIMINGS = 103,              /* K_IOTIMINGS  */
  YYSYMBOL_K_RISE = 104,                   /* K_RISE  */
  YYSYMBOL_K_FALL = 105,                   /* K_FALL  */
  YYSYMBOL_K_VARIABLE = 106,               /* K_VARIABLE  */
  YYSYMBOL_K_SLEWRATE = 107,               /* K_SLEWRATE  */
  YYSYMBOL_K_CAPACITANCE = 108,            /* K_CAPACITANCE  */
  YYSYMBOL_K_DRIVECELL = 109,              /* K_DRIVECELL  */
  YYSYMBOL_K_FROMPIN = 110,                /* K_FROMPIN  */
  YYSYMBOL_K_TOPIN = 111,                  /* K_TOPIN  */
  YYSYMBOL_K_PARALLEL = 112,               /* K_PARALLEL  */
  YYSYMBOL_K_TIMINGDISABLES = 113,         /* K_TIMINGDISABLES  */
  YYSYMBOL_K_THRUPIN = 114,                /* K_THRUPIN  */
  YYSYMBOL_K_MACRO = 115,                  /* K_MACRO  */
  YYSYMBOL_K_PARTITIONS = 116,             /* K_PARTITIONS  */
  YYSYMBOL_K_TURNOFF = 117,                /* K_TURNOFF  */
  YYSYMBOL_K_COMPONENTS = 118,             /* K_COMPONENTS  */
  YYSYMBOL_K_FROMCLOCKPIN = 119,           /* K_FROMCLOCKPIN  */
  YYSYMBOL_K_FROMCOMPPIN = 120,            /* K_FROMCOMPPIN  */
  YYSYMBOL_K_FROMIOPIN = 121,              /* K_FROMIOPIN  */
  YYSYMBOL_K_TOCLOCKPIN = 122,             /* K_TOCLOCKPIN  */
  YYSYMBOL_K_TOCOMPPIN = 123,              /* K_TOCOMPPIN  */
  YYSYMBOL_K_TOIOPIN = 124,                /* K_TOIOPIN  */
  YYSYMBOL_K_SETUPRISE = 125,              /* K_SETUPRISE  */
  YYSYMBOL_K_SETUPFALL = 126,              /* K_SETUPFALL  */
  YYSYMBOL_K_HOLDRISE = 127,               /* K_HOLDRISE  */
  YYSYMBOL_K_HOLDFALL = 128,               /* K_HOLDFALL  */
  YYSYMBOL_K_VPIN = 129,                   /* K_VPIN  */
  YYSYMBOL_K_SUBNET = 130,                 /* K_SUBNET  */
  YYSYMBOL_K_XTALK = 131,                  /* K_XTALK  */
  YYSYMBOL_K_PIN = 132,                    /* K_PIN  */
  YYSYMBOL_K_SYNTHESIZED = 133,            /* K_SYNTHESIZED  */
  YYSYMBOL_K_IF = 134,                     /* K_IF  */
  YYSYMBOL_K_THEN = 135,                   /* K_THEN  */
  YYSYMBOL_K_ELSE = 136,                   /* K_ELSE  */
  YYSYMBOL_K_FALSE = 137,                  /* K_FALSE  */
  YYSYMBOL_K_TRUE = 138,                   /* K_TRUE  */
  YYSYMBOL_K_EQ = 139,                     /* K_EQ  */
  YYSYMBOL_K_NE = 140,                     /* K_NE  */
  YYSYMBOL_K_LE = 141,                     /* K_LE  */
  YYSYMBOL_K_LT = 142,                     /* K_LT  */
  YYSYMBOL_K_GE = 143,                     /* K_GE  */
  YYSYMBOL_K_GT = 144,                     /* K_GT  */
  YYSYMBOL_K_OR = 145,                     /* K_OR  */
  YYSYMBOL_K_AND = 146,                    /* K_AND  */
  YYSYMBOL_K_NOT = 147,                    /* K_NOT  */
  YYSYMBOL_K_SPECIAL = 148,                /* K_SPECIAL  */
  YYSYMBOL_K_DIRECTION = 149,              /* K_DIRECTION  */
  YYSYMBOL_K_RANGE = 150,                  /* K_RANGE  */
  YYSYMBOL_K_WIRE = 151,                   /* K_WIRE  */
  YYSYMBOL_K_FPC = 152,                    /* K_FPC  */
  YYSYMBOL_K_HORIZONTAL = 153,             /* K_HORIZONTAL  */
  YYSYMBOL_K_VERTICAL = 154,               /* K_VERTICAL  */
  YYSYMBOL_K_ALIGN = 155,                  /* K_ALIGN  */
  YYSYMBOL_K_MIN = 156,                    /* K_MIN  */
  YYSYMBOL_K_MAX = 157,                    /* K_MAX  */
  YYSYMBOL_K_EQUAL = 158,                  /* K_EQUAL  */
  YYSYMBOL_K_BOTTOMLEFT = 159,             /* K_BOTTOMLEFT  */
  YYSYMBOL_K_TOPRIGHT = 160,               /* K_TOPRIGHT  */
  YYSYMBOL_K_ROWS = 161,                   /* K_ROWS  */
  YYSYMBOL_K_TAPER = 162,                  /* K_TAPER  */
  YYSYMBOL_K_TAPERRULE = 163,              /* K_TAPERRULE  */
  YYSYMBOL_K_VERSION = 164,                /* K_VERSION  */
  YYSYMBOL_K_DIVIDERCHAR = 165,            /* K_DIVIDERCHAR  */
  YYSYMBOL_K_BUSBITCHARS = 166,            /* K_BUSBITCHARS  */
  YYSYMBOL_K_PROPERTYDEFINITIONS = 167,    /* K_PROPERTYDEFINITIONS  */
  YYSYMBOL_K_STRING = 168,                 /* K_STRING  */
  YYSYMBOL_K_REAL = 169,                   /* K_REAL  */
  YYSYMBOL_K_INTEGER = 170,                /* K_INTEGER  */
  YYSYMBOL_K_PROPERTY = 171,               /* K_PROPERTY  */
  YYSYMBOL_K_BEGINEXT = 172,               /* K_BEGINEXT  */
  YYSYMBOL_K_ENDEXT = 173,                 /* K_ENDEXT  */
  YYSYMBOL_K_NAMEMAPSTRING = 174,          /* K_NAMEMAPSTRING  */
  YYSYMBOL_K_ON = 175,                     /* K_ON  */
  YYSYMBOL_K_OFF = 176,                    /* K_OFF  */
  YYSYMBOL_K_X = 177,                      /* K_X  */
  YYSYMBOL_K_Y = 178,                      /* K_Y  */
  YYSYMBOL_K_COMPONENT = 179,              /* K_COMPONENT  */
  YYSYMBOL_K_MASK = 180,                   /* K_MASK  */
  YYSYMBOL_K_MASKSHIFT = 181,              /* K_MASKSHIFT  */
  YYSYMBOL_K_COMPSMASKSHIFT = 182,         /* K_COMPSMASKSHIFT  */
  YYSYMBOL_K_SAMEMASK = 183,               /* K_SAMEMASK  */
  YYSYMBOL_K_PINPROPERTIES = 184,          /* K_PINPROPERTIES  */
  YYSYMBOL_K_TEST = 185,                   /* K_TEST  */
  YYSYMBOL_K_ONLYBLOCKS = 186,             /* K_ONLYBLOCKS  */
  YYSYMBOL_K_COMMONSCANPINS = 187,         /* K_COMMONSCANPINS  */
  YYSYMBOL_K_SNET = 188,                   /* K_SNET  */
  YYSYMBOL_K_COMPONENTPIN = 189,           /* K_COMPONENTPIN  */
  YYSYMBOL_K_REENTRANTPATHS = 190,         /* K_REENTRANTPATHS  */
  YYSYMBOL_K_SHIELD = 191,                 /* K_SHIELD  */
  YYSYMBOL_K_SHIELDNET = 192,              /* K_SHIELDNET  */
  YYSYMBOL_K_NOSHIELD = 193,               /* K_NOSHIELD  */
  YYSYMBOL_K_VIRTUAL = 194,                /* K_VIRTUAL  */
  YYSYMBOL_K_ANTENNAPINPARTIALMETALAREA = 195, /* K_ANTENNAPINPARTIALMETALAREA  */
  YYSYMBOL_K_ANTENNAPINPARTIALMETALSIDEAREA = 196, /* K_ANTENNAPINPARTIALMETALSIDEAREA  */
  YYSYMBOL_K_ANTENNAPINGATEAREA = 197,     /* K_ANTENNAPINGATEAREA  */
  YYSYMBOL_K_ANTENNAPINDIFFAREA = 198,     /* K_ANTENNAPINDIFFAREA  */
  YYSYMBOL_K_ANTENNAPINMAXAREACAR = 199,   /* K_ANTENNAPINMAXAREACAR  */
  YYSYMBOL_K_ANTENNAPINMAXSIDEAREACAR = 200, /* K_ANTENNAPINMAXSIDEAREACAR  */
  YYSYMBOL_K_ANTENNAPINPARTIALCUTAREA = 201, /* K_ANTENNAPINPARTIALCUTAREA  */
  YYSYMBOL_K_ANTENNAPINMAXCUTCAR = 202,    /* K_ANTENNAPINMAXCUTCAR  */
  YYSYMBOL_K_SIGNAL = 203,                 /* K_SIGNAL  */
  YYSYMBOL_K_POWER = 204,                  /* K_POWER  */
  YYSYMBOL_K_GROUND = 205,                 /* K_GROUND  */
  YYSYMBOL_K_CLOCK = 206,                  /* K_CLOCK  */
  YYSYMBOL_K_TIEOFF = 207,                 /* K_TIEOFF  */
  YYSYMBOL_K_ANALOG = 208,                 /* K_ANALOG  */
  YYSYMBOL_K_SCAN = 209,                   /* K_SCAN  */
  YYSYMBOL_K_RESET = 210,                  /* K_RESET  */
  YYSYMBOL_K_RING = 211,                   /* K_RING  */
  YYSYMBOL_K_STRIPE = 212,                 /* K_STRIPE  */
  YYSYMBOL_K_FOLLOWPIN = 213,              /* K_FOLLOWPIN  */
  YYSYMBOL_K_IOWIRE = 214,                 /* K_IOWIRE  */
  YYSYMBOL_K_COREWIRE = 215,               /* K_COREWIRE  */
  YYSYMBOL_K_BLOCKWIRE = 216,              /* K_BLOCKWIRE  */
  YYSYMBOL_K_FILLWIRE = 217,               /* K_FILLWIRE  */
  YYSYMBOL_K_BLOCKAGEWIRE = 218,           /* K_BLOCKAGEWIRE  */
  YYSYMBOL_K_PADRING = 219,                /* K_PADRING  */
  YYSYMBOL_K_BLOCKRING = 220,              /* K_BLOCKRING  */
  YYSYMBOL_K_BLOCKAGES = 221,              /* K_BLOCKAGES  */
  YYSYMBOL_K_PLACEMENT = 222,              /* K_PLACEMENT  */
  YYSYMBOL_K_SLOTS = 223,                  /* K_SLOTS  */
  YYSYMBOL_K_FILLS = 224,                  /* K_FILLS  */
  YYSYMBOL_K_PUSHDOWN = 225,               /* K_PUSHDOWN  */
  YYSYMBOL_K_NETLIST = 226,                /* K_NETLIST  */
  YYSYMBOL_K_DIST = 227,                   /* K_DIST  */
  YYSYMBOL_K_USER = 228,                   /* K_USER  */
  YYSYMBOL_K_TIMING = 229,                 /* K_TIMING  */
  YYSYMBOL_K_BALANCED = 230,               /* K_BALANCED  */
  YYSYMBOL_K_STEINER = 231,                /* K_STEINER  */
  YYSYMBOL_K_TRUNK = 232,                  /* K_TRUNK  */
  YYSYMBOL_K_FIXEDBUMP = 233,              /* K_FIXEDBUMP  */
  YYSYMBOL_K_FENCE = 234,                  /* K_FENCE  */
  YYSYMBOL_K_FREQUENCY = 235,              /* K_FREQUENCY  */
  YYSYMBOL_K_GUIDE = 236,                  /* K_GUIDE  */
  YYSYMBOL_K_MAXBITS = 237,                /* K_MAXBITS  */
  YYSYMBOL_K_PARTITION = 238,              /* K_PARTITION  */
  YYSYMBOL_K_TYPE = 239,                   /* K_TYPE  */
  YYSYMBOL_K_ANTENNAMODEL = 240,           /* K_ANTENNAMODEL  */
  YYSYMBOL_K_DRCFILL = 241,                /* K_DRCFILL  */
  YYSYMBOL_K_OXIDE1 = 242,                 /* K_OXIDE1  */
  YYSYMBOL_K_OXIDE2 = 243,                 /* K_OXIDE2  */
  YYSYMBOL_K_OXIDE3 = 244,                 /* K_OXIDE3  */
  YYSYMBOL_K_OXIDE4 = 245,                 /* K_OXIDE4  */
  YYSYMBOL_K_OXIDE5 = 246,                 /* K_OXIDE5  */
  YYSYMBOL_K_OXIDE6 = 247,                 /* K_OXIDE6  */
  YYSYMBOL_K_OXIDE7 = 248,                 /* K_OXIDE7  */
  YYSYMBOL_K_OXIDE8 = 249,                 /* K_OXIDE8  */
  YYSYMBOL_K_OXIDE9 = 250,                 /* K_OXIDE9  */
  YYSYMBOL_K_OXIDE10 = 251,                /* K_OXIDE10  */
  YYSYMBOL_K_OXIDE11 = 252,                /* K_OXIDE11  */
  YYSYMBOL_K_OXIDE12 = 253,                /* K_OXIDE12  */
  YYSYMBOL_K_OXIDE13 = 254,                /* K_OXIDE13  */
  YYSYMBOL_K_OXIDE14 = 255,                /* K_OXIDE14  */
  YYSYMBOL_K_OXIDE15 = 256,                /* K_OXIDE15  */
  YYSYMBOL_K_OXIDE16 = 257,                /* K_OXIDE16  */
  YYSYMBOL_K_OXIDE17 = 258,                /* K_OXIDE17  */
  YYSYMBOL_K_OXIDE18 = 259,                /* K_OXIDE18  */
  YYSYMBOL_K_OXIDE19 = 260,                /* K_OXIDE19  */
  YYSYMBOL_K_OXIDE20 = 261,                /* K_OXIDE20  */
  YYSYMBOL_K_OXIDE21 = 262,                /* K_OXIDE21  */
  YYSYMBOL_K_OXIDE22 = 263,                /* K_OXIDE22  */
  YYSYMBOL_K_OXIDE23 = 264,                /* K_OXIDE23  */
  YYSYMBOL_K_OXIDE24 = 265,                /* K_OXIDE24  */
  YYSYMBOL_K_OXIDE25 = 266,                /* K_OXIDE25  */
  YYSYMBOL_K_OXIDE26 = 267,                /* K_OXIDE26  */
  YYSYMBOL_K_OXIDE27 = 268,                /* K_OXIDE27  */
  YYSYMBOL_K_OXIDE28 = 269,                /* K_OXIDE28  */
  YYSYMBOL_K_OXIDE29 = 270,                /* K_OXIDE29  */
  YYSYMBOL_K_OXIDE30 = 271,                /* K_OXIDE30  */
  YYSYMBOL_K_OXIDE31 = 272,                /* K_OXIDE31  */
  YYSYMBOL_K_OXIDE32 = 273,                /* K_OXIDE32  */
  YYSYMBOL_K_CUTSIZE = 274,                /* K_CUTSIZE  */
  YYSYMBOL_K_CUTSPACING = 275,             /* K_CUTSPACING  */
  YYSYMBOL_K_DESIGNRULEWIDTH = 276,        /* K_DESIGNRULEWIDTH  */
  YYSYMBOL_K_DIAGWIDTH = 277,              /* K_DIAGWIDTH  */
  YYSYMBOL_K_ENCLOSURE = 278,              /* K_ENCLOSURE  */
  YYSYMBOL_K_HALO = 279,                   /* K_HALO  */
  YYSYMBOL_K_GROUNDSENSITIVITY = 280,      /* K_GROUNDSENSITIVITY  */
  YYSYMBOL_K_PHYSICAL = 281,               /* K_PHYSICAL  */
  YYSYMBOL_K_HARDSPACING = 282,            /* K_HARDSPACING  */
  YYSYMBOL_K_LAYERS = 283,                 /* K_LAYERS  */
  YYSYMBOL_K_MINCUTS = 284,                /* K_MINCUTS  */
  YYSYMBOL_K_NETEXPR = 285,                /* K_NETEXPR  */
  YYSYMBOL_K_PINPROPERTY = 286,            /* K_PINPROPERTY  */
  YYSYMBOL_K_OFFSET = 287,                 /* K_OFFSET  */
  YYSYMBOL_K_ORIGIN = 288,                 /* K_ORIGIN  */
  YYSYMBOL_K_ROWCOL = 289,                 /* K_ROWCOL  */
  YYSYMBOL_K_STYLES = 290,                 /* K_STYLES  */
  YYSYMBOL_K_SOFTFIXED = 291,              /* K_SOFTFIXED  */
  YYSYMBOL_K_POLYGON = 292,                /* K_POLYGON  */
  YYSYMBOL_K_PORT = 293,                   /* K_PORT  */
  YYSYMBOL_K_SUPPLYSENSITIVITY = 294,      /* K_SUPPLYSENSITIVITY  */
  YYSYMBOL_K_VIA = 295,                    /* K_VIA  */
  YYSYMBOL_K_VIARULE = 296,                /* K_VIARULE  */
  YYSYMBOL_K_WIREEXT = 297,                /* K_WIREEXT  */
  YYSYMBOL_K_EXCEPTPGNET = 298,            /* K_EXCEPTPGNET  */
  YYSYMBOL_K_ONLYPGNET = 299,              /* K_ONLYPGNET  */
  YYSYMBOL_K_FILLWIREOPC = 300,            /* K_FILLWIREOPC  */
  YYSYMBOL_K_OPC = 301,                    /* K_OPC  */
  YYSYMBOL_K_PARTIAL = 302,                /* K_PARTIAL  */
  YYSYMBOL_K_ROUTEHALO = 303,              /* K_ROUTEHALO  */
  YYSYMBOL_K_BLOCKAGE = 304,               /* K_BLOCKAGE  */
  YYSYMBOL_K_ROUTE = 305,                  /* K_ROUTE  */
  YYSYMBOL_K_SCANCHAIN = 306,              /* K_SCANCHAIN  */
  YYSYMBOL_K_SPECIALROUTE = 307,           /* K_SPECIALROUTE  */
  YYSYMBOL_K_TRACK = 308,                  /* K_TRACK  */
  YYSYMBOL_309_ = 309,                     /* ';'  */
  YYSYMBOL_310_ = 310,                     /* '-'  */
  YYSYMBOL_311_ = 311,                     /* '+'  */
  YYSYMBOL_312_ = 312,                     /* '('  */
  YYSYMBOL_313_ = 313,                     /* ')'  */
  YYSYMBOL_314_ = 314,                     /* '*'  */
  YYSYMBOL_315_ = 315,                     /* ','  */
  YYSYMBOL_YYACCEPT = 316,                 /* $accept  */
  YYSYMBOL_def_file = 317,                 /* def_file  */
  YYSYMBOL_version_stmt = 318,             /* version_stmt  */
  YYSYMBOL_319_1 = 319,                    /* $@1  */
  YYSYMBOL_case_sens_stmt = 320,           /* case_sens_stmt  */
  YYSYMBOL_rules = 321,                    /* rules  */
  YYSYMBOL_rule = 322,                     /* rule  */
  YYSYMBOL_design_section = 323,           /* design_section  */
  YYSYMBOL_design_name = 324,              /* design_name  */
  YYSYMBOL_325_2 = 325,                    /* $@2  */
  YYSYMBOL_end_design = 326,               /* end_design  */
  YYSYMBOL_tech_name = 327,                /* tech_name  */
  YYSYMBOL_328_3 = 328,                    /* $@3  */
  YYSYMBOL_array_name = 329,               /* array_name  */
  YYSYMBOL_330_4 = 330,                    /* $@4  */
  YYSYMBOL_floorplan_name = 331,           /* floorplan_name  */
  YYSYMBOL_332_5 = 332,                    /* $@5  */
  YYSYMBOL_history = 333,                  /* history  */
  YYSYMBOL_prop_def_section = 334,         /* prop_def_section  */
  YYSYMBOL_335_6 = 335,                    /* $@6  */
  YYSYMBOL_property_defs = 336,            /* property_defs  */
  YYSYMBOL_property_def = 337,             /* property_def  */
  YYSYMBOL_338_7 = 338,                    /* $@7  */
  YYSYMBOL_339_8 = 339,                    /* $@8  */
  YYSYMBOL_340_9 = 340,                    /* $@9  */
  YYSYMBOL_341_10 = 341,                   /* $@10  */
  YYSYMBOL_342_11 = 342,                   /* $@11  */
  YYSYMBOL_343_12 = 343,                   /* $@12  */
  YYSYMBOL_344_13 = 344,                   /* $@13  */
  YYSYMBOL_345_14 = 345,                   /* $@14  */
  YYSYMBOL_346_15 = 346,                   /* $@15  */
  YYSYMBOL_347_16 = 347,                   /* $@16  */
  YYSYMBOL_348_17 = 348,                   /* $@17  */
  YYSYMBOL_349_18 = 349,                   /* $@18  */
  YYSYMBOL_350_19 = 350,                   /* $@19  */
  YYSYMBOL_351_20 = 351,                   /* $@20  */
  YYSYMBOL_352_21 = 352,                   /* $@21  */
  YYSYMBOL_353_22 = 353,                   /* $@22  */
  YYSYMBOL_354_23 = 354,                   /* $@23  */
  YYSYMBOL_property_type_and_val = 355,    /* property_type_and_val  */
  YYSYMBOL_356_24 = 356,                   /* $@24  */
  YYSYMBOL_357_25 = 357,                   /* $@25  */
  YYSYMBOL_opt_num_val = 358,              /* opt_num_val  */
  YYSYMBOL_units = 359,                    /* units  */
  YYSYMBOL_divider_char = 360,             /* divider_char  */
  YYSYMBOL_bus_bit_chars = 361,            /* bus_bit_chars  */
  YYSYMBOL_canplace = 362,                 /* canplace  */
  YYSYMBOL_363_26 = 363,                   /* $@26  */
  YYSYMBOL_cannotoccupy = 364,             /* cannotoccupy  */
  YYSYMBOL_365_27 = 365,                   /* $@27  */
  YYSYMBOL_orient = 366,                   /* orient  */
  YYSYMBOL_die_area = 367,                 /* die_area  */
  YYSYMBOL_368_28 = 368,                   /* $@28  */
  YYSYMBOL_pin_cap_rule = 369,             /* pin_cap_rule  */
  YYSYMBOL_start_def_cap = 370,            /* start_def_cap  */
  YYSYMBOL_pin_caps = 371,                 /* pin_caps  */
  YYSYMBOL_pin_cap = 372,                  /* pin_cap  */
  YYSYMBOL_end_def_cap = 373,              /* end_def_cap  */
  YYSYMBOL_pin_rule = 374,                 /* pin_rule  */
  YYSYMBOL_start_pins = 375,               /* start_pins  */
  YYSYMBOL_pins = 376,                     /* pins  */
  YYSYMBOL_pin = 377,                      /* pin  */
  YYSYMBOL_378_29 = 378,                   /* $@29  */
  YYSYMBOL_379_30 = 379,                   /* $@30  */
  YYSYMBOL_380_31 = 380,                   /* $@31  */
  YYSYMBOL_pin_options = 381,              /* pin_options  */
  YYSYMBOL_pin_option = 382,               /* pin_option  */
  YYSYMBOL_383_32 = 383,                   /* $@32  */
  YYSYMBOL_384_33 = 384,                   /* $@33  */
  YYSYMBOL_385_34 = 385,                   /* $@34  */
  YYSYMBOL_386_35 = 386,                   /* $@35  */
  YYSYMBOL_387_36 = 387,                   /* $@36  */
  YYSYMBOL_388_37 = 388,                   /* $@37  */
  YYSYMBOL_389_38 = 389,                   /* $@38  */
  YYSYMBOL_390_39 = 390,                   /* $@39  */
  YYSYMBOL_391_40 = 391,                   /* $@40  */
  YYSYMBOL_392_41 = 392,                   /* $@41  */
  YYSYMBOL_393_42 = 393,                   /* $@42  */
  YYSYMBOL_pin_prop_name_values = 394,     /* pin_prop_name_values  */
  YYSYMBOL_prop_name_value = 395,          /* prop_name_value  */
  YYSYMBOL_396_43 = 396,                   /* $@43  */
  YYSYMBOL_prop_name_value_pair = 397,     /* prop_name_value_pair  */
  YYSYMBOL_net_prop_name_values = 398,     /* net_prop_name_values  */
  YYSYMBOL_prop_string_value = 399,        /* prop_string_value  */
  YYSYMBOL_via_orient = 400,               /* via_orient  */
  YYSYMBOL_pin_layer_mask_opt = 401,       /* pin_layer_mask_opt  */
  YYSYMBOL_pin_via_mask_opt = 402,         /* pin_via_mask_opt  */
  YYSYMBOL_pin_poly_mask_opt = 403,        /* pin_poly_mask_opt  */
  YYSYMBOL_pin_layer_spacing_opt = 404,    /* pin_layer_spacing_opt  */
  YYSYMBOL_pin_layer_props_opt = 405,      /* pin_layer_props_opt  */
  YYSYMBOL_pin_layer_props = 406,          /* pin_layer_props  */
  YYSYMBOL_pin_layer_prop = 407,           /* pin_layer_prop  */
  YYSYMBOL_408_44 = 408,                   /* $@44  */
  YYSYMBOL_pin_poly_props_opt = 409,       /* pin_poly_props_opt  */
  YYSYMBOL_pin_poly_props = 410,           /* pin_poly_props  */
  YYSYMBOL_pin_poly_prop = 411,            /* pin_poly_prop  */
  YYSYMBOL_412_45 = 412,                   /* $@45  */
  YYSYMBOL_pin_via_props_opt = 413,        /* pin_via_props_opt  */
  YYSYMBOL_pin_via_props = 414,            /* pin_via_props  */
  YYSYMBOL_pin_via_prop = 415,             /* pin_via_prop  */
  YYSYMBOL_416_46 = 416,                   /* $@46  */
  YYSYMBOL_pin_layer_spacing = 417,        /* pin_layer_spacing  */
  YYSYMBOL_pin_poly_spacing_opt = 418,     /* pin_poly_spacing_opt  */
  YYSYMBOL_pin_poly_spacing = 419,         /* pin_poly_spacing  */
  YYSYMBOL_pin_oxide = 420,                /* pin_oxide  */
  YYSYMBOL_use_type = 421,                 /* use_type  */
  YYSYMBOL_pin_layer_opt = 422,            /* pin_layer_opt  */
  YYSYMBOL_423_47 = 423,                   /* $@47  */
  YYSYMBOL_end_pins = 424,                 /* end_pins  */
  YYSYMBOL_row_rule = 425,                 /* row_rule  */
  YYSYMBOL_426_48 = 426,                   /* $@48  */
  YYSYMBOL_427_49 = 427,                   /* $@49  */
  YYSYMBOL_row_do_option = 428,            /* row_do_option  */
  YYSYMBOL_row_step_option = 429,          /* row_step_option  */
  YYSYMBOL_row_options = 430,              /* row_options  */
  YYSYMBOL_row_option = 431,               /* row_option  */
  YYSYMBOL_432_50 = 432,                   /* $@50  */
  YYSYMBOL_row_prop_list = 433,            /* row_prop_list  */
  YYSYMBOL_row_prop = 434,                 /* row_prop  */
  YYSYMBOL_tracks_rule = 435,              /* tracks_rule  */
  YYSYMBOL_436_51 = 436,                   /* $@51  */
  YYSYMBOL_track_start = 437,              /* track_start  */
  YYSYMBOL_track_type = 438,               /* track_type  */
  YYSYMBOL_track_opts = 439,               /* track_opts  */
  YYSYMBOL_track_opt_property_statements = 440, /* track_opt_property_statements  */
  YYSYMBOL_track_property_statements = 441, /* track_property_statements  */
  YYSYMBOL_track_property_statement = 442, /* track_property_statement  */
  YYSYMBOL_443_52 = 443,                   /* $@52  */
  YYSYMBOL_track_ndr_statement = 444,      /* track_ndr_statement  */
  YYSYMBOL_445_53 = 445,                   /* $@53  */
  YYSYMBOL_track_width_statement = 446,    /* track_width_statement  */
  YYSYMBOL_track_mask_statement = 447,     /* track_mask_statement  */
  YYSYMBOL_same_mask = 448,                /* same_mask  */
  YYSYMBOL_track_layer_statement = 449,    /* track_layer_statement  */
  YYSYMBOL_450_54 = 450,                   /* $@54  */
  YYSYMBOL_track_layers = 451,             /* track_layers  */
  YYSYMBOL_track_layer = 452,              /* track_layer  */
  YYSYMBOL_gcellgrid = 453,                /* gcellgrid  */
  YYSYMBOL_extension_section = 454,        /* extension_section  */
  YYSYMBOL_extension_stmt = 455,           /* extension_stmt  */
  YYSYMBOL_via_section = 456,              /* via_section  */
  YYSYMBOL_via = 457,                      /* via  */
  YYSYMBOL_via_declarations = 458,         /* via_declarations  */
  YYSYMBOL_via_declaration = 459,          /* via_declaration  */
  YYSYMBOL_460_55 = 460,                   /* $@55  */
  YYSYMBOL_461_56 = 461,                   /* $@56  */
  YYSYMBOL_layer_stmts = 462,              /* layer_stmts  */
  YYSYMBOL_layer_stmt = 463,               /* layer_stmt  */
  YYSYMBOL_464_57 = 464,                   /* $@57  */
  YYSYMBOL_465_58 = 465,                   /* $@58  */
  YYSYMBOL_466_59 = 466,                   /* $@59  */
  YYSYMBOL_467_60 = 467,                   /* $@60  */
  YYSYMBOL_468_61 = 468,                   /* $@61  */
  YYSYMBOL_469_62 = 469,                   /* $@62  */
  YYSYMBOL_470_63 = 470,                   /* $@63  */
  YYSYMBOL_layer_viarule_opts = 471,       /* layer_viarule_opts  */
  YYSYMBOL_472_64 = 472,                   /* $@64  */
  YYSYMBOL_firstPt = 473,                  /* firstPt  */
  YYSYMBOL_nextPt = 474,                   /* nextPt  */
  YYSYMBOL_otherPts = 475,                 /* otherPts  */
  YYSYMBOL_pt = 476,                       /* pt  */
  YYSYMBOL_mask = 477,                     /* mask  */
  YYSYMBOL_via_end = 478,                  /* via_end  */
  YYSYMBOL_regions_section = 479,          /* regions_section  */
  YYSYMBOL_regions_start = 480,            /* regions_start  */
  YYSYMBOL_regions_stmts = 481,            /* regions_stmts  */
  YYSYMBOL_regions_stmt = 482,             /* regions_stmt  */
  YYSYMBOL_483_65 = 483,                   /* $@65  */
  YYSYMBOL_484_66 = 484,                   /* $@66  */
  YYSYMBOL_rect_list = 485,                /* rect_list  */
  YYSYMBOL_region_options = 486,           /* region_options  */
  YYSYMBOL_region_option = 487,            /* region_option  */
  YYSYMBOL_488_67 = 488,                   /* $@67  */
  YYSYMBOL_region_prop_list = 489,         /* region_prop_list  */
  YYSYMBOL_region_prop = 490,              /* region_prop  */
  YYSYMBOL_region_type = 491,              /* region_type  */
  YYSYMBOL_comps_maskShift_section = 492,  /* comps_maskShift_section  */
  YYSYMBOL_493_68 = 493,                   /* $@68  */
  YYSYMBOL_comps_section = 494,            /* comps_section  */
  YYSYMBOL_start_comps = 495,              /* start_comps  */
  YYSYMBOL_layer_statement = 496,          /* layer_statement  */
  YYSYMBOL_maskLayer = 497,                /* maskLayer  */
  YYSYMBOL_comps_rule = 498,               /* comps_rule  */
  YYSYMBOL_comp = 499,                     /* comp  */
  YYSYMBOL_comp_start = 500,               /* comp_start  */
  YYSYMBOL_comp_id_and_name = 501,         /* comp_id_and_name  */
  YYSYMBOL_502_69 = 502,                   /* $@69  */
  YYSYMBOL_comp_net_list = 503,            /* comp_net_list  */
  YYSYMBOL_comp_options = 504,             /* comp_options  */
  YYSYMBOL_comp_option = 505,              /* comp_option  */
  YYSYMBOL_comp_extension_stmt = 506,      /* comp_extension_stmt  */
  YYSYMBOL_comp_eeq = 507,                 /* comp_eeq  */
  YYSYMBOL_508_70 = 508,                   /* $@70  */
  YYSYMBOL_comp_generate = 509,            /* comp_generate  */
  YYSYMBOL_510_71 = 510,                   /* $@71  */
  YYSYMBOL_opt_pattern = 511,              /* opt_pattern  */
  YYSYMBOL_comp_source = 512,              /* comp_source  */
  YYSYMBOL_source_type = 513,              /* source_type  */
  YYSYMBOL_comp_region = 514,              /* comp_region  */
  YYSYMBOL_comp_pnt_list = 515,            /* comp_pnt_list  */
  YYSYMBOL_comp_halo = 516,                /* comp_halo  */
  YYSYMBOL_517_72 = 517,                   /* $@72  */
  YYSYMBOL_halo_soft = 518,                /* halo_soft  */
  YYSYMBOL_comp_routehalo = 519,           /* comp_routehalo  */
  YYSYMBOL_520_73 = 520,                   /* $@73  */
  YYSYMBOL_comp_property = 521,            /* comp_property  */
  YYSYMBOL_522_74 = 522,                   /* $@74  */
  YYSYMBOL_comp_prop_list = 523,           /* comp_prop_list  */
  YYSYMBOL_comp_prop = 524,                /* comp_prop  */
  YYSYMBOL_comp_region_start = 525,        /* comp_region_start  */
  YYSYMBOL_comp_foreign = 526,             /* comp_foreign  */
  YYSYMBOL_527_75 = 527,                   /* $@75  */
  YYSYMBOL_opt_paren = 528,                /* opt_paren  */
  YYSYMBOL_comp_type = 529,                /* comp_type  */
  YYSYMBOL_maskShift = 530,                /* maskShift  */
  YYSYMBOL_531_76 = 531,                   /* $@76  */
  YYSYMBOL_placement_status = 532,         /* placement_status  */
  YYSYMBOL_comp_pinprop = 533,             /* comp_pinprop  */
  YYSYMBOL_534_77 = 534,                   /* $@77  */
  YYSYMBOL_comp_physical = 535,            /* comp_physical  */
  YYSYMBOL_weight = 536,                   /* weight  */
  YYSYMBOL_end_comps = 537,                /* end_comps  */
  YYSYMBOL_nets_section = 538,             /* nets_section  */
  YYSYMBOL_start_nets = 539,               /* start_nets  */
  YYSYMBOL_net_rules = 540,                /* net_rules  */
  YYSYMBOL_one_net = 541,                  /* one_net  */
  YYSYMBOL_net_and_connections = 542,      /* net_and_connections  */
  YYSYMBOL_net_start = 543,                /* net_start  */
  YYSYMBOL_544_78 = 544,                   /* $@78  */
  YYSYMBOL_net_name = 545,                 /* net_name  */
  YYSYMBOL_546_79 = 546,                   /* $@79  */
  YYSYMBOL_547_80 = 547,                   /* $@80  */
  YYSYMBOL_net_connections = 548,          /* net_connections  */
  YYSYMBOL_net_connection = 549,           /* net_connection  */
  YYSYMBOL_550_81 = 550,                   /* $@81  */
  YYSYMBOL_551_82 = 551,                   /* $@82  */
  YYSYMBOL_552_83 = 552,                   /* $@83  */
  YYSYMBOL_conn_opt = 553,                 /* conn_opt  */
  YYSYMBOL_net_options = 554,              /* net_options  */
  YYSYMBOL_net_option = 555,               /* net_option  */
  YYSYMBOL_556_84 = 556,                   /* $@84  */
  YYSYMBOL_557_85 = 557,                   /* $@85  */
  YYSYMBOL_558_86 = 558,                   /* $@86  */
  YYSYMBOL_559_87 = 559,                   /* $@87  */
  YYSYMBOL_560_88 = 560,                   /* $@88  */
  YYSYMBOL_561_89 = 561,                   /* $@89  */
  YYSYMBOL_562_90 = 562,                   /* $@90  */
  YYSYMBOL_563_91 = 563,                   /* $@91  */
  YYSYMBOL_564_92 = 564,                   /* $@92  */
  YYSYMBOL_565_93 = 565,                   /* $@93  */
  YYSYMBOL_netsource_type = 566,           /* netsource_type  */
  YYSYMBOL_vpin_stmt = 567,                /* vpin_stmt  */
  YYSYMBOL_568_94 = 568,                   /* $@94  */
  YYSYMBOL_vpin_begin = 569,               /* vpin_begin  */
  YYSYMBOL_570_95 = 570,                   /* $@95  */
  YYSYMBOL_vpin_layer_opt = 571,           /* vpin_layer_opt  */
  YYSYMBOL_572_96 = 572,                   /* $@96  */
  YYSYMBOL_vpin_options = 573,             /* vpin_options  */
  YYSYMBOL_vpin_status = 574,              /* vpin_status  */
  YYSYMBOL_net_type = 575,                 /* net_type  */
  YYSYMBOL_opt_wire = 576,                 /* opt_wire  */
  YYSYMBOL_577_97 = 577,                   /* $@97  */
  YYSYMBOL_opt_paths = 578,                /* opt_paths  */
  YYSYMBOL_paths = 579,                    /* paths  */
  YYSYMBOL_580_98 = 580,                   /* $@98  */
  YYSYMBOL_new_path = 581,                 /* new_path  */
  YYSYMBOL_582_99 = 582,                   /* $@99  */
  YYSYMBOL_path = 583,                     /* path  */
  YYSYMBOL_584_100 = 584,                  /* $@100  */
  YYSYMBOL_585_101 = 585,                  /* $@101  */
  YYSYMBOL_virtual_statement = 586,        /* virtual_statement  */
  YYSYMBOL_rect_statement = 587,           /* rect_statement  */
  YYSYMBOL_path_item_list_opt = 588,       /* path_item_list_opt  */
  YYSYMBOL_path_item = 589,                /* path_item  */
  YYSYMBOL_590_102 = 590,                  /* $@102  */
  YYSYMBOL_mask_number = 591,              /* mask_number  */
  YYSYMBOL_wire_width = 592,               /* wire_width  */
  YYSYMBOL_path_pt = 593,                  /* path_pt  */
  YYSYMBOL_virtual_pt = 594,               /* virtual_pt  */
  YYSYMBOL_rect_pts = 595,                 /* rect_pts  */
  YYSYMBOL_opt_taper_style_s = 596,        /* opt_taper_style_s  */
  YYSYMBOL_opt_taper_style = 597,          /* opt_taper_style  */
  YYSYMBOL_opt_prop = 598,                 /* opt_prop  */
  YYSYMBOL_599_103 = 599,                  /* $@103  */
  YYSYMBOL_opt_shield = 600,               /* opt_shield  */
  YYSYMBOL_opt_taper = 601,                /* opt_taper  */
  YYSYMBOL_602_104 = 602,                  /* $@104  */
  YYSYMBOL_opt_style = 603,                /* opt_style  */
  YYSYMBOL_opt_spaths = 604,               /* opt_spaths  */
  YYSYMBOL_opt_shape_style_prop = 605,     /* opt_shape_style_prop  */
  YYSYMBOL_606_105 = 606,                  /* $@105  */
  YYSYMBOL_607_106 = 607,                  /* $@106  */
  YYSYMBOL_end_nets = 608,                 /* end_nets  */
  YYSYMBOL_shape_type = 609,               /* shape_type  */
  YYSYMBOL_snets_section = 610,            /* snets_section  */
  YYSYMBOL_snet_rules = 611,               /* snet_rules  */
  YYSYMBOL_snet_rule = 612,                /* snet_rule  */
  YYSYMBOL_snet_options = 613,             /* snet_options  */
  YYSYMBOL_snet_option = 614,              /* snet_option  */
  YYSYMBOL_snet_other_option = 615,        /* snet_other_option  */
  YYSYMBOL_616_107 = 616,                  /* $@107  */
  YYSYMBOL_617_108 = 617,                  /* $@108  */
  YYSYMBOL_618_109 = 618,                  /* $@109  */
  YYSYMBOL_619_110 = 619,                  /* $@110  */
  YYSYMBOL_620_111 = 620,                  /* $@111  */
  YYSYMBOL_621_112 = 621,                  /* $@112  */
  YYSYMBOL_622_113 = 622,                  /* $@113  */
  YYSYMBOL_623_114 = 623,                  /* $@114  */
  YYSYMBOL_snet_type = 624,                /* snet_type  */
  YYSYMBOL_625_115 = 625,                  /* $@115  */
  YYSYMBOL_orient_pt = 626,                /* orient_pt  */
  YYSYMBOL_snet_width = 627,               /* snet_width  */
  YYSYMBOL_628_116 = 628,                  /* $@116  */
  YYSYMBOL_snet_voltage = 629,             /* snet_voltage  */
  YYSYMBOL_630_117 = 630,                  /* $@117  */
  YYSYMBOL_snet_spacing = 631,             /* snet_spacing  */
  YYSYMBOL_632_118 = 632,                  /* $@118  */
  YYSYMBOL_633_119 = 633,                  /* $@119  */
  YYSYMBOL_opt_snet_range = 634,           /* opt_snet_range  */
  YYSYMBOL_opt_range = 635,                /* opt_range  */
  YYSYMBOL_nets_pattern_type = 636,        /* nets_pattern_type  */
  YYSYMBOL_snets_pattern_type = 637,       /* snets_pattern_type  */
  YYSYMBOL_opt_swire = 638,                /* opt_swire  */
  YYSYMBOL_639_120 = 639,                  /* $@120  */
  YYSYMBOL_spaths = 640,                   /* spaths  */
  YYSYMBOL_641_121 = 641,                  /* $@121  */
  YYSYMBOL_snew_path = 642,                /* snew_path  */
  YYSYMBOL_643_122 = 643,                  /* $@122  */
  YYSYMBOL_spath = 644,                    /* spath  */
  YYSYMBOL_645_123 = 645,                  /* $@123  */
  YYSYMBOL_646_124 = 646,                  /* $@124  */
  YYSYMBOL_width = 647,                    /* width  */
  YYSYMBOL_start_snets = 648,              /* start_snets  */
  YYSYMBOL_end_snets = 649,                /* end_snets  */
  YYSYMBOL_groups_section = 650,           /* groups_section  */
  YYSYMBOL_groups_start = 651,             /* groups_start  */
  YYSYMBOL_group_rules = 652,              /* group_rules  */
  YYSYMBOL_group_rule = 653,               /* group_rule  */
  YYSYMBOL_start_group = 654,              /* start_group  */
  YYSYMBOL_655_125 = 655,                  /* $@125  */
  YYSYMBOL_group_members = 656,            /* group_members  */
  YYSYMBOL_group_member = 657,             /* group_member  */
  YYSYMBOL_group_options = 658,            /* group_options  */
  YYSYMBOL_group_option = 659,             /* group_option  */
  YYSYMBOL_660_126 = 660,                  /* $@126  */
  YYSYMBOL_661_127 = 661,                  /* $@127  */
  YYSYMBOL_662_128 = 662,                  /* $@128  */
  YYSYMBOL_663_129 = 663,                  /* $@129  */
  YYSYMBOL_664_130 = 664,                  /* $@130  */
  YYSYMBOL_group_hinsts = 665,             /* group_hinsts  */
  YYSYMBOL_group_hinst = 666,              /* group_hinst  */
  YYSYMBOL_group_components = 667,         /* group_components  */
  YYSYMBOL_group_component = 668,          /* group_component  */
  YYSYMBOL_group_groups = 669,             /* group_groups  */
  YYSYMBOL_group_group = 670,              /* group_group  */
  YYSYMBOL_group_region = 671,             /* group_region  */
  YYSYMBOL_group_prop_list = 672,          /* group_prop_list  */
  YYSYMBOL_group_prop = 673,               /* group_prop  */
  YYSYMBOL_group_soft_options = 674,       /* group_soft_options  */
  YYSYMBOL_group_soft_option = 675,        /* group_soft_option  */
  YYSYMBOL_groups_end = 676,               /* groups_end  */
  YYSYMBOL_assertions_section = 677,       /* assertions_section  */
  YYSYMBOL_constraint_section = 678,       /* constraint_section  */
  YYSYMBOL_assertions_start = 679,         /* assertions_start  */
  YYSYMBOL_constraints_start = 680,        /* constraints_start  */
  YYSYMBOL_constraint_rules = 681,         /* constraint_rules  */
  YYSYMBOL_constraint_rule = 682,          /* constraint_rule  */
  YYSYMBOL_operand_rule = 683,             /* operand_rule  */
  YYSYMBOL_operand = 684,                  /* operand  */
  YYSYMBOL_685_131 = 685,                  /* $@131  */
  YYSYMBOL_686_132 = 686,                  /* $@132  */
  YYSYMBOL_operand_list = 687,             /* operand_list  */
  YYSYMBOL_wiredlogic_rule = 688,          /* wiredlogic_rule  */
  YYSYMBOL_689_133 = 689,                  /* $@133  */
  YYSYMBOL_opt_plus = 690,                 /* opt_plus  */
  YYSYMBOL_delay_specs = 691,              /* delay_specs  */
  YYSYMBOL_delay_spec = 692,               /* delay_spec  */
  YYSYMBOL_constraints_end = 693,          /* constraints_end  */
  YYSYMBOL_assertions_end = 694,           /* assertions_end  */
  YYSYMBOL_scanchains_section = 695,       /* scanchains_section  */
  YYSYMBOL_scanchain_start = 696,          /* scanchain_start  */
  YYSYMBOL_scanchain_rules = 697,          /* scanchain_rules  */
  YYSYMBOL_scan_rule = 698,                /* scan_rule  */
  YYSYMBOL_start_scan = 699,               /* start_scan  */
  YYSYMBOL_700_134 = 700,                  /* $@134  */
  YYSYMBOL_scan_members = 701,             /* scan_members  */
  YYSYMBOL_opt_pin = 702,                  /* opt_pin  */
  YYSYMBOL_scan_member = 703,              /* scan_member  */
  YYSYMBOL_704_135 = 704,                  /* $@135  */
  YYSYMBOL_705_136 = 705,                  /* $@136  */
  YYSYMBOL_706_137 = 706,                  /* $@137  */
  YYSYMBOL_707_138 = 707,                  /* $@138  */
  YYSYMBOL_708_139 = 708,                  /* $@139  */
  YYSYMBOL_709_140 = 709,                  /* $@140  */
  YYSYMBOL_710_141 = 710,                  /* $@141  */
  YYSYMBOL_711_142 = 711,                  /* $@142  */
  YYSYMBOL_712_143 = 712,                  /* $@143  */
  YYSYMBOL_713_144 = 713,                  /* $@144  */
  YYSYMBOL_opt_common_pins = 714,          /* opt_common_pins  */
  YYSYMBOL_floating_inst_list = 715,       /* floating_inst_list  */
  YYSYMBOL_one_floating_inst = 716,        /* one_floating_inst  */
  YYSYMBOL_717_145 = 717,                  /* $@145  */
  YYSYMBOL_floating_pins = 718,            /* floating_pins  */
  YYSYMBOL_ordered_inst_list_opt = 719,    /* ordered_inst_list_opt  */
  YYSYMBOL_ordered_inst_list = 720,        /* ordered_inst_list  */
  YYSYMBOL_one_ordered_inst = 721,         /* one_ordered_inst  */
  YYSYMBOL_722_146 = 722,                  /* $@146  */
  YYSYMBOL_723_147 = 723,                  /* $@147  */
  YYSYMBOL_ordered_pins = 724,             /* ordered_pins  */
  YYSYMBOL_partition_maxbits = 725,        /* partition_maxbits  */
  YYSYMBOL_scanchain_end = 726,            /* scanchain_end  */
  YYSYMBOL_iotiming_section = 727,         /* iotiming_section  */
  YYSYMBOL_iotiming_start = 728,           /* iotiming_start  */
  YYSYMBOL_iotiming_rules = 729,           /* iotiming_rules  */
  YYSYMBOL_iotiming_rule = 730,            /* iotiming_rule  */
  YYSYMBOL_start_iotiming = 731,           /* start_iotiming  */
  YYSYMBOL_732_148 = 732,                  /* $@148  */
  YYSYMBOL_iotiming_members = 733,         /* iotiming_members  */
  YYSYMBOL_iotiming_member = 734,          /* iotiming_member  */
  YYSYMBOL_735_149 = 735,                  /* $@149  */
  YYSYMBOL_736_150 = 736,                  /* $@150  */
  YYSYMBOL_iotiming_drivecell_opt = 737,   /* iotiming_drivecell_opt  */
  YYSYMBOL_738_151 = 738,                  /* $@151  */
  YYSYMBOL_739_152 = 739,                  /* $@152  */
  YYSYMBOL_iotiming_frompin = 740,         /* iotiming_frompin  */
  YYSYMBOL_741_153 = 741,                  /* $@153  */
  YYSYMBOL_iotiming_parallel = 742,        /* iotiming_parallel  */
  YYSYMBOL_risefall = 743,                 /* risefall  */
  YYSYMBOL_iotiming_end = 744,             /* iotiming_end  */
  YYSYMBOL_floorplan_contraints_section = 745, /* floorplan_contraints_section  */
  YYSYMBOL_fp_start = 746,                 /* fp_start  */
  YYSYMBOL_fp_stmts = 747,                 /* fp_stmts  */
  YYSYMBOL_fp_stmt = 748,                  /* fp_stmt  */
  YYSYMBOL_749_154 = 749,                  /* $@154  */
  YYSYMBOL_750_155 = 750,                  /* $@155  */
  YYSYMBOL_h_or_v = 751,                   /* h_or_v  */
  YYSYMBOL_constraint_type = 752,          /* constraint_type  */
  YYSYMBOL_constrain_what_list = 753,      /* constrain_what_list  */
  YYSYMBOL_constrain_what = 754,           /* constrain_what  */
  YYSYMBOL_755_156 = 755,                  /* $@156  */
  YYSYMBOL_756_157 = 756,                  /* $@157  */
  YYSYMBOL_row_or_comp_list = 757,         /* row_or_comp_list  */
  YYSYMBOL_row_or_comp = 758,              /* row_or_comp  */
  YYSYMBOL_759_158 = 759,                  /* $@158  */
  YYSYMBOL_760_159 = 760,                  /* $@159  */
  YYSYMBOL_timingdisables_section = 761,   /* timingdisables_section  */
  YYSYMBOL_timingdisables_start = 762,     /* timingdisables_start  */
  YYSYMBOL_timingdisables_rules = 763,     /* timingdisables_rules  */
  YYSYMBOL_timingdisables_rule = 764,      /* timingdisables_rule  */
  YYSYMBOL_765_160 = 765,                  /* $@160  */
  YYSYMBOL_766_161 = 766,                  /* $@161  */
  YYSYMBOL_767_162 = 767,                  /* $@162  */
  YYSYMBOL_768_163 = 768,                  /* $@163  */
  YYSYMBOL_td_macro_option = 769,          /* td_macro_option  */
  YYSYMBOL_770_164 = 770,                  /* $@164  */
  YYSYMBOL_771_165 = 771,                  /* $@165  */
  YYSYMBOL_772_166 = 772,                  /* $@166  */
  YYSYMBOL_timingdisables_end = 773,       /* timingdisables_end  */
  YYSYMBOL_partitions_section = 774,       /* partitions_section  */
  YYSYMBOL_partitions_start = 775,         /* partitions_start  */
  YYSYMBOL_partition_rules = 776,          /* partition_rules  */
  YYSYMBOL_partition_rule = 777,           /* partition_rule  */
  YYSYMBOL_start_partition = 778,          /* start_partition  */
  YYSYMBOL_779_167 = 779,                  /* $@167  */
  YYSYMBOL_turnoff = 780,                  /* turnoff  */
  YYSYMBOL_turnoff_setup = 781,            /* turnoff_setup  */
  YYSYMBOL_turnoff_hold = 782,             /* turnoff_hold  */
  YYSYMBOL_partition_members = 783,        /* partition_members  */
  YYSYMBOL_partition_member = 784,         /* partition_member  */
  YYSYMBOL_785_168 = 785,                  /* $@168  */
  YYSYMBOL_786_169 = 786,                  /* $@169  */
  YYSYMBOL_787_170 = 787,                  /* $@170  */
  YYSYMBOL_788_171 = 788,                  /* $@171  */
  YYSYMBOL_789_172 = 789,                  /* $@172  */
  YYSYMBOL_790_173 = 790,                  /* $@173  */
  YYSYMBOL_minmaxpins = 791,               /* minmaxpins  */
  YYSYMBOL_792_174 = 792,                  /* $@174  */
  YYSYMBOL_min_or_max_list = 793,          /* min_or_max_list  */
  YYSYMBOL_min_or_max_member = 794,        /* min_or_max_member  */
  YYSYMBOL_pin_list = 795,                 /* pin_list  */
  YYSYMBOL_risefallminmax1_list = 796,     /* risefallminmax1_list  */
  YYSYMBOL_risefallminmax1 = 797,          /* risefallminmax1  */
  YYSYMBOL_risefallminmax2_list = 798,     /* risefallminmax2_list  */
  YYSYMBOL_risefallminmax2 = 799,          /* risefallminmax2  */
  YYSYMBOL_partitions_end = 800,           /* partitions_end  */
  YYSYMBOL_comp_names = 801,               /* comp_names  */
  YYSYMBOL_comp_name = 802,                /* comp_name  */
  YYSYMBOL_803_175 = 803,                  /* $@175  */
  YYSYMBOL_subnet_opt_syn = 804,           /* subnet_opt_syn  */
  YYSYMBOL_subnet_options = 805,           /* subnet_options  */
  YYSYMBOL_subnet_option = 806,            /* subnet_option  */
  YYSYMBOL_807_176 = 807,                  /* $@176  */
  YYSYMBOL_808_177 = 808,                  /* $@177  */
  YYSYMBOL_subnet_type = 809,              /* subnet_type  */
  YYSYMBOL_pin_props_section = 810,        /* pin_props_section  */
  YYSYMBOL_begin_pin_props = 811,          /* begin_pin_props  */
  YYSYMBOL_opt_semi = 812,                 /* opt_semi  */
  YYSYMBOL_end_pin_props = 813,            /* end_pin_props  */
  YYSYMBOL_pin_prop_list = 814,            /* pin_prop_list  */
  YYSYMBOL_pin_prop_terminal = 815,        /* pin_prop_terminal  */
  YYSYMBOL_816_178 = 816,                  /* $@178  */
  YYSYMBOL_817_179 = 817,                  /* $@179  */
  YYSYMBOL_pin_prop_options = 818,         /* pin_prop_options  */
  YYSYMBOL_pin_prop = 819,                 /* pin_prop  */
  YYSYMBOL_820_180 = 820,                  /* $@180  */
  YYSYMBOL_pin_prop_name_value_list = 821, /* pin_prop_name_value_list  */
  YYSYMBOL_pin_prop_name_value = 822,      /* pin_prop_name_value  */
  YYSYMBOL_blockage_section = 823,         /* blockage_section  */
  YYSYMBOL_blockage_start = 824,           /* blockage_start  */
  YYSYMBOL_blockage_end = 825,             /* blockage_end  */
  YYSYMBOL_blockage_defs = 826,            /* blockage_defs  */
  YYSYMBOL_blockage_def = 827,             /* blockage_def  */
  YYSYMBOL_blockage_rule = 828,            /* blockage_rule  */
  YYSYMBOL_829_181 = 829,                  /* $@181  */
  YYSYMBOL_830_182 = 830,                  /* $@182  */
  YYSYMBOL_831_183 = 831,                  /* $@183  */
  YYSYMBOL_layer_blockage_rules = 832,     /* layer_blockage_rules  */
  YYSYMBOL_layer_blockage_rule = 833,      /* layer_blockage_rule  */
  YYSYMBOL_834_184 = 834,                  /* $@184  */
  YYSYMBOL_835_185 = 835,                  /* $@185  */
  YYSYMBOL_comp_blockage_rule = 836,       /* comp_blockage_rule  */
  YYSYMBOL_837_186 = 837,                  /* $@186  */
  YYSYMBOL_placement_comp_rules = 838,     /* placement_comp_rules  */
  YYSYMBOL_placement_comp_rule = 839,      /* placement_comp_rule  */
  YYSYMBOL_840_187 = 840,                  /* $@187  */
  YYSYMBOL_841_188 = 841,                  /* $@188  */
  YYSYMBOL_842_189 = 842,                  /* $@189  */
  YYSYMBOL_rectPoly_blockage_rules = 843,  /* rectPoly_blockage_rules  */
  YYSYMBOL_rectPoly_blockage = 844,        /* rectPoly_blockage  */
  YYSYMBOL_845_190 = 845,                  /* $@190  */
  YYSYMBOL_slot_section = 846,             /* slot_section  */
  YYSYMBOL_slot_start = 847,               /* slot_start  */
  YYSYMBOL_slot_end = 848,                 /* slot_end  */
  YYSYMBOL_slot_defs = 849,                /* slot_defs  */
  YYSYMBOL_slot_def = 850,                 /* slot_def  */
  YYSYMBOL_slot_rule = 851,                /* slot_rule  */
  YYSYMBOL_852_191 = 852,                  /* $@191  */
  YYSYMBOL_853_192 = 853,                  /* $@192  */
  YYSYMBOL_geom_slot_rules = 854,          /* geom_slot_rules  */
  YYSYMBOL_geom_slot = 855,                /* geom_slot  */
  YYSYMBOL_856_193 = 856,                  /* $@193  */
  YYSYMBOL_fill_section = 857,             /* fill_section  */
  YYSYMBOL_fill_start = 858,               /* fill_start  */
  YYSYMBOL_fill_end = 859,                 /* fill_end  */
  YYSYMBOL_fill_defs = 860,                /* fill_defs  */
  YYSYMBOL_fill_def = 861,                 /* fill_def  */
  YYSYMBOL_862_194 = 862,                  /* $@194  */
  YYSYMBOL_863_195 = 863,                  /* $@195  */
  YYSYMBOL_864_196 = 864,                  /* $@196  */
  YYSYMBOL_865_197 = 865,                  /* $@197  */
  YYSYMBOL_geom_fill_rules = 866,          /* geom_fill_rules  */
  YYSYMBOL_geom_fill = 867,                /* geom_fill  */
  YYSYMBOL_868_198 = 868,                  /* $@198  */
  YYSYMBOL_fill_layer_mask_opc_opt = 869,  /* fill_layer_mask_opc_opt  */
  YYSYMBOL_opt_mask_opc_l = 870,           /* opt_mask_opc_l  */
  YYSYMBOL_fill_layer_opc = 871,           /* fill_layer_opc  */
  YYSYMBOL_firstViaPt = 872,               /* firstViaPt  */
  YYSYMBOL_nextViaPt = 873,                /* nextViaPt  */
  YYSYMBOL_otherViaPts = 874,              /* otherViaPts  */
  YYSYMBOL_fill_via_orient = 875,          /* fill_via_orient  */
  YYSYMBOL_fill_via_mask_opc_opt = 876,    /* fill_via_mask_opc_opt  */
  YYSYMBOL_opt_mask_opc = 877,             /* opt_mask_opc  */
  YYSYMBOL_fill_via_prop = 878,            /* fill_via_prop  */
  YYSYMBOL_879_199 = 879,                  /* $@199  */
  YYSYMBOL_fill_layer_prop = 880,          /* fill_layer_prop  */
  YYSYMBOL_881_200 = 881,                  /* $@200  */
  YYSYMBOL_fill_via_opc = 882,             /* fill_via_opc  */
  YYSYMBOL_fill_mask = 883,                /* fill_mask  */
  YYSYMBOL_fill_viaMask = 884,             /* fill_viaMask  */
  YYSYMBOL_nondefaultrule_section = 885,   /* nondefaultrule_section  */
  YYSYMBOL_nondefault_start = 886,         /* nondefault_start  */
  YYSYMBOL_nondefault_end = 887,           /* nondefault_end  */
  YYSYMBOL_nondefault_defs = 888,          /* nondefault_defs  */
  YYSYMBOL_nondefault_def = 889,           /* nondefault_def  */
  YYSYMBOL_890_201 = 890,                  /* $@201  */
  YYSYMBOL_891_202 = 891,                  /* $@202  */
  YYSYMBOL_nondefault_options = 892,       /* nondefault_options  */
  YYSYMBOL_nondefault_option = 893,        /* nondefault_option  */
  YYSYMBOL_894_203 = 894,                  /* $@203  */
  YYSYMBOL_895_204 = 895,                  /* $@204  */
  YYSYMBOL_896_205 = 896,                  /* $@205  */
  YYSYMBOL_897_206 = 897,                  /* $@206  */
  YYSYMBOL_898_207 = 898,                  /* $@207  */
  YYSYMBOL_nondefault_layer_options = 899, /* nondefault_layer_options  */
  YYSYMBOL_nondefault_layer_option = 900,  /* nondefault_layer_option  */
  YYSYMBOL_nondefault_prop_opt = 901,      /* nondefault_prop_opt  */
  YYSYMBOL_902_208 = 902,                  /* $@208  */
  YYSYMBOL_nondefault_prop_list = 903,     /* nondefault_prop_list  */
  YYSYMBOL_nondefault_prop = 904,          /* nondefault_prop  */
  YYSYMBOL_styles_section = 905,           /* styles_section  */
  YYSYMBOL_styles_start = 906,             /* styles_start  */
  YYSYMBOL_styles_end = 907,               /* styles_end  */
  YYSYMBOL_styles_rules = 908,             /* styles_rules  */
  YYSYMBOL_styles_rule = 909,              /* styles_rule  */
  YYSYMBOL_910_209 = 910                   /* $@209  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  5
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1672

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  316
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  595
/* YYNRULES -- Number of rules.  */
#define YYNRULES  1107
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  1865

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   563


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int16 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     312,   313,   314,   311,   315,   310,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,   309,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   239,   239,   242,   243,   243,   281,   282,   295,   313,
     314,   315,   318,   318,   318,   318,   319,   319,   319,   319,
     320,   320,   320,   321,   321,   321,   322,   322,   322,   323,
     323,   323,   323,   324,   327,   327,   327,   327,   328,   328,
     328,   329,   329,   329,   330,   330,   330,   331,   331,   331,
     331,   336,   336,   343,   368,   368,   374,   374,   380,   380,
     386,   393,   392,   404,   405,   408,   408,   417,   417,   426,
     426,   435,   435,   444,   444,   453,   453,   462,   462,   473,
     472,   483,   482,   504,   503,   520,   519,   536,   535,   552,
     551,   568,   567,   584,   583,   600,   599,   616,   615,   631,
     633,   633,   638,   638,   644,   649,   654,   660,   661,   664,
     672,   679,   686,   686,   698,   698,   711,   712,   713,   714,
     715,   716,   717,   718,   721,   720,   733,   736,   748,   749,
     752,   763,   766,   769,   775,   776,   779,   780,   781,   779,
     794,   795,   797,   803,   809,   816,   815,   829,   854,   854,
     874,   874,   894,   898,   922,   923,   922,   948,   949,   948,
     991,   991,  1029,  1054,  1080,  1098,  1116,  1134,  1152,  1152,
    1170,  1170,  1189,  1207,  1207,  1225,  1242,  1243,  1254,  1254,
    1262,  1275,  1283,  1284,  1289,  1293,  1298,  1301,  1311,  1312,
    1326,  1327,  1334,  1335,  1347,  1348,  1352,  1353,  1356,  1357,
    1361,  1360,  1387,  1388,  1391,  1392,  1396,  1395,  1422,  1423,
    1426,  1427,  1431,  1430,  1458,  1485,  1513,  1514,  1519,  1546,
    1574,  1579,  1584,  1589,  1594,  1599,  1604,  1609,  1614,  1619,
    1624,  1629,  1634,  1639,  1644,  1649,  1654,  1659,  1664,  1669,
    1674,  1679,  1684,  1689,  1694,  1699,  1704,  1709,  1714,  1719,
    1724,  1729,  1736,  1738,  1740,  1742,  1744,  1746,  1748,  1750,
    1755,  1756,  1756,  1759,  1765,  1767,  1765,  1781,  1791,  1829,
    1832,  1840,  1841,  1844,  1844,  1848,  1849,  1852,  1864,  1873,
    1884,  1883,  1917,  1922,  1924,  1928,  1935,  1936,  1940,  1941,
    1945,  1944,  1965,  1966,  1966,  1979,  1980,  1993,  1994,  2005,
    2006,  2009,  2010,  2010,  2013,  2014,  2017,  2023,  2052,  2063,
    2072,  2075,  2081,  2082,  2085,  2086,  2085,  2097,  2098,  2101,
    2101,  2109,  2108,  2128,  2129,  2128,  2155,  2155,  2165,  2167,
    2165,  2190,  2191,  2197,  2209,  2221,  2233,  2233,  2246,  2249,
    2252,  2253,  2256,  2263,  2269,  2275,  2282,  2283,  2286,  2292,
    2298,  2304,  2305,  2308,  2309,  2308,  2318,  2321,  2326,  2327,
    2330,  2330,  2333,  2348,  2349,  2352,  2367,  2376,  2386,  2388,
    2392,  2391,  2416,  2419,  2429,  2430,  2433,  2440,  2441,  2444,
    2454,  2460,  2460,  2468,  2469,  2474,  2480,  2481,  2484,  2484,
    2484,  2484,  2484,  2484,  2484,  2485,  2485,  2485,  2485,  2486,
    2486,  2486,  2489,  2496,  2496,  2502,  2502,  2510,  2511,  2514,
    2525,  2527,  2529,  2531,  2536,  2538,  2549,  2560,  2573,  2572,
    2594,  2595,  2615,  2615,  2635,  2635,  2639,  2640,  2643,  2654,
    2663,  2673,  2676,  2676,  2691,  2693,  2696,  2703,  2710,  2725,
    2725,  2734,  2736,  2738,  2740,  2753,  2753,  2770,  2783,  2794,
    2805,  2808,  2822,  2823,  2827,  2845,  2850,  2849,  2860,  2859,
    2873,  2873,  2881,  2882,  2885,  2885,  2899,  2899,  2906,  2906,
    2915,  2916,  2923,  2936,  2937,  2941,  2940,  2953,  2964,  2981,
    2981,  2999,  2999,  3009,  3012,  3015,  3026,  3037,  3040,  3043,
    3043,  3054,  3056,  3057,  3056,  3087,  3098,  3108,  3086,  3123,
    3122,  3131,  3137,  3139,  3141,  3143,  3145,  3149,  3148,  3159,
    3159,  3172,  3173,  3173,  3176,  3177,  3180,  3182,  3184,  3187,
    3192,  3197,  3202,  3216,  3216,  3247,  3248,  3267,  3267,  3287,
    3291,  3290,  3316,  3337,  3315,  3356,  3373,  3391,  3393,  3398,
    3409,  3424,  3432,  3444,  3468,  3499,  3528,  3552,  3553,  3555,
    3554,  3569,  3570,  3580,  3591,  3592,  3604,  3614,  3623,  3632,
    3640,  3650,  3660,  3670,  3681,  3691,  3700,  3709,  3719,  3728,
    3729,  3732,  3733,  3733,  3733,  3738,  3737,  3759,  3772,  3783,
    3783,  3795,  3819,  3820,  3824,  3832,  3860,  3859,  3882,  3881,
    3899,  3912,  3914,  3916,  3918,  3920,  3922,  3924,  3926,  3942,
    3944,  3954,  3956,  3959,  3962,  3963,  3967,  3986,  3987,  3991,
    3991,  3992,  3992,  3996,  3995,  4008,  4013,  4021,  4020,  4029,
    4030,  4029,  4092,  4092,  4161,  4162,  4161,  4213,  4224,  4227,
    4230,  4230,  4241,  4244,  4247,  4257,  4260,  4273,  4276,  4282,
    4288,  4294,  4294,  4307,  4308,  4312,  4312,  4321,  4321,  4339,
    4340,  4339,  4347,  4348,  4353,  4354,  4358,  4368,  4370,  4372,
    4383,  4393,  4395,  4397,  4408,  4409,  4409,  4483,  4483,  4520,
    4524,  4523,  4567,  4576,  4566,  4596,  4603,  4616,  4628,  4631,
    4637,  4638,  4641,  4647,  4647,  4658,  4659,  4662,  4669,  4670,
    4673,  4675,  4675,  4678,  4678,  4680,  4685,  4699,  4698,  4716,
    4715,  4733,  4732,  4749,  4750,  4753,  4760,  4761,  4764,  4771,
    4772,  4775,  4782,  4792,  4797,  4798,  4801,  4812,  4821,  4831,
    4832,  4835,  4843,  4851,  4860,  4867,  4871,  4874,  4888,  4902,
    4903,  4906,  4907,  4917,  4930,  4930,  4935,  4935,  4940,  4945,
    4951,  4952,  4954,  4956,  4956,  4965,  4966,  4969,  4970,  4973,
    4978,  4983,  4988,  4994,  5005,  5016,  5019,  5025,  5026,  5029,
    5035,  5035,  5044,  5045,  5050,  5051,  5054,  5054,  5062,  5061,
    5076,  5075,  5089,  5089,  5096,  5096,  5105,  5105,  5125,  5132,
    5136,  5131,  5162,  5163,  5162,  5186,  5187,  5196,  5210,  5211,
    5215,  5214,  5224,  5225,  5238,  5259,  5290,  5291,  5295,  5296,
    5300,  5303,  5300,  5318,  5319,  5332,  5353,  5385,  5386,  5389,
    5398,  5401,  5412,  5413,  5416,  5422,  5422,  5428,  5429,  5433,
    5438,  5443,  5448,  5449,  5448,  5457,  5464,  5465,  5463,  5471,
    5472,  5472,  5478,  5479,  5485,  5485,  5487,  5493,  5499,  5505,
    5506,  5509,  5510,  5509,  5514,  5516,  5519,  5521,  5523,  5525,
    5528,  5529,  5533,  5532,  5536,  5535,  5540,  5541,  5543,  5543,
    5545,  5545,  5548,  5552,  5559,  5560,  5563,  5564,  5563,  5572,
    5572,  5580,  5580,  5588,  5594,  5595,  5594,  5600,  5600,  5606,
    5613,  5616,  5623,  5624,  5627,  5633,  5633,  5639,  5640,  5647,
    5648,  5650,  5654,  5655,  5657,  5660,  5661,  5664,  5664,  5670,
    5670,  5676,  5676,  5682,  5682,  5688,  5688,  5694,  5694,  5699,
    5707,  5706,  5710,  5711,  5714,  5719,  5725,  5726,  5729,  5730,
    5732,  5734,  5736,  5738,  5742,  5743,  5746,  5749,  5752,  5755,
    5759,  5763,  5764,  5767,  5767,  5776,  5777,  5782,  5783,  5786,
    5785,  5803,  5803,  5806,  5808,  5810,  5812,  5815,  5817,  5832,
    5833,  5836,  5840,  5841,  5844,  5845,  5844,  5854,  5855,  5857,
    5857,  5861,  5862,  5865,  5876,  5885,  5895,  5897,  5901,  5905,
    5906,  5909,  5918,  5919,  5918,  5938,  5937,  5955,  5956,  5959,
    5985,  6012,  6019,  6019,  6031,  6044,  6043,  6063,  6068,  6068,
    6087,  6111,  6132,  6150,  6181,  6207,  6208,  6213,  6213,  6231,
    6231,  6244,  6243,  6263,  6281,  6299,  6337,  6356,  6397,  6414,
    6415,  6418,  6424,  6423,  6449,  6451,  6462,  6467,  6468,  6471,
    6479,  6480,  6479,  6487,  6488,  6491,  6497,  6496,  6509,  6511,
    6515,  6519,  6520,  6523,  6524,  6523,  6538,  6539,  6538,  6558,
    6559,  6562,  6576,  6575,  6602,  6603,  6605,  6606,  6607,  6612,
    6631,  6637,  6643,  6644,  6648,  6651,  6662,  6663,  6666,  6667,
    6668,  6672,  6671,  6693,  6692,  6715,  6735,  6745,  6755,  6758,
    6776,  6780,  6781,  6784,  6785,  6784,  6795,  6796,  6799,  6804,
    6806,  6804,  6813,  6813,  6819,  6819,  6825,  6825,  6831,  6834,
    6835,  6838,  6844,  6850,  6858,  6858,  6862,  6863,  6866,  6877,
    6886,  6897,  6899,  6920,  6924,  6925,  6929,  6928
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "QSTRING", "T_STRING",
  "SITE_PATTERN", "NUMBER", "K_HISTORY", "K_NAME", "K_NAMESCASESENSITIVE",
  "K_DESIGN", "K_VIAS", "K_TECH", "K_UNITS", "K_ARRAY", "K_FLOORPLAN",
  "K_SITE", "K_CANPLACE", "K_CANNOTOCCUPY", "K_DIEAREA", "K_PINS",
  "K_PINSHAPE", "K_DEFAULTCAP", "K_MINPINS", "K_WIRECAP", "K_TRACKS",
  "K_GCELLGRID", "K_DO", "K_BY", "K_STEP", "K_LAYER", "K_ROW", "K_RECT",
  "K_COMPS", "K_COMP_GEN", "K_SOURCE", "K_WEIGHT", "K_EEQMASTER",
  "K_FIXED", "K_COVER", "K_UNPLACED", "K_PLACED", "K_FOREIGN", "K_REGION",
  "K_REGIONS", "K_NETS", "K_START_NET", "K_MUSTJOIN", "K_ORIGINAL",
  "K_USE", "K_STYLE", "K_PATTERN", "K_PATTERNNAME", "K_ESTCAP", "K_ROUTED",
  "K_NEW", "K_SNETS", "K_SHAPE", "K_WIDTH", "K_VOLTAGE", "K_SPACING",
  "K_NONDEFAULTRULE", "K_NONDEFAULTRULES", "K_NOFLOPS", "K_N", "K_S",
  "K_E", "K_W", "K_FN", "K_FE", "K_FS", "K_FW", "K_GROUPS", "K_GROUP",
  "K_SOFT", "K_MAXX", "K_MAXY", "K_MAXHALFPERIMETER", "K_CONSTRAINTS",
  "K_NET", "K_PATH", "K_SUM", "K_DIFF", "K_SCANCHAINS", "K_START",
  "K_FLOATING", "K_ORDERED", "K_STOP", "K_IN", "K_OUT", "K_RISEMIN",
  "K_RISEMAX", "K_FALLMIN", "K_FALLMAX", "K_WIREDLOGIC", "K_MAXDIST",
  "K_ASSERTIONS", "K_DISTANCE", "K_MICRONS", "K_NDR", "K_END",
  "K_POWERDOMAIN", "K_HINSTS", "K_IOTIMINGS", "K_RISE", "K_FALL",
  "K_VARIABLE", "K_SLEWRATE", "K_CAPACITANCE", "K_DRIVECELL", "K_FROMPIN",
  "K_TOPIN", "K_PARALLEL", "K_TIMINGDISABLES", "K_THRUPIN", "K_MACRO",
  "K_PARTITIONS", "K_TURNOFF", "K_COMPONENTS", "K_FROMCLOCKPIN",
  "K_FROMCOMPPIN", "K_FROMIOPIN", "K_TOCLOCKPIN", "K_TOCOMPPIN",
  "K_TOIOPIN", "K_SETUPRISE", "K_SETUPFALL", "K_HOLDRISE", "K_HOLDFALL",
  "K_VPIN", "K_SUBNET", "K_XTALK", "K_PIN", "K_SYNTHESIZED", "K_IF",
  "K_THEN", "K_ELSE", "K_FALSE", "K_TRUE", "K_EQ", "K_NE", "K_LE", "K_LT",
  "K_GE", "K_GT", "K_OR", "K_AND", "K_NOT", "K_SPECIAL", "K_DIRECTION",
  "K_RANGE", "K_WIRE", "K_FPC", "K_HORIZONTAL", "K_VERTICAL", "K_ALIGN",
  "K_MIN", "K_MAX", "K_EQUAL", "K_BOTTOMLEFT", "K_TOPRIGHT", "K_ROWS",
  "K_TAPER", "K_TAPERRULE", "K_VERSION", "K_DIVIDERCHAR", "K_BUSBITCHARS",
  "K_PROPERTYDEFINITIONS", "K_STRING", "K_REAL", "K_INTEGER", "K_PROPERTY",
  "K_BEGINEXT", "K_ENDEXT", "K_NAMEMAPSTRING", "K_ON", "K_OFF", "K_X",
  "K_Y", "K_COMPONENT", "K_MASK", "K_MASKSHIFT", "K_COMPSMASKSHIFT",
  "K_SAMEMASK", "K_PINPROPERTIES", "K_TEST", "K_ONLYBLOCKS",
  "K_COMMONSCANPINS", "K_SNET", "K_COMPONENTPIN", "K_REENTRANTPATHS",
  "K_SHIELD", "K_SHIELDNET", "K_NOSHIELD", "K_VIRTUAL",
  "K_ANTENNAPINPARTIALMETALAREA", "K_ANTENNAPINPARTIALMETALSIDEAREA",
  "K_ANTENNAPINGATEAREA", "K_ANTENNAPINDIFFAREA", "K_ANTENNAPINMAXAREACAR",
  "K_ANTENNAPINMAXSIDEAREACAR", "K_ANTENNAPINPARTIALCUTAREA",
  "K_ANTENNAPINMAXCUTCAR", "K_SIGNAL", "K_POWER", "K_GROUND", "K_CLOCK",
  "K_TIEOFF", "K_ANALOG", "K_SCAN", "K_RESET", "K_RING", "K_STRIPE",
  "K_FOLLOWPIN", "K_IOWIRE", "K_COREWIRE", "K_BLOCKWIRE", "K_FILLWIRE",
  "K_BLOCKAGEWIRE", "K_PADRING", "K_BLOCKRING", "K_BLOCKAGES",
  "K_PLACEMENT", "K_SLOTS", "K_FILLS", "K_PUSHDOWN", "K_NETLIST", "K_DIST",
  "K_USER", "K_TIMING", "K_BALANCED", "K_STEINER", "K_TRUNK",
  "K_FIXEDBUMP", "K_FENCE", "K_FREQUENCY", "K_GUIDE", "K_MAXBITS",
  "K_PARTITION", "K_TYPE", "K_ANTENNAMODEL", "K_DRCFILL", "K_OXIDE1",
  "K_OXIDE2", "K_OXIDE3", "K_OXIDE4", "K_OXIDE5", "K_OXIDE6", "K_OXIDE7",
  "K_OXIDE8", "K_OXIDE9", "K_OXIDE10", "K_OXIDE11", "K_OXIDE12",
  "K_OXIDE13", "K_OXIDE14", "K_OXIDE15", "K_OXIDE16", "K_OXIDE17",
  "K_OXIDE18", "K_OXIDE19", "K_OXIDE20", "K_OXIDE21", "K_OXIDE22",
  "K_OXIDE23", "K_OXIDE24", "K_OXIDE25", "K_OXIDE26", "K_OXIDE27",
  "K_OXIDE28", "K_OXIDE29", "K_OXIDE30", "K_OXIDE31", "K_OXIDE32",
  "K_CUTSIZE", "K_CUTSPACING", "K_DESIGNRULEWIDTH", "K_DIAGWIDTH",
  "K_ENCLOSURE", "K_HALO", "K_GROUNDSENSITIVITY", "K_PHYSICAL",
  "K_HARDSPACING", "K_LAYERS", "K_MINCUTS", "K_NETEXPR", "K_PINPROPERTY",
  "K_OFFSET", "K_ORIGIN", "K_ROWCOL", "K_STYLES", "K_SOFTFIXED",
  "K_POLYGON", "K_PORT", "K_SUPPLYSENSITIVITY", "K_VIA", "K_VIARULE",
  "K_WIREEXT", "K_EXCEPTPGNET", "K_ONLYPGNET", "K_FILLWIREOPC", "K_OPC",
  "K_PARTIAL", "K_ROUTEHALO", "K_BLOCKAGE", "K_ROUTE", "K_SCANCHAIN",
  "K_SPECIALROUTE", "K_TRACK", "';'", "'-'", "'+'", "'('", "')'", "'*'",
  "','", "$accept", "def_file", "version_stmt", "$@1", "case_sens_stmt",
  "rules", "rule", "design_section", "design_name", "$@2", "end_design",
  "tech_name", "$@3", "array_name", "$@4", "floorplan_name", "$@5",
  "history", "prop_def_section", "$@6", "property_defs", "property_def",
  "$@7", "$@8", "$@9", "$@10", "$@11", "$@12", "$@13", "$@14", "$@15",
  "$@16", "$@17", "$@18", "$@19", "$@20", "$@21", "$@22", "$@23",
  "property_type_and_val", "$@24", "$@25", "opt_num_val", "units",
  "divider_char", "bus_bit_chars", "canplace", "$@26", "cannotoccupy",
  "$@27", "orient", "die_area", "$@28", "pin_cap_rule", "start_def_cap",
  "pin_caps", "pin_cap", "end_def_cap", "pin_rule", "start_pins", "pins",
  "pin", "$@29", "$@30", "$@31", "pin_options", "pin_option", "$@32",
  "$@33", "$@34", "$@35", "$@36", "$@37", "$@38", "$@39", "$@40", "$@41",
  "$@42", "pin_prop_name_values", "prop_name_value", "$@43",
  "prop_name_value_pair", "net_prop_name_values", "prop_string_value",
  "via_orient", "pin_layer_mask_opt", "pin_via_mask_opt",
  "pin_poly_mask_opt", "pin_layer_spacing_opt", "pin_layer_props_opt",
  "pin_layer_props", "pin_layer_prop", "$@44", "pin_poly_props_opt",
  "pin_poly_props", "pin_poly_prop", "$@45", "pin_via_props_opt",
  "pin_via_props", "pin_via_prop", "$@46", "pin_layer_spacing",
  "pin_poly_spacing_opt", "pin_poly_spacing", "pin_oxide", "use_type",
  "pin_layer_opt", "$@47", "end_pins", "row_rule", "$@48", "$@49",
  "row_do_option", "row_step_option", "row_options", "row_option", "$@50",
  "row_prop_list", "row_prop", "tracks_rule", "$@51", "track_start",
  "track_type", "track_opts", "track_opt_property_statements",
  "track_property_statements", "track_property_statement", "$@52",
  "track_ndr_statement", "$@53", "track_width_statement",
  "track_mask_statement", "same_mask", "track_layer_statement", "$@54",
  "track_layers", "track_layer", "gcellgrid", "extension_section",
  "extension_stmt", "via_section", "via", "via_declarations",
  "via_declaration", "$@55", "$@56", "layer_stmts", "layer_stmt", "$@57",
  "$@58", "$@59", "$@60", "$@61", "$@62", "$@63", "layer_viarule_opts",
  "$@64", "firstPt", "nextPt", "otherPts", "pt", "mask", "via_end",
  "regions_section", "regions_start", "regions_stmts", "regions_stmt",
  "$@65", "$@66", "rect_list", "region_options", "region_option", "$@67",
  "region_prop_list", "region_prop", "region_type",
  "comps_maskShift_section", "$@68", "comps_section", "start_comps",
  "layer_statement", "maskLayer", "comps_rule", "comp", "comp_start",
  "comp_id_and_name", "$@69", "comp_net_list", "comp_options",
  "comp_option", "comp_extension_stmt", "comp_eeq", "$@70",
  "comp_generate", "$@71", "opt_pattern", "comp_source", "source_type",
  "comp_region", "comp_pnt_list", "comp_halo", "$@72", "halo_soft",
  "comp_routehalo", "$@73", "comp_property", "$@74", "comp_prop_list",
  "comp_prop", "comp_region_start", "comp_foreign", "$@75", "opt_paren",
  "comp_type", "maskShift", "$@76", "placement_status", "comp_pinprop",
  "$@77", "comp_physical", "weight", "end_comps", "nets_section",
  "start_nets", "net_rules", "one_net", "net_and_connections", "net_start",
  "$@78", "net_name", "$@79", "$@80", "net_connections", "net_connection",
  "$@81", "$@82", "$@83", "conn_opt", "net_options", "net_option", "$@84",
  "$@85", "$@86", "$@87", "$@88", "$@89", "$@90", "$@91", "$@92", "$@93",
  "netsource_type", "vpin_stmt", "$@94", "vpin_begin", "$@95",
  "vpin_layer_opt", "$@96", "vpin_options", "vpin_status", "net_type",
  "opt_wire", "$@97", "opt_paths", "paths", "$@98", "new_path", "$@99",
  "path", "$@100", "$@101", "virtual_statement", "rect_statement",
  "path_item_list_opt", "path_item", "$@102", "mask_number", "wire_width",
  "path_pt", "virtual_pt", "rect_pts", "opt_taper_style_s",
  "opt_taper_style", "opt_prop", "$@103", "opt_shield", "opt_taper",
  "$@104", "opt_style", "opt_spaths", "opt_shape_style_prop", "$@105",
  "$@106", "end_nets", "shape_type", "snets_section", "snet_rules",
  "snet_rule", "snet_options", "snet_option", "snet_other_option", "$@107",
  "$@108", "$@109", "$@110", "$@111", "$@112", "$@113", "$@114",
  "snet_type", "$@115", "orient_pt", "snet_width", "$@116", "snet_voltage",
  "$@117", "snet_spacing", "$@118", "$@119", "opt_snet_range", "opt_range",
  "nets_pattern_type", "snets_pattern_type", "opt_swire", "$@120",
  "spaths", "$@121", "snew_path", "$@122", "spath", "$@123", "$@124",
  "width", "start_snets", "end_snets", "groups_section", "groups_start",
  "group_rules", "group_rule", "start_group", "$@125", "group_members",
  "group_member", "group_options", "group_option", "$@126", "$@127",
  "$@128", "$@129", "$@130", "group_hinsts", "group_hinst",
  "group_components", "group_component", "group_groups", "group_group",
  "group_region", "group_prop_list", "group_prop", "group_soft_options",
  "group_soft_option", "groups_end", "assertions_section",
  "constraint_section", "assertions_start", "constraints_start",
  "constraint_rules", "constraint_rule", "operand_rule", "operand",
  "$@131", "$@132", "operand_list", "wiredlogic_rule", "$@133", "opt_plus",
  "delay_specs", "delay_spec", "constraints_end", "assertions_end",
  "scanchains_section", "scanchain_start", "scanchain_rules", "scan_rule",
  "start_scan", "$@134", "scan_members", "opt_pin", "scan_member", "$@135",
  "$@136", "$@137", "$@138", "$@139", "$@140", "$@141", "$@142", "$@143",
  "$@144", "opt_common_pins", "floating_inst_list", "one_floating_inst",
  "$@145", "floating_pins", "ordered_inst_list_opt", "ordered_inst_list",
  "one_ordered_inst", "$@146", "$@147", "ordered_pins",
  "partition_maxbits", "scanchain_end", "iotiming_section",
  "iotiming_start", "iotiming_rules", "iotiming_rule", "start_iotiming",
  "$@148", "iotiming_members", "iotiming_member", "$@149", "$@150",
  "iotiming_drivecell_opt", "$@151", "$@152", "iotiming_frompin", "$@153",
  "iotiming_parallel", "risefall", "iotiming_end",
  "floorplan_contraints_section", "fp_start", "fp_stmts", "fp_stmt",
  "$@154", "$@155", "h_or_v", "constraint_type", "constrain_what_list",
  "constrain_what", "$@156", "$@157", "row_or_comp_list", "row_or_comp",
  "$@158", "$@159", "timingdisables_section", "timingdisables_start",
  "timingdisables_rules", "timingdisables_rule", "$@160", "$@161", "$@162",
  "$@163", "td_macro_option", "$@164", "$@165", "$@166",
  "timingdisables_end", "partitions_section", "partitions_start",
  "partition_rules", "partition_rule", "start_partition", "$@167",
  "turnoff", "turnoff_setup", "turnoff_hold", "partition_members",
  "partition_member", "$@168", "$@169", "$@170", "$@171", "$@172", "$@173",
  "minmaxpins", "$@174", "min_or_max_list", "min_or_max_member",
  "pin_list", "risefallminmax1_list", "risefallminmax1",
  "risefallminmax2_list", "risefallminmax2", "partitions_end",
  "comp_names", "comp_name", "$@175", "subnet_opt_syn", "subnet_options",
  "subnet_option", "$@176", "$@177", "subnet_type", "pin_props_section",
  "begin_pin_props", "opt_semi", "end_pin_props", "pin_prop_list",
  "pin_prop_terminal", "$@178", "$@179", "pin_prop_options", "pin_prop",
  "$@180", "pin_prop_name_value_list", "pin_prop_name_value",
  "blockage_section", "blockage_start", "blockage_end", "blockage_defs",
  "blockage_def", "blockage_rule", "$@181", "$@182", "$@183",
  "layer_blockage_rules", "layer_blockage_rule", "$@184", "$@185",
  "comp_blockage_rule", "$@186", "placement_comp_rules",
  "placement_comp_rule", "$@187", "$@188", "$@189",
  "rectPoly_blockage_rules", "rectPoly_blockage", "$@190", "slot_section",
  "slot_start", "slot_end", "slot_defs", "slot_def", "slot_rule", "$@191",
  "$@192", "geom_slot_rules", "geom_slot", "$@193", "fill_section",
  "fill_start", "fill_end", "fill_defs", "fill_def", "$@194", "$@195",
  "$@196", "$@197", "geom_fill_rules", "geom_fill", "$@198",
  "fill_layer_mask_opc_opt", "opt_mask_opc_l", "fill_layer_opc",
  "firstViaPt", "nextViaPt", "otherViaPts", "fill_via_orient",
  "fill_via_mask_opc_opt", "opt_mask_opc", "fill_via_prop", "$@199",
  "fill_layer_prop", "$@200", "fill_via_opc", "fill_mask", "fill_viaMask",
  "nondefaultrule_section", "nondefault_start", "nondefault_end",
  "nondefault_defs", "nondefault_def", "$@201", "$@202",
  "nondefault_options", "nondefault_option", "$@203", "$@204", "$@205",
  "$@206", "$@207", "nondefault_layer_options", "nondefault_layer_option",
  "nondefault_prop_opt", "$@208", "nondefault_prop_list",
  "nondefault_prop", "styles_section", "styles_start", "styles_end",
  "styles_rules", "styles_rule", "$@209", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-1458)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-801)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -68, -1458,   110,   109,   150, -1458,    28,   403,  -151,  -131,
    -121, -1458,   628, -1458, -1458, -1458, -1458, -1458,   250, -1458,
     135, -1458, -1458, -1458, -1458, -1458,   266,   275,   139,   139,
   -1458,   278,   283,   290,   299,   316,   330,   343,   351,   354,
     357,   367,   375,   382,   400,   406,   409, -1458, -1458, -1458,
     413,   421,   424,   429,   440, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,   446, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,   170, -1458, -1458,   457,   192,
     508,   419,   514,   517,   518,   534,   227,   244, -1458, -1458,
   -1458, -1458,   559,   562,   262,   264,   265,   267,   270,   271,
     272,   273,   274, -1458,   277,   279,   291,   300,   301,   302,
   -1458, -1458,   303,   304,   305,   319,   320,    46,   -66, -1458,
     -53,   -50,   -48,   -44,   -41,   -40,   -37,   -36,   -34,   -30,
     -13,   -12,    -2,    40,    48,    49,    59, -1458, -1458,    60,
     321, -1458,   323,   572,   324,   325,   578,   593,    10,   227,
   -1458, -1458,   598,   632, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,    47,
      39, -1458, -1458, -1458, -1458, -1458, -1458,   631,   627, -1458,
   -1458,   635, -1458, -1458, -1458,   624,   645, -1458, -1458, -1458,
     613, -1458, -1458,   625, -1458, -1458, -1458, -1458, -1458,   615,
   -1458, -1458, -1458, -1458, -1458,   618, -1458, -1458, -1458,   599,
   -1458, -1458, -1458, -1458,   579,   136, -1458, -1458, -1458, -1458,
     600, -1458,   594, -1458, -1458, -1458, -1458,   577,   370, -1458,
   -1458, -1458,   529, -1458, -1458,   570,     1, -1458, -1458,   569,
   -1458, -1458, -1458, -1458,   503, -1458, -1458, -1458,   467,    37,
   -1458, -1458,   -15,   466,   661, -1458, -1458, -1458,   468,    12,
   -1458, -1458,   690,    61,   407,   646, -1458, -1458, -1458, -1458,
     389, -1458, -1458,   693,   695,    13,    14, -1458, -1458,   696,
     697,   395, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   538,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458,   683, -1458, -1458,   704,   703, -1458,
     706, -1458,   708, -1458,   709,  -125,     6, -1458,   108,   -70,
   -1458,    78, -1458,   711,   712, -1458, -1458, -1458,   405,   408,
   -1458, -1458, -1458, -1458,   714,    96, -1458, -1458,   134, -1458,
     715, -1458, -1458, -1458, -1458,   412, -1458,   722,   140, -1458,
     723, -1458, -1458, -1458,   227, -1458, -1458, -1458, -1458,   -10,
   -1458, -1458, -1458, -1458,   667, -1458, -1458, -1458,   724, -1458,
     552,   552,   422,   423,   425,   426,  -190,   705,   726, -1458,
     733,   736,   738,   739,   741,   742,   743, -1458,   745,   746,
     749,   752,   754,   755,   756,   758,   759,   764,   765,   461,
     748, -1458, -1458,   774, -1458,   211, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,    17, -1458, -1458, -1458,
     227, -1458, -1458, -1458, -1458, -1458, -1458,   469, -1458, -1458,
     768, -1458, -1458, -1458,   753, -1458,   716, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,   151,   775,   780,   359,
     359,   781,   182, -1458, -1458,   206, -1458, -1458,   783, -1458,
     294, -1458, -1458,    70,   784,   786,   788, -1458,   672, -1458,
     348, -1458, -1458,   792,   793, -1458,   227,   227,    -7,   794,
     227, -1458, -1458, -1458,   795,   797, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,   778,   782, -1458,
   -1458, -1458, -1458, -1458, -1458,   796,   552,   263,   263,   263,
     263,   263,   263,   263,   263,   263,   263,   263,   263,   263,
     263,   263,   263,   263,   499,   732,   807, -1458,   227, -1458,
   -1458,   229,   808, -1458, -1458, -1458,   227, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,   814, -1458,   227,
     227,   552, -1458,   819,   -84,   820, -1458, -1458, -1458,   459,
     824,   -22,   825, -1458, -1458, -1458, -1458,   828, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,   227, -1458,   229,   830, -1458,
   -1458, -1458,   459,   831,   -19,   832, -1458,   331, -1458, -1458,
   -1458, -1458,   833, -1458, -1458,   834, -1458, -1458, -1458, -1458,
     352, -1458, -1458, -1458,   821, -1458,     0,    71,   530, -1458,
     386, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
     838, -1458, -1458,   837, -1458,   158, -1458, -1458, -1458,   840,
     841,    -1,   238, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458,   535, -1458,   227, -1458, -1458, -1458,   227,   227,
   -1458, -1458,   189,   227,   842,   844,   544, -1458,   851, -1458,
   -1458,   843,   548,   549,   550,   551,   553,   554,   555,   557,
     558,   560,   561,   563,   564,   565,   566,   567,   568, -1458,
   -1458,   700,   202,   227,   227,   857, -1458, -1458, -1458, -1458,
   -1458, -1458,   864,   552,   874,   885,   886,   817,   888, -1458,
   -1458,   227, -1458,   581, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458,   890, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   891,
     896,   897, -1458, -1458,   898,   899, -1458,   900,   227,   902,
   -1458, -1458,   904, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458,   905,   906,   907, -1458, -1458,   908,
   -1458,   909,   910,   911, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458,   912, -1458,   359, -1458, -1458, -1458,   827,   913,   914,
     917,   919,   922,   923, -1458,   924,   925, -1458,   591,   926,
     619, -1458,   927,   928,   929,   327,   822,   629, -1458, -1458,
     633, -1458, -1458,   269,   932,   933,   937,   939,   940,   942,
   -1458, -1458,    54, -1458,   227,    29, -1458,   227, -1458, -1458,
   -1458,    84, -1458, -1458,   227,   930,   931, -1458,   920, -1458,
     802,   802, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
     949, -1458,   647,   787,   700, -1458, -1458,    22, -1458, -1458,
   -1458, -1458,   227,   223,   950, -1458, -1458,    34,   101,   885,
   -1458, -1458, -1458,   956, -1458,   951, -1458,     9, -1458,   962,
   -1458, -1458, -1458, -1458,   966, -1458, -1458, -1458,   967, -1458,
   -1458,   227, -1458,   968, -1458,   990,   966, -1458, -1458,   552,
   -1458, -1458,   969,    19,   993,    87,   994, -1458,   995, -1458,
     996, -1458, -1458, -1458, -1458, -1458,  1000,  1001, -1458,   924,
   -1458,  1002,  1000, -1458,  1003,  1005, -1458,   735, -1458, -1458,
    1004,  1006, -1458,  1007,  1008,  1010, -1458, -1458, -1458,  1013,
    1014, -1458, -1458, -1458, -1458,  1016,  1017, -1458,  1018,  1019,
   -1458,   226,   689, -1458,  1053, -1458, -1458, -1458, -1458,  1054,
   -1458, -1458,   227,   -17,    65, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458,  1055,  1057,  1058, -1458,  1059,  1060,  1060, -1458,
   -1458, -1458,  1061,  1011, -1458, -1458, -1458, -1458, -1458,  1062,
    1064,  1065, -1458, -1458, -1458, -1458,  -118, -1458, -1458, -1458,
    1066, -1458,   552, -1458, -1458, -1458, -1458,  1067, -1458,  1020,
   -1458, -1458, -1458,   761, -1458, -1458, -1458, -1458,   970,  1071,
      86,   227, -1458, -1458,   227, -1458, -1458,  1021,  1073, -1458,
     969, -1458, -1458,   227, -1458, -1458,   993, -1458,  1074,  1075,
    1077, -1458, -1458,   994, -1458,  1080, -1458,   770,   924, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,   924,   171, -1458,  1081,
    1082, -1458,   976, -1458, -1458, -1458, -1458, -1458,   245,  1083,
     978, -1458,   369,   417,   435,   369,   417,   435, -1458,   921,
   -1458,    63, -1458, -1458,  1086, -1458, -1458,  1087,  1030,   227,
   -1458,   227, -1458,   -95, -1458, -1458, -1458, -1458, -1458,   -80,
   -1458, -1458,   227, -1458, -1458, -1458, -1458,  1090, -1458,  1091,
    1092,  1093,  -110,  1069,  1070,  1072,   248,  1095, -1458, -1458,
   -1458, -1458, -1458,   934,  1096,  1009,  1099,  1100,  1101, -1458,
    1103,  1104,  1105,  1102,  1108, -1458,   138, -1458, -1458,  1107,
   -1458,  1110,  1111,  1112, -1458,   806, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458,   227, -1458,   957,   227,   227, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
     186, -1458, -1458, -1458,   809,   810, -1458, -1458, -1458, -1458,
   -1458,   811, -1458, -1458, -1458,  1012, -1458,   328, -1458,  1115,
   -1458, -1458,  1114,  1119,  1120,  1121,   417, -1458,  1122,  1123,
    1124,  1125, -1458, -1458,   417, -1458, -1458,  1126, -1458, -1458,
    1127, -1458, -1458, -1458,  1128, -1458, -1458,  1129, -1458, -1458,
   -1458, -1458,   227,   227,   227, -1458,  1130, -1458,     3, -1458,
    1131, -1458,   102, -1458,  1084,  1134,  1133, -1458, -1458, -1458,
    1135,  1137,  1138, -1458,   974, -1458, -1458,   249, -1458, -1458,
   -1458, -1458,  1116,   839, -1458, -1458, -1458,  1143, -1458, -1458,
     839,   845, -1458, -1458, -1458, -1458,  1145,   846,   846,   846,
   -1458, -1458, -1458,  1071, -1458,   552,  1146, -1458,   227, -1458,
    1073,  1147, -1458, -1458, -1458,  1150, -1458,  1151, -1458,   847,
    1154, -1458, -1458, -1458,   852,  1156, -1458,    25,  1157,  1158,
    1159,  1160, -1458, -1458, -1458, -1458, -1458, -1458, -1458,  1163,
   -1458, -1458,  1164, -1458, -1458, -1458, -1458,   227, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,   227,  1165,   216, -1458,
   -1458,  1166,  1167,  1140, -1458, -1458,   684, -1458, -1458,   227,
    1170, -1458, -1458,   982,   227,  1169, -1458,   903,  1172, -1458,
     -75, -1458,   865,   867,   868,  1178,    45, -1458,    -6, -1458,
    1177, -1458,   227, -1458, -1458, -1458,  1180,  1181,  1182, -1458,
    1183, -1458, -1458, -1458, -1458, -1458,  1184,  1185, -1458, -1458,
   -1458, -1458, -1458,  1188, -1458, -1458, -1458,   227, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,   861,   879,  1187, -1458, -1458,
   -1458,   459, -1458,  1190, -1458,  1189,  1191,  1192,  1193,  1194,
    1195,  1196,  1197,   785, -1458,  1186, -1458, -1458, -1458, -1458,
     552, -1458,  1200,  1199,   227, -1458,   227,  1201,   225, -1458,
   -1458, -1458, -1458, -1458,  1202, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458,  1203, -1458, -1458, -1458, -1458,    18, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,   227,   183,   883,   895,  1206,
   -1458,   901,   901, -1458,  1205,  1208,   297, -1458, -1458, -1458,
   -1458, -1458,  1209,  1212,  1213, -1458, -1458, -1458,  1198,  1198,
    1198,  1198,  1204,  1207,  1198,  1210, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,  1214,
   -1458,  1215,  1216,  1217, -1458, -1458,  1200, -1458, -1458,   227,
    1218, -1458, -1458, -1458,   915,  1219, -1458, -1458,  1221, -1458,
      21,    23, -1458,    36, -1458, -1458,   918,   935,   916,  1132,
      44, -1458,  1223, -1458, -1458, -1458, -1458, -1458,   227,   -14,
   -1458,   527, -1458, -1458,   966, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,  1032, -1458,  1200,
     227,   941,  1089,   938, -1458,   970, -1458, -1458,    20,    30,
      31,    32,     7,  1225,   331, -1458, -1458, -1458,  1228,  1229,
   -1458,  1230, -1458, -1458, -1458, -1458,  1232,  1233,  1235, -1458,
   -1458, -1458, -1458,  1063, -1458,  1231,  1238,  1241,  1242,  1068,
    1247,  1085, -1458, -1458,   971, -1458, -1458,   944, -1458,   945,
   -1458,   946, -1458,   947, -1458,   525,   943,  1255,  1256,   952,
   -1458, -1458, -1458,  1211,   953, -1458, -1458, -1458,  1263,     7,
    1266,  1267, -1458,  1268,  1269, -1458, -1458, -1458,  1270,    -9,
   -1458, -1458, -1458, -1458,  1272,    -5, -1458, -1458,   552,  1085,
   -1458,   227, -1458, -1458, -1458, -1458, -1458,  1273,  1248,  1274,
   -1458, -1458,    85,    24, -1458,   953, -1458, -1458, -1458,   961,
     972,   973,   975, -1458,  1275,  1276,  1113, -1458, -1458,  1277,
    1281,  1118, -1458, -1458, -1458,   979, -1458,  1286,  1264,  1287,
    1288,   537, -1458,    26,    27, -1458,   983,   984, -1458, -1458,
   -1458, -1458, -1458,   227,  1113, -1458, -1458, -1458, -1458,   227,
    1118, -1458, -1458,  1291,  1294,  1293,  1278,  1295,  1296,  1280,
     988,   991,   992,   998,   999,  1299,  1304, -1458,   227, -1458,
   -1458,   227, -1458,  1303,  1309,  1285,  1310,  1311,  1290,  1313,
    1314, -1458, -1458, -1458, -1458,  1317,  1318, -1458, -1458, -1458,
     227,  1015,  1022,  1319,  1297,  1023,  1321,  1301,  1324,  1024,
    1025, -1458, -1458,  1040,  1325,  1326, -1458,  1305,  1329,  1333,
   -1458, -1458,   227,  1334, -1458,  1335,  1336,  1315,  1337,  1339,
   -1458,  1340,  1341,  1036,  1041, -1458,  1345, -1458,  1045, -1458,
    1348,  1349,  1350,  1351, -1458
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       3,     4,     0,     6,     0,     1,     0,     0,     0,     0,
       0,    11,     0,     5,     7,     8,    60,    51,     0,    54,
       0,    56,    58,   112,   114,   124,     0,     0,     0,     0,
     264,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    61,   308,   370,
       0,     0,     0,     0,     0,    10,    12,    38,     2,    48,
      34,    41,    43,    46,    50,    40,    35,    36,    37,    39,
      44,   128,    45,   134,    47,    49,     0,    42,    17,    33,
     312,    27,   351,    19,    15,   377,    23,   452,    30,   604,
      21,   680,    13,    16,   729,   729,    28,   757,    22,   812,
      20,   839,    32,   864,    25,   882,    26,   952,    14,   969,
      29,  1017,    18,  1031,    24,     0,    31,  1104,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   127,   283,
     284,   282,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    53,     0,     0,     0,     0,     0,     0,
      63,   374,   949,     0,     0,     0,     0,     0,     0,   280,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  1073,  1071,     0,
       0,   311,     0,     0,     0,     0,     0,     0,     0,     0,
     338,   133,     0,     0,   373,   350,   451,   676,  1069,   679,
     728,   756,   727,   811,   863,   881,   838,   110,   111,     0,
       0,   950,   948,   967,  1015,  1029,  1102,     0,     0,   129,
     126,     0,   136,   135,   132,     0,     0,   314,   313,   310,
       0,   353,   352,     0,   381,   378,   386,   383,   372,     0,
     456,   453,   473,   455,   450,     0,   607,   605,   603,     0,
     683,   681,   685,   678,     0,     0,   730,   731,   732,   725,
       0,   726,     0,   760,   758,   762,   755,     0,     0,   813,
     817,   810,     0,   841,   840,     0,     0,   865,   862,     0,
     885,   883,   895,   880,     0,   954,   947,   953,     0,     0,
     966,   970,     0,     0,     0,  1014,  1018,  1023,     0,     0,
    1028,  1032,     0,     0,     0,     0,  1101,  1105,    52,    55,
       0,    57,    59,     0,     0,     0,     0,   340,   339,     0,
       0,     0,    65,    87,    77,    71,    81,    73,    67,     0,
      85,    75,    69,    79,    95,    83,    89,    91,    93,    97,
      64,   376,   371,   375,     0,   131,   263,     0,     0,   348,
       0,   349,     0,   449,     0,     0,   380,   590,     0,     0,
     677,     0,   724,     0,   688,   754,   734,   736,     0,     0,
     743,   747,   753,   809,     0,     0,   836,   815,     0,   837,
       0,   879,   866,   869,   871,     0,   930,     0,     0,   951,
       0,   968,   972,   975,     0,  1012,  1009,  1016,  1020,     0,
    1030,  1033,  1036,  1074,     0,  1068,  1072,  1103,     0,   109,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    99,
       0,     0,     0,     0,     0,     0,     0,    62,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   315,   354,     0,   379,     0,   402,   387,   401,   397,
     388,   389,   396,   398,   399,   400,     0,   395,   390,   394,
       0,   391,   392,   393,   385,   384,   458,     0,   457,   454,
       0,   501,   474,   491,   511,   606,     0,   637,   608,   612,
     609,   610,   611,   684,   687,   686,     0,     0,     0,     0,
       0,     0,     0,   761,   759,     0,   778,   763,     0,   814,
       0,   825,   818,     0,     0,     0,     0,   873,   887,   884,
       0,   909,   896,     0,     0,   995,     0,     0,     0,     0,
       0,  1026,  1019,  1024,     0,     0,  1076,  1070,  1106,   116,
     118,   119,   117,   120,   123,   122,   121,     0,     0,   342,
     344,   343,   345,   125,   341,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   317,     0,   382,
     405,     0,     0,   403,   441,   442,   437,   443,   432,   431,
     424,   309,   439,   418,   447,   445,   444,     0,   415,     0,
     414,     0,   462,     0,     0,     0,   519,   520,   481,     0,
       0,     0,     0,   521,   489,   509,   495,     0,   499,   492,
     522,   478,   479,   475,   512,     0,   622,     0,     0,   638,
     639,   630,     0,     0,     0,     0,   640,     0,   645,   647,
     649,   617,     0,   641,   628,     0,   619,   624,   613,   682,
       0,   695,   689,   735,     0,   740,     0,     0,   745,   733,
       0,   748,   782,   766,   768,   770,   772,   779,   774,   776,
       0,   834,   835,     0,   822,     0,   844,   845,   842,     0,
       0,     0,   889,   886,   897,   899,   901,   903,   905,   907,
     955,   973,   976,  1011,     0,   971,  1010,  1021,     0,     0,
    1034,  1037,     0,     0,     0,     0,     0,   265,   104,   102,
     100,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   130,
     137,   286,     0,     0,   358,     0,   410,   411,   412,   413,
     409,   448,     0,     0,     0,     0,     0,   420,     0,   422,
     416,     0,   436,   459,   460,   506,   502,   503,   504,   505,
     477,   484,     0,   252,   253,   254,   255,   256,   257,   258,
     259,   487,   488,   659,   656,   657,   658,   483,   486,     0,
       0,     0,   485,   182,     0,     0,   523,     0,     0,     0,
     627,   633,     0,   635,   636,   663,   660,   661,   662,   632,
     634,   591,   592,   593,   594,   595,   596,   597,   600,   601,
     602,   599,   598,   615,     0,     0,     0,   182,   616,     0,
     629,     0,     0,   664,   699,   693,   701,   719,   696,   697,
     691,     0,   738,     0,   741,   739,   746,     0,     0,     0,
       0,     0,     0,     0,   788,   796,     0,   178,   785,     0,
       0,   821,     0,     0,     0,     0,     0,     0,   874,   877,
       0,   890,   891,   892,     0,     0,     0,     0,     0,     0,
     957,   977,     0,   996,     0,     0,  1025,     0,  1044,  1056,
    1075,     0,  1077,  1088,     0,     0,     0,   307,   267,   105,
     654,   654,   106,    66,    88,    78,    72,    82,    74,    68,
      86,    76,    70,    80,    96,    84,    90,    92,    94,    98,
       0,   290,     0,   297,   287,   289,   316,     0,   332,   318,
     331,   356,     0,     0,   407,   404,   438,     0,     0,   425,
     426,   440,   421,     0,   178,     0,   417,     0,   463,     0,
     482,   490,   510,   496,   500,   493,   480,   476,   525,   513,
     507,     0,   631,     0,   648,     0,   618,   642,   620,   643,
     614,   667,     0,     0,     0,   690,     0,   714,     0,   742,
       0,   749,   750,   751,   752,   783,   764,   769,   771,   797,
     799,     0,   764,   780,     0,     0,   775,   807,   816,   823,
       0,     0,   846,     0,     0,     0,   850,   867,   870,     0,
       0,   872,   893,   894,   888,     0,     0,   918,     0,     0,
     918,     0,   974,   999,  1005,  1001,   997,  1003,  1004,     0,
     340,  1022,     0,     0,  1054,  1079,  1094,  1078,  1086,  1082,
    1084,   340,     0,     0,     0,   271,     0,   107,   107,   138,
     178,   281,     0,   295,   288,   319,   336,   326,   321,     0,
       0,     0,   323,   328,   357,   355,     0,   359,   408,   406,
       0,   434,     0,   429,   430,   428,   427,     0,   446,     0,
     464,   468,   466,     0,   931,   183,   494,   524,   526,     0,
     514,     0,   646,   650,     0,   644,   625,   666,     0,   708,
     700,   707,   713,     0,   694,   711,   702,   710,     0,     0,
       0,   720,   705,   698,   704,   692,   737,     0,   796,   765,
     767,   790,   789,   798,   801,   773,   796,     0,   179,     0,
       0,   777,   829,   819,   820,   848,   847,   849,     0,     0,
       0,   878,     0,     0,   902,     0,     0,   908,   956,     0,
     958,     0,   978,   987,     0,  1006,   178,     0,  1007,  1013,
     340,     0,  1042,     0,  1039,  1045,  1046,  1048,  1047,     0,
    1055,  1052,     0,  1057,  1060,  1058,  1059,     0,  1096,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   108,   103,
     101,   140,   291,   299,     0,   292,     0,     0,     0,   178,
       0,     0,     0,     0,     0,   360,     0,   435,   433,     0,
     423,     0,     0,     0,   461,   497,   530,   529,   532,   528,
     517,   518,   516,   508,     0,   623,   652,     0,     0,   670,
     669,   672,   668,   706,   712,   709,   721,   722,   723,   703,
       0,   715,   744,   784,   792,   803,   781,   185,   184,   180,
     181,     0,   808,   830,   824,     0,   843,     0,   851,     0,
     875,   912,     0,     0,     0,     0,   900,   924,     0,     0,
       0,     0,   919,   912,   906,   959,   982,     0,   985,   988,
       0,   990,   991,   992,     0,   993,   994,     0,  1000,  1002,
     998,  1008,  1027,     0,     0,  1063,     0,  1049,     0,  1061,
       0,  1065,  1054,  1050,     0,  1095,     0,  1083,  1085,  1107,
       0,     0,     0,   266,     0,   272,   655,     0,   300,   298,
     296,   293,   301,   346,   337,   327,   322,     0,   334,   333,
     346,     0,   363,   368,   369,   362,     0,   470,   470,   470,
     933,   937,   932,     0,   569,     0,     0,   651,     0,   340,
       0,     0,   717,   718,   716,     0,   791,     0,   802,   786,
       0,   826,   852,   854,     0,     0,   898,     0,     0,     0,
       0,     0,   925,   920,   922,   921,   923,   904,   961,     0,
     979,   178,     0,   981,   980,   984,  1041,     0,   178,  1066,
    1035,  1040,   178,  1067,  1038,  1053,     0,     0,     0,  1097,
    1087,     0,     0,   269,   273,   139,     0,   141,   143,   162,
       0,   302,   285,     0,     0,     0,   324,     0,   361,   419,
       0,   471,     0,     0,     0,     0,   498,   531,     0,   515,
       0,   340,   626,   671,   675,   582,     0,     0,     0,   831,
       0,   856,   856,   868,   876,   910,     0,     0,   913,   926,
     928,   927,   929,   960,   983,   986,   989,     0,  1064,  1062,
    1051,  1080,  1099,  1100,  1098,     0,     0,     0,   268,   275,
     154,     0,   142,     0,   145,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   150,     0,   157,   153,   148,   160,
       0,   294,     0,     0,     0,   335,     0,     0,     0,   364,
     472,   465,   469,   467,     0,   943,   944,   945,   941,   946,
     938,   939,     0,   578,   579,   575,   577,     0,   533,   570,
     573,   574,   572,   571,   653,   621,     0,     0,     0,     0,
     827,   853,   855,   916,     0,     0,     0,   962,   340,  1089,
     113,   115,     0,   274,     0,   152,   144,   176,   260,   260,
     260,   260,     0,     0,   260,     0,   220,   221,   222,   223,
     224,   225,   226,   227,   228,   229,   230,   231,   232,   233,
     234,   235,   236,   237,   238,   239,   240,   241,   242,   243,
     244,   245,   246,   247,   248,   249,   250,   251,   175,     0,
     147,     0,     0,     0,   163,   306,   304,   347,   320,     0,
       0,   366,   367,   365,   935,     0,   527,   581,     0,   178,
       0,     0,   537,     0,   673,   583,   793,   804,     0,   832,
       0,   857,   911,   914,   915,   964,   965,   963,  1043,  1081,
     270,     0,   276,   155,   146,   261,   164,   165,   166,   167,
     168,   170,   172,   173,   151,   158,   149,   190,   303,   304,
       0,     0,     0,     0,   942,   940,   580,   576,     0,     0,
       0,     0,   534,     0,     0,   586,   588,   537,     0,     0,
     787,     0,   828,   860,   858,   917,     0,     0,     0,  1090,
     278,   279,   277,   188,   177,     0,     0,     0,     0,   192,
       0,   208,   305,   340,     0,   936,   934,     0,   556,     0,
     558,     0,   557,     0,   559,   539,     0,     0,     0,     0,
     547,   548,   538,   554,     0,   585,   584,   178,     0,   674,
       0,     0,   833,     0,     0,  1092,  1091,  1093,     0,   194,
     262,   169,   171,   174,     0,   216,   191,   212,   186,   209,
     211,   325,   329,   560,   562,   561,   563,     0,   541,     0,
     536,   555,   553,     0,   535,     0,   552,   587,   589,     0,
       0,     0,     0,   189,     0,     0,   196,   195,   193,     0,
       0,   202,   217,   178,   187,     0,   210,     0,     0,     0,
       0,   540,   549,     0,     0,   551,   794,   805,   861,   859,
     214,   215,   200,     0,   197,   199,   218,   219,   206,     0,
     203,   205,   213,     0,     0,     0,     0,     0,     0,   542,
       0,     0,     0,     0,     0,     0,     0,   178,     0,   198,
     178,     0,   204,     0,     0,     0,     0,     0,     0,     0,
       0,   564,   566,   565,   567,     0,     0,   201,   156,   207,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   340,   161,     0,     0,     0,   568,     0,     0,     0,
     795,   806,   159,     0,   544,     0,     0,     0,     0,     0,
     545,     0,     0,     0,     0,   543,     0,   550,     0,   546,
       0,     0,     0,     0,   330
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   432,
   -1458, -1458,   296, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
    -408, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,  -832,
   -1458, -1458,   556, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458,  -416, -1458, -1458, -1458,  -421, -1458, -1458, -1458,  -359,
   -1458, -1458, -1458, -1458, -1458,  -616, -1334, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458,  1332, -1458, -1458, -1458,   458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,  -265,  -107, -1458, -1458,  -347,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,  -513,  -188, -1003,
    -126,    56, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,   750, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   449, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,    72, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,  1220, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,  -814, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458,  -216, -1458, -1458, -1458,    50, -1458, -1458,
   -1458, -1458,  -276, -1458, -1458, -1458,  -321, -1457, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458,  -270, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   494,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,    51, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
     284, -1458,   298, -1458,   293, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,  1292, -1458, -1458,  -246, -1458,
   -1458,   892, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458,   404, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1004, -1458,   411, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458,  -864, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458,   -39, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458,   132, -1458, -1458, -1458, -1458,
     388, -1458,   260, -1151, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   871, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   528,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458,   112, -1458, -1458, -1458, -1458, -1458, -1458, -1458,   113,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458,  1094, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458, -1458,
   -1458, -1458, -1458, -1458, -1458
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     2,     3,     4,     7,    12,    55,    56,    57,   118,
      58,    59,   120,    60,   122,    61,   123,    62,    63,   150,
     209,   340,   420,   426,   430,   423,   425,   429,   422,   431,
     424,   433,   428,   421,   434,   435,   436,   432,   437,   702,
     881,   880,  1169,    64,    65,    66,    67,   124,    68,   125,
    1150,    69,   126,    70,    71,   157,   219,   220,    72,    73,
     158,   223,   347,   900,  1171,  1297,  1387,  1527,  1572,  1569,
    1524,  1663,  1571,  1669,  1573,  1666,  1667,  1668,  1614,  1065,
     974,  1108,   934,  1230,  1755,  1709,  1671,  1715,  1746,  1773,
    1774,  1775,  1797,  1779,  1780,  1781,  1800,  1718,  1719,  1720,
    1753,  1747,  1751,  1752,  1568,   761,  1616,  1665,   224,    74,
     133,   878,  1025,  1448,  1166,  1295,  1449,  1523,  1612,    75,
     225,    76,   131,   902,   903,   904,   905,  1030,  1302,  1390,
    1175,  1033,  1299,  1392,  1472,  1628,  1629,    77,    78,  1401,
      79,    80,   160,   228,   350,   567,   722,   909,  1176,  1179,
    1183,  1476,  1178,  1184,  1757,   910,  1177,   189,   544,   416,
     318,  1394,   229,    81,    82,   161,   232,   352,   568,   724,
     913,  1047,  1312,  1398,  1479,  1315,    83,   151,    84,    85,
     210,   343,   162,   235,   236,   237,   354,   356,   355,   447,
     448,   449,   732,   450,   725,  1049,   451,   730,   452,   590,
     453,   737,   923,   454,   925,   455,   735,   919,   920,   456,
     457,   734,  1052,   458,   459,   736,   460,   461,   738,   462,
     463,   238,    86,    87,   163,   241,   242,   243,   358,   468,
     592,   929,   743,   928,  1191,  1193,  1192,  1402,   359,   472,
     776,   775,   752,   769,   774,  1066,   771,  1064,  1321,   773,
     750,   473,  1070,   474,   770,   615,   777,  1203,  1204,   613,
     937,   938,  1067,  1068,  1069,  1197,  1323,  1199,  1324,  1592,
    1690,  1691,  1642,  1692,  1790,  1693,  1694,  1498,  1734,  1730,
    1408,  1499,  1500,  1589,  1501,  1502,  1588,  1503,  1506,  1595,
    1697,  1698,   244,   803,    88,   164,   247,   361,   478,   479,
     813,   807,   811,  1074,   779,   812,  1208,   782,   638,   809,
    1076,   480,   804,   481,   805,   482,   806,  1206,  1327,  1027,
     767,   789,   950,   951,  1077,  1078,  1210,  1330,  1212,  1331,
    1647,  1415,    89,   248,    90,    91,   165,   251,   252,   363,
     364,   485,   486,   642,   957,   953,   956,   952,   954,  1093,
    1094,  1080,  1081,  1086,  1087,  1084,  1095,  1221,   955,  1091,
     253,    92,    93,    94,    95,   166,   256,   257,   645,   487,
     488,   646,   258,   491,   827,   492,   651,   261,   259,    96,
      97,   168,   264,   265,   374,   375,  1100,   497,   833,   834,
     835,   836,   838,   839,   837,  1106,   832,  1098,   976,   967,
    1102,  1224,  1336,   968,   969,   970,   971,  1225,  1338,  1111,
     266,    98,    99,   169,   269,   270,   498,   378,   502,   842,
    1112,  1234,  1420,  1599,  1235,  1340,  1652,   665,   271,   100,
     101,   170,   274,   380,   845,   668,   986,  1118,  1238,  1421,
    1422,  1511,  1601,  1704,  1703,   102,   103,   171,   277,   504,
    1119,   505,   506,   850,   989,  1345,   990,   278,   104,   105,
     172,   281,   282,   387,   673,   853,   994,   388,   512,   854,
     855,   856,   857,   858,   859,  1346,  1513,  1347,  1428,  1602,
    1124,  1252,  1246,  1247,   283,  1195,  1322,  1405,  1633,  1406,
    1490,  1586,  1585,  1491,   106,   107,   212,   286,   173,   287,
     390,   860,  1001,  1130,  1358,  1433,  1517,   108,   109,   290,
     174,   291,   292,   514,   861,   515,  1002,  1132,  1359,  1361,
    1133,  1362,   682,   863,  1137,  1134,  1136,   518,   396,   517,
     110,   111,   295,   175,   296,   297,   519,   865,   399,   523,
     689,   112,   113,   300,   176,   301,   524,   868,   525,   869,
    1278,  1144,  1274,  1013,  1145,  1146,  1151,  1375,  1282,  1152,
    1014,  1153,  1154,  1372,  1147,  1368,  1155,  1148,  1156,   114,
     115,   405,   303,   178,   302,   526,   692,   872,  1157,  1519,
    1160,  1161,  1159,  1609,  1659,   873,  1158,  1285,  1379,   116,
     117,   306,   179,   307,   693
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     190,   317,   537,   538,   684,   973,   783,  1139,   446,   371,
     464,  1685,   471,  1060,   477,  1141,   315,   394,  1162,   412,
     414,   588,   520,  1082,  1590,   394,  1677,  1638,   496,  1640,
    1763,   501,  1791,  1793,   221,  1141,  1679,  1681,  1683,  1686,
    1050,   511,   401,   341,  1492,  1425,  1656,   226,   321,  1594,
     230,  1744,   233,  1185,  1035,  1749,   239,   322,  1480,   245,
     249,   520,  1003,   254,   260,  1687,   262,   392,   323,   217,
     267,  1256,   763,  1036,  1037,   785,  1275,  1653,   324,   366,
     367,   368,   369,  1485,  1486,  1276,  1643,   272,   275,  1761,
     325,  1279,  1058,  1644,  1223,  1352,     1,   581,   279,  1487,
    1280,   745,  1226,  1352,  1053,  1054,  1488,  1055,   326,   848,
       5,   382,   466,   849,  1015,   383,   384,  1762,     6,   543,
     327,  1186,   188,  1257,  1200,  1201,   328,  1202,  1004,   529,
     530,   531,   532,   533,   534,   535,   536,  1272,   697,   641,
     284,  1061,   746,   747,   748,   749,   218,   329,   288,   293,
     366,   367,   368,   369,     8,   467,  1493,  1494,    13,   298,
     304,   404,  1088,  1089,  1090,  1495,   529,   530,   531,   532,
     533,   534,   535,   536,  1227,  1228,   867,  1229,    14,   330,
     874,  1426,  1427,   742,   444,  1496,   445,  1688,    15,  1332,
    1333,   385,  1334,  1038,   581,  1617,  1618,  1619,  1172,  1289,
    1622,  1689,   188,     9,    10,  1654,  1277,  1645,   764,   765,
     766,   786,   787,   788,   652,   366,   367,   368,   369,  1442,
    1443,  1281,  1444,   666,   667,  1005,   331,  1646,  1581,  1582,
     370,  1583,   121,  1006,  1258,   332,   333,  1736,  1489,   469,
    1007,   470,  1259,  1260,   222,   570,   571,   572,   573,   574,
     575,   576,   577,   578,   579,  1016,   119,   227,  1241,   393,
     231,  1253,   234,  1657,   843,   844,   240,  1745,   516,   240,
     250,  1750,   127,   255,   255,  1142,   263,   395,  1765,  1008,
     268,   128,   521,  1658,   134,   395,  1261,  1262,  1263,   135,
     653,   654,   655,   656,  1143,  1142,   136,   273,   276,   522,
    1605,  1606,   685,  1607,  1269,   137,  1497,   402,   280,  1039,
    1040,  1041,  1370,   822,  1042,   823,   129,   130,  1043,  -554,
     465,   521,   138,  1062,   316,   916,  1412,   413,   415,   188,
     589,   188,  1591,  1678,   591,  1639,   139,  1641,  1764,  1264,
    1792,  1794,   334,  1680,  1682,  1684,   188,  1306,   342,   140,
     285,   335,   336,   337,   338,   339,  1009,   141,   289,   294,
     142,  1265,  1266,   851,   852,  1267,  1017,   143,  1018,   299,
     305,   177,  1313,   144,  1314,   908,  1149,   657,   581,  1019,
    1020,   145,   580,   581,   825,   814,   823,   475,   146,   476,
     683,   190,   582,   658,   688,   815,   992,   993,   661,   662,
     824,   824,   663,   664,    11,   494,   147,   495,  1505,   148,
      -9,  1374,   149,    -9,    -9,    -9,    -9,    -9,    -9,   152,
      -9,    -9,    -9,    -9,   816,    -9,   817,   153,    -9,    -9,
     154,   698,   699,   700,    -9,   155,    -9,   701,   366,   367,
     368,   369,   723,   499,   659,   500,   156,    -9,    -9,   509,
     733,   510,   159,   818,   819,   726,   727,   728,   729,    -9,
     639,   180,   640,   740,   741,    -9,   581,   674,   675,   676,
     677,   678,   679,   661,   662,    -9,   828,   829,   830,   831,
     177,    -9,   982,   983,   984,   985,    -9,  1342,  1343,   778,
     583,   649,   584,   650,  1593,  1497,   864,   585,   870,    -9,
     871,   181,   586,    -9,  1403,  1404,    -9,  1242,  1243,  1244,
    1245,   906,   182,   907,   587,  1608,    -9,   183,   184,    -9,
     581,   185,   186,   820,   581,  1248,  1249,  1250,  1251,  1435,
    1660,  1661,  1045,  1662,  1046,  1128,  1438,  1129,   187,   188,
    1439,  1075,   791,   792,   793,   794,   795,   796,   797,   798,
     799,   800,  1727,   191,  1236,    -9,  1237,  1293,  1385,  1294,
    1386,  1207,   866,   190,  1788,   192,   193,   190,    -9,    -9,
      -9,   194,   801,   195,   196,    -9,   197,   959,   310,   198,
     199,   200,   201,   202,   313,    -9,   203,    -9,   204,   529,
     530,   531,   532,   533,   534,   535,   536,   911,   912,   314,
     205,   529,   530,   531,   532,   533,   534,   535,   536,   206,
     207,   208,   211,   213,   214,   926,   529,   530,   531,   532,
     533,   534,   535,   536,    -9,   319,    -9,    -9,   215,   216,
     308,   802,   309,   311,   312,    16,   320,   344,    17,    18,
      19,    20,    21,    22,  1188,    23,    24,    25,    26,   345,
      27,   348,   940,    28,    29,   346,   349,   351,   353,    30,
     357,    31,   753,   754,   755,   756,   757,   758,   759,   760,
    1721,   362,    32,    33,   360,   365,  1010,   373,   372,  1012,
     376,   379,   377,   381,    34,   386,  1021,   389,   391,   397,
      35,   398,   400,    -9,   403,  1329,   408,   407,   409,   410,
      36,   411,   417,   418,   419,   427,    37,   438,   439,   440,
     441,    38,   442,   443,  1450,   483,   484,   489,   493,   503,
     490,   507,   574,   575,    39,   577,   508,   513,    40,   527,
     528,    41,   546,  1451,   545,   539,   540,   547,   541,   542,
     548,    42,   549,   550,    43,   551,   552,   553,   616,   554,
     555,   617,   618,   556,   619,   620,   557,  1637,   558,   559,
     560,  1367,   561,   562,   621,   622,   623,   624,   563,   625,
     626,   564,   565,   627,   628,   629,   630,   566,   569,   643,
      44,   593,  1664,   614,   644,   648,  1044,   660,   669,   672,
     670,  1051,   671,    45,    46,    47,   680,   681,   687,   690,
      48,   691,   696,   594,   595,   694,   596,   597,   719,   695,
      49,   720,    50,   721,   731,  1071,   598,   599,   600,   601,
     739,   602,   603,   744,  1140,   821,   751,  1083,  1842,   604,
     762,   768,  1452,  1453,   772,  1525,   781,   784,   790,   808,
     810,   826,   840,   841,   846,   847,   862,   882,   875,    51,
     876,    52,    53,   877,   879,  1454,   581,   883,   884,   885,
     886,   914,   887,   888,   889,  1737,   890,   891,   915,   892,
     893,   901,   894,   895,   896,   897,   898,   899,   917,  1455,
    1456,  1457,  1458,  1459,  1460,  1461,  1462,   631,   581,   918,
     921,   922,   924,   927,   930,   931,   632,   605,   606,   607,
     932,   933,   935,   975,   939,   936,   941,   633,   942,   943,
     944,   945,   947,   948,   949,  -665,   958,  1409,    54,   961,
     962,  1782,   960,   963,  1463,   964,   965,   966,  -800,   972,
     977,   979,   978,   987,   980,   981,   995,   996,   988,   608,
     581,   997,   991,   998,   999,  1205,  1000,  1024,   190,   634,
    1388,   635,  1026,  1029,  1048,  1059,  1031,  1214,  1022,  1023,
     609,   610,  1057,  1579,  1464,  1817,  1063,  1032,  1819,  1465,
    -178,  -527,  1110,  1079,  1072,   586,  1466,  1467,  1468,  1469,
     703,   704,   705,   706,   707,   708,   709,   710,   711,   712,
     713,   714,   715,   716,   717,   718,  1073,  1085,  1092,  1096,
    1131,   611,  1097,   612,  1099,  1101,  1104,  1107,   636,  1109,
    1113,   637,  1114,  1115,  1116,  1273,  1117,  1120,  1121,  1328,
    1122,  1123,  1125,  1126,  1190,  1196,  1283,  1536,  1537,  1538,
    1539,  1540,  1541,  1542,  1543,  1544,  1545,  1546,  1547,  1548,
    1549,  1550,  1551,  1552,  1553,  1554,  1555,  1556,  1557,  1558,
    1559,  1560,  1561,  1562,  1563,  1564,  1565,  1566,  1567,  1135,
    1138,  1163,  1574,  1164,  1165,  1167,  1168,  1173,  1180,  1174,
    1181,  1182,  1187,  1189,  1194,  1198,  1209,  1211,  1325,  1222,
    1216,  1217,   190,  1218,  1220,  1231,  1233,  1239,  1232,  1240,
    1268,  1270,  1255,  1271,  1284,  1286,  1287,  1288,  1290,  1291,
    1292,  1296,  1300,  1303,  1304,  1305,  1310,  1326,  1301,  1307,
    1308,  1309,  1311,  1316,  1317,  1318,  1319,  1298,  1320,  1344,
    1348,  1335,  1337,  1341,  1339,  1349,  1350,  1351,  1353,  1354,
    1355,  1356,  1360,  1363,  1364,  1365,  1369,  1373,  1378,  1380,
    1411,  1381,  1377,  1382,  1383,  1384,  1391,  1366,   190,  1395,
    1393,  1399,  1410,  1414,  1416,  1417,  1397,  1400,  1419,  1418,
    1424,  1423,  1473,  1429,  1430,  1431,  1432,  1434,  1436,  1447,
    1520,  1441,  1445,  1446,  1471,  1475,  1478,  1477,  1481,  1437,
    1482,  1483,  1484,  1504,  1507,  1508,  1509,  1510,  1521,  1570,
    1514,  1515,  1516,  1522,  1526,  1528,  1596,  1529,  1530,  1531,
    1532,  1533,  1534,  1535,  1575,  1577,  1584,  1580,  1597,  1587,
    1598,  1603,  1670,  1600,  1604,  1610,  1611,  1613,  1624,  1625,
    1626,  1627,  1675,  1634,  1631,  1636,  1632,  1655,  1615,  1650,
    1648,  1695,  1700,  1701,  1620,  1710,  1702,  1621,  1705,  1706,
    1623,  1707,  1711,  1708,  1651,  1712,  1713,  1649,  1714,  1518,
    1440,  1676,  1674,  1716,  1722,  1729,  1717,  1723,  1724,  1725,
    1726,  1731,  1732,  1470,  1733,  1497,  1801,  1738,  1474,  1687,
    1739,  1740,  1741,  1742,  1766,  1759,  1743,  1728,  1748,  1758,
    1760,  1770,  1771,  1776,  1772,  1767,  1768,  1777,  1769,  1778,
    1784,  1783,  1785,  1786,  1787,  1795,  1796,  1803,  1804,  1805,
    1810,  1807,  1808,  1815,  1811,  1812,  1806,  1809,  1816,  1821,
    1754,  1813,  1814,  1822,  1823,  1843,  1824,  1825,  1826,  1827,
    1828,  1829,  1830,  1860,  1170,  1834,  1835,  1837,  1832,  1838,
    1839,  1844,  1845,  1833,  1846,  1847,  1836,  1840,  1841,  1848,
    1849,  1850,  1851,  1853,  1852,  1854,  1855,  1856,  1578,  1857,
     190,  1859,  1858,  1789,  1861,  1862,  1863,  1864,  1799,  1802,
    1756,   132,  1034,   946,  1672,  1576,  1396,   780,  1056,  1389,
    1635,  1699,  1735,  1407,  1696,  1028,  1105,  1219,  1213,  1215,
    1103,  1413,   647,  1512,   246,  1357,  1254,   167,  1127,   686,
    1371,  1630,     0,  1011,     0,  1376,     0,   406,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,  1673,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,  1820,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,  1831,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  1798,     0,     0,
       0,     0,     0,   190,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,  1818
};

static const yytype_int16 yycheck[] =
{
     126,   189,   410,   411,   517,   837,   622,  1010,   355,   255,
       4,     4,   359,     4,   361,    32,     6,    32,  1021,     6,
       6,     4,    32,     4,     6,    32,     6,     6,   375,     6,
       6,   378,     6,     6,   100,    32,     6,     6,     6,    32,
       6,   388,    30,     4,    50,    20,    60,   100,     1,  1506,
     100,    60,   100,   171,    32,    60,   100,    10,   133,   100,
     100,    32,     8,   100,   100,    58,   100,    30,    21,    23,
     100,     8,    94,    51,    52,    94,   171,    33,    31,    79,
      80,    81,    82,    38,    39,   180,    50,   100,   100,     4,
      43,   171,   924,    57,  1098,  1246,   164,   172,   100,    54,
     180,   185,  1106,  1254,     3,     4,    61,     6,    61,   110,
       0,   110,     4,   114,    30,   114,   115,    32,     9,   309,
      73,   239,   312,    60,    38,    39,    79,    41,    74,    64,
      65,    66,    67,    68,    69,    70,    71,  1140,   546,   486,
     100,   132,   226,   227,   228,   229,   100,   100,   100,   100,
      79,    80,    81,    82,     4,    47,   162,   163,   309,   100,
     100,   100,    75,    76,    77,   171,    64,    65,    66,    67,
      68,    69,    70,    71,     3,     4,   689,     6,   309,   132,
     693,   156,   157,   591,   309,   191,   311,   180,   309,     3,
       4,   190,     6,   171,   172,  1529,  1530,  1531,  1030,   309,
    1534,   194,   312,   175,   176,   161,   301,   171,   230,   231,
     232,   230,   231,   232,     8,    79,    80,    81,    82,     3,
       4,   301,     6,   153,   154,   171,   179,   191,     3,     4,
      94,     6,    97,   179,   171,   188,   189,  1694,   193,   309,
     186,   311,   179,   180,   310,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,   171,     6,   310,  1122,   222,
     310,  1125,   310,   277,   106,   107,   310,   276,   394,   310,
     310,   276,     6,   310,   310,   292,   310,   292,  1735,   225,
     310,     6,   292,   297,     6,   292,   223,   224,   225,     6,
      84,    85,    86,    87,   311,   292,     6,   310,   310,   309,
       3,     4,   309,     6,  1136,     6,   312,   295,   310,   287,
     288,   289,   309,   313,   292,   315,   177,   178,   296,   312,
     314,   292,     6,   314,   314,   733,  1329,   314,   314,   312,
     456,   312,   314,   313,   460,   314,     6,   314,   314,   276,
     314,   314,   295,   313,   313,   313,   312,  1179,   309,     6,
     310,   304,   305,   306,   307,   308,   302,     6,   310,   310,
       6,   298,   299,   125,   126,   302,   282,    10,   284,   310,
     310,   310,   234,     6,   236,   722,   311,   171,   172,   295,
     296,     6,   171,   172,   313,    33,   315,   309,     6,   311,
     516,   517,   181,   187,   520,    43,   127,   128,   104,   105,
     646,   647,   108,   109,     1,   309,     6,   311,  1411,     3,
       7,   309,     3,    10,    11,    12,    13,    14,    15,     6,
      17,    18,    19,    20,    72,    22,    74,     6,    25,    26,
       6,   168,   169,   170,    31,     6,    33,   174,    79,    80,
      81,    82,   568,   309,   238,   311,     6,    44,    45,   309,
     576,   311,     6,   101,   102,   226,   227,   228,   229,    56,
     309,     4,   311,   589,   590,    62,   172,   119,   120,   121,
     122,   123,   124,   104,   105,    72,    90,    91,    92,    93,
     310,    78,   155,   156,   157,   158,    83,   159,   160,   615,
     279,   309,   281,   311,   311,   312,   684,   286,   309,    96,
     311,   309,   291,   100,  1318,  1319,   103,    90,    91,    92,
      93,   309,     4,   311,   303,  1518,   113,    98,     4,   116,
     172,     4,     4,   171,   172,    90,    91,    92,    93,  1361,
       3,     4,   309,     6,   311,   309,  1368,   311,     4,   312,
    1372,   949,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,    27,   309,   309,   152,   311,   309,   309,   311,
     311,  1074,   688,   689,    27,     6,     4,   693,   165,   166,
     167,   309,   241,   309,   309,   172,   309,   823,     6,   309,
     309,   309,   309,   309,     6,   182,   309,   184,   309,    64,
      65,    66,    67,    68,    69,    70,    71,   723,   724,     6,
     309,    64,    65,    66,    67,    68,    69,    70,    71,   309,
     309,   309,   309,   309,   309,   741,    64,    65,    66,    67,
      68,    69,    70,    71,   221,    27,   223,   224,   309,   309,
     309,   300,   309,   309,   309,     7,     4,     6,    10,    11,
      12,    13,    14,    15,  1052,    17,    18,    19,    20,    22,
      22,    27,   778,    25,    26,    20,    11,    44,    33,    31,
      45,    33,   203,   204,   205,   206,   207,   208,   209,   210,
    1673,    72,    44,    45,    56,    96,   864,    83,    78,   867,
     103,   152,   312,   113,    56,   116,   874,   184,   221,   223,
      62,    30,   224,   290,     4,  1208,    50,   290,   309,     6,
      72,     6,     6,     6,   309,   167,    78,    24,     4,     6,
       4,    83,     4,     4,    30,     4,     4,   312,     4,     4,
     312,   309,    38,    39,    96,    41,     4,     4,   100,    62,
       6,   103,     6,    49,    29,   313,   313,     4,   313,   313,
       4,   113,     4,     4,   116,     4,     4,     4,    32,     4,
       4,    35,    36,     4,    38,    39,     4,  1589,     4,     4,
       4,  1274,     4,     4,    48,    49,    50,    51,     4,    53,
      54,     6,   311,    57,    58,    59,    60,    29,     4,     4,
     152,   312,  1614,    30,     4,     4,   912,     4,     4,   117,
       4,   917,     4,   165,   166,   167,     4,     4,     4,     4,
     172,     4,     6,    35,    36,    27,    38,    39,   309,    27,
     182,    79,   184,     6,     6,   941,    48,    49,    50,    51,
       6,    53,    54,     4,  1012,     4,     6,   953,  1831,    61,
       6,     6,   148,   149,     6,  1451,     6,     6,     6,     6,
       6,   311,     4,     6,     4,     4,   311,     4,     6,   221,
       6,   223,   224,   309,     3,   171,   172,   309,   309,   309,
     309,     4,   309,   309,   309,  1697,   309,   309,     4,   309,
     309,   171,   309,   309,   309,   309,   309,   309,     4,   195,
     196,   197,   198,   199,   200,   201,   202,   171,   172,     4,
       4,    74,     4,   312,     4,     4,   180,   129,   130,   131,
       4,     4,     4,   312,     4,     6,     4,   191,     4,     4,
       4,     4,     4,     4,     4,     4,     4,  1325,   290,     6,
       6,  1753,    95,     6,   240,     6,     4,     4,     4,     4,
       4,     4,   313,   111,     6,     6,     4,     4,   309,   171,
     172,     4,   309,     4,     4,  1071,     4,    27,  1074,   233,
    1297,   235,   150,     4,     4,     4,   309,  1083,    28,    28,
     192,   193,     6,  1476,   280,  1797,     4,   180,  1800,   285,
       4,     4,   237,     4,     6,   291,   292,   293,   294,   295,
     548,   549,   550,   551,   552,   553,   554,   555,   556,   557,
     558,   559,   560,   561,   562,   563,     6,     4,     4,     4,
     311,   233,     6,   235,     4,     4,     4,     4,   292,     4,
       6,   295,     6,     6,     6,  1141,     6,     4,     4,  1207,
       4,     4,     4,     4,     4,    55,  1152,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,     6,
       6,     6,  1470,     6,     6,     6,     6,     6,     6,    58,
       6,     6,     6,     6,   313,     4,    55,     4,  1204,   309,
       6,     6,  1208,     6,     4,     4,   110,     4,     6,   111,
       4,     4,   171,    63,     4,     4,     4,     4,    29,    29,
      28,     6,     6,     4,     4,     4,     4,   150,    99,     6,
       6,     6,     4,     6,     4,     4,     4,   183,   312,     4,
       6,   312,   312,   111,   313,     6,     6,     6,     6,     6,
       6,     6,     6,     6,     6,     6,     6,     6,     4,     6,
    1328,     6,    58,     6,     6,   171,    30,  1273,  1274,     6,
     311,     6,     6,     6,     4,     4,   311,   311,     4,   312,
       4,   309,   180,     6,     6,     6,     6,     4,     4,    29,
     309,     6,     6,     6,     4,     6,     4,   274,   313,  1367,
     313,   313,     4,     6,     4,     4,     4,     4,   309,     3,
       6,     6,     4,     6,     4,     6,   313,     6,     6,     6,
       6,     6,     6,     6,     4,     6,     4,     6,   313,     6,
       4,     6,   180,   312,     6,     6,     4,     4,     4,     4,
       4,     4,   133,     4,     6,     4,   311,     4,    30,   313,
     312,     6,     4,     4,    30,     4,     6,    30,     6,     6,
      30,     6,     4,   180,   112,     4,     4,   312,   180,  1437,
    1376,   313,   311,     6,   283,   312,   171,   313,   313,   313,
     313,     6,     6,  1389,   312,   312,  1779,     4,  1394,    58,
       4,     4,     4,     4,   313,    27,     6,  1685,     6,     6,
       6,     6,     6,     6,   171,   313,   313,     6,   313,   171,
       4,   312,    28,     6,     6,   312,   312,     6,     4,     6,
     312,     6,     6,     4,   313,   313,    28,    27,     4,     6,
    1718,   313,   313,     4,    29,   275,     6,     6,    28,     6,
       6,     4,     4,   278,  1028,     6,    29,     6,   313,    28,
       6,     6,     6,   311,    29,     6,   313,   313,   313,     6,
       6,     6,     6,     6,    29,     6,     6,     6,  1474,   313,
    1476,     6,   311,  1761,     6,     6,     6,     6,  1774,  1780,
    1719,    29,   904,   807,  1629,  1472,  1310,   617,   919,  1297,
    1586,  1647,  1693,  1323,  1644,   881,   972,  1093,  1080,  1086,
     969,  1330,   490,  1422,   164,  1253,  1126,    95,  1000,   518,
    1278,  1579,    -1,   865,    -1,  1282,    -1,   303,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1630,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,  1801,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1820,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1773,    -1,    -1,
      -1,    -1,    -1,  1779,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1798
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int16 yystos[] =
{
       0,   164,   317,   318,   319,     0,     9,   320,     4,   175,
     176,     1,   321,   309,   309,   309,     7,    10,    11,    12,
      13,    14,    15,    17,    18,    19,    20,    22,    25,    26,
      31,    33,    44,    45,    56,    62,    72,    78,    83,    96,
     100,   103,   113,   116,   152,   165,   166,   167,   172,   182,
     184,   221,   223,   224,   290,   322,   323,   324,   326,   327,
     329,   331,   333,   334,   359,   360,   361,   362,   364,   367,
     369,   370,   374,   375,   425,   435,   437,   453,   454,   456,
     457,   479,   480,   492,   494,   495,   538,   539,   610,   648,
     650,   651,   677,   678,   679,   680,   695,   696,   727,   728,
     745,   746,   761,   762,   774,   775,   810,   811,   823,   824,
     846,   847,   857,   858,   885,   886,   905,   906,   325,     6,
     328,    97,   330,   332,   363,   365,   368,     6,     6,   177,
     178,   438,   438,   426,     6,     6,     6,     6,     6,     6,
       6,     6,     6,    10,     6,     6,     6,     6,     3,     3,
     335,   493,     6,     6,     6,     6,     6,   371,   376,     6,
     458,   481,   498,   540,   611,   652,   681,   681,   697,   729,
     747,   763,   776,   814,   826,   849,   860,   310,   889,   908,
       4,   309,     4,    98,     4,     4,     4,     4,   312,   473,
     476,   309,     6,     4,   309,   309,   309,   309,   309,   309,
     309,   309,   309,   309,   309,   309,   309,   309,   309,   336,
     496,   309,   812,   309,   309,   309,   309,    23,   100,   372,
     373,   100,   310,   377,   424,   436,   100,   310,   459,   478,
     100,   310,   482,   100,   310,   499,   500,   501,   537,   100,
     310,   541,   542,   543,   608,   100,   542,   612,   649,   100,
     310,   653,   654,   676,   100,   310,   682,   683,   688,   694,
     100,   693,   100,   310,   698,   699,   726,   100,   310,   730,
     731,   744,   100,   310,   748,   100,   310,   764,   773,   100,
     310,   777,   778,   800,   100,   310,   813,   815,   100,   310,
     825,   827,   828,   100,   310,   848,   850,   851,   100,   310,
     859,   861,   890,   888,   100,   310,   907,   909,   309,   309,
       6,   309,   309,     6,     6,     6,   314,   474,   476,    27,
       4,     1,    10,    21,    31,    43,    61,    73,    79,   100,
     132,   179,   188,   189,   295,   304,   305,   306,   307,   308,
     337,     4,   309,   497,     6,    22,    20,   378,    27,    11,
     460,    44,   483,    33,   502,   504,   503,    45,   544,   554,
      56,   613,    72,   655,   656,    96,    79,    80,    81,    82,
      94,   684,    78,    83,   700,   701,   103,   312,   733,   152,
     749,   113,   110,   114,   115,   190,   116,   779,   783,   184,
     816,   221,    30,   222,    32,   292,   844,   223,    30,   854,
     224,    30,   295,     4,   100,   887,   889,   290,    50,   309,
       6,     6,     6,   314,     6,   314,   475,     6,     6,   309,
     338,   349,   344,   341,   346,   342,   339,   167,   348,   343,
     340,   345,   353,   347,   350,   351,   352,   354,    24,     4,
       6,     4,     4,     4,   309,   311,   455,   505,   506,   507,
     509,   512,   514,   516,   519,   521,   525,   526,   529,   530,
     532,   533,   535,   536,     4,   314,     4,    47,   545,   309,
     311,   455,   555,   567,   569,   309,   311,   455,   614,   615,
     627,   629,   631,     4,     4,   657,   658,   685,   686,   312,
     312,   689,   691,     4,   309,   311,   455,   703,   732,   309,
     311,   455,   734,     4,   765,   767,   768,   309,     4,   309,
     311,   455,   784,     4,   829,   831,   476,   845,   843,   852,
      32,   292,   309,   855,   862,   864,   891,    62,     6,    64,
      65,    66,    67,    68,    69,    70,    71,   366,   366,   313,
     313,   313,   313,   309,   474,    29,     6,     4,     4,     4,
       4,     4,     4,     4,     4,     4,     4,     4,     4,     4,
       4,     4,     4,     4,     6,   311,    29,   461,   484,     4,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
     171,   172,   181,   279,   281,   286,   291,   303,     4,   476,
     515,   476,   546,   312,    35,    36,    38,    39,    48,    49,
      50,    51,    53,    54,    61,   129,   130,   131,   171,   192,
     193,   233,   235,   575,    30,   571,    32,    35,    36,    38,
      39,    48,    49,    50,    51,    53,    54,    57,    58,    59,
      60,   171,   180,   191,   233,   235,   292,   295,   624,   309,
     311,   455,   659,     4,     4,   684,   687,   687,     4,   309,
     311,   692,     8,    84,    85,    86,    87,   171,   187,   238,
       4,   104,   105,   108,   109,   743,   153,   154,   751,     4,
       4,     4,   117,   780,   119,   120,   121,   122,   123,   124,
       4,     4,   838,   476,   473,   309,   844,     4,   476,   856,
       4,     4,   892,   910,    27,    27,     6,   366,   168,   169,
     170,   174,   355,   355,   355,   355,   355,   355,   355,   355,
     355,   355,   355,   355,   355,   355,   355,   355,   355,   309,
      79,     6,   462,   476,   485,   510,   226,   227,   228,   229,
     513,     6,   508,   476,   527,   522,   531,   517,   534,     6,
     476,   476,   366,   548,     4,   185,   226,   227,   228,   229,
     566,     6,   558,   203,   204,   205,   206,   207,   208,   209,
     210,   421,     6,    94,   230,   231,   232,   636,     6,   559,
     570,   562,     6,   565,   560,   557,   556,   572,   476,   620,
     513,     6,   623,   421,     6,    94,   230,   231,   232,   637,
       6,   211,   212,   213,   214,   215,   216,   217,   218,   219,
     220,   241,   300,   609,   628,   630,   632,   617,     6,   625,
       6,   618,   621,   616,    33,    43,    72,    74,   101,   102,
     171,     4,   313,   315,   684,   313,   311,   690,    90,    91,
      92,    93,   712,   704,   705,   706,   707,   710,   708,   709,
       4,     6,   735,   106,   107,   750,     4,     4,   110,   114,
     769,   125,   126,   781,   785,   786,   787,   788,   789,   790,
     817,   830,   311,   839,   474,   853,   476,   473,   863,   865,
     309,   311,   893,   901,   473,     6,     6,   309,   427,     3,
     357,   356,     4,   309,   309,   309,   309,   309,   309,   309,
     309,   309,   309,   309,   309,   309,   309,   309,   309,   309,
     379,   171,   439,   440,   441,   442,   309,   311,   455,   463,
     471,   476,   476,   486,     4,     4,   366,     4,     4,   523,
     524,     4,    74,   518,     4,   520,   476,   312,   549,   547,
       4,     4,     4,     4,   398,     4,     6,   576,   577,     4,
     476,     4,     4,     4,     4,     4,   398,     4,     4,     4,
     638,   639,   663,   661,   664,   674,   662,   660,     4,   684,
      95,     6,     6,     6,     6,     4,     4,   715,   719,   720,
     721,   722,     4,   395,   396,   312,   714,     4,   313,     4,
       6,     6,   155,   156,   157,   158,   752,   111,   309,   770,
     772,   309,   127,   128,   782,     4,     4,     4,     4,     4,
       4,   818,   832,     8,    74,   171,   179,   186,   225,   302,
     474,   855,   474,   869,   876,    30,   171,   282,   284,   295,
     296,   474,    28,    28,    27,   428,   150,   635,   635,     4,
     443,   309,   180,   447,   442,    32,    51,    52,   171,   287,
     288,   289,   292,   296,   476,   309,   311,   487,     4,   511,
       6,   476,   528,     3,     4,     6,   524,     6,   395,     4,
       4,   132,   314,     4,   563,   395,   561,   578,   579,   580,
     568,   476,     6,     6,   619,   366,   626,   640,   641,     4,
     667,   668,     4,   476,   671,     4,   669,   670,    75,    76,
      77,   675,     4,   665,   666,   672,     4,     6,   713,     4,
     702,     4,   716,   721,     4,   702,   711,     4,   397,     4,
     237,   725,   736,     6,     6,     6,     6,     6,   753,   766,
       4,     4,     4,     4,   796,     4,     4,   796,   309,   311,
     819,   311,   833,   836,   841,     6,   842,   840,     6,   475,
     474,    32,   292,   311,   867,   870,   871,   880,   883,   311,
     366,   872,   875,   877,   878,   882,   884,   894,   902,   898,
     896,   897,   475,     6,     6,     6,   430,     6,     6,   358,
     358,   380,   395,     6,    58,   446,   464,   472,   468,   465,
       6,     6,     6,   466,   469,   171,   239,     6,   366,     6,
       4,   550,   552,   551,   313,   801,    55,   581,     4,   583,
      38,    39,    41,   573,   574,   476,   633,   473,   622,    55,
     642,     4,   644,   668,   476,   670,     6,     6,     6,   666,
       4,   673,   309,   719,   717,   723,   719,     3,     4,     6,
     399,     4,     6,   110,   737,   740,   309,   311,   754,     4,
     111,   743,    90,    91,    92,    93,   798,   799,    90,    91,
      92,    93,   797,   743,   798,   171,     8,    60,   171,   179,
     180,   223,   224,   225,   276,   298,   299,   302,     4,   395,
       4,    63,   475,   476,   868,   171,   180,   301,   866,   171,
     180,   301,   874,   476,     4,   903,     4,     4,     4,   309,
      29,    29,    28,   309,   311,   431,     6,   381,   183,   448,
       6,    99,   444,     4,     4,     4,   395,     6,     6,     6,
       4,     4,   488,   234,   236,   491,     6,     4,     4,     4,
     312,   564,   802,   582,   584,   476,   150,   634,   474,   473,
     643,   645,     3,     4,     6,   312,   718,   312,   724,   313,
     741,   111,   159,   160,     4,   771,   791,   793,     6,     6,
       6,     6,   799,     6,     6,     6,     6,   791,   820,   834,
       6,   835,   837,     6,     6,     6,   476,   473,   881,     6,
     309,   867,   879,     6,   309,   873,   875,    58,     4,   904,
       6,     6,     6,     6,   171,   309,   311,   382,   455,   532,
     445,    30,   449,   311,   477,     6,   477,   311,   489,     6,
     311,   455,   553,   553,   553,   803,   805,   583,   596,   366,
       6,   474,   475,   644,     6,   647,     4,     4,   312,     4,
     738,   755,   756,   309,     4,    20,   156,   157,   794,     6,
       6,     6,     6,   821,     4,   395,     4,   474,   395,   395,
     476,     6,     3,     4,     6,     6,     6,    29,   429,   432,
      30,    49,   148,   149,   171,   195,   196,   197,   198,   199,
     200,   201,   202,   240,   280,   285,   292,   293,   294,   295,
     476,     4,   450,   180,   476,     6,   467,   274,     4,   490,
     133,   313,   313,   313,     4,    38,    39,    54,    61,   193,
     806,   809,    50,   162,   163,   171,   191,   312,   593,   597,
     598,   600,   601,   603,     6,   475,   604,     4,     4,     4,
       4,   757,   757,   792,     6,     6,     4,   822,   474,   895,
     309,   309,     6,   433,   386,   421,     4,   383,     6,     6,
       6,     6,     6,     6,     6,     6,   242,   243,   244,   245,
     246,   247,   248,   249,   250,   251,   252,   253,   254,   255,
     256,   257,   258,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   420,   385,
       3,   388,   384,   390,   366,     4,   452,     6,   476,   473,
       6,     3,     4,     6,     4,   808,   807,     6,   602,   599,
       6,   314,   585,   311,   593,   605,   313,   313,     4,   739,
     312,   758,   795,     6,     6,     3,     4,     6,   475,   899,
       6,     4,   434,     4,   394,    30,   422,   422,   422,   422,
      30,    30,   422,    30,     4,     4,     4,     4,   451,   452,
     474,     6,   311,   804,     4,   579,     4,   395,     6,   314,
       6,   314,   588,    50,    57,   171,   191,   646,   312,   312,
     313,   112,   742,    33,   161,     4,    60,   277,   297,   900,
       3,     4,     6,   387,   395,   423,   391,   392,   393,   389,
     180,   402,   451,   474,   311,   133,   313,     6,   313,     6,
     313,     6,   313,     6,   313,     4,    32,    58,   180,   194,
     586,   587,   589,   591,   592,     6,   609,   606,   607,   588,
       4,     4,     6,   760,   759,     6,     6,     6,   180,   401,
       4,     4,     4,     4,   180,   403,     6,   171,   413,   414,
     415,   475,   283,   313,   313,   313,   313,    27,   366,   312,
     595,     6,     6,   312,   594,   592,   593,   395,     4,     4,
       4,     4,     4,     6,    60,   276,   404,   417,     6,    60,
     276,   418,   419,   416,   366,   400,   415,   470,     6,    27,
       6,     4,    32,     6,   314,   593,   313,   313,   313,   313,
       6,     6,   171,   405,   406,   407,     6,     6,   171,   409,
     410,   411,   395,   312,     4,    28,     6,     6,    27,   366,
     590,     6,   314,     6,   314,   312,   312,   408,   476,   407,
     412,   473,   411,     6,     4,     6,    28,     6,     6,    27,
     312,   313,   313,   313,   313,     4,     4,   395,   476,   395,
     474,     6,     4,    29,     6,     6,    28,     6,     6,     4,
       4,   474,   313,   311,     6,    29,   313,     6,    28,     6,
     313,   313,   475,   275,     6,     6,    29,     6,     6,     6,
       6,     6,    29,     6,     6,     6,     6,   313,   311,     6,
     278,     6,     6,     6,     6
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int16 yyr1[] =
{
       0,   316,   317,   318,   319,   318,   320,   320,   320,   321,
     321,   321,   322,   322,   322,   322,   322,   322,   322,   322,
     322,   322,   322,   322,   322,   322,   322,   322,   322,   322,
     322,   322,   322,   322,   323,   323,   323,   323,   323,   323,
     323,   323,   323,   323,   323,   323,   323,   323,   323,   323,
     323,   325,   324,   326,   328,   327,   330,   329,   332,   331,
     333,   335,   334,   336,   336,   338,   337,   339,   337,   340,
     337,   341,   337,   342,   337,   343,   337,   344,   337,   345,
     337,   346,   337,   347,   337,   348,   337,   349,   337,   350,
     337,   351,   337,   352,   337,   353,   337,   354,   337,   337,
     356,   355,   357,   355,   355,   355,   355,   358,   358,   359,
     360,   361,   363,   362,   365,   364,   366,   366,   366,   366,
     366,   366,   366,   366,   368,   367,   369,   370,   371,   371,
     372,   373,   374,   375,   376,   376,   378,   379,   380,   377,
     381,   381,   382,   382,   382,   383,   382,   382,   384,   382,
     385,   382,   382,   382,   386,   387,   382,   388,   389,   382,
     390,   382,   382,   382,   382,   382,   382,   382,   391,   382,
     392,   382,   382,   393,   382,   382,   394,   394,   396,   395,
     397,   397,   398,   398,   399,   399,   400,   400,   401,   401,
     402,   402,   403,   403,   404,   404,   405,   405,   406,   406,
     408,   407,   409,   409,   410,   410,   412,   411,   413,   413,
     414,   414,   416,   415,   417,   417,   418,   418,   419,   419,
     420,   420,   420,   420,   420,   420,   420,   420,   420,   420,
     420,   420,   420,   420,   420,   420,   420,   420,   420,   420,
     420,   420,   420,   420,   420,   420,   420,   420,   420,   420,
     420,   420,   421,   421,   421,   421,   421,   421,   421,   421,
     422,   423,   422,   424,   426,   427,   425,   428,   428,   429,
     429,   430,   430,   432,   431,   433,   433,   434,   434,   434,
     436,   435,   437,   438,   438,   439,   440,   440,   441,   441,
     443,   442,   444,   445,   444,   446,   446,   447,   447,   448,
     448,   449,   450,   449,   451,   451,   452,   453,   454,   455,
     456,   457,   458,   458,   460,   461,   459,   462,   462,   464,
     463,   465,   463,   466,   467,   463,   468,   463,   469,   470,
     463,   463,   463,   471,   471,   471,   472,   471,   473,   474,
     475,   475,   476,   476,   476,   476,   477,   477,   478,   479,
     480,   481,   481,   483,   484,   482,   485,   485,   486,   486,
     488,   487,   487,   489,   489,   490,   490,   490,   491,   491,
     493,   492,   494,   495,   496,   496,   497,   498,   498,   499,
     500,   502,   501,   503,   503,   503,   504,   504,   505,   505,
     505,   505,   505,   505,   505,   505,   505,   505,   505,   505,
     505,   505,   506,   508,   507,   510,   509,   511,   511,   512,
     513,   513,   513,   513,   514,   514,   515,   515,   517,   516,
     518,   518,   520,   519,   522,   521,   523,   523,   524,   524,
     524,   525,   527,   526,   528,   528,   529,   529,   529,   531,
     530,   532,   532,   532,   532,   534,   533,   535,   536,   537,
     538,   539,   540,   540,   541,   542,   544,   543,   546,   545,
     547,   545,   548,   548,   550,   549,   551,   549,   552,   549,
     553,   553,   553,   554,   554,   556,   555,   555,   555,   557,
     555,   558,   555,   555,   555,   555,   555,   555,   555,   559,
     555,   555,   560,   561,   555,   562,   563,   564,   555,   565,
     555,   555,   566,   566,   566,   566,   566,   568,   567,   570,
     569,   571,   572,   571,   573,   573,   574,   574,   574,   575,
     575,   575,   575,   577,   576,   578,   578,   580,   579,   579,
     582,   581,   584,   585,   583,   586,   587,   588,   588,   589,
     589,   589,   589,   589,   589,   589,   589,   589,   589,   590,
     589,   589,   589,   591,   592,   592,   593,   593,   593,   593,
     593,   593,   593,   593,   594,   594,   594,   594,   595,   596,
     596,   597,   597,   597,   597,   599,   598,   600,   601,   602,
     601,   603,   604,   604,   605,   605,   606,   605,   607,   605,
     608,   609,   609,   609,   609,   609,   609,   609,   609,   609,
     609,   609,   609,   610,   611,   611,   612,   613,   613,   614,
     614,   614,   614,   616,   615,   615,   615,   617,   615,   618,
     619,   615,   620,   615,   621,   622,   615,   615,   615,   615,
     623,   615,   615,   615,   615,   615,   615,   615,   624,   624,
     624,   625,   624,   626,   626,   628,   627,   630,   629,   632,
     633,   631,   634,   634,   635,   635,   636,   636,   636,   636,
     637,   637,   637,   637,   638,   639,   638,   641,   640,   640,
     643,   642,   645,   646,   644,   647,   648,   649,   650,   651,
     652,   652,   653,   655,   654,   656,   656,   657,   658,   658,
     659,   660,   659,   661,   659,   659,   659,   662,   659,   663,
     659,   664,   659,   665,   665,   666,   667,   667,   668,   669,
     669,   670,   671,   671,   672,   672,   673,   673,   673,   674,
     674,   675,   675,   675,   676,   677,   678,   679,   680,   681,
     681,   682,   682,   683,   685,   684,   686,   684,   684,   684,
     687,   687,   687,   689,   688,   690,   690,   691,   691,   692,
     692,   692,   692,   693,   694,   695,   696,   697,   697,   698,
     700,   699,   701,   701,   702,   702,   704,   703,   705,   703,
     706,   703,   707,   703,   708,   703,   709,   703,   703,   710,
     711,   703,   712,   713,   703,   714,   714,   714,   715,   715,
     717,   716,   718,   718,   718,   718,   719,   719,   720,   720,
     722,   723,   721,   724,   724,   724,   724,   725,   725,   726,
     727,   728,   729,   729,   730,   732,   731,   733,   733,   734,
     734,   734,   735,   736,   734,   734,   738,   739,   737,   740,
     741,   740,   742,   742,   743,   743,   744,   745,   746,   747,
     747,   749,   750,   748,   751,   751,   752,   752,   752,   752,
     753,   753,   755,   754,   756,   754,   757,   757,   759,   758,
     760,   758,   761,   762,   763,   763,   765,   766,   764,   767,
     764,   768,   764,   764,   770,   771,   769,   772,   769,   773,
     774,   775,   776,   776,   777,   779,   778,   780,   780,   781,
     781,   781,   782,   782,   782,   783,   783,   785,   784,   786,
     784,   787,   784,   788,   784,   789,   784,   790,   784,   784,
     792,   791,   793,   793,   794,   794,   795,   795,   796,   796,
     797,   797,   797,   797,   798,   798,   799,   799,   799,   799,
     800,   801,   801,   803,   802,   804,   804,   805,   805,   807,
     806,   808,   806,   809,   809,   809,   809,   810,   811,   812,
     812,   813,   814,   814,   816,   817,   815,   818,   818,   820,
     819,   821,   821,   822,   822,   822,   823,   824,   825,   826,
     826,   827,   829,   830,   828,   831,   828,   832,   832,   833,
     833,   833,   834,   833,   833,   835,   833,   833,   837,   836,
     836,   836,   836,   836,   836,   838,   838,   840,   839,   841,
     839,   842,   839,   839,   839,   839,   839,   839,   839,   843,
     843,   844,   845,   844,   846,   847,   848,   849,   849,   850,
     852,   853,   851,   854,   854,   855,   856,   855,   857,   858,
     859,   860,   860,   862,   863,   861,   864,   865,   861,   866,
     866,   867,   868,   867,   869,   869,   870,   870,   870,   871,
     872,   873,   874,   874,   875,   875,   876,   876,   877,   877,
     877,   879,   878,   881,   880,   882,   883,   884,   885,   886,
     887,   888,   888,   890,   891,   889,   892,   892,   893,   894,
     895,   893,   896,   893,   897,   893,   898,   893,   893,   899,
     899,   900,   900,   900,   902,   901,   903,   903,   904,   904,
     904,   905,   906,   907,   908,   908,   910,   909
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     4,     0,     0,     4,     0,     3,     3,     0,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     0,     4,     2,     0,     4,     0,     4,     0,     4,
       1,     0,     5,     0,     2,     0,     5,     0,     5,     0,
       5,     0,     5,     0,     5,     0,     5,     0,     5,     0,
       5,     0,     5,     0,     5,     0,     5,     0,     5,     0,
       5,     0,     5,     0,     5,     0,     5,     0,     5,     2,
       0,     4,     0,     4,     1,     2,     2,     0,     1,     5,
       3,     3,     0,    14,     0,    14,     1,     1,     1,     1,
       1,     1,     1,     1,     0,     6,     3,     2,     0,     2,
       5,     2,     3,     3,     0,     2,     0,     0,     0,    10,
       0,     2,     2,     1,     3,     0,     4,     3,     0,     4,
       0,     4,     3,     2,     0,     0,    10,     0,     0,    12,
       0,    11,     1,     3,     4,     4,     4,     4,     0,     6,
       0,     6,     4,     0,     6,     3,     0,     2,     0,     2,
       2,     2,     0,     2,     1,     1,     0,     1,     0,     2,
       0,     2,     0,     2,     0,     1,     0,     1,     2,     1,
       0,     3,     0,     1,     2,     1,     0,     3,     0,     1,
       2,     1,     0,     3,     2,     2,     0,     1,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       0,     0,     3,     2,     0,     0,    11,     0,     5,     0,
       3,     0,     2,     0,     4,     0,     2,     2,     2,     2,
       0,     9,     2,     1,     1,     5,     0,     1,     2,     1,
       0,     3,     0,     0,     3,     0,     2,     0,     3,     0,
       1,     0,     0,     4,     0,     2,     1,     8,     1,     2,
       3,     3,     0,     2,     0,     0,     6,     0,     2,     0,
       7,     0,     4,     0,     0,    10,     0,     4,     0,     0,
      24,     1,     1,     4,     4,     6,     0,     4,     1,     1,
       0,     2,     4,     4,     4,     4,     0,     3,     2,     4,
       3,     0,     2,     0,     0,     7,     2,     3,     0,     2,
       0,     4,     3,     0,     2,     2,     2,     2,     1,     1,
       0,     4,     3,     3,     0,     2,     1,     0,     2,     3,
       2,     0,     4,     0,     2,     2,     0,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     0,     4,     0,     5,     0,     1,     3,
       1,     1,     1,     1,     2,     2,     2,     3,     0,     8,
       0,     1,     0,     6,     0,     4,     1,     2,     2,     2,
       2,     2,     0,     6,     1,     2,     3,     2,     4,     0,
       4,     2,     2,     2,     2,     0,     5,     2,     3,     2,
       3,     3,     0,     2,     3,     1,     0,     3,     0,     3,
       0,     6,     0,     2,     0,     6,     0,     6,     0,     6,
       0,     1,     2,     0,     2,     0,     4,     3,     2,     0,
       4,     0,     4,     3,     3,     3,     3,     3,     3,     0,
       4,     1,     0,     0,     5,     0,     0,     0,     8,     0,
       4,     1,     1,     1,     1,     1,     1,     0,     6,     0,
       4,     0,     0,     3,     0,     3,     1,     1,     1,     1,
       1,     1,     1,     0,     2,     0,     1,     0,     2,     2,
       0,     3,     0,     0,     6,     2,     2,     0,     2,     1,
       3,     2,     4,    10,     8,     9,    11,     1,     1,     0,
      10,     3,     2,     2,     0,     2,     4,     4,     4,     4,
       5,     5,     5,     5,     4,     4,     4,     4,     6,     0,
       2,     1,     1,     1,     1,     0,     3,     1,     1,     0,
       3,     2,     0,     2,     3,     3,     0,     4,     0,     4,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     0,     2,     3,     0,     2,     1,
       1,     1,     1,     0,     4,     3,     3,     0,     4,     0,
       0,     9,     0,     6,     0,     0,     8,     3,     2,     3,
       0,     4,     3,     3,     3,     3,     3,     1,     1,     1,
       1,     0,     3,     0,     1,     0,     5,     0,     4,     0,
       0,     7,     0,     3,     0,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     0,     0,     2,     0,     2,     2,
       0,     3,     0,     0,     7,     1,     3,     2,     3,     3,
       0,     2,     4,     0,     3,     0,     2,     1,     0,     2,
       3,     0,     4,     0,     4,     1,     2,     0,     4,     0,
       4,     0,     4,     2,     1,     1,     2,     1,     1,     2,
       1,     1,     2,     1,     0,     2,     2,     2,     2,     0,
       2,     2,     2,     2,     2,     3,     3,     3,     3,     0,
       2,     1,     1,     4,     0,     3,     0,     6,     4,     4,
       1,     2,     3,     0,     8,     0,     1,     0,     2,     3,
       3,     3,     3,     2,     2,     3,     3,     0,     2,     3,
       0,     3,     0,     2,     0,     1,     0,     5,     0,     4,
       0,     4,     0,     5,     0,     4,     0,     5,     1,     0,
       0,     6,     0,     0,     6,     0,     4,     8,     0,     2,
       0,     3,     0,     4,     8,    12,     0,     1,     2,     1,
       0,     0,     4,     0,     4,     8,    12,     0,     2,     2,
       3,     3,     0,     2,     3,     0,     6,     0,     2,     5,
       5,     3,     0,     0,     6,     1,     0,     0,     6,     0,
       0,     3,     0,     2,     1,     1,     2,     4,     3,     0,
       2,     0,     0,     8,     1,     1,     1,     2,     2,     2,
       0,     2,     0,     4,     0,     4,     0,     2,     0,     5,
       0,     5,     3,     3,     0,     2,     0,     0,    10,     0,
       6,     0,     6,     3,     0,     0,     6,     0,     3,     2,
       3,     3,     0,     2,     3,     0,     4,     0,     3,     0,
       1,     1,     0,     1,     1,     0,     2,     0,     7,     0,
       6,     0,     5,     0,     7,     0,     6,     0,     5,     1,
       0,     4,     0,     2,     3,     3,     0,     2,     0,     2,
       2,     2,     2,     2,     1,     2,     3,     3,     3,     3,
       2,     0,     2,     0,     6,     0,     2,     0,     2,     0,
       3,     0,     3,     1,     1,     1,     1,     3,     3,     0,
       1,     2,     0,     2,     0,     0,     7,     0,     2,     0,
       4,     0,     2,     2,     2,     2,     3,     3,     2,     0,
       2,     4,     0,     0,     6,     0,     4,     0,     2,     3,
       3,     3,     0,     4,     3,     0,     4,     1,     0,     4,
       2,     2,     2,     2,     2,     0,     2,     0,     4,     0,
       4,     0,     4,     2,     2,     2,     3,     3,     4,     0,
       2,     3,     0,     6,     3,     3,     2,     0,     2,     3,
       0,     0,     6,     0,     2,     3,     0,     6,     3,     3,
       2,     0,     2,     0,     0,     9,     0,     0,     9,     0,
       2,     3,     0,     6,     0,     2,     1,     1,     1,     2,
       2,     2,     0,     2,     0,     1,     0,     2,     1,     1,
       1,     0,     4,     0,     4,     2,     3,     3,     4,     3,
       2,     0,     2,     0,     0,     6,     0,     2,     2,     0,
       0,     8,     0,     4,     0,     4,     0,     5,     1,     0,
       2,     2,     2,     2,     0,     4,     0,     2,     2,     2,
       2,     3,     3,     2,     0,     2,     0,     8
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (defData, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, defData); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, defrData *defData)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (defData);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, defrData *defData)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep, defData);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule, defrData *defData)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)], defData);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule, defData); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, defrData *defData)
{
  YY_USE (yyvaluep);
  YY_USE (defData);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (defrData *defData)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, defData);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 4: /* $@1: %empty  */
#line 243 "def.y"
                { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3607 "def.tab.cpp"
    break;

  case 5: /* version_stmt: K_VERSION $@1 T_STRING ';'  */
#line 244 "def.y"
      {
        defData->VersionNum = defrData::convert_defname2num((yyvsp[-1].string));
        if (defData->VersionNum == 0 || defData->VersionNum > CURRENT_VERSION + 0.0001) {
          char temp[300];
          sprintf(temp,
                  "The execution has been stopped because the DEF parser %.2f does not support DEF file with version '%s'. Update your DEF file to version %.2f or earlier.",
                  CURRENT_VERSION, 
                  (yyvsp[-1].string), 
                  CURRENT_VERSION);
          defData->defError(6503, temp);
          return 1;
        }

        if (!defData->settings->AllowVer60Plus && defData->VersionNum > 5.8001) {
          char temp[300];
          sprintf(temp,
                  "The execution has been stopped because DEF parser can process %.2f version DEF files, but the translator software does not support processing of DEF files with version greater than 5.8. Update the translator software.",
                  CURRENT_VERSION);
          defData->defError(6565, temp);
          return 1;
        }

        if (defData->callbacks->VersionStrCbk) {
          CALLBACK(defData->callbacks->VersionStrCbk, defrVersionStrCbkType, (yyvsp[-1].string));
        } else if (defData->callbacks->VersionCbk) {
            CALLBACK(defData->callbacks->VersionCbk, defrVersionCbkType, defData->VersionNum);
        }
        if (defData->VersionNum > 5.3 && defData->VersionNum < 5.4)
          defData->defIgnoreVersion = 1;
        if (defData->VersionNum < 5.6)     // default to false before 5.6
          defData->names_case_sensitive = defData->session->reader_case_sensitive;
        else
          defData->names_case_sensitive = 1;
        defData->hasVer = 1;
        defData->doneDesign = 0;
    }
#line 3648 "def.tab.cpp"
    break;

  case 7: /* case_sens_stmt: K_NAMESCASESENSITIVE K_ON ';'  */
#line 283 "def.y"
      {
        if (defData->VersionNum < 5.6) {
          defData->names_case_sensitive = 1;
          if (defData->callbacks->CaseSensitiveCbk)
            CALLBACK(defData->callbacks->CaseSensitiveCbk, defrCaseSensitiveCbkType,
                     defData->names_case_sensitive); 
          defData->hasNameCase = 1;
        } else
          if (defData->callbacks->CaseSensitiveCbk) // write error only if cbk is set 
             if (defData->caseSensitiveWarnings++ < defData->settings->CaseSensitiveWarnings)
               defData->defWarning(7011, "The NAMESCASESENSITIVE statement is obsolete in version 5.6 and later.\nThe DEF parser will ignore this statement.");
      }
#line 3665 "def.tab.cpp"
    break;

  case 8: /* case_sens_stmt: K_NAMESCASESENSITIVE K_OFF ';'  */
#line 296 "def.y"
      {
        if (defData->VersionNum < 5.6) {
          defData->names_case_sensitive = 0;
          if (defData->callbacks->CaseSensitiveCbk)
            CALLBACK(defData->callbacks->CaseSensitiveCbk, defrCaseSensitiveCbkType,
                     defData->names_case_sensitive);
          defData->hasNameCase = 1;
        } else {
          if (defData->callbacks->CaseSensitiveCbk) { // write error only if cbk is set 
            if (defData->caseSensitiveWarnings++ < defData->settings->CaseSensitiveWarnings) {
              defData->defError(6504, "Def parser version 5.7 and later does not support NAMESCASESENSITIVE OFF.\nEither remove this optional construct or set it to ON.");
              CHKERR();
            }
          }
        }
      }
#line 3686 "def.tab.cpp"
    break;

  case 51: /* $@2: %empty  */
#line 336 "def.y"
                      {defData->dumb_mode = 1; defData->no_num = 1; }
#line 3692 "def.tab.cpp"
    break;

  case 52: /* design_name: K_DESIGN $@2 T_STRING ';'  */
#line 337 "def.y"
      {
            if (defData->callbacks->DesignCbk)
              CALLBACK(defData->callbacks->DesignCbk, defrDesignStartCbkType, (yyvsp[-1].string));
            defData->hasDes = 1;
          }
#line 3702 "def.tab.cpp"
    break;

  case 53: /* end_design: K_END K_DESIGN  */
#line 344 "def.y"
          {
            defData->doneDesign = 1;
            if (defData->callbacks->DesignEndCbk)
              CALLBACK(defData->callbacks->DesignEndCbk, defrDesignEndCbkType, 0);
            // 11/16/2001 - pcr 408334
            // Return 1 if there is any defData->errors during parsing
            if (defData->errors)
                return 1;

            if (!defData->hasVer) {
              char temp[300];
              sprintf(temp, "No VERSION statement found, using the default value %2g.", defData->VersionNum);
              defData->defWarning(7012, temp);            
            }
            if (!defData->hasNameCase && defData->VersionNum < 5.6)
              defData->defWarning(7013, "The DEF file is invalid if NAMESCASESENSITIVE is undefined.\nNAMESCASESENSITIVE is a mandatory statement in the DEF file with version 5.6 and earlier.\nTo define the NAMESCASESENSITIVE statement, refer to the LEF/DEF 5.5 and earlier Language Reference manual.");
            if (!defData->hasBusBit && defData->VersionNum < 5.6)
              defData->defWarning(7014, "The DEF file is invalid if BUSBITCHARS is undefined.\nBUSBITCHARS is a mandatory statement in the DEF file with version 5.6 and earlier.\nTo define the BUSBITCHARS statement, refer to the LEF/DEF 5.5 and earlier Language Reference manual.");
            if (!defData->hasDivChar && defData->VersionNum < 5.6)
              defData->defWarning(7015, "The DEF file is invalid if DIVIDERCHAR is undefined.\nDIVIDERCHAR is a mandatory statement in the DEF file with version 5.6 and earlier.\nTo define the DIVIDERCHAR statement, refer to the LEF/DEF 5.5 and earlier Language Reference manual.");
            if (!defData->hasDes)
              defData->defWarning(7016, "DESIGN is a mandatory statement in the DEF file. Ensure that it exists in the file.");
          }
#line 3730 "def.tab.cpp"
    break;

  case 54: /* $@3: %empty  */
#line 368 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3736 "def.tab.cpp"
    break;

  case 55: /* tech_name: K_TECH $@3 T_STRING ';'  */
#line 369 "def.y"
          { 
            if (defData->callbacks->TechnologyCbk)
              CALLBACK(defData->callbacks->TechnologyCbk, defrTechNameCbkType, (yyvsp[-1].string));
          }
#line 3745 "def.tab.cpp"
    break;

  case 56: /* $@4: %empty  */
#line 374 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 3751 "def.tab.cpp"
    break;

  case 57: /* array_name: K_ARRAY $@4 T_STRING ';'  */
#line 375 "def.y"
          { 
            if (defData->callbacks->ArrayNameCbk)
              CALLBACK(defData->callbacks->ArrayNameCbk, defrArrayNameCbkType, (yyvsp[-1].string));
          }
#line 3760 "def.tab.cpp"
    break;

  case 58: /* $@5: %empty  */
#line 380 "def.y"
                            { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3766 "def.tab.cpp"
    break;

  case 59: /* floorplan_name: K_FLOORPLAN $@5 T_STRING ';'  */
#line 381 "def.y"
          { 
            if (defData->callbacks->FloorPlanNameCbk)
              CALLBACK(defData->callbacks->FloorPlanNameCbk, defrFloorPlanNameCbkType, (yyvsp[-1].string));
          }
#line 3775 "def.tab.cpp"
    break;

  case 60: /* history: K_HISTORY  */
#line 387 "def.y"
          { 
            if (defData->callbacks->HistoryCbk)
              CALLBACK(defData->callbacks->HistoryCbk, defrHistoryCbkType, &defData->History_text[0]);
          }
#line 3784 "def.tab.cpp"
    break;

  case 61: /* $@6: %empty  */
#line 393 "def.y"
          {
            if (defData->callbacks->PropDefStartCbk)
              CALLBACK(defData->callbacks->PropDefStartCbk, defrPropDefStartCbkType, 0);
          }
#line 3793 "def.tab.cpp"
    break;

  case 62: /* prop_def_section: K_PROPERTYDEFINITIONS $@6 property_defs K_END K_PROPERTYDEFINITIONS  */
#line 398 "def.y"
          { 
            if (defData->callbacks->PropDefEndCbk)
              CALLBACK(defData->callbacks->PropDefEndCbk, defrPropDefEndCbkType, 0);
            defData->real_num = 0;     // just want to make sure it is reset 
          }
#line 3803 "def.tab.cpp"
    break;

  case 64: /* property_defs: property_defs property_def  */
#line 406 "def.y"
            { }
#line 3809 "def.tab.cpp"
    break;

  case 65: /* $@7: %empty  */
#line 408 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1; defData->Prop.clear(); }
#line 3815 "def.tab.cpp"
    break;

  case 66: /* property_def: K_DESIGN $@7 T_STRING property_type_and_val ';'  */
#line 410 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("design", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->DesignProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3827 "def.tab.cpp"
    break;

  case 67: /* $@8: %empty  */
#line 417 "def.y"
                { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3833 "def.tab.cpp"
    break;

  case 68: /* property_def: K_NET $@8 T_STRING property_type_and_val ';'  */
#line 419 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("net", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->NetProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3845 "def.tab.cpp"
    break;

  case 69: /* $@9: %empty  */
#line 426 "def.y"
                 { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3851 "def.tab.cpp"
    break;

  case 70: /* property_def: K_SNET $@9 T_STRING property_type_and_val ';'  */
#line 428 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("specialnet", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->SNetProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3863 "def.tab.cpp"
    break;

  case 71: /* $@10: %empty  */
#line 435 "def.y"
                   { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3869 "def.tab.cpp"
    break;

  case 72: /* property_def: K_REGION $@10 T_STRING property_type_and_val ';'  */
#line 437 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("region", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->RegionProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3881 "def.tab.cpp"
    break;

  case 73: /* $@11: %empty  */
#line 444 "def.y"
                  { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3887 "def.tab.cpp"
    break;

  case 74: /* property_def: K_GROUP $@11 T_STRING property_type_and_val ';'  */
#line 446 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("group", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->GroupProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3899 "def.tab.cpp"
    break;

  case 75: /* $@12: %empty  */
#line 453 "def.y"
                      { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3905 "def.tab.cpp"
    break;

  case 76: /* property_def: K_COMPONENT $@12 T_STRING property_type_and_val ';'  */
#line 455 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("component", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->CompProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3917 "def.tab.cpp"
    break;

  case 77: /* $@13: %empty  */
#line 462 "def.y"
                { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3923 "def.tab.cpp"
    break;

  case 78: /* property_def: K_ROW $@13 T_STRING property_type_and_val ';'  */
#line 464 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("row", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->RowProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3935 "def.tab.cpp"
    break;

  case 79: /* $@14: %empty  */
#line 473 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3941 "def.tab.cpp"
    break;

  case 80: /* property_def: K_COMPONENTPIN $@14 T_STRING property_type_and_val ';'  */
#line 475 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("componentpin", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->CompPinProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3953 "def.tab.cpp"
    break;

  case 81: /* $@15: %empty  */
#line 483 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3959 "def.tab.cpp"
    break;

  case 82: /* property_def: K_NONDEFAULTRULE $@15 T_STRING property_type_and_val ';'  */
#line 485 "def.y"
            {
              if (defData->VersionNum < 5.6) {
                if (defData->nonDefaultWarnings++ < defData->settings->NonDefaultWarnings) {
                  defData->defMsg = (char*)malloc(1000); 
                  sprintf (defData->defMsg,
                     "The NONDEFAULTRULE statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6505, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              } else {
                if (defData->callbacks->PropCbk) {
                  defData->Prop.setPropType("nondefaultrule", (yyvsp[-2].string));
                  CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                }
                defData->session->NDefProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
              }
            }
#line 3982 "def.tab.cpp"
    break;

  case 83: /* $@16: %empty  */
#line 504 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3988 "def.tab.cpp"
    break;

  case 84: /* property_def: K_BLOCKAGE $@16 T_STRING property_type_and_val ';'  */
#line 506 "def.y"
          {
                if (defData->VersionNum < 6.0 - 0.00001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [BLOCKAGE propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                  if (defData->callbacks->PropCbk) {
                    defData->Prop.setPropType("blockage", (yyvsp[-2].string));
                    CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                  }
                  defData->session->BlockageProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
               }
           }
#line 4006 "def.tab.cpp"
    break;

  case 85: /* $@17: %empty  */
#line 520 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4012 "def.tab.cpp"
    break;

  case 86: /* property_def: K_PIN $@17 T_STRING property_type_and_val ';'  */
#line 522 "def.y"
          {
                if (defData->VersionNum < 6.0 - 0.00001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [PIN propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                  if (defData->callbacks->PropCbk) {
                    defData->Prop.setPropType("pin", (yyvsp[-2].string));
                    CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                  }
                  defData->session->PinProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
               }
           }
#line 4030 "def.tab.cpp"
    break;

  case 87: /* $@18: %empty  */
#line 536 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4036 "def.tab.cpp"
    break;

  case 88: /* property_def: K_PINSHAPE $@18 T_STRING property_type_and_val ';'  */
#line 538 "def.y"
          {
                if (defData->VersionNum < 6.0 - 0.00001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [PINSHAPE propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                  if (defData->callbacks->PropCbk) {
                    defData->Prop.setPropType("pinshape", (yyvsp[-2].string));
                    CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                  }
                  defData->session->PinPropShape.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
               }
           }
#line 4054 "def.tab.cpp"
    break;

  case 89: /* $@19: %empty  */
#line 552 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4060 "def.tab.cpp"
    break;

  case 90: /* property_def: K_ROUTE $@19 T_STRING property_type_and_val ';'  */
#line 554 "def.y"
          {
                if (defData->VersionNum < 6.0 - 0.0001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [ROUTE propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                  if (defData->callbacks->PropCbk) {
                    defData->Prop.setPropType("route", (yyvsp[-2].string));
                    CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                  }
                  defData->session->RouteProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
               }
           }
#line 4078 "def.tab.cpp"
    break;

  case 91: /* $@20: %empty  */
#line 568 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4084 "def.tab.cpp"
    break;

  case 92: /* property_def: K_SCANCHAIN $@20 T_STRING property_type_and_val ';'  */
#line 570 "def.y"
            {
                if (defData->VersionNum < 6.0 - 0.0001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [SCANCHAIN ...]")) {
                        CHKERR();
                    }
                } else {
                    if (defData->callbacks->PropCbk) {
                        defData->Prop.setPropType("scanchain", (yyvsp[-2].string));
                        CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
                    }
                    defData->session->ScanChainProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
                }
            }
#line 4102 "def.tab.cpp"
    break;

  case 93: /* $@21: %empty  */
#line 584 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4108 "def.tab.cpp"
    break;

  case 94: /* property_def: K_SPECIALROUTE $@21 T_STRING property_type_and_val ';'  */
#line 586 "def.y"
            {
                if (defData->VersionNum < 6.0 - 0.0001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [SPECIALROUTE propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                    if (defData->callbacks->PropCbk) {
                        defData->Prop.setPropType("specialroute", (yyvsp[-2].string));
                        CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop)
                    }
                    defData->session->SpecialRouteProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
                }
            }
#line 4126 "def.tab.cpp"
    break;

  case 95: /* $@22: %empty  */
#line 600 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4132 "def.tab.cpp"
    break;

  case 96: /* property_def: K_VIA $@22 T_STRING property_type_and_val ';'  */
#line 602 "def.y"
            {
                if (defData->VersionNum < 6.0 - 0.0001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [VIA propName propType ... ]")) {
                        CHKERR();
                    }
                } else {
                    if (defData->callbacks->PropCbk) {
                        defData->Prop.setPropType("via", (yyvsp[-2].string));
                        CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop)
                    }
                    defData->session->ViaProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
                }
            }
#line 4150 "def.tab.cpp"
    break;

  case 97: /* $@23: %empty  */
#line 616 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 4156 "def.tab.cpp"
    break;

  case 98: /* property_def: K_TRACK $@23 T_STRING property_type_and_val ';'  */
#line 618 "def.y"
            {
                if (defData->VersionNum < 6.0 - 0.0001) {
                    if (defData->def60NewSyntaxError("PROPERTYDEFINITIONS ... [TRACK propName propType ...]")) {
                        CHKERR();
                    }
                } else {
                    if (defData->callbacks->PropCbk) {
                        defData->Prop.setPropType("track", (yyvsp[-2].string));
                        CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop)
                    }
                    defData->session->TrackProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
                }
            }
#line 4174 "def.tab.cpp"
    break;

  case 99: /* property_def: error ';'  */
#line 631 "def.y"
                    { yyerrok; yyclearin;}
#line 4180 "def.tab.cpp"
    break;

  case 100: /* $@24: %empty  */
#line 633 "def.y"
                                 { defData->real_num = 0; }
#line 4186 "def.tab.cpp"
    break;

  case 101: /* property_type_and_val: K_INTEGER $@24 opt_range opt_num_val  */
#line 634 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropInteger();
              defData->defPropDefType = 'I';
            }
#line 4195 "def.tab.cpp"
    break;

  case 102: /* $@25: %empty  */
#line 638 "def.y"
                 { defData->real_num = 1; }
#line 4201 "def.tab.cpp"
    break;

  case 103: /* property_type_and_val: K_REAL $@25 opt_range opt_num_val  */
#line 639 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropReal();
              defData->defPropDefType = 'R';
              defData->real_num = 0;
            }
#line 4211 "def.tab.cpp"
    break;

  case 104: /* property_type_and_val: K_STRING  */
#line 645 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropString();
              defData->defPropDefType = 'S';
            }
#line 4220 "def.tab.cpp"
    break;

  case 105: /* property_type_and_val: K_STRING QSTRING  */
#line 650 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropQString((yyvsp[0].string));
              defData->defPropDefType = 'Q';
            }
#line 4229 "def.tab.cpp"
    break;

  case 106: /* property_type_and_val: K_NAMEMAPSTRING T_STRING  */
#line 655 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropNameMapString((yyvsp[0].string));
              defData->defPropDefType = 'S';
            }
#line 4238 "def.tab.cpp"
    break;

  case 108: /* opt_num_val: NUMBER  */
#line 662 "def.y"
            { if (defData->callbacks->PropCbk) defData->Prop.setNumber((yyvsp[0].dval)); }
#line 4244 "def.tab.cpp"
    break;

  case 109: /* units: K_UNITS K_DISTANCE K_MICRONS NUMBER ';'  */
#line 665 "def.y"
          {
            if (defData->callbacks->UnitsCbk) {
              if (defData->defValidNum((int)(yyvsp[-1].dval)))
                CALLBACK(defData->callbacks->UnitsCbk,  defrUnitsCbkType, (yyvsp[-1].dval));
            }
          }
#line 4255 "def.tab.cpp"
    break;

  case 110: /* divider_char: K_DIVIDERCHAR QSTRING ';'  */
#line 673 "def.y"
          {
            if (defData->callbacks->DividerCbk)
              CALLBACK(defData->callbacks->DividerCbk, defrDividerCbkType, (yyvsp[-1].string));
            defData->hasDivChar = 1;
          }
#line 4265 "def.tab.cpp"
    break;

  case 111: /* bus_bit_chars: K_BUSBITCHARS QSTRING ';'  */
#line 680 "def.y"
          { 
            if (defData->callbacks->BusBitCbk)
              CALLBACK(defData->callbacks->BusBitCbk, defrBusBitCbkType, (yyvsp[-1].string));
            defData->hasBusBit = 1;
          }
#line 4275 "def.tab.cpp"
    break;

  case 112: /* $@26: %empty  */
#line 686 "def.y"
                     {defData->dumb_mode = 1;defData->no_num = 1; }
#line 4281 "def.tab.cpp"
    break;

  case 113: /* canplace: K_CANPLACE $@26 T_STRING NUMBER NUMBER orient K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER ';'  */
#line 688 "def.y"
            {
              if (defData->callbacks->CanplaceCbk) {
                defData->Canplace.setName((yyvsp[-11].string));
                defData->Canplace.setLocation((yyvsp[-10].dval),(yyvsp[-9].dval));
                defData->Canplace.setOrient((yyvsp[-8].integer));
                defData->Canplace.setDo((yyvsp[-6].dval),(yyvsp[-4].dval),(yyvsp[-2].dval),(yyvsp[-1].dval));
                CALLBACK(defData->callbacks->CanplaceCbk, defrCanplaceCbkType,
                &(defData->Canplace));
              }
            }
#line 4296 "def.tab.cpp"
    break;

  case 114: /* $@27: %empty  */
#line 698 "def.y"
                             {defData->dumb_mode = 1;defData->no_num = 1; }
#line 4302 "def.tab.cpp"
    break;

  case 115: /* cannotoccupy: K_CANNOTOCCUPY $@27 T_STRING NUMBER NUMBER orient K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER ';'  */
#line 700 "def.y"
            {
              if (defData->callbacks->CannotOccupyCbk) {
                defData->CannotOccupy.setName((yyvsp[-11].string));
                defData->CannotOccupy.setLocation((yyvsp[-10].dval),(yyvsp[-9].dval));
                defData->CannotOccupy.setOrient((yyvsp[-8].integer));
                defData->CannotOccupy.setDo((yyvsp[-6].dval),(yyvsp[-4].dval),(yyvsp[-2].dval),(yyvsp[-1].dval));
                CALLBACK(defData->callbacks->CannotOccupyCbk, defrCannotOccupyCbkType,
                        &(defData->CannotOccupy));
              }
            }
#line 4317 "def.tab.cpp"
    break;

  case 116: /* orient: K_N  */
#line 711 "def.y"
               {(yyval.integer) = 0;}
#line 4323 "def.tab.cpp"
    break;

  case 117: /* orient: K_W  */
#line 712 "def.y"
               {(yyval.integer) = 1;}
#line 4329 "def.tab.cpp"
    break;

  case 118: /* orient: K_S  */
#line 713 "def.y"
               {(yyval.integer) = 2;}
#line 4335 "def.tab.cpp"
    break;

  case 119: /* orient: K_E  */
#line 714 "def.y"
               {(yyval.integer) = 3;}
#line 4341 "def.tab.cpp"
    break;

  case 120: /* orient: K_FN  */
#line 715 "def.y"
               {(yyval.integer) = 4;}
#line 4347 "def.tab.cpp"
    break;

  case 121: /* orient: K_FW  */
#line 716 "def.y"
               {(yyval.integer) = 5;}
#line 4353 "def.tab.cpp"
    break;

  case 122: /* orient: K_FS  */
#line 717 "def.y"
               {(yyval.integer) = 6;}
#line 4359 "def.tab.cpp"
    break;

  case 123: /* orient: K_FE  */
#line 718 "def.y"
               {(yyval.integer) = 7;}
#line 4365 "def.tab.cpp"
    break;

  case 124: /* $@28: %empty  */
#line 721 "def.y"
          {
            defData->Geometries.Reset();
          }
#line 4373 "def.tab.cpp"
    break;

  case 125: /* die_area: K_DIEAREA $@28 firstPt nextPt otherPts ';'  */
#line 725 "def.y"
          {
            if (defData->callbacks->DieAreaCbk) {
               defData->DieArea.addPoint(&defData->Geometries);
               CALLBACK(defData->callbacks->DieAreaCbk, defrDieAreaCbkType, &(defData->DieArea));
            }
          }
#line 4384 "def.tab.cpp"
    break;

  case 126: /* pin_cap_rule: start_def_cap pin_caps end_def_cap  */
#line 734 "def.y"
            { }
#line 4390 "def.tab.cpp"
    break;

  case 127: /* start_def_cap: K_DEFAULTCAP NUMBER  */
#line 737 "def.y"
        {
          if (defData->VersionNum < 5.4) {
             if (defData->callbacks->DefaultCapCbk)
                CALLBACK(defData->callbacks->DefaultCapCbk, defrDefaultCapCbkType, ROUND((yyvsp[0].dval)));
          } else {
             if (defData->callbacks->DefaultCapCbk) // write error only if cbk is set 
                if (defData->defaultCapWarnings++ < defData->settings->DefaultCapWarnings)
                   defData->defWarning(7017, "The DEFAULTCAP statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
#line 4405 "def.tab.cpp"
    break;

  case 130: /* pin_cap: K_MINPINS NUMBER K_WIRECAP NUMBER ';'  */
#line 753 "def.y"
          {
            if (defData->VersionNum < 5.4) {
              if (defData->callbacks->PinCapCbk) {
                defData->PinCap.setPin(ROUND((yyvsp[-3].dval)));
                defData->PinCap.setCap((yyvsp[-1].dval));
                CALLBACK(defData->callbacks->PinCapCbk, defrPinCapCbkType, &(defData->PinCap));
              }
            }
          }
#line 4419 "def.tab.cpp"
    break;

  case 131: /* end_def_cap: K_END K_DEFAULTCAP  */
#line 764 "def.y"
            { }
#line 4425 "def.tab.cpp"
    break;

  case 132: /* pin_rule: start_pins pins end_pins  */
#line 767 "def.y"
            { }
#line 4431 "def.tab.cpp"
    break;

  case 133: /* start_pins: K_PINS NUMBER ';'  */
#line 770 "def.y"
          { 
            if (defData->callbacks->StartPinsCbk)
              CALLBACK(defData->callbacks->StartPinsCbk, defrStartPinsCbkType, ROUND((yyvsp[-1].dval)));
          }
#line 4440 "def.tab.cpp"
    break;

  case 136: /* $@29: %empty  */
#line 779 "def.y"
         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 4446 "def.tab.cpp"
    break;

  case 137: /* $@30: %empty  */
#line 780 "def.y"
         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 4452 "def.tab.cpp"
    break;

  case 138: /* $@31: %empty  */
#line 781 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              defData->Pin.Setup((yyvsp[-4].string), (yyvsp[0].string));
            }
            defData->hasPort = 0;
            defData->hadPortOnce = 0;
          }
#line 4464 "def.tab.cpp"
    break;

  case 139: /* pin: '-' $@29 T_STRING '+' K_NET $@30 T_STRING $@31 pin_options ';'  */
#line 789 "def.y"
          { 
            if (defData->callbacks->PinCbk)
              CALLBACK(defData->callbacks->PinCbk, defrPinCbkType, &defData->Pin);
          }
#line 4473 "def.tab.cpp"
    break;

  case 142: /* pin_option: '+' K_SPECIAL  */
#line 798 "def.y"
          {
            if (defData->callbacks->PinCbk)
              defData->Pin.setSpecial();
          }
#line 4482 "def.tab.cpp"
    break;

  case 143: /* pin_option: extension_stmt  */
#line 804 "def.y"
          { 
            if (defData->callbacks->PinExtCbk)
              CALLBACK(defData->callbacks->PinExtCbk, defrPinExtCbkType, &defData->History_text[0]);
          }
#line 4491 "def.tab.cpp"
    break;

  case 144: /* pin_option: '+' K_DIRECTION T_STRING  */
#line 810 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.setDirection((yyvsp[0].string));
          }
#line 4500 "def.tab.cpp"
    break;

  case 145: /* $@32: %empty  */
#line 816 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + PROPERTY {propName propVal}...")) {
                    CHKERR();
                }
            } 
            defData->dumb_mode = DEF_MAX_INT; 
          }
#line 4513 "def.tab.cpp"
    break;

  case 146: /* pin_option: '+' K_PROPERTY $@32 pin_prop_name_values  */
#line 825 "def.y"
          {
            defData->dumb_mode = 0; 
          }
#line 4521 "def.tab.cpp"
    break;

  case 147: /* pin_option: '+' K_NETEXPR QSTRING  */
#line 830 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The NETEXPR statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6506, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("PINS ... + NETEXPR \"netExprPropName defaultNetName\"")) {
                    CHKERR();
                }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                defData->Pin.setNetExpr((yyvsp[0].string));
              }
            }
          }
#line 4549 "def.tab.cpp"
    break;

  case 148: /* $@33: %empty  */
#line 854 "def.y"
                                  { defData->dumb_mode = 1; }
#line 4555 "def.tab.cpp"
    break;

  case 149: /* pin_option: '+' K_SUPPLYSENSITIVITY $@33 T_STRING  */
#line 855 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The SUPPLYSENSITIVITY statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6507, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
                defData->Pin.setSupplySens((yyvsp[0].string));
            }
          }
#line 4578 "def.tab.cpp"
    break;

  case 150: /* $@34: %empty  */
#line 874 "def.y"
                                  { defData->dumb_mode = 1; }
#line 4584 "def.tab.cpp"
    break;

  case 151: /* pin_option: '+' K_GROUNDSENSITIVITY $@34 T_STRING  */
#line 875 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The GROUNDSENSITIVITY statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6508, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
                defData->Pin.setGroundSens((yyvsp[0].string));
            }
          }
#line 4607 "def.tab.cpp"
    break;

  case 152: /* pin_option: '+' K_USE use_type  */
#line 895 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) defData->Pin.setUse((yyvsp[0].string));
          }
#line 4615 "def.tab.cpp"
    break;

  case 153: /* pin_option: '+' K_PORT  */
#line 899 "def.y"
          {
            if (defData->VersionNum < 5.7) {
               if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                 if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                     (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                   defData->defMsg = (char*)malloc(10000);
                   sprintf (defData->defMsg,
                     "The PORT in PINS is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                   defData->defError(6555, defData->defMsg);
                   free(defData->defMsg);
                   CHKERR();
                 }
               }
            } else {
               if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                   defData->Pin.addPort();
               }

               defData->hasPort = 1;
               defData->hadPortOnce = 1;
            }
          }
#line 4642 "def.tab.cpp"
    break;

  case 154: /* $@35: %empty  */
#line 922 "def.y"
                      { defData->dumb_mode = 1; }
#line 4648 "def.tab.cpp"
    break;

  case 155: /* $@36: %empty  */
#line 923 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              if (defData->hasPort) {
                 defData->Pin.addPortLayer((yyvsp[0].string));
              } else if (defData->hadPortOnce) {
                 if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                   (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                   defData->defError(7418, "syntax error");
                   CHKERR();
                 }
              } else {
                 defData->Pin.addLayer((yyvsp[0].string));
              }
            }
          }
#line 4668 "def.tab.cpp"
    break;

  case 156: /* pin_option: '+' K_LAYER $@35 T_STRING $@36 pin_layer_mask_opt pin_layer_spacing_opt pin_layer_props_opt pt pt  */
#line 939 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              if (defData->hasPort)
                 defData->Pin.addPortLayerPts((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
              else if (!defData->hadPortOnce)
                 defData->Pin.addLayerPts((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
            }
          }
#line 4681 "def.tab.cpp"
    break;

  case 157: /* $@37: %empty  */
#line 948 "def.y"
                        { defData->dumb_mode = 1; }
#line 4687 "def.tab.cpp"
    break;

  case 158: /* $@38: %empty  */
#line 949 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The POLYGON statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6509, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort) {
                   defData->Pin.addPortPolygon((yyvsp[0].string));
                } else if (defData->hadPortOnce) {
                   if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                     (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                     defData->defError(7418, "syntax error");
                     CHKERR();
                   }
                } else {
                   defData->Pin.addPolygon((yyvsp[0].string));
                }
              }
            }
            
            defData->Geometries.Reset();            
          }
#line 4723 "def.tab.cpp"
    break;

  case 159: /* pin_option: '+' K_POLYGON $@37 T_STRING $@38 pin_poly_mask_opt pin_poly_spacing_opt pin_poly_props_opt firstPt nextPt nextPt otherPts  */
#line 981 "def.y"
          {
            if (defData->VersionNum >= 5.6) {  // only add if 5.6 or beyond
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortPolygonPts(&defData->Geometries);
                else if (!defData->hadPortOnce)
                   defData->Pin.addPolygonPts(&defData->Geometries);
              }
            }
          }
#line 4738 "def.tab.cpp"
    break;

  case 160: /* $@39: %empty  */
#line 991 "def.y"
                    { defData->dumb_mode = 1; }
#line 4744 "def.tab.cpp"
    break;

  case 161: /* pin_option: '+' K_VIA $@39 T_STRING pin_via_mask_opt pin_via_props_opt via_orient '(' NUMBER NUMBER ')'  */
#line 992 "def.y"
          {
            if (defData->VersionNum < 5.7) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The PIN VIA statement is available in version 5.7 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6556, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort) {
                   defData->Pin.addPortVia((yyvsp[-7].string), 
                                           (int)(yyvsp[-2].dval),
                                           (int)(yyvsp[-1].dval), 
                                           (yyvsp[-6].integer), 
                                           (yyvsp[-4].integer));
                } else if (defData->hadPortOnce) {
                   if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                     (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                     defData->defError(7418, "syntax error");
                     CHKERR();
                   }
                } else {
                   defData->Pin.addVia((yyvsp[-7].string), 
                                       (int)(yyvsp[-2].dval),
                                       (int)(yyvsp[-1].dval), 
                                       (yyvsp[-6].integer), 
                                       (yyvsp[-4].integer));
                }
              }
            }
          }
#line 4786 "def.tab.cpp"
    break;

  case 162: /* pin_option: placement_status  */
#line 1030 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + COVER|FIXED|PLACED")) {
                    CHKERR();
                }
            } 

            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              if (defData->hasPort) {
                  defData->Pin.setPortPlacement((yyvsp[0].integer), 0, 0, 0);
                  defData->hasPort = 0;
                  defData->hadPortOnce = 1;
               } else if (defData->hadPortOnce) {
                 if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                   (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                   defData->defError(7418, "syntax error");
                   CHKERR();
                 }
                } else {
                    defData->Pin.setPlacement((yyvsp[0].integer), 0, 0, 0);
                 }
            }
          }
#line 4814 "def.tab.cpp"
    break;

  case 163: /* pin_option: placement_status pt orient  */
#line 1055 "def.y"
          {
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("PINS ... + COVER|FIXED|PLACED pt orient")) {
                    CHKERR();
                }
            } 

            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              if (defData->hasPort) {
                  defData->Pin.setPortPlacement((yyvsp[-2].integer), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
                  defData->hasPort = 0;
                  defData->hadPortOnce = 1;
               } else if (defData->hadPortOnce) {
                 if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                   (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                   defData->defError(7418, "syntax error");
                   CHKERR();
                 }
                } else {
                    defData->Pin.setPlacement((yyvsp[-2].integer), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
                 }
            }
          }
#line 4842 "def.tab.cpp"
    break;

  case 164: /* pin_option: '+' K_ANTENNAPINPARTIALMETALAREA NUMBER pin_layer_opt  */
#line 1081 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINPARTIALMETALAREA statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6510, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinPartialMetalArea((int)(yyvsp[-1].dval), (yyvsp[0].string)); 
          }
#line 4864 "def.tab.cpp"
    break;

  case 165: /* pin_option: '+' K_ANTENNAPINPARTIALMETALSIDEAREA NUMBER pin_layer_opt  */
#line 1099 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINPARTIALMETALSIDEAREA statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6511, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinPartialMetalSideArea((int)(yyvsp[-1].dval), (yyvsp[0].string)); 
          }
#line 4886 "def.tab.cpp"
    break;

  case 166: /* pin_option: '+' K_ANTENNAPINGATEAREA NUMBER pin_layer_opt  */
#line 1117 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINGATEAREA statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6512, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
                defData->Pin.addAPinGateArea((int)(yyvsp[-1].dval), (yyvsp[0].string)); 
            }
#line 4908 "def.tab.cpp"
    break;

  case 167: /* pin_option: '+' K_ANTENNAPINDIFFAREA NUMBER pin_layer_opt  */
#line 1135 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINDIFFAREA statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6513, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinDiffArea((int)(yyvsp[-1].dval), (yyvsp[0].string)); 
          }
#line 4930 "def.tab.cpp"
    break;

  case 168: /* $@40: %empty  */
#line 1152 "def.y"
                                                    {defData->dumb_mode=1;}
#line 4936 "def.tab.cpp"
    break;

  case 169: /* pin_option: '+' K_ANTENNAPINMAXAREACAR NUMBER K_LAYER $@40 T_STRING  */
#line 1153 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINMAXAREACAR statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6514, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinMaxAreaCar((int)(yyvsp[-3].dval), (yyvsp[0].string)); 
          }
#line 4958 "def.tab.cpp"
    break;

  case 170: /* $@41: %empty  */
#line 1170 "def.y"
                                                        {defData->dumb_mode=1;}
#line 4964 "def.tab.cpp"
    break;

  case 171: /* pin_option: '+' K_ANTENNAPINMAXSIDEAREACAR NUMBER K_LAYER $@41 T_STRING  */
#line 1172 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINMAXSIDEAREACAR statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6515, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinMaxSideAreaCar((int)(yyvsp[-3].dval), (yyvsp[0].string)); 
          }
#line 4986 "def.tab.cpp"
    break;

  case 172: /* pin_option: '+' K_ANTENNAPINPARTIALCUTAREA NUMBER pin_layer_opt  */
#line 1190 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINPARTIALCUTAREA statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6516, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinPartialCutArea((int)(yyvsp[-1].dval), (yyvsp[0].string)); 
          }
#line 5008 "def.tab.cpp"
    break;

  case 173: /* $@42: %empty  */
#line 1207 "def.y"
                                                   {defData->dumb_mode=1;}
#line 5014 "def.tab.cpp"
    break;

  case 174: /* pin_option: '+' K_ANTENNAPINMAXCUTCAR NUMBER K_LAYER $@42 T_STRING  */
#line 1208 "def.y"
          {
            if (defData->VersionNum <= 5.3) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAPINMAXCUTCAR statement is available in version 5.4 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6517, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAPinMaxCutCar((int)(yyvsp[-3].dval), (yyvsp[0].string)); 
          }
#line 5036 "def.tab.cpp"
    break;

  case 175: /* pin_option: '+' K_ANTENNAMODEL pin_oxide  */
#line 1226 "def.y"
          {  // 5.5 syntax 
            if (defData->VersionNum < 5.5) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The ANTENNAMODEL statement is available in version 5.5 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6518, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            }
          }
#line 5056 "def.tab.cpp"
    break;

  case 177: /* pin_prop_name_values: pin_prop_name_values prop_name_value  */
#line 1244 "def.y"
        {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                defData->setPropDataType((yyvsp[0].prop), "PIN", defData->session->PinProp);
                defData->Pin.addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = 0;
            }

            delete (yyvsp[0].prop);
        }
#line 5070 "def.tab.cpp"
    break;

  case 178: /* $@43: %empty  */
#line 1254 "def.y"
        {
        }
#line 5077 "def.tab.cpp"
    break;

  case 179: /* prop_name_value: $@43 prop_name_value_pair  */
#line 1257 "def.y"
        {
            (yyval.prop) = (yyvsp[0].prop);
        }
#line 5085 "def.tab.cpp"
    break;

  case 180: /* prop_name_value_pair: T_STRING NUMBER  */
#line 1263 "def.y"
        {
            char*  str = defData->ringCopy("                       ");
            // For backword compatibility, also set the string value 
            sprintf(str, "%g", (yyvsp[0].dval));

            (yyval.prop) = new defiProp(defData);
            (yyval.prop)->setPropQString(str);
            (yyval.prop)->setNumber((yyvsp[0].dval));
            (yyval.prop)->setPropType("", (yyvsp[-1].string));
            (yyval.prop)->setPropInteger();
        }
#line 5101 "def.tab.cpp"
    break;

  case 181: /* prop_name_value_pair: T_STRING prop_string_value  */
#line 1276 "def.y"
        {
            (yyval.prop) = new defiProp(defData);
            (yyval.prop)->setPropQString((yyvsp[0].string));
            (yyval.prop)->setPropString();
            (yyval.prop)->setPropType("", (yyvsp[-1].string));
        }
#line 5112 "def.tab.cpp"
    break;

  case 183: /* net_prop_name_values: net_prop_name_values prop_name_value  */
#line 1285 "def.y"
    {
        defData->addProp((yyvsp[0].prop));
    }
#line 5120 "def.tab.cpp"
    break;

  case 184: /* prop_string_value: T_STRING  */
#line 1290 "def.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 5128 "def.tab.cpp"
    break;

  case 185: /* prop_string_value: QSTRING  */
#line 1294 "def.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 5136 "def.tab.cpp"
    break;

  case 186: /* via_orient: %empty  */
#line 1298 "def.y"
          { 
            (yyval.integer) = 0;
          }
#line 5144 "def.tab.cpp"
    break;

  case 187: /* via_orient: orient  */
#line 1302 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + VIA viaName [MASK] orient pt")) {
                    CHKERR();
                }
            } 

            (yyval.integer) = (yyvsp[0].integer);
          }
#line 5158 "def.tab.cpp"
    break;

  case 189: /* pin_layer_mask_opt: K_MASK NUMBER  */
#line 1313 "def.y"
         { 
           if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->pinWarnings, defData->settings->PinWarnings)) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortLayerMask((int)(yyvsp[0].dval));
                else
                   defData->Pin.addLayerMask((int)(yyvsp[0].dval));
              }
           }
         }
#line 5173 "def.tab.cpp"
    break;

  case 190: /* pin_via_mask_opt: %empty  */
#line 1326 "def.y"
        { (yyval.integer) = 0; }
#line 5179 "def.tab.cpp"
    break;

  case 191: /* pin_via_mask_opt: K_MASK NUMBER  */
#line 1328 "def.y"
         { 
           if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->pinWarnings, defData->settings->PinWarnings)) {
             (yyval.integer) = (yyvsp[0].dval);
           }
         }
#line 5189 "def.tab.cpp"
    break;

  case 193: /* pin_poly_mask_opt: K_MASK NUMBER  */
#line 1336 "def.y"
         { 
           if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->pinWarnings, defData->settings->PinWarnings)) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortPolyMask((int)(yyvsp[0].dval));
                else
                   defData->Pin.addPolyMask((int)(yyvsp[0].dval));
              }
           }
         }
#line 5204 "def.tab.cpp"
    break;

  case 195: /* pin_layer_spacing_opt: pin_layer_spacing  */
#line 1349 "def.y"
          {
          }
#line 5211 "def.tab.cpp"
    break;

  case 197: /* pin_layer_props_opt: pin_layer_props  */
#line 1354 "def.y"
    {}
#line 5217 "def.tab.cpp"
    break;

  case 199: /* pin_layer_props: pin_layer_prop  */
#line 1358 "def.y"
    {}
#line 5223 "def.tab.cpp"
    break;

  case 200: /* $@44: %empty  */
#line 1361 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 5231 "def.tab.cpp"
    break;

  case 201: /* pin_layer_prop: K_PROPERTY $@44 prop_name_value  */
#line 1365 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + PORT ... + LAYER ... PROPERTY propName propValue")) {
                    CHKERR();
                }
            }

            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              defData->setPropDataType((yyvsp[0].prop), "PINSHAPE", defData->session->PinPropShape);

              if (defData->hasPort) {
                defData->Pin.addPortLayerProp((yyvsp[0].prop));
              } else {
                defData->Pin.addLayerProp((yyvsp[0].prop));     
              }

                (yyvsp[0].prop) = 0;
              }

            delete (yyvsp[0].prop);
          }
#line 5257 "def.tab.cpp"
    break;

  case 203: /* pin_poly_props_opt: pin_poly_props  */
#line 1389 "def.y"
    {}
#line 5263 "def.tab.cpp"
    break;

  case 205: /* pin_poly_props: pin_poly_prop  */
#line 1393 "def.y"
    {}
#line 5269 "def.tab.cpp"
    break;

  case 206: /* $@45: %empty  */
#line 1396 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 5277 "def.tab.cpp"
    break;

  case 207: /* pin_poly_prop: K_PROPERTY $@45 prop_name_value  */
#line 1400 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + PORT ... + POLYGON ... PROPERTY propName propValue")) {
                    CHKERR();
                }
            }

            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              defData->setPropDataType((yyvsp[0].prop), "PINSHAPE", defData->session->PinPropShape);

              if (defData->hasPort) {
                defData->Pin.addPortPolyProp((yyvsp[0].prop));
              } else {
                defData->Pin.addPolyProp((yyvsp[0].prop));     
              }

                (yyvsp[0].prop) = 0;
              }

            delete (yyvsp[0].prop);
          }
#line 5303 "def.tab.cpp"
    break;

  case 209: /* pin_via_props_opt: pin_via_props  */
#line 1424 "def.y"
    {}
#line 5309 "def.tab.cpp"
    break;

  case 211: /* pin_via_props: pin_via_prop  */
#line 1428 "def.y"
        {}
#line 5315 "def.tab.cpp"
    break;

  case 212: /* $@46: %empty  */
#line 1431 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 5323 "def.tab.cpp"
    break;

  case 213: /* pin_via_prop: K_PROPERTY $@46 prop_name_value  */
#line 1435 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + PORT ... + VIA ... PROPERTY propName propValue")) {
                    CHKERR();
                }
            }

            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                defData->setPropDataType((yyvsp[0].prop), "PINSHAPE", defData->session->PinPropShape);

              if (defData->hasPort) {
                defData->Pin.addPortViaProp((yyvsp[0].prop));
                } else {
                    defData->Pin.addViaProp((yyvsp[0].prop));     
                }

                (yyvsp[0].prop) = 0;
              }

            delete (yyvsp[0].prop);
          }
#line 5349 "def.tab.cpp"
    break;

  case 214: /* pin_layer_spacing: K_SPACING NUMBER  */
#line 1459 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The SPACING statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6519, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("PINS ... + LAYER SPACING minSpacing")) {
                    CHKERR();
                }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortLayerSpacing((int)(yyvsp[0].dval));
                else
                   defData->Pin.addLayerSpacing((int)(yyvsp[0].dval));
              }
            }
          }
#line 5380 "def.tab.cpp"
    break;

  case 215: /* pin_layer_spacing: K_DESIGNRULEWIDTH NUMBER  */
#line 1486 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "DESIGNRULEWIDTH statement is a version 5.6 and later syntax.\nYour def file is defined with version %.2f", defData->VersionNum);
                  defData->defError(6520, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else if (defData->VersionNum >= 6.0 - 0.00001) {
              if (defData->def60ObsoletedError("PINS ... + LAYER ... DESIGNRULEWIDTH effectiveWidth")) {
                CHKERR();
              }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortLayerDesignRuleWidth((int)(yyvsp[0].dval));
                else
                   defData->Pin.addLayerDesignRuleWidth((int)(yyvsp[0].dval));
              }
            }
          }
#line 5411 "def.tab.cpp"
    break;

  case 217: /* pin_poly_spacing_opt: pin_poly_spacing  */
#line 1515 "def.y"
          {
          }
#line 5418 "def.tab.cpp"
    break;

  case 218: /* pin_poly_spacing: K_SPACING NUMBER  */
#line 1520 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "SPACING statement is a version 5.6 and later syntax.\nYour def file is defined with version %.2f", defData->VersionNum);
                  defData->defError(6521, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("PINS ... + POLYGON SPACING minSpacing")) {
                    CHKERR();
                }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortPolySpacing((int)(yyvsp[0].dval));
                else
                   defData->Pin.addPolySpacing((int)(yyvsp[0].dval));
              }
            }
          }
#line 5449 "def.tab.cpp"
    break;

  case 219: /* pin_poly_spacing: K_DESIGNRULEWIDTH NUMBER  */
#line 1547 "def.y"
          {
            if (defData->VersionNum < 5.6) {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if ((defData->pinWarnings++ < defData->settings->PinWarnings) &&
                    (defData->pinWarnings++ < defData->settings->PinExtWarnings)) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The DESIGNRULEWIDTH statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                  defData->defError(6520, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
            } else if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("PINS ... + POLYGON ... DESIGNRULEWIDTH effectiveWidth")) {
                    CHKERR();
                }
            } else {
              if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                if (defData->hasPort)
                   defData->Pin.addPortPolyDesignRuleWidth((int)(yyvsp[0].dval));
                else
                   defData->Pin.addPolyDesignRuleWidth((int)(yyvsp[0].dval));
              }
            }
          }
#line 5480 "def.tab.cpp"
    break;

  case 220: /* pin_oxide: K_OXIDE1  */
#line 1575 "def.y"
          { defData->aOxide = 1;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5489 "def.tab.cpp"
    break;

  case 221: /* pin_oxide: K_OXIDE2  */
#line 1580 "def.y"
          { defData->aOxide = 2;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5498 "def.tab.cpp"
    break;

  case 222: /* pin_oxide: K_OXIDE3  */
#line 1585 "def.y"
          { defData->aOxide = 3;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5507 "def.tab.cpp"
    break;

  case 223: /* pin_oxide: K_OXIDE4  */
#line 1590 "def.y"
          { defData->aOxide = 4;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5516 "def.tab.cpp"
    break;

  case 224: /* pin_oxide: K_OXIDE5  */
#line 1595 "def.y"
          { defData->aOxide = 5;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5525 "def.tab.cpp"
    break;

  case 225: /* pin_oxide: K_OXIDE6  */
#line 1600 "def.y"
          { defData->aOxide = 6;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5534 "def.tab.cpp"
    break;

  case 226: /* pin_oxide: K_OXIDE7  */
#line 1605 "def.y"
          { defData->aOxide = 7;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5543 "def.tab.cpp"
    break;

  case 227: /* pin_oxide: K_OXIDE8  */
#line 1610 "def.y"
          { defData->aOxide = 8;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5552 "def.tab.cpp"
    break;

  case 228: /* pin_oxide: K_OXIDE9  */
#line 1615 "def.y"
          { defData->aOxide = 9;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5561 "def.tab.cpp"
    break;

  case 229: /* pin_oxide: K_OXIDE10  */
#line 1620 "def.y"
          { defData->aOxide = 10;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5570 "def.tab.cpp"
    break;

  case 230: /* pin_oxide: K_OXIDE11  */
#line 1625 "def.y"
          { defData->aOxide = 11;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5579 "def.tab.cpp"
    break;

  case 231: /* pin_oxide: K_OXIDE12  */
#line 1630 "def.y"
          { defData->aOxide = 12;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5588 "def.tab.cpp"
    break;

  case 232: /* pin_oxide: K_OXIDE13  */
#line 1635 "def.y"
          { defData->aOxide = 13;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5597 "def.tab.cpp"
    break;

  case 233: /* pin_oxide: K_OXIDE14  */
#line 1640 "def.y"
          { defData->aOxide = 14;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5606 "def.tab.cpp"
    break;

  case 234: /* pin_oxide: K_OXIDE15  */
#line 1645 "def.y"
          { defData->aOxide = 15;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5615 "def.tab.cpp"
    break;

  case 235: /* pin_oxide: K_OXIDE16  */
#line 1650 "def.y"
          { defData->aOxide = 16;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5624 "def.tab.cpp"
    break;

  case 236: /* pin_oxide: K_OXIDE17  */
#line 1655 "def.y"
          { defData->aOxide = 17;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5633 "def.tab.cpp"
    break;

  case 237: /* pin_oxide: K_OXIDE18  */
#line 1660 "def.y"
          { defData->aOxide = 18;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5642 "def.tab.cpp"
    break;

  case 238: /* pin_oxide: K_OXIDE19  */
#line 1665 "def.y"
          { defData->aOxide = 19;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5651 "def.tab.cpp"
    break;

  case 239: /* pin_oxide: K_OXIDE20  */
#line 1670 "def.y"
          { defData->aOxide = 20;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5660 "def.tab.cpp"
    break;

  case 240: /* pin_oxide: K_OXIDE21  */
#line 1675 "def.y"
          { defData->aOxide = 21;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5669 "def.tab.cpp"
    break;

  case 241: /* pin_oxide: K_OXIDE22  */
#line 1680 "def.y"
          { defData->aOxide = 22;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5678 "def.tab.cpp"
    break;

  case 242: /* pin_oxide: K_OXIDE23  */
#line 1685 "def.y"
          { defData->aOxide = 23;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5687 "def.tab.cpp"
    break;

  case 243: /* pin_oxide: K_OXIDE24  */
#line 1690 "def.y"
          { defData->aOxide = 24;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5696 "def.tab.cpp"
    break;

  case 244: /* pin_oxide: K_OXIDE25  */
#line 1695 "def.y"
          { defData->aOxide = 25;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5705 "def.tab.cpp"
    break;

  case 245: /* pin_oxide: K_OXIDE26  */
#line 1700 "def.y"
          { defData->aOxide = 26;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5714 "def.tab.cpp"
    break;

  case 246: /* pin_oxide: K_OXIDE27  */
#line 1705 "def.y"
          { defData->aOxide = 27;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5723 "def.tab.cpp"
    break;

  case 247: /* pin_oxide: K_OXIDE28  */
#line 1710 "def.y"
          { defData->aOxide = 28;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5732 "def.tab.cpp"
    break;

  case 248: /* pin_oxide: K_OXIDE29  */
#line 1715 "def.y"
          { defData->aOxide = 29;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5741 "def.tab.cpp"
    break;

  case 249: /* pin_oxide: K_OXIDE30  */
#line 1720 "def.y"
          { defData->aOxide = 30;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5750 "def.tab.cpp"
    break;

  case 250: /* pin_oxide: K_OXIDE31  */
#line 1725 "def.y"
          { defData->aOxide = 31;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5759 "def.tab.cpp"
    break;

  case 251: /* pin_oxide: K_OXIDE32  */
#line 1730 "def.y"
          { defData->aOxide = 32;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5768 "def.tab.cpp"
    break;

  case 252: /* use_type: K_SIGNAL  */
#line 1737 "def.y"
          { (yyval.string) = (char*)"SIGNAL"; }
#line 5774 "def.tab.cpp"
    break;

  case 253: /* use_type: K_POWER  */
#line 1739 "def.y"
          { (yyval.string) = (char*)"POWER"; }
#line 5780 "def.tab.cpp"
    break;

  case 254: /* use_type: K_GROUND  */
#line 1741 "def.y"
          { (yyval.string) = (char*)"GROUND"; }
#line 5786 "def.tab.cpp"
    break;

  case 255: /* use_type: K_CLOCK  */
#line 1743 "def.y"
          { (yyval.string) = (char*)"CLOCK"; }
#line 5792 "def.tab.cpp"
    break;

  case 256: /* use_type: K_TIEOFF  */
#line 1745 "def.y"
          { (yyval.string) = (char*)"TIEOFF"; }
#line 5798 "def.tab.cpp"
    break;

  case 257: /* use_type: K_ANALOG  */
#line 1747 "def.y"
          { (yyval.string) = (char*)"ANALOG"; }
#line 5804 "def.tab.cpp"
    break;

  case 258: /* use_type: K_SCAN  */
#line 1749 "def.y"
          { (yyval.string) = (char*)"SCAN"; }
#line 5810 "def.tab.cpp"
    break;

  case 259: /* use_type: K_RESET  */
#line 1751 "def.y"
          { (yyval.string) = (char*)"RESET"; }
#line 5816 "def.tab.cpp"
    break;

  case 260: /* pin_layer_opt: %empty  */
#line 1755 "def.y"
          { (yyval.string) = (char*)""; }
#line 5822 "def.tab.cpp"
    break;

  case 261: /* $@47: %empty  */
#line 1756 "def.y"
                  {defData->dumb_mode=1;}
#line 5828 "def.tab.cpp"
    break;

  case 262: /* pin_layer_opt: K_LAYER $@47 T_STRING  */
#line 1757 "def.y"
          { (yyval.string) = (yyvsp[0].string); }
#line 5834 "def.tab.cpp"
    break;

  case 263: /* end_pins: K_END K_PINS  */
#line 1760 "def.y"
        { 
          if (defData->callbacks->PinEndCbk)
            CALLBACK(defData->callbacks->PinEndCbk, defrPinEndCbkType, 0);
        }
#line 5843 "def.tab.cpp"
    break;

  case 264: /* $@48: %empty  */
#line 1765 "def.y"
                {defData->dumb_mode = 2; defData->no_num = 2; }
#line 5849 "def.tab.cpp"
    break;

  case 265: /* $@49: %empty  */
#line 1767 "def.y"
        {
          if (defData->callbacks->RowCbk) {
            defData->rowName = (yyvsp[-4].string);
            defData->Row.setup((yyvsp[-4].string), (yyvsp[-3].string), (yyvsp[-2].dval), (yyvsp[-1].dval), (yyvsp[0].integer));
          }
        }
#line 5860 "def.tab.cpp"
    break;

  case 266: /* row_rule: K_ROW $@48 T_STRING T_STRING NUMBER NUMBER orient $@49 row_do_option row_options ';'  */
#line 1775 "def.y"
        {
          if (defData->callbacks->RowCbk) 
            CALLBACK(defData->callbacks->RowCbk, defrRowCbkType, &defData->Row);
        }
#line 5869 "def.tab.cpp"
    break;

  case 267: /* row_do_option: %empty  */
#line 1781 "def.y"
        {
          if (defData->VersionNum < 5.6) {
            if (defData->callbacks->RowCbk) {
              if (defData->rowWarnings++ < defData->settings->RowWarnings) {
                defData->defError(6523, "Invalid ROW statement defined in the DEF file. The DO statement which is required in the ROW statement is not defined.\nUpdate your DEF file with a DO statement.");
                CHKERR();
              }
            }
          }
        }
#line 5884 "def.tab.cpp"
    break;

  case 268: /* row_do_option: K_DO NUMBER K_BY NUMBER row_step_option  */
#line 1792 "def.y"
        {
          // 06/05/2002 - pcr 448455 
          // Check for 1 and 0 in the correct position 
          // 07/26/2002 - Commented out due to pcr 459218 
          if (defData->hasDoStep) {
            // 04/29/2004 - pcr 695535 
            // changed the testing 
            if ((((yyvsp[-1].dval) == 1) && (defData->yStep == 0)) ||
                (((yyvsp[-3].dval) == 1) && (defData->xStep == 0))) {
              // do nothing 
            } else 
              if (defData->VersionNum < 5.6) {
                if (defData->callbacks->RowCbk) {
                  if (defData->rowWarnings++ < defData->settings->RowWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf(defData->defMsg,
                            "The DO statement in the ROW statement with the name %s has invalid syntax.\nThe valid syntax is \"DO numX BY 1 STEP spaceX 0 | DO 1 BY numY STEP 0 spaceY\".\nSpecify the valid syntax and try again.", defData->rowName);
                    defData->defWarning(7018, defData->defMsg);
                    free(defData->defMsg);
                    }
                  }
              }
          }
          // pcr 459218 - Error if at least numX or numY does not equal 1 
          if (((yyvsp[-3].dval) != 1) && ((yyvsp[-1].dval) != 1)) {
            if (defData->callbacks->RowCbk) {
              if (defData->rowWarnings++ < defData->settings->RowWarnings) {
                defData->defError(6524, "Invalid syntax specified. The valid syntax is either \"DO 1 BY num or DO num BY 1\". Specify the valid syntax and try again.");
                CHKERR();
              }
            }
          }
          if (defData->callbacks->RowCbk)
            defData->Row.setDo(ROUND((yyvsp[-3].dval)), ROUND((yyvsp[-1].dval)), defData->xStep, defData->yStep);
        }
#line 5924 "def.tab.cpp"
    break;

  case 269: /* row_step_option: %empty  */
#line 1829 "def.y"
        {
          defData->hasDoStep = 0;
        }
#line 5932 "def.tab.cpp"
    break;

  case 270: /* row_step_option: K_STEP NUMBER NUMBER  */
#line 1833 "def.y"
        {
          defData->hasDoStep = 1;
          defData->Row.setHasDoStep();
          defData->xStep = (yyvsp[-1].dval);
          defData->yStep = (yyvsp[0].dval);
        }
#line 5943 "def.tab.cpp"
    break;

  case 273: /* $@50: %empty  */
#line 1844 "def.y"
                            {defData->dumb_mode = DEF_MAX_INT; }
#line 5949 "def.tab.cpp"
    break;

  case 274: /* row_option: '+' K_PROPERTY $@50 row_prop_list  */
#line 1846 "def.y"
         { defData->dumb_mode = 0; }
#line 5955 "def.tab.cpp"
    break;

  case 277: /* row_prop: T_STRING NUMBER  */
#line 1853 "def.y"
        {
          if (defData->callbacks->RowCbk) {
             char propTp;
             char* str = defData->ringCopy("                       ");
             propTp =  defData->session->RowProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "ROW");
             // For backword compatibility, also set the string value 
             sprintf(str, "%g", (yyvsp[0].dval));
             defData->Row.addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
          }
        }
#line 5971 "def.tab.cpp"
    break;

  case 278: /* row_prop: T_STRING QSTRING  */
#line 1865 "def.y"
        {
          if (defData->callbacks->RowCbk) {
             char propTp;
             propTp =  defData->session->RowProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "ROW");
             defData->Row.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 5984 "def.tab.cpp"
    break;

  case 279: /* row_prop: T_STRING T_STRING  */
#line 1874 "def.y"
        {
          if (defData->callbacks->RowCbk) {
             char propTp;
             propTp =  defData->session->RowProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "ROW");
             defData->Row.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 5997 "def.tab.cpp"
    break;

  case 280: /* $@51: %empty  */
#line 1884 "def.y"
        {
          if (defData->callbacks->TrackCbk) {
            defData->Track.setup((yyvsp[-1].string));
          }
        }
#line 6007 "def.tab.cpp"
    break;

  case 281: /* tracks_rule: track_start NUMBER $@51 K_DO NUMBER K_STEP NUMBER track_opts ';'  */
#line 1890 "def.y"
        {
          if (((yyvsp[-4].dval) <= 0) && (defData->VersionNum >= 5.4)) {
            if (defData->callbacks->TrackCbk)
              if (defData->trackWarnings++ < defData->settings->TrackWarnings) {
                defData->defMsg = (char*)malloc(1000);
                sprintf (defData->defMsg,
                   "The DO number %g in TRACK is invalid.\nThe number value has to be greater than 0. Specify the valid syntax and try again.", (yyvsp[-4].dval));
                defData->defError(6525, defData->defMsg);
                free(defData->defMsg);
              }
          }
          if ((yyvsp[-2].dval) < 0) {
            if (defData->callbacks->TrackCbk)
              if (defData->trackWarnings++ < defData->settings->TrackWarnings) {
                defData->defMsg = (char*)malloc(1000);
                sprintf (defData->defMsg,
                   "The STEP number %g in TRACK is invalid.\nThe number value has to be greater than 0. Specify the valid syntax and try again.", (yyvsp[-2].dval));
                defData->defError(6526, defData->defMsg);
                free(defData->defMsg);
              }
          }
          if (defData->callbacks->TrackCbk) {
            defData->Track.setDo(ROUND((yyvsp[-7].dval)), ROUND((yyvsp[-4].dval)), (yyvsp[-2].dval));
            CALLBACK(defData->callbacks->TrackCbk, defrTrackCbkType, &defData->Track);
          }
        }
#line 6038 "def.tab.cpp"
    break;

  case 282: /* track_start: K_TRACKS track_type  */
#line 1918 "def.y"
        {
          (yyval.string) = (yyvsp[0].string);
        }
#line 6046 "def.tab.cpp"
    break;

  case 283: /* track_type: K_X  */
#line 1923 "def.y"
            { (yyval.string) = (char*)"X";}
#line 6052 "def.tab.cpp"
    break;

  case 284: /* track_type: K_Y  */
#line 1925 "def.y"
            { (yyval.string) = (char*)"Y";}
#line 6058 "def.tab.cpp"
    break;

  case 285: /* track_opts: track_opt_property_statements track_mask_statement track_width_statement track_ndr_statement track_layer_statement  */
#line 1933 "def.y"
    {}
#line 6064 "def.tab.cpp"
    break;

  case 287: /* track_opt_property_statements: track_property_statements  */
#line 1937 "def.y"
    {}
#line 6070 "def.tab.cpp"
    break;

  case 289: /* track_property_statements: track_property_statement  */
#line 1942 "def.y"
    {}
#line 6076 "def.tab.cpp"
    break;

  case 290: /* $@52: %empty  */
#line 1945 "def.y"
           { 
                defData->dumb_mode = 2; 
           }
#line 6084 "def.tab.cpp"
    break;

  case 291: /* track_property_statement: K_PROPERTY $@52 prop_name_value  */
#line 1949 "def.y"
           { 
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("TRACKS ... PROPERTY propName propVal")) {
                    CHKERR();
                }
            } else {
                if (defData->callbacks->TrackCbk) {
                    defData->setPropDataType((yyvsp[0].prop), "TRACK", defData->session->TrackProp);
                    defData->Track.addProp((yyvsp[0].prop));
                    (yyvsp[0].prop) = NULL;
                }
            }
            
            delete (yyvsp[0].prop);
           }
#line 6104 "def.tab.cpp"
    break;

  case 293: /* $@53: %empty  */
#line 1966 "def.y"
                {defData->dumb_mode=1;}
#line 6110 "def.tab.cpp"
    break;

  case 294: /* track_ndr_statement: K_NDR $@53 T_STRING  */
#line 1967 "def.y"
           { 
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("TRACKS ... NDR ruleName")) {
                    CHKERR();
                }
            } else {
                if (defData->callbacks->TrackCbk) {
                    defData->Track.setNdr((yyvsp[0].string));
                }
            }
           }
#line 6126 "def.tab.cpp"
    break;

  case 296: /* track_width_statement: K_WIDTH NUMBER  */
#line 1981 "def.y"
           { 
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("TRACKS ... WIDTH width")) {
                    CHKERR();
                }
            } else {
                if (defData->callbacks->TrackCbk) {
                    defData->Track.setWidth((yyvsp[0].dval));
                }
            }
           }
#line 6142 "def.tab.cpp"
    break;

  case 298: /* track_mask_statement: K_MASK NUMBER same_mask  */
#line 1995 "def.y"
           { 
              if (defData->validateMaskInput((int)(yyvsp[-1].dval), defData->trackWarnings, defData->settings->TrackWarnings)) {
                  if (defData->callbacks->TrackCbk) {
                    defData->Track.addMask((yyvsp[-1].dval), (yyvsp[0].integer));
                  }
               }
            }
#line 6154 "def.tab.cpp"
    break;

  case 299: /* same_mask: %empty  */
#line 2005 "def.y"
        { (yyval.integer) = 0; }
#line 6160 "def.tab.cpp"
    break;

  case 300: /* same_mask: K_SAMEMASK  */
#line 2007 "def.y"
        { (yyval.integer) = 1; }
#line 6166 "def.tab.cpp"
    break;

  case 302: /* $@54: %empty  */
#line 2010 "def.y"
                  { defData->dumb_mode = 1000; }
#line 6172 "def.tab.cpp"
    break;

  case 303: /* track_layer_statement: K_LAYER $@54 track_layer track_layers  */
#line 2011 "def.y"
            { defData->dumb_mode = 0; }
#line 6178 "def.tab.cpp"
    break;

  case 306: /* track_layer: T_STRING  */
#line 2018 "def.y"
        {
          if (defData->callbacks->TrackCbk)
            defData->Track.addLayer((yyvsp[0].string));
        }
#line 6187 "def.tab.cpp"
    break;

  case 307: /* gcellgrid: K_GCELLGRID track_type NUMBER K_DO NUMBER K_STEP NUMBER ';'  */
#line 2025 "def.y"
        {
          if ((yyvsp[-3].dval) <= 0) {
            if (defData->callbacks->GcellGridCbk)
              if (defData->gcellGridWarnings++ < defData->settings->GcellGridWarnings) {
                defData->defMsg = (char*)malloc(1000);
                sprintf (defData->defMsg,
                   "The DO number %g in GCELLGRID is invalid.\nThe number value has to be greater than 0. Specify the valid syntax and try again.", (yyvsp[-3].dval));
                defData->defError(6527, defData->defMsg);
                free(defData->defMsg);
              }
          }
          if ((yyvsp[-1].dval) < 0) {
            if (defData->callbacks->GcellGridCbk)
              if (defData->gcellGridWarnings++ < defData->settings->GcellGridWarnings) {
                defData->defMsg = (char*)malloc(1000);
                sprintf (defData->defMsg,
                   "The STEP number %g in GCELLGRID is invalid.\nThe number value has to be greater than 0. Specify the valid syntax and try again.", (yyvsp[-1].dval));
                defData->defError(6528, defData->defMsg);
                free(defData->defMsg);
              }
          }
          if (defData->callbacks->GcellGridCbk) {
            defData->GcellGrid.setup((yyvsp[-6].string), ROUND((yyvsp[-5].dval)), ROUND((yyvsp[-3].dval)), (yyvsp[-1].dval));
            CALLBACK(defData->callbacks->GcellGridCbk, defrGcellGridCbkType, &defData->GcellGrid);
          }
        }
#line 6218 "def.tab.cpp"
    break;

  case 308: /* extension_section: K_BEGINEXT  */
#line 2053 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) { 
                if (defData->def60ObsoletedError("BEGINEXT ... ENDEXT")) {
                    CHKERR();
                }
            } else if (defData->callbacks->ExtensionCbk) {
                CALLBACK(defData->callbacks->ExtensionCbk, defrExtensionCbkType, &defData->History_text[0]);
            }
        }
#line 6232 "def.tab.cpp"
    break;

  case 309: /* extension_stmt: '+' K_BEGINEXT  */
#line 2064 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) { 
                if (defData->def60ObsoletedError("+ BEGINEXT ... ENDEXT")) {
                    CHKERR();
                }
            }
        }
#line 6244 "def.tab.cpp"
    break;

  case 311: /* via: K_VIAS NUMBER ';'  */
#line 2076 "def.y"
        {
          if (defData->callbacks->ViaStartCbk)
            CALLBACK(defData->callbacks->ViaStartCbk, defrViaStartCbkType, ROUND((yyvsp[-1].dval)));
        }
#line 6253 "def.tab.cpp"
    break;

  case 314: /* $@55: %empty  */
#line 2085 "def.y"
                     {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6259 "def.tab.cpp"
    break;

  case 315: /* $@56: %empty  */
#line 2086 "def.y"
            {
              if (defData->callbacks->ViaCbk) defData->Via.setup((yyvsp[0].string));
              defData->viaRule = 0;
            }
#line 6268 "def.tab.cpp"
    break;

  case 316: /* via_declaration: '-' $@55 T_STRING $@56 layer_stmts ';'  */
#line 2091 "def.y"
            {
              if (defData->callbacks->ViaCbk)
                CALLBACK(defData->callbacks->ViaCbk, defrViaCbkType, &defData->Via);
              defData->Via.clear();
            }
#line 6278 "def.tab.cpp"
    break;

  case 319: /* $@57: %empty  */
#line 2101 "def.y"
                       {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6284 "def.tab.cpp"
    break;

  case 320: /* layer_stmt: '+' K_RECT $@57 T_STRING mask pt pt  */
#line 2102 "def.y"
        { 
            if (defData->callbacks->ViaCbk)
            if (defData->validateMaskInput((yyvsp[-2].integer), defData->viaWarnings, defData->settings->ViaWarnings)) {
                defData->Via.addLayer((yyvsp[-3].string), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y, (yyvsp[-2].integer));
            }
        }
#line 6295 "def.tab.cpp"
    break;

  case 321: /* $@58: %empty  */
#line 2109 "def.y"
        { 
            defData->dumb_mode = 2; 
        }
#line 6303 "def.tab.cpp"
    break;

  case 322: /* layer_stmt: '+' K_PROPERTY $@58 prop_name_value  */
#line 2113 "def.y"
        {
            if (defData->VersionNum < 6.0 - 0.0001) {
                if (defData->def60NewSyntaxError("VIAS ... - viaName + PROPERTY propName propValue")) {
                    CHKERR();
                }
             } else {
                if (defData->callbacks->ViaCbk) {
                    defData->setPropDataType((yyvsp[0].prop), "VIA", defData->session->ViaProp);
                    defData->Via.addProp((yyvsp[0].prop));
                    (yyvsp[0].prop) = NULL;
                }
             }

             delete (yyvsp[0].prop);
        }
#line 6323 "def.tab.cpp"
    break;

  case 323: /* $@59: %empty  */
#line 2128 "def.y"
                        { defData->dumb_mode = 1; }
#line 6329 "def.tab.cpp"
    break;

  case 324: /* $@60: %empty  */
#line 2129 "def.y"
            {
              if (defData->VersionNum < 5.6) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf (defData->defMsg,
                       "The POLYGON statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                    defData->defError(6509, defData->defMsg);
                    free(defData->defMsg);
                    CHKERR();
                  }
                }
              }
              
              defData->Geometries.Reset();
              
            }
#line 6351 "def.tab.cpp"
    break;

  case 325: /* layer_stmt: '+' K_POLYGON $@59 T_STRING mask $@60 firstPt nextPt nextPt otherPts  */
#line 2147 "def.y"
            {
              if (defData->VersionNum >= 5.6) {  // only add if 5.6 or beyond
                if (defData->callbacks->ViaCbk)
                  if (defData->validateMaskInput((yyvsp[-5].integer), defData->viaWarnings, defData->settings->ViaWarnings)) {
                    defData->Via.addPolygon((yyvsp[-6].string), &defData->Geometries, (yyvsp[-5].integer));
                  }
              }
            }
#line 6364 "def.tab.cpp"
    break;

  case 326: /* $@61: %empty  */
#line 2155 "def.y"
                            {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6370 "def.tab.cpp"
    break;

  case 327: /* layer_stmt: '+' K_PATTERNNAME $@61 T_STRING  */
#line 2156 "def.y"
            {
              if (defData->VersionNum < 5.6) {
                if (defData->callbacks->ViaCbk)
                  defData->Via.addPattern((yyvsp[0].string));
              } else
                if (defData->callbacks->ViaCbk)
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings)
                    defData->defWarning(7019, "The PATTERNNAME statement is obsolete in version 5.6 and later.\nThe DEF parser will ignore this statement."); 
            }
#line 6384 "def.tab.cpp"
    break;

  case 328: /* $@62: %empty  */
#line 2165 "def.y"
                        {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6390 "def.tab.cpp"
    break;

  case 329: /* $@63: %empty  */
#line 2167 "def.y"
                       {defData->dumb_mode = 3;defData->no_num = 1; }
#line 6396 "def.tab.cpp"
    break;

  case 330: /* layer_stmt: '+' K_VIARULE $@62 T_STRING '+' K_CUTSIZE NUMBER NUMBER '+' K_LAYERS $@63 T_STRING T_STRING T_STRING '+' K_CUTSPACING NUMBER NUMBER '+' K_ENCLOSURE NUMBER NUMBER NUMBER NUMBER  */
#line 2170 "def.y"
            {
               defData->viaRule = 1;
               if (defData->VersionNum < 5.6) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf (defData->defMsg,
                       "The VIARULE statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                    defData->defError(6557, defData->defMsg);
                    free(defData->defMsg);
                    CHKERR();
                  }
                }
              } else {
                if (defData->callbacks->ViaCbk)
                   defData->Via.addViaRule((yyvsp[-20].string), (int)(yyvsp[-17].dval), (int)(yyvsp[-16].dval), (yyvsp[-12].string), (yyvsp[-11].string),
                                             (yyvsp[-10].string), (int)(yyvsp[-7].dval), (int)(yyvsp[-6].dval), (int)(yyvsp[-3].dval),
                                             (int)(yyvsp[-2].dval), (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval)); 
              }
            }
#line 6421 "def.tab.cpp"
    break;

  case 332: /* layer_stmt: extension_stmt  */
#line 2192 "def.y"
          { 
            if (defData->callbacks->ViaExtCbk)
              CALLBACK(defData->callbacks->ViaExtCbk, defrViaExtCbkType, &defData->History_text[0]);
          }
#line 6430 "def.tab.cpp"
    break;

  case 333: /* layer_viarule_opts: '+' K_ROWCOL NUMBER NUMBER  */
#line 2198 "def.y"
            {
              if (!defData->viaRule) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defError(6559, "The ROWCOL statement is missing from the VIARULE statement. Ensure that it exists in the VIARULE statement.");
                    CHKERR();
                  }
                }
              } else if (defData->callbacks->ViaCbk)
                 defData->Via.addRowCol((int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
            }
#line 6446 "def.tab.cpp"
    break;

  case 334: /* layer_viarule_opts: '+' K_ORIGIN NUMBER NUMBER  */
#line 2210 "def.y"
            {
              if (!defData->viaRule) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defError(6560, "The ORIGIN statement is missing from the VIARULE statement. Ensure that it exists in the VIARULE statement.");
                    CHKERR();
                  }
                }
              } else if (defData->callbacks->ViaCbk)
                 defData->Via.addOrigin((int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
            }
#line 6462 "def.tab.cpp"
    break;

  case 335: /* layer_viarule_opts: '+' K_OFFSET NUMBER NUMBER NUMBER NUMBER  */
#line 2222 "def.y"
            {
              if (!defData->viaRule) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defError(6561, "The OFFSET statement is missing from the VIARULE statement. Ensure that it exists in the VIARULE statement.");
                    CHKERR();
                  }
                }
              } else if (defData->callbacks->ViaCbk)
                 defData->Via.addOffset((int)(yyvsp[-3].dval), (int)(yyvsp[-2].dval), (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
            }
#line 6478 "def.tab.cpp"
    break;

  case 336: /* $@64: %empty  */
#line 2233 "def.y"
                        {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6484 "def.tab.cpp"
    break;

  case 337: /* layer_viarule_opts: '+' K_PATTERN $@64 T_STRING  */
#line 2234 "def.y"
            {
              if (!defData->viaRule) {
                if (defData->callbacks->ViaCbk) {
                  if (defData->viaWarnings++ < defData->settings->ViaWarnings) {
                    defData->defError(6562, "The PATTERN statement is missing from the VIARULE statement. Ensure that it exists in the VIARULE statement.");
                    CHKERR();
                  }
                }
              } else if (defData->callbacks->ViaCbk)
                 defData->Via.addCutPattern((yyvsp[0].string));
            }
#line 6500 "def.tab.cpp"
    break;

  case 338: /* firstPt: pt  */
#line 2247 "def.y"
          { defData->Geometries.startList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6506 "def.tab.cpp"
    break;

  case 339: /* nextPt: pt  */
#line 2250 "def.y"
          { defData->Geometries.addToList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6512 "def.tab.cpp"
    break;

  case 342: /* pt: '(' NUMBER NUMBER ')'  */
#line 2257 "def.y"
          {
            defData->save_x = (yyvsp[-2].dval);
            defData->save_y = (yyvsp[-1].dval);
            (yyval.pt).x = ROUND((yyvsp[-2].dval));
            (yyval.pt).y = ROUND((yyvsp[-1].dval));
          }
#line 6523 "def.tab.cpp"
    break;

  case 343: /* pt: '(' '*' NUMBER ')'  */
#line 2264 "def.y"
          {
            defData->save_y = (yyvsp[-1].dval);
            (yyval.pt).x = ROUND(defData->save_x);
            (yyval.pt).y = ROUND((yyvsp[-1].dval));
          }
#line 6533 "def.tab.cpp"
    break;

  case 344: /* pt: '(' NUMBER '*' ')'  */
#line 2270 "def.y"
          {
            defData->save_x = (yyvsp[-2].dval);
            (yyval.pt).x = ROUND((yyvsp[-2].dval));
            (yyval.pt).y = ROUND(defData->save_y);
          }
#line 6543 "def.tab.cpp"
    break;

  case 345: /* pt: '(' '*' '*' ')'  */
#line 2276 "def.y"
          {
            (yyval.pt).x = ROUND(defData->save_x);
            (yyval.pt).y = ROUND(defData->save_y);
          }
#line 6552 "def.tab.cpp"
    break;

  case 346: /* mask: %empty  */
#line 2282 "def.y"
      { (yyval.integer) = 0; }
#line 6558 "def.tab.cpp"
    break;

  case 347: /* mask: '+' K_MASK NUMBER  */
#line 2284 "def.y"
      { (yyval.integer) = (yyvsp[0].dval); }
#line 6564 "def.tab.cpp"
    break;

  case 348: /* via_end: K_END K_VIAS  */
#line 2287 "def.y"
        { 
          if (defData->callbacks->ViaEndCbk)
            CALLBACK(defData->callbacks->ViaEndCbk, defrViaEndCbkType, 0);
        }
#line 6573 "def.tab.cpp"
    break;

  case 349: /* regions_section: regions_start regions_stmts K_END K_REGIONS  */
#line 2293 "def.y"
        {
          if (defData->callbacks->RegionEndCbk)
            CALLBACK(defData->callbacks->RegionEndCbk, defrRegionEndCbkType, 0);
        }
#line 6582 "def.tab.cpp"
    break;

  case 350: /* regions_start: K_REGIONS NUMBER ';'  */
#line 2299 "def.y"
        {
          if (defData->callbacks->RegionStartCbk)
            CALLBACK(defData->callbacks->RegionStartCbk, defrRegionStartCbkType, ROUND((yyvsp[-1].dval)));
        }
#line 6591 "def.tab.cpp"
    break;

  case 352: /* regions_stmts: regions_stmts regions_stmt  */
#line 2306 "def.y"
            {}
#line 6597 "def.tab.cpp"
    break;

  case 353: /* $@65: %empty  */
#line 2308 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 6603 "def.tab.cpp"
    break;

  case 354: /* $@66: %empty  */
#line 2309 "def.y"
        {
          if (defData->callbacks->RegionCbk)
             defData->Region.setup((yyvsp[0].string));
          defData->regTypeDef = 0;
        }
#line 6613 "def.tab.cpp"
    break;

  case 355: /* regions_stmt: '-' $@65 T_STRING $@66 rect_list region_options ';'  */
#line 2315 "def.y"
        { CALLBACK(defData->callbacks->RegionCbk, defrRegionCbkType, &defData->Region); }
#line 6619 "def.tab.cpp"
    break;

  case 356: /* rect_list: pt pt  */
#line 2319 "def.y"
        { if (defData->callbacks->RegionCbk)
          defData->Region.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6626 "def.tab.cpp"
    break;

  case 357: /* rect_list: rect_list pt pt  */
#line 2322 "def.y"
        { if (defData->callbacks->RegionCbk)
          defData->Region.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6633 "def.tab.cpp"
    break;

  case 360: /* $@67: %empty  */
#line 2330 "def.y"
                               {defData->dumb_mode = DEF_MAX_INT; }
#line 6639 "def.tab.cpp"
    break;

  case 361: /* region_option: '+' K_PROPERTY $@67 region_prop_list  */
#line 2332 "def.y"
         { defData->dumb_mode = 0; }
#line 6645 "def.tab.cpp"
    break;

  case 362: /* region_option: '+' K_TYPE region_type  */
#line 2334 "def.y"
         {
           if (defData->regTypeDef) {
              if (defData->callbacks->RegionCbk) {
                if (defData->regionWarnings++ < defData->settings->RegionWarnings) {
                  defData->defError(6563, "The TYPE statement already exists. It has been defined in the REGION statement.");
                  CHKERR();
                }
              }
           }
           if (defData->callbacks->RegionCbk) defData->Region.setType((yyvsp[0].string));
           defData->regTypeDef = 1;
         }
#line 6662 "def.tab.cpp"
    break;

  case 365: /* region_prop: T_STRING NUMBER  */
#line 2353 "def.y"
        {
          if (defData->callbacks->RegionCbk) {
             char propTp;
             char* str = defData->ringCopy("                       ");
             propTp = defData->session->RegionProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "REGION");
             // For backword compatibility, also set the string value 
             // We will use a temporary string to store the number.
             // The string space is borrowed from the ring buffer
             // in the lexer.
             sprintf(str, "%g", (yyvsp[0].dval));
             defData->Region.addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
          }
        }
#line 6681 "def.tab.cpp"
    break;

  case 366: /* region_prop: T_STRING QSTRING  */
#line 2368 "def.y"
        {
          if (defData->callbacks->RegionCbk) {
             char propTp;
             propTp = defData->session->RegionProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "REGION");
             defData->Region.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6694 "def.tab.cpp"
    break;

  case 367: /* region_prop: T_STRING T_STRING  */
#line 2377 "def.y"
        {
          if (defData->callbacks->RegionCbk) {
             char propTp;
             propTp = defData->session->RegionProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "REGION");
             defData->Region.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6707 "def.tab.cpp"
    break;

  case 368: /* region_type: K_FENCE  */
#line 2387 "def.y"
            { (yyval.string) = (char*)"FENCE"; }
#line 6713 "def.tab.cpp"
    break;

  case 369: /* region_type: K_GUIDE  */
#line 2389 "def.y"
            { (yyval.string) = (char*)"GUIDE"; }
#line 6719 "def.tab.cpp"
    break;

  case 370: /* $@68: %empty  */
#line 2392 "def.y"
         {
           defData->dumb_mode = DEF_MAX_INT; 
           defData->no_num = DEF_MAX_INT;
         }
#line 6728 "def.tab.cpp"
    break;

  case 371: /* comps_maskShift_section: K_COMPSMASKSHIFT $@68 layer_statement ';'  */
#line 2397 "def.y"
         {
           defData->dumb_mode = 0;
           defData->no_num = 0;

           if (defData->VersionNum < 5.8) {
                if (defData->componentWarnings++ < defData->settings->ComponentWarnings) {
                   defData->defMsg = (char*)malloc(10000);
                   sprintf (defData->defMsg,
                     "The MASKSHIFT statement is available in version 5.8 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
                   defData->defError(7415, defData->defMsg);
                   free(defData->defMsg);
                   CHKERR();
                }
            }
            if (defData->callbacks->ComponentMaskShiftLayerCbk) {
                CALLBACK(defData->callbacks->ComponentMaskShiftLayerCbk, defrComponentMaskShiftLayerCbkType, &defData->ComponentMaskShiftLayer);
            }
         }
#line 6751 "def.tab.cpp"
    break;

  case 373: /* start_comps: K_COMPS NUMBER ';'  */
#line 2420 "def.y"
         {
            defData->Component = new defiComponent(defData);

            if (defData->callbacks->ComponentStartCbk) {
                CALLBACK(defData->callbacks->ComponentStartCbk,
                         defrComponentStartCbkType, ROUND((yyvsp[-1].dval)));
            }
         }
#line 6764 "def.tab.cpp"
    break;

  case 376: /* maskLayer: T_STRING  */
#line 2434 "def.y"
        {
            if (defData->callbacks->ComponentMaskShiftLayerCbk) {
              defData->ComponentMaskShiftLayer.addMaskShiftLayer((yyvsp[0].string));
            }
        }
#line 6774 "def.tab.cpp"
    break;

  case 379: /* comp: comp_start comp_options ';'  */
#line 2445 "def.y"
         {
            if (defData->callbacks->ComponentCbk) {
                CALLBACK(defData->callbacks->ComponentCbk,
                         defrComponentCbkType, defData->Component);

                defData->Component->clear();
            }
         }
#line 6787 "def.tab.cpp"
    break;

  case 380: /* comp_start: comp_id_and_name comp_net_list  */
#line 2455 "def.y"
         {
            defData->dumb_mode = 0;
            defData->no_num = 0;
         }
#line 6796 "def.tab.cpp"
    break;

  case 381: /* $@69: %empty  */
#line 2460 "def.y"
                      {defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT; }
#line 6802 "def.tab.cpp"
    break;

  case 382: /* comp_id_and_name: '-' $@69 T_STRING T_STRING  */
#line 2462 "def.y"
         {
            if (defData->callbacks->ComponentCbk)
              defData->Component->IdAndName((yyvsp[-1].string), (yyvsp[0].string));
         }
#line 6811 "def.tab.cpp"
    break;

  case 383: /* comp_net_list: %empty  */
#line 2468 "def.y"
        { }
#line 6817 "def.tab.cpp"
    break;

  case 384: /* comp_net_list: comp_net_list '*'  */
#line 2470 "def.y"
            {
              if (defData->callbacks->ComponentCbk)
                defData->Component->addNet("*");
            }
#line 6826 "def.tab.cpp"
    break;

  case 385: /* comp_net_list: comp_net_list T_STRING  */
#line 2475 "def.y"
            {
              if (defData->callbacks->ComponentCbk)
                defData->Component->addNet((yyvsp[0].string));
            }
#line 6835 "def.tab.cpp"
    break;

  case 402: /* comp_extension_stmt: extension_stmt  */
#line 2490 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            CALLBACK(defData->callbacks->ComponentExtCbk, defrComponentExtCbkType,
                     &defData->History_text[0]);
        }
#line 6845 "def.tab.cpp"
    break;

  case 403: /* $@70: %empty  */
#line 2496 "def.y"
                          {defData->dumb_mode=1; defData->no_num = 1; }
#line 6851 "def.tab.cpp"
    break;

  case 404: /* comp_eeq: '+' K_EEQMASTER $@70 T_STRING  */
#line 2497 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setEEQ((yyvsp[0].string));
        }
#line 6860 "def.tab.cpp"
    break;

  case 405: /* $@71: %empty  */
#line 2502 "def.y"
                              { defData->dumb_mode = 2;  defData->no_num = 2; }
#line 6866 "def.tab.cpp"
    break;

  case 406: /* comp_generate: '+' K_COMP_GEN $@71 T_STRING opt_pattern  */
#line 2504 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
             defData->Component->setGenerate((yyvsp[-1].string), (yyvsp[0].string));
        }
#line 6875 "def.tab.cpp"
    break;

  case 407: /* opt_pattern: %empty  */
#line 2510 "def.y"
      { (yyval.string) = (char*)""; }
#line 6881 "def.tab.cpp"
    break;

  case 408: /* opt_pattern: T_STRING  */
#line 2512 "def.y"
      { (yyval.string) = (yyvsp[0].string); }
#line 6887 "def.tab.cpp"
    break;

  case 409: /* comp_source: '+' K_SOURCE source_type  */
#line 2515 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("COMPONENTS ... + SOURCE DIST|NETLIST|TEST|TIMING|USER")) {
                    CHKERR();
                }
            } else if (defData->callbacks->ComponentCbk) {
                defData->Component->setSource((yyvsp[0].string));
            }
        }
#line 6901 "def.tab.cpp"
    break;

  case 410: /* source_type: K_NETLIST  */
#line 2526 "def.y"
            { (yyval.string) = (char*)"NETLIST"; }
#line 6907 "def.tab.cpp"
    break;

  case 411: /* source_type: K_DIST  */
#line 2528 "def.y"
            { (yyval.string) = (char*)"DIST"; }
#line 6913 "def.tab.cpp"
    break;

  case 412: /* source_type: K_USER  */
#line 2530 "def.y"
            { (yyval.string) = (char*)"USER"; }
#line 6919 "def.tab.cpp"
    break;

  case 413: /* source_type: K_TIMING  */
#line 2532 "def.y"
            { (yyval.string) = (char*)"TIMING"; }
#line 6925 "def.tab.cpp"
    break;

  case 414: /* comp_region: comp_region_start comp_pnt_list  */
#line 2537 "def.y"
        { }
#line 6931 "def.tab.cpp"
    break;

  case 415: /* comp_region: comp_region_start T_STRING  */
#line 2539 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("COMPONENTS ... + REGION regionName")) {
                    CHKERR();
                }
            } else if (defData->callbacks->ComponentCbk) {
                defData->Component->setRegionName((yyvsp[0].string));
            }
        }
#line 6945 "def.tab.cpp"
    break;

  case 416: /* comp_pnt_list: pt pt  */
#line 2550 "def.y"
        { 
          // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
          if (defData->VersionNum < 5.5) {
            if (defData->callbacks->ComponentCbk)
               defData->Component->setRegionBounds((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, 
                                                            (yyvsp[0].pt).x, (yyvsp[0].pt).y);
          }
          else
            defData->defWarning(7020, "The REGION pt pt statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
        }
#line 6960 "def.tab.cpp"
    break;

  case 417: /* comp_pnt_list: comp_pnt_list pt pt  */
#line 2561 "def.y"
        { 
          // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
          if (defData->VersionNum < 5.5) {
            if (defData->callbacks->ComponentCbk)
               defData->Component->setRegionBounds((yyvsp[-1].pt).x, (yyvsp[-1].pt).y,
                                                            (yyvsp[0].pt).x, (yyvsp[0].pt).y);
          }
          else
            defData->defWarning(7020, "The REGION pt pt statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
        }
#line 6975 "def.tab.cpp"
    break;

  case 418: /* $@72: %empty  */
#line 2573 "def.y"
        {
          if (defData->VersionNum < 5.6) {
             if (defData->callbacks->ComponentCbk) {
               if (defData->componentWarnings++ < defData->settings->ComponentWarnings) {
                 defData->defMsg = (char*)malloc(1000);
                 sprintf (defData->defMsg,
                    "The HALO statement is a version 5.6 and later syntax.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                 defData->defError(6529, defData->defMsg);
                 free(defData->defMsg);
                 CHKERR();
               }
             }
          }
        }
#line 6994 "def.tab.cpp"
    break;

  case 419: /* comp_halo: '+' K_HALO $@72 halo_soft NUMBER NUMBER NUMBER NUMBER  */
#line 2588 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setHalo((int)(yyvsp[-3].dval), (int)(yyvsp[-2].dval),
                                                 (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
        }
#line 7004 "def.tab.cpp"
    break;

  case 421: /* halo_soft: K_SOFT  */
#line 2596 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->ComponentCbk) {
             if (defData->componentWarnings++ < defData->settings->ComponentWarnings) {
                defData->defMsg = (char*)malloc(10000);
                sprintf (defData->defMsg,
                  "The HALO SOFT is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                defData->defError(6550, defData->defMsg);
                free(defData->defMsg);
                CHKERR();
             }
           }
        } else {
           if (defData->callbacks->ComponentCbk)
             defData->Component->setHaloSoft();
        }
      }
#line 7026 "def.tab.cpp"
    break;

  case 422: /* $@73: %empty  */
#line 2615 "def.y"
                                       { defData->dumb_mode = 2; defData->no_num = 2; }
#line 7032 "def.tab.cpp"
    break;

  case 423: /* comp_routehalo: '+' K_ROUTEHALO NUMBER $@73 T_STRING T_STRING  */
#line 2616 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->ComponentCbk) {
             if (defData->componentWarnings++ < defData->settings->ComponentWarnings) {
                defData->defMsg = (char*)malloc(10000);
                sprintf (defData->defMsg,
                  "The ROUTEHALO is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                defData->defError(6551, defData->defMsg);
                free(defData->defMsg);
                CHKERR();
             }
           }
        } else {
           if (defData->callbacks->ComponentCbk)
             defData->Component->setRouteHalo(
                            (int)(yyvsp[-3].dval), (yyvsp[-1].string), (yyvsp[0].string));
        }
      }
#line 7055 "def.tab.cpp"
    break;

  case 424: /* $@74: %empty  */
#line 2635 "def.y"
                              { defData->dumb_mode = DEF_MAX_INT; }
#line 7061 "def.tab.cpp"
    break;

  case 425: /* comp_property: '+' K_PROPERTY $@74 comp_prop_list  */
#line 2637 "def.y"
      { defData->dumb_mode = 0; }
#line 7067 "def.tab.cpp"
    break;

  case 428: /* comp_prop: T_STRING NUMBER  */
#line 2644 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            char propTp;
            char* str = defData->ringCopy("                       ");
            propTp = defData->session->CompProp.propType((yyvsp[-1].string));
            CHKPROPTYPE(propTp, (yyvsp[-1].string), "COMPONENT");
            sprintf(str, "%g", (yyvsp[0].dval));
            defData->Component->addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
          }
        }
#line 7082 "def.tab.cpp"
    break;

  case 429: /* comp_prop: T_STRING QSTRING  */
#line 2655 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            char propTp;
            propTp = defData->session->CompProp.propType((yyvsp[-1].string));
            CHKPROPTYPE(propTp, (yyvsp[-1].string), "COMPONENT");
            defData->Component->addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 7095 "def.tab.cpp"
    break;

  case 430: /* comp_prop: T_STRING T_STRING  */
#line 2664 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            char propTp;
            propTp = defData->session->CompProp.propType((yyvsp[-1].string));
            CHKPROPTYPE(propTp, (yyvsp[-1].string), "COMPONENT");
            defData->Component->addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 7108 "def.tab.cpp"
    break;

  case 431: /* comp_region_start: '+' K_REGION  */
#line 2674 "def.y"
        { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7114 "def.tab.cpp"
    break;

  case 432: /* $@75: %empty  */
#line 2676 "def.y"
                            { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7120 "def.tab.cpp"
    break;

  case 433: /* comp_foreign: '+' K_FOREIGN $@75 T_STRING opt_paren orient  */
#line 2678 "def.y"
        { 
          if (defData->VersionNum < 5.6) {
            if (defData->callbacks->ComponentCbk) {
              defData->Component->setForeignName((yyvsp[-2].string));
              defData->Component->setForeignLocation((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
            }
         } else
            if (defData->callbacks->ComponentCbk)
              if (defData->componentWarnings++ < defData->settings->ComponentWarnings)
                defData->defWarning(7021, "The FOREIGN statement is obsolete in version 5.6 and later.\nThe DEF parser will ignore this statement.");
         }
#line 7136 "def.tab.cpp"
    break;

  case 434: /* opt_paren: pt  */
#line 2692 "def.y"
         { (yyval.pt) = (yyvsp[0].pt); }
#line 7142 "def.tab.cpp"
    break;

  case 435: /* opt_paren: NUMBER NUMBER  */
#line 2694 "def.y"
         { (yyval.pt).x = ROUND((yyvsp[-1].dval)); (yyval.pt).y = ROUND((yyvsp[0].dval)); }
#line 7148 "def.tab.cpp"
    break;

  case 436: /* comp_type: placement_status pt orient  */
#line 2697 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            defData->Component->setPlacementStatus((yyvsp[-2].integer));
            defData->Component->setPlacementLocation((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
          }
        }
#line 7159 "def.tab.cpp"
    break;

  case 437: /* comp_type: '+' K_UNPLACED  */
#line 2704 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setPlacementStatus(
                                         DEFI_COMPONENT_UNPLACED);
            defData->Component->setPlacementLocation(-1, -1, -1);
        }
#line 7170 "def.tab.cpp"
    break;

  case 438: /* comp_type: '+' K_UNPLACED pt orient  */
#line 2711 "def.y"
        {
          if (defData->VersionNum < 5.4) {   // PCR 495463 
            if (defData->callbacks->ComponentCbk) {
              defData->Component->setPlacementStatus(
                                          DEFI_COMPONENT_UNPLACED);
              defData->Component->setPlacementLocation((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
            }
          } else {
            if (defData->componentWarnings++ < defData->settings->ComponentWarnings)
               defData->defWarning(7022, "In the COMPONENT UNPLACED statement, point and orient are invalid in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
#line 7187 "def.tab.cpp"
    break;

  case 439: /* $@76: %empty  */
#line 2725 "def.y"
                           { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7193 "def.tab.cpp"
    break;

  case 440: /* maskShift: '+' K_MASKSHIFT $@76 T_STRING  */
#line 2726 "def.y"
        {  
          if (defData->callbacks->ComponentCbk) {
            if (defData->validateMaskShiftInput((yyvsp[0].string), defData->componentWarnings, defData->settings->ComponentWarnings)) {
                defData->Component->setMaskShift((yyvsp[0].string));
            }
          }
        }
#line 7205 "def.tab.cpp"
    break;

  case 441: /* placement_status: '+' K_FIXED  */
#line 2735 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_FIXED; }
#line 7211 "def.tab.cpp"
    break;

  case 442: /* placement_status: '+' K_COVER  */
#line 2737 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_COVER; }
#line 7217 "def.tab.cpp"
    break;

  case 443: /* placement_status: '+' K_PLACED  */
#line 2739 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_PLACED; }
#line 7223 "def.tab.cpp"
    break;

  case 444: /* placement_status: '+' K_SOFTFIXED  */
#line 2741 "def.y"
        { 
            if (defData->VersionNum < 6.0 - 0.0001) {
                if (defData->def60NewSyntaxError("COMPONENTS numComps ; - compName modelName SOFTFIXED ...")) {
                    CHKERR();
                }
            
                (yyval.integer) = DEFI_COMPONENT_UNPLACED; 
            } else {
                (yyval.integer) = DEFI_COMPONENT_SOFTFIXED; 
            }
        }
#line 7239 "def.tab.cpp"
    break;

  case 445: /* $@77: %empty  */
#line 2753 "def.y"
                                {defData->dumb_mode = 3;}
#line 7245 "def.tab.cpp"
    break;

  case 446: /* comp_pinprop: '+' K_PINPROPERTY $@77 T_STRING prop_name_value  */
#line 2754 "def.y"
        {
          if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("COMPONENTS ... + PINPROPERTY propName propType")) {
                CHKERR();
            }
          }

          if (defData->callbacks->ComponentCbk) {
            defData->setPropDataType((yyvsp[0].prop), "COMPONENTPIN", defData->session->CompPinProp);
            defData->Component->addPinprop((yyvsp[-1].string), (yyvsp[0].prop));
            (yyvsp[0].prop) = 0;
          }

          delete (yyvsp[0].prop);
        }
#line 7265 "def.tab.cpp"
    break;

  case 447: /* comp_physical: '+' K_PHYSICAL  */
#line 2771 "def.y"
        {
          if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("COMPONENTS ... + PHYSICAL")) {
                CHKERR();
            }
          }

          if (defData->callbacks->ComponentCbk) {
            defData->Component->setPhysical();
          }
        }
#line 7281 "def.tab.cpp"
    break;

  case 448: /* weight: '+' K_WEIGHT NUMBER  */
#line 2784 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("COMPONENTS ...+ WEIGHT weight")) {
                    CHKERR();
                }
            } else if (defData->callbacks->ComponentCbk) {
                defData->Component->setWeight(ROUND((yyvsp[0].dval)));
            }
        }
#line 7295 "def.tab.cpp"
    break;

  case 449: /* end_comps: K_END K_COMPS  */
#line 2795 "def.y"
        { 
            if (defData->callbacks->ComponentEndCbk) {
                  CALLBACK(defData->callbacks->ComponentEndCbk,
                           defrComponentEndCbkType, 0);
            }

            delete defData->Component;
            defData->Component = NULL;
        }
#line 7309 "def.tab.cpp"
    break;

  case 451: /* start_nets: K_NETS NUMBER ';'  */
#line 2809 "def.y"
    {
        defData->Net = new defiNet(defData);

        if (defData->callbacks->NetStartCbk) {
            CALLBACK(defData->callbacks->NetStartCbk,
                     defrNetStartCbkType, ROUND((yyvsp[-1].dval)));
        }

        defData->netOsnet = 1;
        defData->routeStatus = (char*)"ROUTED";
        defData->shieldName = NULL;
    }
#line 7326 "def.tab.cpp"
    break;

  case 454: /* one_net: net_and_connections net_options ';'  */
#line 2828 "def.y"
    { 
        if (defData->callbacks->NetCbk) {
            defData->setPropsDataTypes("NET", defData->session->NetProp);
            defData->addNetProps();
            defData->cleanProps();

            CALLBACK(defData->callbacks->NetCbk, defrNetCbkType, defData->Net);

            defData->Net->clear();
        }
    }
#line 7342 "def.tab.cpp"
    break;

  case 455: /* net_and_connections: net_start  */
#line 2846 "def.y"
        {defData->dumb_mode = 0; defData->no_num = 0; }
#line 7348 "def.tab.cpp"
    break;

  case 456: /* $@78: %empty  */
#line 2850 "def.y"
        {
            defData->dumb_mode = DEF_MAX_INT; 
            defData->no_num = DEF_MAX_INT; 
            defData->nondef_is_keyword = TRUE; 
            defData->mustjoin_is_keyword = TRUE;
            defData->routeStatus = (char*)"ROUTED";
            defData->shieldName = NULL;
        }
#line 7361 "def.tab.cpp"
    break;

  case 458: /* $@79: %empty  */
#line 2860 "def.y"
        {
          // 9/22/1999 
          // this is shared by both net and special net 
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->setName((yyvsp[0].string));
          if (defData->callbacks->NetNameCbk)
            CALLBACK(defData->callbacks->NetNameCbk, defrNetNameCbkType, (yyvsp[0].string));
          // Skip net body if flag is set
          if ((defData->settings->SkipNetDetails && (defData->netOsnet==1)) ||
              (defData->settings->SkipSNetDetails && (defData->netOsnet==2))) {
              defData->skip_net_body(defData->netOsnet);
          }
        }
#line 7379 "def.tab.cpp"
    break;

  case 460: /* $@80: %empty  */
#line 2873 "def.y"
                                  {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7385 "def.tab.cpp"
    break;

  case 461: /* net_name: K_MUSTJOIN '(' T_STRING $@80 T_STRING ')'  */
#line 2874 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addMustPin((yyvsp[-3].string), (yyvsp[-1].string), 0);
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7396 "def.tab.cpp"
    break;

  case 464: /* $@81: %empty  */
#line 2885 "def.y"
                             {defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT;}
#line 7402 "def.tab.cpp"
    break;

  case 465: /* net_connection: '(' T_STRING $@81 T_STRING conn_opt ')'  */
#line 2887 "def.y"
        {
          // 9/22/1999 
          // since the code is shared by both net & special net, 
          // need to check on both flags 
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addPin((yyvsp[-4].string), (yyvsp[-2].string), (yyvsp[-1].integer));
          // 1/14/2000 - pcr 289156 
          // reset defData->dumb_mode & defData->no_num to 3 , just in case 
          // the next statement is another net_connection 
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7419 "def.tab.cpp"
    break;

  case 466: /* $@82: %empty  */
#line 2899 "def.y"
                  {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7425 "def.tab.cpp"
    break;

  case 467: /* net_connection: '(' '*' $@82 T_STRING conn_opt ')'  */
#line 2900 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addPin("*", (yyvsp[-2].string), (yyvsp[-1].integer));
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7436 "def.tab.cpp"
    break;

  case 468: /* $@83: %empty  */
#line 2906 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7442 "def.tab.cpp"
    break;

  case 469: /* net_connection: '(' K_PIN $@83 T_STRING conn_opt ')'  */
#line 2907 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addPin("PIN", (yyvsp[-2].string), (yyvsp[-1].integer));
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7453 "def.tab.cpp"
    break;

  case 470: /* conn_opt: %empty  */
#line 2915 "def.y"
          { (yyval.integer) = 0; }
#line 7459 "def.tab.cpp"
    break;

  case 471: /* conn_opt: extension_stmt  */
#line 2917 "def.y"
        {
          if (defData->callbacks->NetConnectionExtCbk)
            CALLBACK(defData->callbacks->NetConnectionExtCbk, defrNetConnectionExtCbkType,
              &defData->History_text[0]);
          (yyval.integer) = 0;
        }
#line 7470 "def.tab.cpp"
    break;

  case 472: /* conn_opt: '+' K_SYNTHESIZED  */
#line 2924 "def.y"
        {  
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... {compName pinName | PIN pinName} + SYNTHESIZED")) {
                    CHKERR();
                }
            } 
           
            (yyval.integer) = 1; 
        }
#line 7484 "def.tab.cpp"
    break;

  case 475: /* $@84: %empty  */
#line 2941 "def.y"
        {
            if (defData->callbacks->NetCbk) {
                defData->setPropsDataTypes("NET", defData->session->NetProp);
                defData->addNetProps();
             }

            defData->cleanProps();        
            defData->dumb_mode = 1; 
        }
#line 7498 "def.tab.cpp"
    break;

  case 476: /* net_option: '+' net_type $@84 opt_wire  */
#line 2951 "def.y"
        {}
#line 7504 "def.tab.cpp"
    break;

  case 477: /* net_option: '+' K_SOURCE netsource_type  */
#line 2954 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + SOURCE {DIST|NETLIST|TEST|TIMING|USER}")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setSource((yyvsp[0].string)); 
            }
        }
#line 7518 "def.tab.cpp"
    break;

  case 478: /* net_option: '+' K_FIXEDBUMP  */
#line 2965 "def.y"
        {
          if (defData->VersionNum < 5.5) {
            if (defData->callbacks->NetCbk) {
              if (defData->netWarnings++ < defData->settings->NetWarnings) {
                 defData->defMsg = (char*)malloc(1000);
                 sprintf (defData->defMsg,
                    "The FIXEDBUMP statement is available in version 5.5 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                 defData->defError(6530, defData->defMsg);
                 free(defData->defMsg);
                 CHKERR();
              }
            }
          }
          if (defData->callbacks->NetCbk) defData->Net->setFixedbump();
        }
#line 7538 "def.tab.cpp"
    break;

  case 479: /* $@85: %empty  */
#line 2981 "def.y"
                          { defData->real_num = 1; }
#line 7544 "def.tab.cpp"
    break;

  case 480: /* net_option: '+' K_FREQUENCY $@85 NUMBER  */
#line 2982 "def.y"
        {
          if (defData->VersionNum < 5.5) {
            if (defData->callbacks->NetCbk) {
              if (defData->netWarnings++ < defData->settings->NetWarnings) {
                 defData->defMsg = (char*)malloc(1000);
                 sprintf (defData->defMsg,
                    "The FREQUENCY statement is a version 5.5 and later syntax.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
                 defData->defError(6558, defData->defMsg);
                 free(defData->defMsg);
                 CHKERR();
              }
            }
          }
          if (defData->callbacks->NetCbk) defData->Net->setFrequency((yyvsp[0].dval));
          defData->real_num = 0;
        }
#line 7565 "def.tab.cpp"
    break;

  case 481: /* $@86: %empty  */
#line 2999 "def.y"
                         {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7571 "def.tab.cpp"
    break;

  case 482: /* net_option: '+' K_ORIGINAL $@86 T_STRING  */
#line 3000 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + ORIGINAL netName")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setOriginal((yyvsp[0].string)); 
            }
        }
#line 7585 "def.tab.cpp"
    break;

  case 483: /* net_option: '+' K_PATTERN nets_pattern_type  */
#line 3010 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setPattern((yyvsp[0].string)); }
#line 7591 "def.tab.cpp"
    break;

  case 484: /* net_option: '+' K_WEIGHT NUMBER  */
#line 3013 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setWeight(ROUND((yyvsp[0].dval))); }
#line 7597 "def.tab.cpp"
    break;

  case 485: /* net_option: '+' K_XTALK NUMBER  */
#line 3016 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + XTALK class")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setXTalk(ROUND((yyvsp[0].dval))); 
            }
        }
#line 7611 "def.tab.cpp"
    break;

  case 486: /* net_option: '+' K_ESTCAP NUMBER  */
#line 3027 "def.y"
        {  
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + ESTCAP wireCapacitance")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setCap((yyvsp[0].dval)); 
            }
         }
#line 7625 "def.tab.cpp"
    break;

  case 487: /* net_option: '+' K_USE use_type  */
#line 3038 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setUse((yyvsp[0].string)); }
#line 7631 "def.tab.cpp"
    break;

  case 488: /* net_option: '+' K_STYLE NUMBER  */
#line 3041 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setStyle((int)(yyvsp[0].dval)); }
#line 7637 "def.tab.cpp"
    break;

  case 489: /* $@87: %empty  */
#line 3043 "def.y"
                               { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7643 "def.tab.cpp"
    break;

  case 490: /* net_option: '+' K_NONDEFAULTRULE $@87 T_STRING  */
#line 3044 "def.y"
        { 
          if (defData->callbacks->NetCbk && defData->callbacks->NetNonDefaultRuleCbk) {
             // User wants a callback on nondefaultrule 
             CALLBACK(defData->callbacks->NetNonDefaultRuleCbk,
                      defrNetNonDefaultRuleCbkType, (yyvsp[0].string));
          }
          // Still save data in the class 
          if (defData->callbacks->NetCbk) defData->Net->setNonDefaultRule((yyvsp[0].string));
        }
#line 7657 "def.tab.cpp"
    break;

  case 492: /* $@88: %empty  */
#line 3056 "def.y"
                          { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7663 "def.tab.cpp"
    break;

  case 493: /* $@89: %empty  */
#line 3057 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addShieldNet((yyvsp[0].string)); }
#line 7669 "def.tab.cpp"
    break;

  case 494: /* net_option: '+' K_SHIELDNET $@88 T_STRING $@89  */
#line 3059 "def.y"
        {
          if (defData->VersionNum < 5.4) {   // PCR 445209 
            defData->by_is_keyword = FALSE;
            defData->do_is_keyword = FALSE;
            defData->new_is_keyword = FALSE;
            defData->step_is_keyword = FALSE;
            defData->nondef_is_keyword = FALSE;
            defData->mustjoin_is_keyword = FALSE;
            defData->orient_is_keyword = FALSE;
            defData->virtual_is_keyword = FALSE;
            defData->rect_is_keyword = FALSE;
            defData->mask_is_keyword = FALSE;
          } else {
            defData->by_is_keyword = FALSE;
            defData->do_is_keyword = FALSE;
            defData->new_is_keyword = FALSE;
            defData->step_is_keyword = FALSE;
            defData->nondef_is_keyword = FALSE;
            defData->mustjoin_is_keyword = FALSE;
            defData->orient_is_keyword = FALSE;
            defData->virtual_is_keyword = FALSE;
            defData->rect_is_keyword = FALSE;
            defData->mask_is_keyword = FALSE;
          }
          defData->needNPCbk = 0;
        }
#line 7700 "def.tab.cpp"
    break;

  case 495: /* $@90: %empty  */
#line 3087 "def.y"
        { defData->dumb_mode = 1; defData->no_num = 1;
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + SUBNET")) {
                    CHKERR();
                }
            } 
          
            if (defData->callbacks->NetCbk) {
                defData->Subnet = new defiSubnet(defData);
            }
        }
#line 7716 "def.tab.cpp"
    break;

  case 496: /* $@91: %empty  */
#line 3098 "def.y"
                 {
          if (defData->callbacks->NetCbk && defData->callbacks->NetSubnetNameCbk) {
            // User wants a callback on Net subnetName 
            CALLBACK(defData->callbacks->NetSubnetNameCbk, defrNetSubnetNameCbkType, (yyvsp[0].string));
          }
          // Still save the subnet name in the class 
          if (defData->callbacks->NetCbk) {
            defData->Subnet->setName((yyvsp[0].string));
          }
        }
#line 7731 "def.tab.cpp"
    break;

  case 497: /* $@92: %empty  */
#line 3108 "def.y"
                   {
          defData->routed_is_keyword = TRUE;
          defData->fixed_is_keyword = TRUE;
          defData->cover_is_keyword = TRUE;
        }
#line 7741 "def.tab.cpp"
    break;

  case 498: /* net_option: '+' K_SUBNET $@90 T_STRING $@91 comp_names $@92 subnet_options  */
#line 3112 "def.y"
                         {
          if (defData->callbacks->NetCbk) {
            defData->Net->addSubnet(defData->Subnet);
            defData->Subnet = NULL;
            defData->routed_is_keyword = FALSE;
            defData->fixed_is_keyword = FALSE;
            defData->cover_is_keyword = FALSE;
          }
        }
#line 7755 "def.tab.cpp"
    break;

  case 499: /* $@93: %empty  */
#line 3123 "def.y"
        {
            defData->dumb_mode = DEF_MAX_INT;
        }
#line 7763 "def.tab.cpp"
    break;

  case 500: /* net_option: '+' K_PROPERTY $@93 net_prop_name_values  */
#line 3127 "def.y"
        {
            defData->dumb_mode = 0;
        }
#line 7771 "def.tab.cpp"
    break;

  case 501: /* net_option: extension_stmt  */
#line 3132 "def.y"
        { 
          if (defData->callbacks->NetExtCbk)
            CALLBACK(defData->callbacks->NetExtCbk, defrNetExtCbkType, &defData->History_text[0]);
        }
#line 7780 "def.tab.cpp"
    break;

  case 502: /* netsource_type: K_NETLIST  */
#line 3138 "def.y"
        { (yyval.string) = (char*)"NETLIST"; }
#line 7786 "def.tab.cpp"
    break;

  case 503: /* netsource_type: K_DIST  */
#line 3140 "def.y"
        { (yyval.string) = (char*)"DIST"; }
#line 7792 "def.tab.cpp"
    break;

  case 504: /* netsource_type: K_USER  */
#line 3142 "def.y"
        { (yyval.string) = (char*)"USER"; }
#line 7798 "def.tab.cpp"
    break;

  case 505: /* netsource_type: K_TIMING  */
#line 3144 "def.y"
        { (yyval.string) = (char*)"TIMING"; }
#line 7804 "def.tab.cpp"
    break;

  case 506: /* netsource_type: K_TEST  */
#line 3146 "def.y"
        { (yyval.string) = (char*)"TEST"; }
#line 7810 "def.tab.cpp"
    break;

  case 507: /* $@94: %empty  */
#line 3149 "def.y"
        {
          // vpin_options may have to deal with orient 
          defData->orient_is_keyword = TRUE;
        }
#line 7819 "def.tab.cpp"
    break;

  case 508: /* vpin_stmt: vpin_begin vpin_layer_opt pt pt $@94 vpin_options  */
#line 3154 "def.y"
        { if (defData->callbacks->NetCbk)
            defData->Net->addVpinBounds((yyvsp[-3].pt).x, (yyvsp[-3].pt).y, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y);
          defData->orient_is_keyword = FALSE;
        }
#line 7828 "def.tab.cpp"
    break;

  case 509: /* $@95: %empty  */
#line 3159 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7834 "def.tab.cpp"
    break;

  case 510: /* vpin_begin: '+' K_VPIN $@95 T_STRING  */
#line 3160 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + VPIN")) {
                    CHKERR();
                }
            } 
            
            if (defData->callbacks->NetCbk) {
                defData->Net->addVpin((yyvsp[0].string)); 
            }
         }
#line 7850 "def.tab.cpp"
    break;

  case 512: /* $@96: %empty  */
#line 3173 "def.y"
                  {defData->dumb_mode=1;}
#line 7856 "def.tab.cpp"
    break;

  case 513: /* vpin_layer_opt: K_LAYER $@96 T_STRING  */
#line 3174 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addVpinLayer((yyvsp[0].string)); }
#line 7862 "def.tab.cpp"
    break;

  case 515: /* vpin_options: vpin_status pt orient  */
#line 3178 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addVpinLoc((yyvsp[-2].string), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer)); }
#line 7868 "def.tab.cpp"
    break;

  case 516: /* vpin_status: K_PLACED  */
#line 3181 "def.y"
        { (yyval.string) = (char*)"PLACED"; }
#line 7874 "def.tab.cpp"
    break;

  case 517: /* vpin_status: K_FIXED  */
#line 3183 "def.y"
        { (yyval.string) = (char*)"FIXED"; }
#line 7880 "def.tab.cpp"
    break;

  case 518: /* vpin_status: K_COVER  */
#line 3185 "def.y"
        { (yyval.string) = (char*)"COVER"; }
#line 7886 "def.tab.cpp"
    break;

  case 519: /* net_type: K_FIXED  */
#line 3188 "def.y"
        { 
            defData->routeStatus = (char*)"FIXED";
            defData->shieldName = NULL;
        }
#line 7895 "def.tab.cpp"
    break;

  case 520: /* net_type: K_COVER  */
#line 3193 "def.y"
        { 
            defData->routeStatus = (char*)"COVER";
            defData->shieldName = NULL;
        }
#line 7904 "def.tab.cpp"
    break;

  case 521: /* net_type: K_ROUTED  */
#line 3198 "def.y"
        { 
            defData->routeStatus = (char*)"ROUTED"; 
            defData->shieldName = NULL;
        }
#line 7913 "def.tab.cpp"
    break;

  case 522: /* net_type: K_NOSHIELD  */
#line 3203 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) {            
                if (defData->def60ObsoletedError("NETS ... regularWiring ... + NOSHIELD")) {
                    CHKERR();
                }
            }

            defData->routeStatus = (char*)"NOSHIELD";
            defData->shieldName = NULL;
        }
#line 7928 "def.tab.cpp"
    break;

  case 523: /* $@97: %empty  */
#line 3216 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            if (defData->VersionNum < 5.4
                && strcmp(defData->routeStatus, "NOSHIELD") == 0) { // PCR445209
                defData->Shield = new defiShield(defData);
                defData->Shield->Init("");
                defData->Net->addShield(defData->Shield, true);
            } else {
                defData->Wire = new defiWire(defData);
                defData->Wire->Init(defData->routeStatus, NULL);
                defData->Net->addWire(defData->Wire);
            }
        }
    }
#line 7947 "def.tab.cpp"
    break;

  case 524: /* opt_wire: $@97 opt_paths  */
#line 3231 "def.y"
    {
        if (defData->callbacks->NetCbk
            && defData->callbacks->NetPartialPathCbk
            && !defData->Shield) {
            // Wire ended: call NetPartialPathCbk regardless of threshold
            // This ensures all paths in this wire are processed
            CALLBACK(defData->callbacks->NetPartialPathCbk,
                     defrNetPartialPathCbkType, defData->Net);
            defData->Net->clearLastWire();
            defData->Net->clearRectPolyNPath();
        }

        defData->Shield = NULL;
        defData->Wire = NULL;
    }
#line 7967 "def.tab.cpp"
    break;

  case 526: /* opt_paths: paths  */
#line 3249 "def.y"
      {
          defData->by_is_keyword = FALSE;
          defData->do_is_keyword = FALSE;
          defData->new_is_keyword = FALSE;
          defData->nondef_is_keyword = FALSE;
          defData->mustjoin_is_keyword = FALSE;
          defData->step_is_keyword = FALSE;
          defData->orient_is_keyword = FALSE;
          defData->virtual_is_keyword = FALSE;
          defData->rect_is_keyword = FALSE;
          defData->mask_is_keyword = FALSE;
          defData->width_is_keyword = FALSE;
          defData->needNPCbk = 0;
          defData->routeStatus = (char*)"ROUTED";
          defData->shieldName = NULL;
      }
#line 7988 "def.tab.cpp"
    break;

  case 527: /* $@98: %empty  */
#line 3267 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 7999 "def.tab.cpp"
    break;

  case 528: /* paths: $@98 path  */
#line 3274 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            if (defData->needNPCbk && defData->callbacks->NetPartialPathCbk) {
                CALLBACK(defData->callbacks->NetPartialPathCbk, defrNetPartialPathCbkType, defData->Net);
                defData->needNPCbk = 0;
                defData->finishPath(1, &defData->needNPCbk);
                defData->Net->clearRectPolyNPath();
            } else {
                defData->finishPath(0, &defData->needNPCbk);
            }
            defData->PathObj = NULL;
        }
    }
#line 8017 "def.tab.cpp"
    break;

  case 529: /* paths: paths new_path  */
#line 3288 "def.y"
    { }
#line 8023 "def.tab.cpp"
    break;

  case 530: /* $@99: %empty  */
#line 3291 "def.y"
    {
        defData->dumb_mode = 1;

        if (defData->callbacks->NetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 8036 "def.tab.cpp"
    break;

  case 531: /* new_path: K_NEW $@99 path  */
#line 3300 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            if (defData->needNPCbk && defData->callbacks->NetPartialPathCbk) {
                CALLBACK(defData->callbacks->NetPartialPathCbk, defrNetPartialPathCbkType, defData->Net);
                defData->needNPCbk = 0;
                defData->finishPath(1, &defData->needNPCbk);
                defData->Net->clearRectPolyNPath();
            } else {
                defData->finishPath(0, &defData->needNPCbk);
            }
            defData->PathObj = NULL;
        }
    }
#line 8054 "def.tab.cpp"
    break;

  case 532: /* $@100: %empty  */
#line 3316 "def.y"
      {
        if ((strcmp((yyvsp[0].string), "TAPER") == 0) || (strcmp((yyvsp[0].string), "TAPERRULE") == 0)) {
          if (defData->callbacks->NetCbk) {
            if (defData->netWarnings++ < defData->settings->NetWarnings) {
              defData->defError(6531, "The layerName which is required in path is missing. Include the layerName in the path and then try again.");
              CHKERR();
            }
          }
          // Since there is already error, the next token is insignificant 
          defData->dumb_mode = 1; defData->no_num = 1;
        } else {
          // CCR 766289 - Do not accummulate the layer information if there 
          // is not a callback set 
          if (defData->callbacks->NetCbk) {
              defData->PathObj->addLayer((yyvsp[0].string));
          }

          defData->dumb_mode = 0; defData->no_num = 0;
        }
      }
#line 8079 "def.tab.cpp"
    break;

  case 533: /* $@101: %empty  */
#line 3337 "def.y"
      { defData->dumb_mode = DEF_MAX_INT; defData->by_is_keyword = TRUE; defData->do_is_keyword = TRUE;
/*
       dumb_mode = 1; by_is_keyword = TRUE; do_is_keyword = TRUE;
*/
        defData->new_is_keyword = TRUE; 
        defData->step_is_keyword = TRUE; 
        defData->orient_is_keyword = TRUE; 
        defData->virtual_is_keyword = TRUE;
        defData->mask_is_keyword = TRUE, 
        defData->rect_is_keyword = TRUE;
        defData->width_is_keyword = TRUE;
      }
#line 8096 "def.tab.cpp"
    break;

  case 534: /* path: T_STRING $@100 opt_taper_style_s path_pt $@101 path_item_list_opt  */
#line 3352 "def.y"
      { defData->dumb_mode = 0;   defData->virtual_is_keyword = FALSE; defData->mask_is_keyword = FALSE,
       defData->rect_is_keyword = FALSE; }
#line 8103 "def.tab.cpp"
    break;

  case 535: /* virtual_statement: K_VIRTUAL virtual_pt  */
#line 3357 "def.y"
    {
      if (defData->VersionNum < 5.8) {
              if (defData->callbacks->SNetCbk) {
                if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The VIRTUAL statement is available in version 5.8 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
                  defData->defError(6536, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
          }
    }
#line 8122 "def.tab.cpp"
    break;

  case 536: /* rect_statement: K_RECT rect_pts  */
#line 3374 "def.y"
    {
      if (defData->VersionNum < 5.8) {
              if (defData->callbacks->SNetCbk) {
                if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
                  defData->defMsg = (char*)malloc(1000);
                  sprintf (defData->defMsg,
                     "The RECT statement is available in version 5.8 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
                  defData->defError(6536, defData->defMsg);
                  free(defData->defMsg);
                  CHKERR();
                }
              }
      }
    }
#line 8141 "def.tab.cpp"
    break;

  case 537: /* path_item_list_opt: %empty  */
#line 3391 "def.y"
    {
    }
#line 8148 "def.tab.cpp"
    break;

  case 538: /* path_item_list_opt: path_item_list_opt path_item  */
#line 3394 "def.y"
    {}
#line 8154 "def.tab.cpp"
    break;

  case 539: /* path_item: T_STRING  */
#line 3399 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            if (strcmp((yyvsp[0].string), "TAPER") == 0) {
                defData->PathObj->setTaper();
            } else {
                defData->PathObj->addVia((yyvsp[0].string));
            }
        }
    }
#line 8169 "def.tab.cpp"
    break;

  case 540: /* path_item: K_MASK NUMBER T_STRING  */
#line 3410 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[-1].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                if (strcmp((yyvsp[0].string), "TAPER") == 0) {
                    defData->PathObj->setTaper();
                } else {
                    defData->PathObj->addViaMask((yyvsp[-1].dval));
                    defData->PathObj->addVia((yyvsp[0].string));
                }
            }
        }
    }
#line 8188 "def.tab.cpp"
    break;

  case 541: /* path_item: T_STRING orient  */
#line 3425 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVia((yyvsp[-1].string));
            defData->PathObj->addViaRotation((yyvsp[0].integer));
        }
    }
#line 8200 "def.tab.cpp"
    break;

  case 542: /* path_item: K_MASK NUMBER T_STRING orient  */
#line 3433 "def.y"
    { 
        if (defData->validateMaskInput((int)(yyvsp[-2].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                defData->PathObj->addViaMask((yyvsp[-2].dval));
                defData->PathObj->addVia((yyvsp[-1].string));
                defData->PathObj->addViaRotation((yyvsp[0].integer));
            }
        }
    }
#line 8216 "def.tab.cpp"
    break;

  case 543: /* path_item: K_MASK NUMBER T_STRING K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER  */
#line 3445 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[-8].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {      
            if (((yyvsp[-5].dval) == 0 || (yyvsp[-3].dval) == 0)
                && defData->callbacks->SNetCbk
                && (defData->netWarnings++ < defData->settings->NetWarnings)) {
                    defData->defError(6533, "Either the numX or numY in the VIA DO statement has the value. The value specified is 0.\nUpdate your DEF file with the correct value and then try again.\n");
                    CHKERR();
            }

            if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
                defData->PathObj->addViaMask((yyvsp[-8].dval));
                defData->PathObj->addVia((yyvsp[-7].string));
                defData->PathObj->addViaData((int)(yyvsp[-5].dval), (int)(yyvsp[-3].dval),
                                             (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
            } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
                if (defData->netWarnings++ < defData->settings->NetWarnings) {
                    defData->defError(6567, "The VIA DO statement is defined in the NET statement and is invalid.\nRemove this statement from your DEF file and try again.");
                    CHKERR();
                }
            }
        }
    }
#line 8244 "def.tab.cpp"
    break;

  case 544: /* path_item: T_STRING K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER  */
#line 3469 "def.y"
    {
        if ((defData->VersionNum < 5.5)
            && defData->callbacks->SNetCbk
            && (defData->netWarnings++ < defData->settings->NetWarnings)) {
            defData->defMsg = (char*)malloc(1000);
            sprintf (defData->defMsg,
                     "The VIA DO statement is available in version 5.5 and later.\nHowever, your DEF file is defined with version %.2f",
                     defData->VersionNum);
            defData->defError(6532, defData->defMsg);
            free(defData->defMsg);
            CHKERR();
        }

        if (((yyvsp[-5].dval) == 0 || (yyvsp[-3].dval) == 0)
            && defData->callbacks->SNetCbk
            && (defData->netWarnings++ < defData->settings->NetWarnings)) {
            defData->defError(6533, "Either the numX or numY in the VIA DO statement has the value. The value specified is 0.\nUpdate your DEF file with the correct value and then try again.\n");
            CHKERR();
        }

        if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
            defData->PathObj->addVia((yyvsp[-7].string));
            defData->PathObj->addViaData((int)(yyvsp[-5].dval), (int)(yyvsp[-3].dval), (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
        } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
            if (defData->netWarnings++ < defData->settings->NetWarnings) {
                defData->defError(6567, "The VIA DO statement is defined in the NET statement and is invalid.\nRemove this statement from your DEF file and try again.");
                CHKERR();
            }
        }
    }
#line 8279 "def.tab.cpp"
    break;

  case 545: /* path_item: T_STRING orient K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER  */
#line 3500 "def.y"
    {
        if ((defData->VersionNum < 5.5)
            && defData->callbacks->SNetCbk
            && (defData->netWarnings++ < defData->settings->NetWarnings)) {
            defData->defMsg = (char*)malloc(1000);
            sprintf (defData->defMsg, "The VIA DO statement is available in version 5.5 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
            defData->defError(6532, defData->defMsg);
            CHKERR();
        }

        if (((yyvsp[-5].dval) == 0 || (yyvsp[-3].dval) == 0)
            && defData->callbacks->SNetCbk
            && (defData->netWarnings++ < defData->settings->NetWarnings)) {
            defData->defError(6533, "Either the numX or numY in the VIA DO statement has the value. The value specified is 0.\nUpdate your DEF file with the correct value and then try again.\n");
            CHKERR();
        }

        if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
            defData->PathObj->addVia((yyvsp[-8].string));
            defData->PathObj->addViaRotation((yyvsp[-7].integer));
            defData->PathObj->addViaData((int)(yyvsp[-5].dval), (int)(yyvsp[-3].dval), (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
        } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
            if (defData->netWarnings++ < defData->settings->NetWarnings) {
                defData->defError(6567, "The VIA DO statement is defined in the NET statement and is invalid.\nRemove this statement from your DEF file and try again.");
                CHKERR();
            }
        }
    }
#line 8312 "def.tab.cpp"
    break;

  case 546: /* path_item: K_MASK NUMBER T_STRING orient K_DO NUMBER K_BY NUMBER K_STEP NUMBER NUMBER  */
#line 3529 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[-9].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if (((yyvsp[-5].dval) == 0 || (yyvsp[-3].dval) == 0)
                && defData->callbacks->SNetCbk
                && (defData->netWarnings++ < defData->settings->NetWarnings)) {
                defData->defError(6533, "Either the numX or numY in the VIA DO statement has the value. The value specified is 0.\nUpdate your DEF file with the correct value and then try again.\n");
                CHKERR();
            }

            if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
                defData->PathObj->addViaMask((yyvsp[-9].dval)); 
                defData->PathObj->addVia((yyvsp[-8].string));
                defData->PathObj->addViaRotation((yyvsp[-7].integer));;
                defData->PathObj->addViaData((int)(yyvsp[-5].dval), (int)(yyvsp[-3].dval), (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
            } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
                if (defData->netWarnings++ < defData->settings->NetWarnings) {
                    defData->defError(6567, "The VIA DO statement is defined in the NET statement and is invalid.\nRemove this statement from your DEF file and try again.");
                    CHKERR();
                }
            }
        }
    }
#line 8340 "def.tab.cpp"
    break;

  case 549: /* $@102: %empty  */
#line 3555 "def.y"
    {
        defData->dumb_mode = 6;
    }
#line 8348 "def.tab.cpp"
    break;

  case 550: /* path_item: K_MASK NUMBER K_RECT $@102 '(' NUMBER NUMBER NUMBER NUMBER ')'  */
#line 3559 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[-8].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                defData->PathObj->addMask((yyvsp[-8].dval));
                defData->PathObj->addViaRect((yyvsp[-4].dval), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval));
            }
        }
    }
#line 8363 "def.tab.cpp"
    break;

  case 552: /* path_item: wire_width path_pt  */
#line 3571 "def.y"
    {
       // reset defData->dumb_mode to 1 just incase the next token is a via of the path
        // 2/5/2004 - pcr 686781
        defData->dumb_mode = DEF_MAX_INT; defData->by_is_keyword = TRUE; defData->do_is_keyword = TRUE;
        defData->new_is_keyword = TRUE; defData->step_is_keyword = TRUE;
        defData->orient_is_keyword = TRUE;
    }
#line 8375 "def.tab.cpp"
    break;

  case 553: /* mask_number: K_MASK NUMBER  */
#line 3581 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                defData->PathObj->addMask((yyvsp[0].dval)); 
            }
        }  
    }
#line 8389 "def.tab.cpp"
    break;

  case 555: /* wire_width: K_WIDTH NUMBER  */
#line 3593 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.0001) {
            if (defData->def60NewSyntaxError("NETS ... ( x y [extValue] ) [MASK maskNum] [WIDTH width] ( x y [extValue] )")) {
                CHKERR();
            }
        } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
            defData->PathObj->addWidth((yyvsp[0].dval));
        } 
    }
#line 8403 "def.tab.cpp"
    break;

  case 556: /* path_pt: '(' NUMBER NUMBER ')'  */
#line 3605 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-2].dval);
        defData->save_y = (yyvsp[-1].dval); 
    }
#line 8417 "def.tab.cpp"
    break;

  case 557: /* path_pt: '(' '*' NUMBER ')'  */
#line 3615 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND(defData->save_x), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-1].dval);
      }
#line 8430 "def.tab.cpp"
    break;

  case 558: /* path_pt: '(' NUMBER '*' ')'  */
#line 3624 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addPoint(ROUND((yyvsp[-2].dval)), ROUND(defData->save_y));
        }

        defData->save_x = (yyvsp[-2].dval);
    }
#line 8443 "def.tab.cpp"
    break;

  case 559: /* path_pt: '(' '*' '*' ')'  */
#line 3633 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND(defData->save_x),
                                       ROUND(defData->save_y));
        }
    }
#line 8455 "def.tab.cpp"
    break;

  case 560: /* path_pt: '(' NUMBER NUMBER NUMBER ')'  */
#line 3641 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND((yyvsp[-3].dval)), ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-3].dval);
        defData->save_y = (yyvsp[-2].dval);
    }
#line 8469 "def.tab.cpp"
    break;

  case 561: /* path_pt: '(' '*' NUMBER NUMBER ')'  */
#line 3651 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND(defData->save_x),
                                            ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-2].dval);
    }
#line 8483 "def.tab.cpp"
    break;

  case 562: /* path_pt: '(' NUMBER '*' NUMBER ')'  */
#line 3661 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND((yyvsp[-3].dval)), ROUND(defData->save_y),
                                           ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-3].dval);
    }
#line 8497 "def.tab.cpp"
    break;

  case 563: /* path_pt: '(' '*' '*' NUMBER ')'  */
#line 3671 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addFlushPoint(ROUND(defData->save_x),
                                           ROUND(defData->save_y),
                                           ROUND((yyvsp[-1].dval)));
        }
    }
#line 8510 "def.tab.cpp"
    break;

  case 564: /* virtual_pt: '(' NUMBER NUMBER ')'  */
#line 3682 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-2].dval);
        defData->save_y = (yyvsp[-1].dval);
    }
#line 8524 "def.tab.cpp"
    break;

  case 565: /* virtual_pt: '(' '*' NUMBER ')'  */
#line 3692 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND(defData->save_x), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-1].dval);
    }
#line 8537 "def.tab.cpp"
    break;

  case 566: /* virtual_pt: '(' NUMBER '*' ')'  */
#line 3701 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND((yyvsp[-2].dval)), ROUND(defData->save_y));
        }

        defData->save_x = (yyvsp[-2].dval);
    }
#line 8550 "def.tab.cpp"
    break;

  case 567: /* virtual_pt: '(' '*' '*' ')'  */
#line 3710 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
             || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND(defData->save_x),
                                              ROUND(defData->save_y));
        }
    }
#line 8562 "def.tab.cpp"
    break;

  case 568: /* rect_pts: '(' NUMBER NUMBER NUMBER NUMBER ')'  */
#line 3720 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addViaRect((yyvsp[-4].dval), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval)); 
        }    
    }
#line 8573 "def.tab.cpp"
    break;

  case 575: /* $@103: %empty  */
#line 3738 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 8581 "def.tab.cpp"
    break;

  case 576: /* opt_prop: K_PROPERTY $@103 prop_name_value  */
#line 3742 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("NETS regularWiring: layerName + PROPERTY propName propValue ... routingPoints")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
                defData->setPropDataType((yyvsp[0].prop), "ROUTE",
                                         defData->session->RouteProp);
                defData->PathObj->addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = NULL;
            }
        }

        delete (yyvsp[0].prop);
    }
#line 8602 "def.tab.cpp"
    break;

  case 577: /* opt_shield: K_SHIELD  */
#line 3760 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("NETS regularWiring: layerName + SHIELD ... routingPoints")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
                defData->PathObj->setShield("");
            }
        }
    }
#line 8618 "def.tab.cpp"
    break;

  case 578: /* opt_taper: K_TAPER  */
#line 3773 "def.y"
    {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("NETS ... layerName ... [TAPER]")) {
                CHKERR();
            }
        } else if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
                   || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->setTaper(); 
        }
    }
#line 8633 "def.tab.cpp"
    break;

  case 579: /* $@104: %empty  */
#line 3783 "def.y"
                { defData->dumb_mode = 1; }
#line 8639 "def.tab.cpp"
    break;

  case 580: /* opt_taper: K_TAPERRULE $@104 T_STRING  */
#line 3784 "def.y"
    {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("NETS ... layerName ... [TAPERRULE ruleName]")) {
                CHKERR();
            }
        } else if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
                   || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addTaperRule((yyvsp[0].string)); 
        }
    }
#line 8654 "def.tab.cpp"
    break;

  case 581: /* opt_style: K_STYLE NUMBER  */
#line 3796 "def.y"
    { 
        if (defData->VersionNum < 5.6) {
            if (defData->callbacks->NetCbk || defData->callbacks->SNetCbk) {
                if (defData->netWarnings++ < defData->settings->NetWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf (defData->defMsg,
                             "The STYLE statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f",
                             defData->VersionNum);
                    defData->defError(6534, defData->defMsg);
                    free(defData->defMsg);
                    CHKERR();
                }
            }
        } else if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("NETS ... layerName ... [STYLE styleNum]")) {
                CHKERR();
            }
        } else if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
                   || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addStyle((int)(yyvsp[0].dval));
        }
    }
#line 8681 "def.tab.cpp"
    break;

  case 584: /* opt_shape_style_prop: '+' K_SHAPE shape_type  */
#line 3825 "def.y"
    {  
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addShape((yyvsp[0].string)); 
        }
    }
#line 8692 "def.tab.cpp"
    break;

  case 585: /* opt_shape_style_prop: '+' K_STYLE NUMBER  */
#line 3833 "def.y"
    {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS specialWiring: layerName routeWidth ... + STYLE styleType")) {
                CHKERR();
            }
        } else if (defData->VersionNum < 5.6) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                if (defData->netWarnings++ < defData->settings->NetWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf (defData->defMsg,
                             "The STYLE statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f",
                             defData->VersionNum);
                    defData->defError(6534, defData->defMsg);
                    free(defData->defMsg);
                    CHKERR();
                }
            }
        } else {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                defData->PathObj->addStyle((int)(yyvsp[0].dval));
            }
        }
    }
#line 8722 "def.tab.cpp"
    break;

  case 586: /* $@105: %empty  */
#line 3860 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 8730 "def.tab.cpp"
    break;

  case 587: /* opt_shape_style_prop: '+' K_PROPERTY $@105 prop_name_value  */
#line 3864 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("SPECIALNETS specialWiring: layerName routeWidth + PROPERTY propName propValue ... routingPoints")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
                defData->setPropDataType((yyvsp[0].prop), "SPECIALROUTE",
                                         defData->session->SpecialRouteProp);
                defData->PathObj->addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = NULL;
            }
        }
        
        delete (yyvsp[0].prop);
    }
#line 8751 "def.tab.cpp"
    break;

  case 588: /* $@106: %empty  */
#line 3882 "def.y"
    { 
        defData->dumb_mode = 1; 
        defData->no_num = 1; 
    }
#line 8760 "def.tab.cpp"
    break;

  case 589: /* opt_shape_style_prop: '+' K_SHIELD $@106 T_STRING  */
#line 3887 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("SPECIALNETS specialWiring: layerName routeWidth + SHIELD shieldNetName ... routingPoints")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->SNetCbk && (defData->netOsnet == 2)) {
                defData->PathObj->setShield((yyvsp[0].string));
            }
        }    
    }
#line 8776 "def.tab.cpp"
    break;

  case 590: /* end_nets: K_END K_NETS  */
#line 3900 "def.y"
    {
        if (defData->callbacks->NetEndCbk) {
            CALLBACK(defData->callbacks->NetEndCbk, defrNetEndCbkType, 0);
        }

        defData->netOsnet = 0;
        defData->width_is_keyword = FALSE;

        delete defData->Net;
        defData->Net = NULL;
    }
#line 8792 "def.tab.cpp"
    break;

  case 591: /* shape_type: K_RING  */
#line 3913 "def.y"
            { (yyval.string) = (char*)"RING"; }
#line 8798 "def.tab.cpp"
    break;

  case 592: /* shape_type: K_STRIPE  */
#line 3915 "def.y"
            { (yyval.string) = (char*)"STRIPE"; }
#line 8804 "def.tab.cpp"
    break;

  case 593: /* shape_type: K_FOLLOWPIN  */
#line 3917 "def.y"
            { (yyval.string) = (char*)"FOLLOWPIN"; }
#line 8810 "def.tab.cpp"
    break;

  case 594: /* shape_type: K_IOWIRE  */
#line 3919 "def.y"
            { (yyval.string) = (char*)"IOWIRE"; }
#line 8816 "def.tab.cpp"
    break;

  case 595: /* shape_type: K_COREWIRE  */
#line 3921 "def.y"
            { (yyval.string) = (char*)"COREWIRE"; }
#line 8822 "def.tab.cpp"
    break;

  case 596: /* shape_type: K_BLOCKWIRE  */
#line 3923 "def.y"
            { (yyval.string) = (char*)"BLOCKWIRE"; }
#line 8828 "def.tab.cpp"
    break;

  case 597: /* shape_type: K_FILLWIRE  */
#line 3925 "def.y"
            { (yyval.string) = (char*)"FILLWIRE"; }
#line 8834 "def.tab.cpp"
    break;

  case 598: /* shape_type: K_FILLWIREOPC  */
#line 3927 "def.y"
            {
              if (defData->VersionNum < 5.7) {
                 if (defData->callbacks->NetCbk || defData->callbacks->SNetCbk) {
                   if (defData->fillWarnings++ < defData->settings->FillWarnings) {
                     defData->defMsg = (char*)malloc(10000);
                     sprintf (defData->defMsg,
                       "The FILLWIREOPC is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                     defData->defError(6552, defData->defMsg);
                     free(defData->defMsg);
                     CHKERR();
                  }
                }
              }
              (yyval.string) = (char*)"FILLWIREOPC";
            }
#line 8854 "def.tab.cpp"
    break;

  case 599: /* shape_type: K_DRCFILL  */
#line 3943 "def.y"
            { (yyval.string) = (char*)"DRCFILL"; }
#line 8860 "def.tab.cpp"
    break;

  case 600: /* shape_type: K_BLOCKAGEWIRE  */
#line 3945 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... + SHAPE BLOCKAGEWIRE")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"BLOCKAGEWIRE"; 
            }
#line 8874 "def.tab.cpp"
    break;

  case 601: /* shape_type: K_PADRING  */
#line 3955 "def.y"
            { (yyval.string) = (char*)"PADRING"; }
#line 8880 "def.tab.cpp"
    break;

  case 602: /* shape_type: K_BLOCKRING  */
#line 3957 "def.y"
            { (yyval.string) = (char*)"BLOCKRING"; }
#line 8886 "def.tab.cpp"
    break;

  case 606: /* snet_rule: net_and_connections snet_options ';'  */
#line 3968 "def.y"
    { 
        defData->routeStatus = (char*)"ROUTED";
        defData->shieldName = NULL;

        if (defData->callbacks->SNetCbk) {
            defData->setPropsDataTypes("SPECIAL NET",
                                       defData->session->SNetProp);
            defData->addNetProps();

            CALLBACK(defData->callbacks->SNetCbk,
                     defrSNetCbkType, defData->Net);

            defData->Net->clear();
        }

        defData->cleanProps();
     }
#line 8908 "def.tab.cpp"
    break;

  case 608: /* snet_options: snet_options snet_option  */
#line 3988 "def.y"
        {
        }
#line 8915 "def.tab.cpp"
    break;

  case 613: /* $@107: %empty  */
#line 3996 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->setPropsDataTypes("SPECIAL NET", defData->session->SNetProp);
            defData->addNetProps();
         }

        defData->cleanProps();       
    }
#line 8928 "def.tab.cpp"
    break;

  case 614: /* snet_other_option: '+' snet_type $@107 opt_swire  */
#line 4005 "def.y"
    {
    }
#line 8935 "def.tab.cpp"
    break;

  case 615: /* snet_other_option: '+' K_SHAPE shape_type  */
#line 4009 "def.y"
    {  
      defData->shapeType = (yyvsp[0].string);
    }
#line 8943 "def.tab.cpp"
    break;

  case 616: /* snet_other_option: '+' K_MASK NUMBER  */
#line 4014 "def.y"
    {
      if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->sNetWarnings, defData->settings->SNetWarnings)) {
          defData->specialWire_mask = (yyvsp[0].dval);
      }     
    }
#line 8953 "def.tab.cpp"
    break;

  case 617: /* $@108: %empty  */
#line 4021 "def.y"
    {
      defData->dumb_mode = DEF_MAX_INT;
    }
#line 8961 "def.tab.cpp"
    break;

  case 618: /* snet_other_option: '+' K_PROPERTY $@108 net_prop_name_values  */
#line 4025 "def.y"
    {
      defData->dumb_mode = 0;
    }
#line 8969 "def.tab.cpp"
    break;

  case 619: /* $@109: %empty  */
#line 4029 "def.y"
                  { defData->dumb_mode = 1; }
#line 8975 "def.tab.cpp"
    break;

  case 620: /* $@110: %empty  */
#line 4030 "def.y"
    {
      if (defData->VersionNum < 5.6) {
        if (defData->callbacks->SNetCbk) {
          if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
            defData->defMsg = (char*)malloc(1000);
            sprintf (defData->defMsg,
               "The POLYGON statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
            defData->defError(6535, defData->defMsg);
            free(defData->defMsg);
            CHKERR();
          }
        }
      }

      defData->Geometries.Reset();
    }
#line 8996 "def.tab.cpp"
    break;

  case 621: /* snet_other_option: '+' K_POLYGON $@109 T_STRING $@110 firstPt nextPt nextPt otherPts  */
#line 4048 "def.y"
    {
      defrProps* polyProps = NULL;

      if (defData->VersionNum >= 6.0 - 0.00001) {
          defData->setPropsDataTypes("SPECIALROUTE",
                                     defData->session->SpecialRouteProp);
          polyProps = defData->props;
          defData->props = NULL;
          defData->cleanProps();
      }

      if (defData->VersionNum >= 5.6) {  // only add if 5.6 or beyond
        if (defData->callbacks->SNetCbk) {
          defiNetPoly *poly = new defiNetPoly((yyvsp[-5].string),
                                              &defData->Geometries,
                                              defData->specialWire_mask,
                                              defData->routeStatus,
                                              defData->shieldName,
                                              defData->shapeType,
                                              polyProps);

          defData->cleanProps();
          defData->specialWire_mask = 0;

          // defData->needSNPCbk will indicate that it has reach the max
          // memory and if user has set partialPathCBk, def parser
          // will make the callback.
          // This will improve performance
          // This construct is only in specialnet
          defData->Net->addPolygon(poly, &defData->needSNPCbk);

          if (defData->needSNPCbk && defData->callbacks->SNetPartialPathCbk) {
             CALLBACK(defData->callbacks->SNetPartialPathCbk, defrSNetPartialPathCbkType,
                      defData->Net);
             defData->Net->clearRectPolyNPath();
             defData->Net->clearVia();
          }
        }
      }

      defData->routeStatus = (char*)"ROUTED";
      defData->shapeType = NULL;
    }
#line 9044 "def.tab.cpp"
    break;

  case 622: /* $@111: %empty  */
#line 4092 "def.y"
               { defData->dumb_mode = 1; }
#line 9050 "def.tab.cpp"
    break;

  case 623: /* snet_other_option: '+' K_RECT $@111 T_STRING pt pt  */
#line 4093 "def.y"
    {
        defrProps* rectProps = NULL;

        if (defData->VersionNum >= 6.0 - 0.00001) {
            defData->setPropsDataTypes("SPECIALROUTE",
                                       defData->session->SpecialRouteProp);
            rectProps = defData->props;
            defData->props = NULL;
            defData->cleanProps();
        }

        if (defData->VersionNum < 5.6) {
            if (defData->callbacks->SNetCbk) {
                if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
                    defData->defMsg = (char*)malloc(1000);
                    sprintf (defData->defMsg,
                             "The RECT statement is available in version 5.6 "
                             "and later.\nHowever, your DEF file is defined "
                             "with version %.2f",
                             defData->VersionNum);
                    defData->defError(6536, defData->defMsg);
                    free(defData->defMsg);
                    CHKERR();
                }
            }
        }

        if (defData->callbacks->SNetCbk) {
            defiNetRect *rect = new defiNetRect((yyvsp[-2].string),
                                                (yyvsp[-1].pt).x,
                                                (yyvsp[-1].pt).y,
                                                (yyvsp[0].pt).x,
                                                (yyvsp[0].pt).y,
                                                defData->specialWire_mask,
                                                defData->routeStatus,
                                                defData->shieldName,
                                                defData->shapeType,
                                                rectProps);

            defData->specialWire_mask = 0;

            if (defData->callbacks->RectInSNetCbk) {
                CALLBACK2(defData->callbacks->RectInSNetCbk,
                          defrRectInNetCbkType, rect, defData->Net);

                delete rect;
            } else {
                // defData->needSNPCbk will indicate that it has reach the max
                // memory and if user has set partialPathCBk, def parser
                // will make the callback.
                // This will improve performance
                // This construct is only in specialnet
                defData->Net->addRect(rect, &defData->needSNPCbk);

                if (defData->needSNPCbk
                    && defData->callbacks->SNetPartialPathCbk) {
                    CALLBACK(defData->callbacks->SNetPartialPathCbk,
                             defrSNetPartialPathCbkType,
                             defData->Net);
                    defData->Net->clearRectPolyNPath();
                    defData->Net->clearVia();
                }
            }
        }

        defData->routeStatus = (char*)"ROUTED";
        defData->shapeType = NULL;
    }
#line 9123 "def.tab.cpp"
    break;

  case 624: /* $@112: %empty  */
#line 4161 "def.y"
              { defData->dumb_mode = 1; }
#line 9129 "def.tab.cpp"
    break;

  case 625: /* $@113: %empty  */
#line 4162 "def.y"
    {
      if (defData->VersionNum < 5.8) {
          if (defData->callbacks->SNetCbk) {
            if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
              defData->defMsg = (char*)malloc(1000);
              sprintf (defData->defMsg,
                 "The VIA statement is available in version 5.8 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
              defData->defError(6536, defData->defMsg);
              free(defData->defMsg);
              CHKERR();
            }
          }
      }
    }
#line 9148 "def.tab.cpp"
    break;

  case 626: /* snet_other_option: '+' K_VIA $@112 T_STRING orient_pt $@113 firstPt otherPts  */
#line 4177 "def.y"
    {
      defrProps* viaProps = NULL;

      if (defData->VersionNum >= 6.0 - 0.00001) {
        defData->setPropsDataTypes("SPECIALROUTE", defData->session->SpecialRouteProp);
        viaProps = defData->props;
        defData->props = NULL;
        defData->cleanProps();
      }

      if (defData->VersionNum >= 5.8 && defData->callbacks->SNetCbk) {
          defiNetVia *via = new defiNetVia((yyvsp[-4].string),
                                           (yyvsp[-3].integer), 
                                           &defData->Geometries,
                                           defData->specialWire_mask,
                                           defData->routeStatus,
                                           defData->shieldName,
                                           defData->shapeType,
                                           viaProps);

          defData->specialWire_mask = 0;

          defData->Net->addVia(via, &defData->needSNPCbk);

          if (defData->needSNPCbk && defData->callbacks->SNetPartialPathCbk) {
             CALLBACK(defData->callbacks->SNetPartialPathCbk, defrSNetPartialPathCbkType,
                      defData->Net);
             defData->Net->clearRectPolyNPath();
             defData->Net->clearVia();
          }
        }

        defData->routeStatus = (char*)"ROUTED";
        defData->shapeType = NULL;
    }
#line 9188 "def.tab.cpp"
    break;

  case 627: /* snet_other_option: '+' K_SOURCE source_type  */
#line 4214 "def.y"
    { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + SOURCE DIST|NETLIST|TEST|TIMING|USER")) {
                CHKERR();
            }
        } else if (defData->callbacks->SNetCbk) {
            defData->Net->setSource((yyvsp[0].string)); 
        }
    }
#line 9202 "def.tab.cpp"
    break;

  case 628: /* snet_other_option: '+' K_FIXEDBUMP  */
#line 4225 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setFixedbump(); }
#line 9208 "def.tab.cpp"
    break;

  case 629: /* snet_other_option: '+' K_FREQUENCY NUMBER  */
#line 4228 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setFrequency((yyvsp[0].dval)); }
#line 9214 "def.tab.cpp"
    break;

  case 630: /* $@114: %empty  */
#line 4230 "def.y"
                   {defData->dumb_mode = 1; defData->no_num = 1;}
#line 9220 "def.tab.cpp"
    break;

  case 631: /* snet_other_option: '+' K_ORIGINAL $@114 T_STRING  */
#line 4231 "def.y"
    { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + ORIGINAL netName")) {
                CHKERR();
            }
        } else if (defData->callbacks->SNetCbk) {
            defData->Net->setOriginal((yyvsp[0].string)); 
        }
    }
#line 9234 "def.tab.cpp"
    break;

  case 632: /* snet_other_option: '+' K_PATTERN snets_pattern_type  */
#line 4242 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setPattern((yyvsp[0].string)); }
#line 9240 "def.tab.cpp"
    break;

  case 633: /* snet_other_option: '+' K_WEIGHT NUMBER  */
#line 4245 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setWeight(ROUND((yyvsp[0].dval))); }
#line 9246 "def.tab.cpp"
    break;

  case 634: /* snet_other_option: '+' K_ESTCAP NUMBER  */
#line 4248 "def.y"
    { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5) {
            if (defData->callbacks->SNetCbk) {
                defData->Net->setCap((yyvsp[0].dval));
            }
        }
    }
#line 9259 "def.tab.cpp"
    break;

  case 635: /* snet_other_option: '+' K_USE use_type  */
#line 4258 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setUse((yyvsp[0].string)); }
#line 9265 "def.tab.cpp"
    break;

  case 636: /* snet_other_option: '+' K_STYLE NUMBER  */
#line 4261 "def.y"
    { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS ... [STYLE styleNum]")) {
                CHKERR();
            }
        }

        if (defData->callbacks->SNetCbk) {
            defData->Net->setStyle((int)(yyvsp[0].dval)); 
        }
    }
#line 9281 "def.tab.cpp"
    break;

  case 637: /* snet_other_option: extension_stmt  */
#line 4274 "def.y"
    { CALLBACK(defData->callbacks->NetExtCbk, defrNetExtCbkType, &defData->History_text[0]); }
#line 9287 "def.tab.cpp"
    break;

  case 638: /* snet_type: K_FIXED  */
#line 4277 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"FIXED"; 
            defData->dumb_mode = 1; 
        }
#line 9297 "def.tab.cpp"
    break;

  case 639: /* snet_type: K_COVER  */
#line 4283 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"COVER"; 
            defData->dumb_mode = 1; 
        }
#line 9307 "def.tab.cpp"
    break;

  case 640: /* snet_type: K_ROUTED  */
#line 4289 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"ROUTED"; 
            defData->dumb_mode = 1; 
        }
#line 9317 "def.tab.cpp"
    break;

  case 641: /* $@115: %empty  */
#line 4294 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9323 "def.tab.cpp"
    break;

  case 642: /* snet_type: K_SHIELD $@115 T_STRING  */
#line 4295 "def.y"
        {
            if (defData->VersionNum < 6.0 - 0.00001) {
                defData->routeStatus = (char*)"SHIELD";
            } 

            defData->dumb_mode = 1; 
            defData->no_num = 1; 

            defData->shieldName = (yyvsp[0].string);
        }
#line 9338 "def.tab.cpp"
    break;

  case 643: /* orient_pt: %empty  */
#line 4307 "def.y"
        { (yyval.integer) = 0; }
#line 9344 "def.tab.cpp"
    break;

  case 644: /* orient_pt: orient  */
#line 4309 "def.y"
        { (yyval.integer) = (yyvsp[0].integer); }
#line 9350 "def.tab.cpp"
    break;

  case 645: /* $@116: %empty  */
#line 4312 "def.y"
                        { defData->dumb_mode = 1; }
#line 9356 "def.tab.cpp"
    break;

  case 646: /* snet_width: '+' K_WIDTH $@116 T_STRING NUMBER  */
#line 4313 "def.y"
            {
              // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
              if (defData->VersionNum < 5.5)
                 if (defData->callbacks->SNetCbk) defData->Net->setWidth((yyvsp[-1].string), (yyvsp[0].dval));
              else
                 defData->defWarning(7026, "The WIDTH statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
            }
#line 9368 "def.tab.cpp"
    break;

  case 647: /* $@117: %empty  */
#line 4321 "def.y"
                             { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9374 "def.tab.cpp"
    break;

  case 648: /* snet_voltage: '+' K_VOLTAGE $@117 T_STRING  */
#line 4322 "def.y"
            {
              if (defrData::numIsInt((yyvsp[0].string))) {
                 if (defData->callbacks->SNetCbk) defData->Net->setVoltage(atoi((yyvsp[0].string)));
              } else {
                 if (defData->callbacks->SNetCbk) {
                   if (defData->sNetWarnings++ < defData->settings->SNetWarnings) {
                     defData->defMsg = (char*)malloc(1000);
                     sprintf (defData->defMsg,
                        "The value %s for statement VOLTAGE is invalid. The value can only be integer.\nSpecify a valid value in units of millivolts", (yyvsp[0].string));
                     defData->defError(6537, defData->defMsg);
                     free(defData->defMsg);
                     CHKERR();
                   }
                 }
              }
            }
#line 9395 "def.tab.cpp"
    break;

  case 649: /* $@118: %empty  */
#line 4339 "def.y"
                            { defData->dumb_mode = 1; }
#line 9401 "def.tab.cpp"
    break;

  case 650: /* $@119: %empty  */
#line 4340 "def.y"
            {
              if (defData->callbacks->SNetCbk) defData->Net->setSpacing((yyvsp[-1].string),(yyvsp[0].dval));
            }
#line 9409 "def.tab.cpp"
    break;

  case 651: /* snet_spacing: '+' K_SPACING $@118 T_STRING NUMBER $@119 opt_snet_range  */
#line 4344 "def.y"
            {
            }
#line 9416 "def.tab.cpp"
    break;

  case 653: /* opt_snet_range: K_RANGE NUMBER NUMBER  */
#line 4349 "def.y"
            {
              if (defData->callbacks->SNetCbk) defData->Net->setRange((yyvsp[-1].dval),(yyvsp[0].dval));
            }
#line 9424 "def.tab.cpp"
    break;

  case 655: /* opt_range: K_RANGE NUMBER NUMBER  */
#line 4355 "def.y"
            { defData->Prop.setRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 9430 "def.tab.cpp"
    break;

  case 656: /* nets_pattern_type: K_BALANCED  */
#line 4359 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("NETS ... netName ... + PATTERN BALANCED")) {
                        CHKERR();
                    }
                }

                (yyval.string) = (char*)"BALANCED"; 
              }
#line 9444 "def.tab.cpp"
    break;

  case 657: /* nets_pattern_type: K_STEINER  */
#line 4369 "def.y"
            { (yyval.string) = (char*)"STEINER"; }
#line 9450 "def.tab.cpp"
    break;

  case 658: /* nets_pattern_type: K_TRUNK  */
#line 4371 "def.y"
            { (yyval.string) = (char*)"TRUNK"; }
#line 9456 "def.tab.cpp"
    break;

  case 659: /* nets_pattern_type: K_WIREDLOGIC  */
#line 4373 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("NETS ... netName ... + PATTERN WIREDLOGIC")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"WIREDLOGIC"; 
             }
#line 9470 "def.tab.cpp"
    break;

  case 660: /* snets_pattern_type: K_BALANCED  */
#line 4384 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + PATTERN BALANCED")) {
                        CHKERR();
                    }
                }

                (yyval.string) = (char*)"BALANCED"; 
            }
#line 9484 "def.tab.cpp"
    break;

  case 661: /* snets_pattern_type: K_STEINER  */
#line 4394 "def.y"
            { (yyval.string) = (char*)"STEINER"; }
#line 9490 "def.tab.cpp"
    break;

  case 662: /* snets_pattern_type: K_TRUNK  */
#line 4396 "def.y"
            { (yyval.string) = (char*)"TRUNK"; }
#line 9496 "def.tab.cpp"
    break;

  case 663: /* snets_pattern_type: K_WIREDLOGIC  */
#line 4398 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + PATTERN WIREDLOGIC")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"WIREDLOGIC"; 
             }
#line 9510 "def.tab.cpp"
    break;

  case 665: /* $@120: %empty  */
#line 4409 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            if ((defData->VersionNum < 5.4)
                && !(strcmp(defData->routeStatus, "SHIELD"))) { // PCR 445209
                defData->Shield = new defiShield(defData);
                defData->Shield->Init(defData->shieldName);
                defData->Net->addShield(defData->Shield, false);
            } else {
                if (defData->VersionNum >= 6.0 - 0.00001
                    && defData->shieldName) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... + SHIELD specialWiring")) {
                        CHKERR();
                    }
                }

                defData->Wire = new defiWire(defData);
                defData->Wire->Init(defData->routeStatus,
                                    defData->shieldName);

                if (!defData->callbacks->WireInSNetCbk) {
                    defData->Net->addWire(defData->Wire);
                }
            }
        }
    }
#line 9540 "def.tab.cpp"
    break;

  case 666: /* opt_swire: $@120 spaths  */
#line 4435 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            if (defData->callbacks->WireInSNetCbk
                && defData->Wire) {
                CALLBACK2(defData->callbacks->WireInSNetCbk,
                          defrWireInNetCbkType,
                          defData->Wire, defData->Net);

                delete defData->Wire;
            } else if (defData->callbacks->SNetWireCbk) {
                CALLBACK(defData->callbacks->SNetWireCbk,
                         defrSNetWireCbkType, defData->Net);
                if (defData->Shield)
                    defData->Net->freeShield();
                else
                    defData->Net->freeWire();
            } else if (defData->callbacks->SNetPartialPathCbk
                       && !defData->callbacks->WireInSNetCbk) {
                // Wire ended: call SNetPartialPathCbk regardless of threshold
                // This ensures all paths in this wire are processed
                CALLBACK(defData->callbacks->SNetPartialPathCbk,
                         defrSNetPartialPathCbkType,
                         defData->Net);
                // Clear the last wire to avoid duplicate data
                defData->Net->clearLastWire();
                defData->Net->clearRectPolyNPath();
                defData->Net->clearVia();
            }
        }

        defData->by_is_keyword = FALSE;
        defData->do_is_keyword = FALSE;
        defData->new_is_keyword = FALSE;
        defData->step_is_keyword = FALSE;
        defData->orient_is_keyword = FALSE;
        defData->virtual_is_keyword = FALSE;
        defData->mask_is_keyword = FALSE;
        defData->rect_is_keyword = FALSE;
        defData->width_is_keyword = FALSE;
        defData->needSNPCbk = 0; 
        defData->shieldName = NULL;
        defData->shapeType = NULL;
        defData->routeStatus = (char*)"ROUTED";
        defData->Shield = NULL;
        defData->Wire = NULL;
     }
#line 9591 "def.tab.cpp"
    break;

  case 667: /* $@121: %empty  */
#line 4483 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 9602 "def.tab.cpp"
    break;

  case 668: /* spaths: $@121 spath  */
#line 4490 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            if (defData->callbacks->PathInSNetWireCbk
                && defData->Wire) {
                CALLBACK3(defData->callbacks->PathInSNetWireCbk,
                          defrPathInWireCbkType,
                          defData->PathObj,
                          defData->Wire,
                          defData->Net);

                delete defData->PathObj;
            } else if (defData->needSNPCbk
                       && defData->callbacks->SNetPartialPathCbk) {
                // require a callback before proceed because defData->needSNPCbk
                // must be set to 1 from the previous finishPath and user has
                // registered a callback routine.
                CALLBACK(defData->callbacks->SNetPartialPathCbk,
                         defrSNetPartialPathCbkType,
                         defData->Net);
                defData->needSNPCbk = 0;   // reset the flag
                defData->finishPath(1, &defData->needSNPCbk);
                defData->Net->clearRectPolyNPath();
                defData->Net->clearVia();
            } else {
                defData->finishPath(0, &defData->needSNPCbk);
            }

            defData->PathObj = NULL;
        }
    }
#line 9637 "def.tab.cpp"
    break;

  case 669: /* spaths: spaths snew_path  */
#line 4521 "def.y"
    { }
#line 9643 "def.tab.cpp"
    break;

  case 670: /* $@122: %empty  */
#line 4524 "def.y"
    {
        defData->dumb_mode = 1;

        if (defData->callbacks->SNetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 9656 "def.tab.cpp"
    break;

  case 671: /* snew_path: K_NEW $@122 spath  */
#line 4533 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            if (defData->callbacks->PathInSNetWireCbk
                && defData->Wire) {
                CALLBACK3(defData->callbacks->PathInSNetWireCbk,
                          defrPathInWireCbkType,
                          defData->PathObj,
                          defData->Wire,
                          defData->Net);

                delete defData->PathObj;
            } else if (defData->needSNPCbk
                       && defData->callbacks->SNetPartialPathCbk) {
                // require a callback before proceed because defData->needSNPCbk
                // must be set to 1 from the previous finishPath and user has
                // registered a callback routine.
                CALLBACK(defData->callbacks->SNetPartialPathCbk,
                         defrSNetPartialPathCbkType,
                         defData->Net);
                defData->needSNPCbk = 0;   // reset the flag
                defData->finishPath(1, &defData->needSNPCbk);
                // reset any poly or rect in special wiring statement
                defData->Net->clearRectPolyNPath();
                defData->Net->clearVia();
            } else {
                defData->finishPath(0, &defData->needSNPCbk);
            }
            
            defData->PathObj = NULL;
        }
    }
#line 9692 "def.tab.cpp"
    break;

  case 672: /* $@123: %empty  */
#line 4567 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj->addLayer((yyvsp[0].string));
        }
        
        defData->dumb_mode = 0;
        defData->no_num = 0;
    }
#line 9705 "def.tab.cpp"
    break;

  case 673: /* $@124: %empty  */
#line 4576 "def.y"
    {
        defData->dumb_mode = DEF_MAX_INT;
        defData->by_is_keyword = TRUE;
        defData->do_is_keyword = TRUE;
        defData->new_is_keyword = TRUE;
        defData->step_is_keyword = TRUE;
        defData->orient_is_keyword = TRUE;
        defData->rect_is_keyword = TRUE;
        defData->mask_is_keyword = TRUE; 
        defData->virtual_is_keyword = TRUE;
    }
#line 9721 "def.tab.cpp"
    break;

  case 674: /* spath: T_STRING $@123 width opt_spaths path_pt $@124 path_item_list_opt  */
#line 4588 "def.y"
    {
        defData->dumb_mode = 0;
        defData->rect_is_keyword = FALSE;
        defData->mask_is_keyword = FALSE;
        defData->virtual_is_keyword = FALSE;
    }
#line 9732 "def.tab.cpp"
    break;

  case 675: /* width: NUMBER  */
#line 4597 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj->addWidth(ROUND((yyvsp[0].dval)));
        }
    }
#line 9742 "def.tab.cpp"
    break;

  case 676: /* start_snets: K_SNETS NUMBER ';'  */
#line 4604 "def.y"
    { 
        defData->Net = new defiNet(defData);

        if (defData->callbacks->SNetStartCbk) {
            CALLBACK(defData->callbacks->SNetStartCbk,
                     defrSNetStartCbkType,
                     ROUND((yyvsp[-1].dval)));
        }

        defData->netOsnet = 2;
    }
#line 9758 "def.tab.cpp"
    break;

  case 677: /* end_snets: K_END K_SNETS  */
#line 4617 "def.y"
    { 
        if (defData->callbacks->SNetEndCbk) {
            CALLBACK(defData->callbacks->SNetEndCbk, defrSNetEndCbkType, 0);
        }

        defData->netOsnet = 0;

        delete defData->Net;
        defData->Net = NULL;
    }
#line 9773 "def.tab.cpp"
    break;

  case 679: /* groups_start: K_GROUPS NUMBER ';'  */
#line 4632 "def.y"
      {
        if (defData->callbacks->GroupsStartCbk)
           CALLBACK(defData->callbacks->GroupsStartCbk, defrGroupsStartCbkType, ROUND((yyvsp[-1].dval)));
      }
#line 9782 "def.tab.cpp"
    break;

  case 682: /* group_rule: start_group group_members group_options ';'  */
#line 4642 "def.y"
      {
        if (defData->callbacks->GroupCbk)
           CALLBACK(defData->callbacks->GroupCbk, defrGroupCbkType, &defData->Group);
      }
#line 9791 "def.tab.cpp"
    break;

  case 683: /* $@125: %empty  */
#line 4647 "def.y"
                 { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9797 "def.tab.cpp"
    break;

  case 684: /* start_group: '-' $@125 T_STRING  */
#line 4648 "def.y"
      {
        defData->dumb_mode = DEF_MAX_INT;
        defData->no_num = DEF_MAX_INT;
        /* dumb_mode is automatically turned off at the first
         * + in the options or at the ; at the end of the group */
        if (defData->callbacks->GroupCbk) defData->Group.setup((yyvsp[0].string));
        if (defData->callbacks->GroupNameCbk)
           CALLBACK(defData->callbacks->GroupNameCbk, defrGroupNameCbkType, (yyvsp[0].string));
      }
#line 9811 "def.tab.cpp"
    break;

  case 686: /* group_members: group_members group_member  */
#line 4660 "def.y"
      {  }
#line 9817 "def.tab.cpp"
    break;

  case 687: /* group_member: T_STRING  */
#line 4663 "def.y"
      {
        // if (defData->callbacks->GroupCbk) defData->Group.addMember($1); 
        if (defData->callbacks->GroupMemberCbk)
          CALLBACK(defData->callbacks->GroupMemberCbk, defrGroupMemberCbkType, (yyvsp[0].string));
      }
#line 9827 "def.tab.cpp"
    break;

  case 690: /* group_option: '+' K_SOFT group_soft_options  */
#line 4674 "def.y"
      { }
#line 9833 "def.tab.cpp"
    break;

  case 691: /* $@126: %empty  */
#line 4675 "def.y"
                           { defData->dumb_mode = DEF_MAX_INT; }
#line 9839 "def.tab.cpp"
    break;

  case 692: /* group_option: '+' K_PROPERTY $@126 group_prop_list  */
#line 4677 "def.y"
      { defData->dumb_mode = 0; }
#line 9845 "def.tab.cpp"
    break;

  case 693: /* $@127: %empty  */
#line 4678 "def.y"
                         { defData->dumb_mode = 1;  defData->no_num = 1; }
#line 9851 "def.tab.cpp"
    break;

  case 694: /* group_option: '+' K_REGION $@127 group_region  */
#line 4679 "def.y"
      { }
#line 9857 "def.tab.cpp"
    break;

  case 695: /* group_option: extension_stmt  */
#line 4681 "def.y"
      { 
        if (defData->callbacks->GroupMemberCbk)
          CALLBACK(defData->callbacks->GroupExtCbk, defrGroupExtCbkType, &defData->History_text[0]);
      }
#line 9866 "def.tab.cpp"
    break;

  case 696: /* group_option: '+' K_POWERDOMAIN  */
#line 4686 "def.y"
      {
         if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + POWERDOMAIN")) {
                CHKERR();
            }
        } else { 
            if (defData->callbacks->GroupCbk) {
                defData->Group.setPowerdomain();
            }
          }      
      }
#line 9882 "def.tab.cpp"
    break;

  case 697: /* $@128: %empty  */
#line 4699 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9891 "def.tab.cpp"
    break;

  case 698: /* group_option: '+' K_HINSTS $@128 group_hinsts  */
#line 4704 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;

        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + HINSTS hinst1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9906 "def.tab.cpp"
    break;

  case 699: /* $@129: %empty  */
#line 4716 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9915 "def.tab.cpp"
    break;

  case 700: /* group_option: '+' K_COMPS $@129 group_components  */
#line 4721 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;
        
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + COMPONENTS component1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9930 "def.tab.cpp"
    break;

  case 701: /* $@130: %empty  */
#line 4733 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9939 "def.tab.cpp"
    break;

  case 702: /* group_option: '+' K_GROUPS $@130 group_groups  */
#line 4738 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;

        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + GROUPS group1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9954 "def.tab.cpp"
    break;

  case 704: /* group_hinsts: group_hinst  */
#line 4751 "def.y"
      {}
#line 9960 "def.tab.cpp"
    break;

  case 705: /* group_hinst: T_STRING  */
#line 4754 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
                defData->Group.addHinst((yyvsp[0].string));
        }
      }
#line 9970 "def.tab.cpp"
    break;

  case 707: /* group_components: group_component  */
#line 4762 "def.y"
      {}
#line 9976 "def.tab.cpp"
    break;

  case 708: /* group_component: T_STRING  */
#line 4765 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
            defData->Group.addComponent((yyvsp[0].string));
        }
      }
#line 9986 "def.tab.cpp"
    break;

  case 710: /* group_groups: group_group  */
#line 4773 "def.y"
      {}
#line 9992 "def.tab.cpp"
    break;

  case 711: /* group_group: T_STRING  */
#line 4776 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
            defData->Group.addGroup((yyvsp[0].string));
        }
      }
#line 10002 "def.tab.cpp"
    break;

  case 712: /* group_region: pt pt  */
#line 4783 "def.y"
      {
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5) {
          if (defData->callbacks->GroupCbk)
            defData->Group.addRegionRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
        }
        else
          defData->defWarning(7027, "The GROUP REGION pt pt statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 10016 "def.tab.cpp"
    break;

  case 713: /* group_region: T_STRING  */
#line 4793 "def.y"
      { if (defData->callbacks->GroupCbk)
          defData->Group.setRegionName((yyvsp[0].string));
      }
#line 10024 "def.tab.cpp"
    break;

  case 716: /* group_prop: T_STRING NUMBER  */
#line 4802 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
          char propTp;
          char* str = defData->ringCopy("                       ");
          propTp = defData->session->GroupProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "GROUP");
          sprintf(str, "%g", (yyvsp[0].dval));
          defData->Group.addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
        }
      }
#line 10039 "def.tab.cpp"
    break;

  case 717: /* group_prop: T_STRING QSTRING  */
#line 4813 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
          char propTp;
          propTp = defData->session->GroupProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "GROUP");
          defData->Group.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 10052 "def.tab.cpp"
    break;

  case 718: /* group_prop: T_STRING T_STRING  */
#line 4822 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
          char propTp;
          propTp = defData->session->GroupProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "GROUP");
          defData->Group.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 10065 "def.tab.cpp"
    break;

  case 720: /* group_soft_options: group_soft_options group_soft_option  */
#line 4833 "def.y"
      { }
#line 10071 "def.tab.cpp"
    break;

  case 721: /* group_soft_option: K_MAXX NUMBER  */
#line 4836 "def.y"
      {
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setMaxX(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7028, "The GROUP SOFT MAXX statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 10083 "def.tab.cpp"
    break;

  case 722: /* group_soft_option: K_MAXY NUMBER  */
#line 4844 "def.y"
      { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setMaxY(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7029, "The GROUP SOFT MAXY statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 10095 "def.tab.cpp"
    break;

  case 723: /* group_soft_option: K_MAXHALFPERIMETER NUMBER  */
#line 4852 "def.y"
      { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setPerim(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7030, "The GROUP SOFT MAXHALFPERIMETER statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 10107 "def.tab.cpp"
    break;

  case 724: /* groups_end: K_END K_GROUPS  */
#line 4861 "def.y"
      { 
        if (defData->callbacks->GroupsEndCbk)
          CALLBACK(defData->callbacks->GroupsEndCbk, defrGroupsEndCbkType, 0);
      }
#line 10116 "def.tab.cpp"
    break;

  case 727: /* assertions_start: K_ASSERTIONS NUMBER ';'  */
#line 4875 "def.y"
      {
        if ((defData->VersionNum < 5.4) && (defData->callbacks->AssertionsStartCbk)) {
          CALLBACK(defData->callbacks->AssertionsStartCbk, defrAssertionsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
        } else {
          if (defData->callbacks->AssertionCbk)
            if (defData->assertionWarnings++ < defData->settings->AssertionWarnings)
              defData->defWarning(7031, "The ASSERTIONS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
        }
        if (defData->callbacks->AssertionCbk)
          defData->Assertion.setAssertionMode();
      }
#line 10133 "def.tab.cpp"
    break;

  case 728: /* constraints_start: K_CONSTRAINTS NUMBER ';'  */
#line 4889 "def.y"
      {
        if ((defData->VersionNum < 5.4) && (defData->callbacks->ConstraintsStartCbk)) {
          CALLBACK(defData->callbacks->ConstraintsStartCbk, defrConstraintsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
        } else {
          if (defData->callbacks->ConstraintCbk)
            if (defData->constraintWarnings++ < defData->settings->ConstraintWarnings)
              defData->defWarning(7032, "The CONSTRAINTS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
        }
        if (defData->callbacks->ConstraintCbk)
          defData->Assertion.setConstraintMode();
      }
#line 10150 "def.tab.cpp"
    break;

  case 732: /* constraint_rule: wiredlogic_rule  */
#line 4908 "def.y"
      {
        if ((defData->VersionNum < 5.4) && (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)) {
          if (defData->Assertion.isConstraint()) 
            CALLBACK(defData->callbacks->ConstraintCbk, defrConstraintCbkType, &defData->Assertion);
          if (defData->Assertion.isAssertion()) 
            CALLBACK(defData->callbacks->AssertionCbk, defrAssertionCbkType, &defData->Assertion);
        }
      }
#line 10163 "def.tab.cpp"
    break;

  case 733: /* operand_rule: '-' operand delay_specs ';'  */
#line 4918 "def.y"
      { 
        if ((defData->VersionNum < 5.4) && (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)) {
          if (defData->Assertion.isConstraint()) 
            CALLBACK(defData->callbacks->ConstraintCbk, defrConstraintCbkType, &defData->Assertion);
          if (defData->Assertion.isAssertion()) 
            CALLBACK(defData->callbacks->AssertionCbk, defrAssertionCbkType, &defData->Assertion);
        }
   
        // reset all the flags and everything
        defData->Assertion.clear();
      }
#line 10179 "def.tab.cpp"
    break;

  case 734: /* $@131: %empty  */
#line 4930 "def.y"
               { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10185 "def.tab.cpp"
    break;

  case 735: /* operand: K_NET $@131 T_STRING  */
#line 4931 "def.y"
      {
         if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.addNet((yyvsp[0].string));
      }
#line 10194 "def.tab.cpp"
    break;

  case 736: /* $@132: %empty  */
#line 4935 "def.y"
               {defData->dumb_mode = 4; defData->no_num = 4;}
#line 10200 "def.tab.cpp"
    break;

  case 737: /* operand: K_PATH $@132 T_STRING T_STRING T_STRING T_STRING  */
#line 4936 "def.y"
      {
         if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.addPath((yyvsp[-3].string), (yyvsp[-2].string), (yyvsp[-1].string), (yyvsp[0].string));
      }
#line 10209 "def.tab.cpp"
    break;

  case 738: /* operand: K_SUM '(' operand_list ')'  */
#line 4941 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.setSum();
      }
#line 10218 "def.tab.cpp"
    break;

  case 739: /* operand: K_DIFF '(' operand_list ')'  */
#line 4946 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.setDiff();
      }
#line 10227 "def.tab.cpp"
    break;

  case 741: /* operand_list: operand_list operand  */
#line 4953 "def.y"
      { }
#line 10233 "def.tab.cpp"
    break;

  case 743: /* $@133: %empty  */
#line 4956 "def.y"
                                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10239 "def.tab.cpp"
    break;

  case 744: /* wiredlogic_rule: '-' K_WIREDLOGIC $@133 T_STRING opt_plus K_MAXDIST NUMBER ';'  */
#line 4958 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setWiredlogic((yyvsp[-4].string), (yyvsp[-1].dval));
      }
#line 10248 "def.tab.cpp"
    break;

  case 745: /* opt_plus: %empty  */
#line 4965 "def.y"
      { (yyval.string) = (char*)""; }
#line 10254 "def.tab.cpp"
    break;

  case 746: /* opt_plus: '+'  */
#line 4967 "def.y"
      { (yyval.string) = (char*)"+"; }
#line 10260 "def.tab.cpp"
    break;

  case 749: /* delay_spec: '+' K_RISEMIN NUMBER  */
#line 4974 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setRiseMin((yyvsp[0].dval));
      }
#line 10269 "def.tab.cpp"
    break;

  case 750: /* delay_spec: '+' K_RISEMAX NUMBER  */
#line 4979 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setRiseMax((yyvsp[0].dval));
      }
#line 10278 "def.tab.cpp"
    break;

  case 751: /* delay_spec: '+' K_FALLMIN NUMBER  */
#line 4984 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setFallMin((yyvsp[0].dval));
      }
#line 10287 "def.tab.cpp"
    break;

  case 752: /* delay_spec: '+' K_FALLMAX NUMBER  */
#line 4989 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setFallMax((yyvsp[0].dval));
      }
#line 10296 "def.tab.cpp"
    break;

  case 753: /* constraints_end: K_END K_CONSTRAINTS  */
#line 4995 "def.y"
      { if ((defData->VersionNum < 5.4) && defData->callbacks->ConstraintsEndCbk) {
          CALLBACK(defData->callbacks->ConstraintsEndCbk, defrConstraintsEndCbkType, 0);
        } else {
          if (defData->callbacks->ConstraintsEndCbk) {
            if (defData->constraintWarnings++ < defData->settings->ConstraintWarnings)
              defData->defWarning(7032, "The CONSTRAINTS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
      }
#line 10310 "def.tab.cpp"
    break;

  case 754: /* assertions_end: K_END K_ASSERTIONS  */
#line 5006 "def.y"
      { if ((defData->VersionNum < 5.4) && defData->callbacks->AssertionsEndCbk) {
          CALLBACK(defData->callbacks->AssertionsEndCbk, defrAssertionsEndCbkType, 0);
        } else {
          if (defData->callbacks->AssertionsEndCbk) {
            if (defData->assertionWarnings++ < defData->settings->AssertionWarnings)
              defData->defWarning(7031, "The ASSERTIONS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
      }
#line 10324 "def.tab.cpp"
    break;

  case 756: /* scanchain_start: K_SCANCHAINS NUMBER ';'  */
#line 5020 "def.y"
      { if (defData->callbacks->ScanchainsStartCbk)
          CALLBACK(defData->callbacks->ScanchainsStartCbk, defrScanchainsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 10333 "def.tab.cpp"
    break;

  case 758: /* scanchain_rules: scanchain_rules scan_rule  */
#line 5027 "def.y"
      {}
#line 10339 "def.tab.cpp"
    break;

  case 759: /* scan_rule: start_scan scan_members ';'  */
#line 5030 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk)
          CALLBACK(defData->callbacks->ScanchainCbk, defrScanchainCbkType, &defData->Scanchain);
      }
#line 10348 "def.tab.cpp"
    break;

  case 760: /* $@134: %empty  */
#line 5035 "def.y"
                {defData->dumb_mode = 1; defData->no_num = 1;}
#line 10354 "def.tab.cpp"
    break;

  case 761: /* start_scan: '-' $@134 T_STRING  */
#line 5036 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.closeOrderedList();
          defData->Scanchain.setName((yyvsp[0].string));
        }
        defData->bit_is_keyword = TRUE;
      }
#line 10366 "def.tab.cpp"
    break;

  case 764: /* opt_pin: %empty  */
#line 5050 "def.y"
      { (yyval.string) = (char*)""; }
#line 10372 "def.tab.cpp"
    break;

  case 765: /* opt_pin: T_STRING  */
#line 5052 "def.y"
      { (yyval.string) = (yyvsp[0].string); }
#line 10378 "def.tab.cpp"
    break;

  case 766: /* $@135: %empty  */
#line 5054 "def.y"
                         {defData->dumb_mode = 2; defData->no_num = 2;}
#line 10384 "def.tab.cpp"
    break;

  case 767: /* scan_member: '+' K_START $@135 T_STRING opt_pin  */
#line 5055 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.closeOrderedList();
          defData->Scanchain.setStart((yyvsp[-1].string), (yyvsp[0].string));
        }
      }
#line 10395 "def.tab.cpp"
    break;

  case 768: /* $@136: %empty  */
#line 5062 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;      
      }
#line 10404 "def.tab.cpp"
    break;

  case 769: /* scan_member: '+' K_FLOATING $@136 floating_inst_list  */
#line 5067 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
           defData->Scanchain.closeOrderedList();     
        }

        defData->dumb_mode = 0; 
        defData->no_num = 0; 
      }
#line 10417 "def.tab.cpp"
    break;

  case 770: /* $@137: %empty  */
#line 5076 "def.y"
      {
         if (defData->callbacks->ScanchainCbk) {
           defData->Scanchain.startOrderedList();
         }

         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 10430 "def.tab.cpp"
    break;

  case 771: /* scan_member: '+' K_ORDERED $@137 ordered_inst_list_opt  */
#line 5085 "def.y"
      {         
         defData->dumb_mode = 0; 
         defData->no_num = 0; 
      }
#line 10439 "def.tab.cpp"
    break;

  case 772: /* $@138: %empty  */
#line 5089 "def.y"
                   {defData->dumb_mode = 2; defData->no_num = 2; }
#line 10445 "def.tab.cpp"
    break;

  case 773: /* scan_member: '+' K_STOP $@138 T_STRING opt_pin  */
#line 5090 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.setStop((yyvsp[-1].string), (yyvsp[0].string));
          defData->Scanchain.closeOrderedList();
        }
      }
#line 10456 "def.tab.cpp"
    break;

  case 774: /* $@139: %empty  */
#line 5096 "def.y"
                             { defData->dumb_mode = 10; defData->no_num = 10; }
#line 10462 "def.tab.cpp"
    break;

  case 775: /* scan_member: '+' K_COMMONSCANPINS $@139 opt_common_pins  */
#line 5097 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
            defData->Scanchain.closeOrderedList();
        }
        
        defData->dumb_mode = 0;  
        defData->no_num = 0; 
      }
#line 10475 "def.tab.cpp"
    break;

  case 776: /* $@140: %empty  */
#line 5105 "def.y"
                        { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10481 "def.tab.cpp"
    break;

  case 777: /* scan_member: '+' K_PARTITION $@140 T_STRING partition_maxbits  */
#line 5107 "def.y"
      {
        if (defData->VersionNum < 5.5) {
          if (defData->callbacks->ScanchainCbk) {
            if (defData->scanchainWarnings++ < defData->settings->ScanchainWarnings) {
              defData->defMsg = (char*)malloc(1000);
              sprintf (defData->defMsg,
                 "The PARTITION statement is available in version 5.5 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
              defData->defError(6538, defData->defMsg);
              free(defData->defMsg);
              CHKERR();
            }
          }
        }
        
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.setPartition((yyvsp[-1].string), (yyvsp[0].integer));
        }
      }
#line 10504 "def.tab.cpp"
    break;

  case 778: /* scan_member: extension_stmt  */
#line 5126 "def.y"
      {
        if (defData->callbacks->ScanChainExtCbk) {
          CALLBACK(defData->callbacks->ScanChainExtCbk, defrScanChainExtCbkType, &defData->History_text[0]);
        }
      }
#line 10514 "def.tab.cpp"
    break;

  case 779: /* $@141: %empty  */
#line 5132 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 10522 "def.tab.cpp"
    break;

  case 780: /* $@142: %empty  */
#line 5136 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("SCANCHAINS ... + PROPERTY propName propValue")) {
                CHKERR();
            }
        } else if (defData->callbacks->ScanchainCbk) {
            defData->setPropDataType((yyvsp[0].prop), "SCANCHAIN", defData->session->ScanChainProp);

            if (defData->Scanchain.hasOpenedOrderedList()) {
                defData->Scanchain.addOrderedProp((yyvsp[0].prop));
            } else {
                defData->Scanchain.addProp((yyvsp[0].prop));
            }
            
            (yyvsp[0].prop) = NULL;
        }

        delete (yyvsp[0].prop);
        defData->dumb_mode = DEF_MAX_INT; 
        defData->no_num = DEF_MAX_INT;
      }
#line 10548 "def.tab.cpp"
    break;

  case 781: /* scan_member: '+' K_PROPERTY $@141 prop_name_value $@142 ordered_inst_list_opt  */
#line 5158 "def.y"
      {
         defData->dumb_mode = 0; 
         defData->no_num = 0;       
      }
#line 10557 "def.tab.cpp"
    break;

  case 782: /* $@143: %empty  */
#line 5162 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10563 "def.tab.cpp"
    break;

  case 783: /* $@144: %empty  */
#line 5163 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("SCANCHAINS ... + ORDERED ... + NAME orderName")) {
                CHKERR();
            }
        } else if (defData->callbacks->ScanchainCbk) {
            if(!defData->Scanchain.hasOpenedOrderedList()) {
                defData->def60SyntaxError("SCANCHAINS ... + NAME orderName statement requires preceding + ORDERED statement");
            } else {
                defData->Scanchain.setOrderedName((yyvsp[0].string));
            }
        }

        defData->dumb_mode = DEF_MAX_INT; 
        defData->no_num = DEF_MAX_INT;
      }
#line 10584 "def.tab.cpp"
    break;

  case 784: /* scan_member: '+' K_NAME $@143 T_STRING $@144 ordered_inst_list_opt  */
#line 5180 "def.y"
      {
         defData->dumb_mode = 0; 
         defData->no_num = 0;       
      }
#line 10593 "def.tab.cpp"
    break;

  case 785: /* opt_common_pins: %empty  */
#line 5186 "def.y"
      { }
#line 10599 "def.tab.cpp"
    break;

  case 786: /* opt_common_pins: '(' T_STRING T_STRING ')'  */
#line 5188 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.setCommonIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.setCommonOut((yyvsp[-1].string));
        }
      }
#line 10612 "def.tab.cpp"
    break;

  case 787: /* opt_common_pins: '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')'  */
#line 5197 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-6].string), "IN") == 0 || strcmp((yyvsp[-6].string), "in") == 0)
            defData->Scanchain.setCommonIn((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "OUT") == 0 || strcmp((yyvsp[-6].string), "out") == 0)
            defData->Scanchain.setCommonOut((yyvsp[-5].string));
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.setCommonIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.setCommonOut((yyvsp[-1].string));
        }
      }
#line 10629 "def.tab.cpp"
    break;

  case 790: /* $@145: %empty  */
#line 5215 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.addFloatingInst((yyvsp[0].string));
        }
      }
#line 10639 "def.tab.cpp"
    break;

  case 791: /* one_floating_inst: T_STRING $@145 floating_pins  */
#line 5221 "def.y"
      {}
#line 10645 "def.tab.cpp"
    break;

  case 792: /* floating_pins: %empty  */
#line 5224 "def.y"
      {}
#line 10651 "def.tab.cpp"
    break;

  case 793: /* floating_pins: '(' T_STRING T_STRING ')'  */
#line 5226 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
        }
      }
#line 10668 "def.tab.cpp"
    break;

  case 794: /* floating_pins: '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')'  */
#line 5239 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-6].string), "IN") == 0 || strcmp((yyvsp[-6].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "OUT") == 0 || strcmp((yyvsp[-6].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "BITS") == 0 || strcmp((yyvsp[-6].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-5].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
        }
      }
#line 10693 "def.tab.cpp"
    break;

  case 795: /* floating_pins: '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')'  */
#line 5261 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-10].string), "IN") == 0 || strcmp((yyvsp[-10].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-9].string));
          else if (strcmp((yyvsp[-10].string), "OUT") == 0 || strcmp((yyvsp[-10].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-9].string));
          else if (strcmp((yyvsp[-10].string), "BITS") == 0 || strcmp((yyvsp[-10].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-9].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-6].string), "IN") == 0 || strcmp((yyvsp[-6].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "OUT") == 0 || strcmp((yyvsp[-6].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "BITS") == 0 || strcmp((yyvsp[-6].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-5].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addFloatingIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addFloatingOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setFloatingBits(defData->bitsNum);
          }
        }
      }
#line 10726 "def.tab.cpp"
    break;

  case 797: /* ordered_inst_list_opt: ordered_inst_list  */
#line 5292 "def.y"
      { 
      }
#line 10733 "def.tab.cpp"
    break;

  case 799: /* ordered_inst_list: one_ordered_inst  */
#line 5297 "def.y"
      {}
#line 10739 "def.tab.cpp"
    break;

  case 800: /* $@146: %empty  */
#line 5300 "def.y"
      { 
      }
#line 10746 "def.tab.cpp"
    break;

  case 801: /* $@147: %empty  */
#line 5303 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
            if (!defData->Scanchain.hasOpenedOrderedList()) {
                defData->def60KeywordRequiresKeywordError("SCANCHAINS", 
                                                          "{fixedComp [ ( IN pin ) ] [ ( OUT pin ) ] [ ( BITS numBits ) ]",
                                                          "+ ORDERED");
            } else {
                defData->Scanchain.addOrderedInst((yyvsp[0].string));
            }
        }
      }
#line 10762 "def.tab.cpp"
    break;

  case 802: /* one_ordered_inst: $@146 T_STRING $@147 ordered_pins  */
#line 5315 "def.y"
      { 
      }
#line 10769 "def.tab.cpp"
    break;

  case 804: /* ordered_pins: '(' T_STRING T_STRING ')'  */
#line 5320 "def.y"
      {
        if (defData->callbacks->ScanchainCbk && defData->Scanchain.hasOpenedOrderedList()) {
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
         }
        }
      }
#line 10786 "def.tab.cpp"
    break;

  case 805: /* ordered_pins: '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')'  */
#line 5333 "def.y"
      {
        if (defData->callbacks->ScanchainCbk && defData->Scanchain.hasOpenedOrderedList()) {
          if (strcmp((yyvsp[-6].string), "IN") == 0 || strcmp((yyvsp[-6].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "OUT") == 0 || strcmp((yyvsp[-6].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "BITS") == 0 || strcmp((yyvsp[-6].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-5].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
          }
        }
      }
#line 10811 "def.tab.cpp"
    break;

  case 806: /* ordered_pins: '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')' '(' T_STRING T_STRING ')'  */
#line 5355 "def.y"
      {
        if (defData->callbacks->ScanchainCbk && defData->Scanchain.hasOpenedOrderedList()) {
          if (strcmp((yyvsp[-10].string), "IN") == 0 || strcmp((yyvsp[-10].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-9].string));
          else if (strcmp((yyvsp[-10].string), "OUT") == 0 || strcmp((yyvsp[-10].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-9].string));
          else if (strcmp((yyvsp[-10].string), "BITS") == 0 || strcmp((yyvsp[-10].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-9].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-6].string), "IN") == 0 || strcmp((yyvsp[-6].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "OUT") == 0 || strcmp((yyvsp[-6].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-5].string));
          else if (strcmp((yyvsp[-6].string), "BITS") == 0 || strcmp((yyvsp[-6].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-5].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
          }
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.addOrderedIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.addOrderedOut((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "BITS") == 0 || strcmp((yyvsp[-2].string), "bits") == 0) {
            defData->bitsNum = atoi((yyvsp[-1].string));
            defData->Scanchain.setOrderedBits(defData->bitsNum);
          }
        }
      }
#line 10844 "def.tab.cpp"
    break;

  case 807: /* partition_maxbits: %empty  */
#line 5385 "def.y"
      { (yyval.integer) = -1; }
#line 10850 "def.tab.cpp"
    break;

  case 808: /* partition_maxbits: K_MAXBITS NUMBER  */
#line 5387 "def.y"
      { (yyval.integer) = ROUND((yyvsp[0].dval)); }
#line 10856 "def.tab.cpp"
    break;

  case 809: /* scanchain_end: K_END K_SCANCHAINS  */
#line 5390 "def.y"
      { 
        if (defData->callbacks->ScanchainsEndCbk)
          CALLBACK(defData->callbacks->ScanchainsEndCbk, defrScanchainsEndCbkType, 0);
        defData->bit_is_keyword = FALSE;
        defData->dumb_mode = 0; defData->no_num = 0;
      }
#line 10867 "def.tab.cpp"
    break;

  case 811: /* iotiming_start: K_IOTIMINGS NUMBER ';'  */
#line 5402 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingsStartCbk) {
          CALLBACK(defData->callbacks->IOTimingsStartCbk, defrIOTimingsStartCbkType, ROUND((yyvsp[-1].dval)));
        } else {
          if (defData->callbacks->IOTimingsStartCbk)
            if (defData->iOTimingWarnings++ < defData->settings->IOTimingWarnings)
              defData->defWarning(7035, "The IOTIMINGS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
        }
      }
#line 10881 "def.tab.cpp"
    break;

  case 813: /* iotiming_rules: iotiming_rules iotiming_rule  */
#line 5414 "def.y"
      { }
#line 10887 "def.tab.cpp"
    break;

  case 814: /* iotiming_rule: start_iotiming iotiming_members ';'  */
#line 5417 "def.y"
      { 
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingCbk)
          CALLBACK(defData->callbacks->IOTimingCbk, defrIOTimingCbkType, &defData->IOTiming);
      }
#line 10896 "def.tab.cpp"
    break;

  case 815: /* $@148: %empty  */
#line 5422 "def.y"
                        {defData->dumb_mode = 2; defData->no_num = 2; }
#line 10902 "def.tab.cpp"
    break;

  case 816: /* start_iotiming: '-' '(' $@148 T_STRING T_STRING ')'  */
#line 5423 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setName((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 10911 "def.tab.cpp"
    break;

  case 819: /* iotiming_member: '+' risefall K_VARIABLE NUMBER NUMBER  */
#line 5434 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setVariable((yyvsp[-3].string), (yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 10920 "def.tab.cpp"
    break;

  case 820: /* iotiming_member: '+' risefall K_SLEWRATE NUMBER NUMBER  */
#line 5439 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setSlewRate((yyvsp[-3].string), (yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 10929 "def.tab.cpp"
    break;

  case 821: /* iotiming_member: '+' K_CAPACITANCE NUMBER  */
#line 5444 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setCapacitance((yyvsp[0].dval));
      }
#line 10938 "def.tab.cpp"
    break;

  case 822: /* $@149: %empty  */
#line 5448 "def.y"
                        {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10944 "def.tab.cpp"
    break;

  case 823: /* $@150: %empty  */
#line 5449 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setDriveCell((yyvsp[0].string));
      }
#line 10953 "def.tab.cpp"
    break;

  case 825: /* iotiming_member: extension_stmt  */
#line 5458 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IoTimingsExtCbk)
          CALLBACK(defData->callbacks->IoTimingsExtCbk, defrIoTimingsExtCbkType, &defData->History_text[0]);
      }
#line 10962 "def.tab.cpp"
    break;

  case 826: /* $@151: %empty  */
#line 5464 "def.y"
              {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10968 "def.tab.cpp"
    break;

  case 827: /* $@152: %empty  */
#line 5465 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setTo((yyvsp[0].string));
      }
#line 10977 "def.tab.cpp"
    break;

  case 830: /* $@153: %empty  */
#line 5472 "def.y"
                  {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10983 "def.tab.cpp"
    break;

  case 831: /* iotiming_frompin: K_FROMPIN $@153 T_STRING  */
#line 5473 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setFrom((yyvsp[0].string));
      }
#line 10992 "def.tab.cpp"
    break;

  case 833: /* iotiming_parallel: K_PARALLEL NUMBER  */
#line 5480 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setParallel((yyvsp[0].dval));
      }
#line 11001 "def.tab.cpp"
    break;

  case 834: /* risefall: K_RISE  */
#line 5485 "def.y"
                 { (yyval.string) = (char*)"RISE"; }
#line 11007 "def.tab.cpp"
    break;

  case 835: /* risefall: K_FALL  */
#line 5485 "def.y"
                                                  { (yyval.string) = (char*)"FALL"; }
#line 11013 "def.tab.cpp"
    break;

  case 836: /* iotiming_end: K_END K_IOTIMINGS  */
#line 5488 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingsEndCbk)
          CALLBACK(defData->callbacks->IOTimingsEndCbk, defrIOTimingsEndCbkType, 0);
      }
#line 11022 "def.tab.cpp"
    break;

  case 837: /* floorplan_contraints_section: fp_start fp_stmts K_END K_FPC  */
#line 5494 "def.y"
      { 
        if (defData->callbacks->FPCEndCbk)
          CALLBACK(defData->callbacks->FPCEndCbk, defrFPCEndCbkType, 0);
      }
#line 11031 "def.tab.cpp"
    break;

  case 838: /* fp_start: K_FPC NUMBER ';'  */
#line 5500 "def.y"
      {
        if (defData->callbacks->FPCStartCbk)
          CALLBACK(defData->callbacks->FPCStartCbk, defrFPCStartCbkType, ROUND((yyvsp[-1].dval)));
      }
#line 11040 "def.tab.cpp"
    break;

  case 840: /* fp_stmts: fp_stmts fp_stmt  */
#line 5507 "def.y"
      {}
#line 11046 "def.tab.cpp"
    break;

  case 841: /* $@154: %empty  */
#line 5509 "def.y"
             { defData->dumb_mode = 1; defData->no_num = 1;  }
#line 11052 "def.tab.cpp"
    break;

  case 842: /* $@155: %empty  */
#line 5510 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setName((yyvsp[-1].string), (yyvsp[0].string)); }
#line 11058 "def.tab.cpp"
    break;

  case 843: /* fp_stmt: '-' $@154 T_STRING h_or_v $@155 constraint_type constrain_what_list ';'  */
#line 5512 "def.y"
      { if (defData->callbacks->FPCCbk) CALLBACK(defData->callbacks->FPCCbk, defrFPCCbkType, &defData->FPC); }
#line 11064 "def.tab.cpp"
    break;

  case 844: /* h_or_v: K_HORIZONTAL  */
#line 5515 "def.y"
      { (yyval.string) = (char*)"HORIZONTAL"; }
#line 11070 "def.tab.cpp"
    break;

  case 845: /* h_or_v: K_VERTICAL  */
#line 5517 "def.y"
      { (yyval.string) = (char*)"VERTICAL"; }
#line 11076 "def.tab.cpp"
    break;

  case 846: /* constraint_type: K_ALIGN  */
#line 5520 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setAlign(); }
#line 11082 "def.tab.cpp"
    break;

  case 847: /* constraint_type: K_MAX NUMBER  */
#line 5522 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setMax((yyvsp[0].dval)); }
#line 11088 "def.tab.cpp"
    break;

  case 848: /* constraint_type: K_MIN NUMBER  */
#line 5524 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setMin((yyvsp[0].dval)); }
#line 11094 "def.tab.cpp"
    break;

  case 849: /* constraint_type: K_EQUAL NUMBER  */
#line 5526 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setEqual((yyvsp[0].dval)); }
#line 11100 "def.tab.cpp"
    break;

  case 852: /* $@156: %empty  */
#line 5533 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setDoingBottomLeft(); }
#line 11106 "def.tab.cpp"
    break;

  case 854: /* $@157: %empty  */
#line 5536 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setDoingTopRight(); }
#line 11112 "def.tab.cpp"
    break;

  case 858: /* $@158: %empty  */
#line 5543 "def.y"
                         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 11118 "def.tab.cpp"
    break;

  case 859: /* row_or_comp: '(' K_ROWS $@158 T_STRING ')'  */
#line 5544 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.addRow((yyvsp[-1].string)); }
#line 11124 "def.tab.cpp"
    break;

  case 860: /* $@159: %empty  */
#line 5545 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1; }
#line 11130 "def.tab.cpp"
    break;

  case 861: /* row_or_comp: '(' K_COMPS $@159 T_STRING ')'  */
#line 5546 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.addComps((yyvsp[-1].string)); }
#line 11136 "def.tab.cpp"
    break;

  case 863: /* timingdisables_start: K_TIMINGDISABLES NUMBER ';'  */
#line 5553 "def.y"
      { 
        if (defData->callbacks->TimingDisablesStartCbk)
          CALLBACK(defData->callbacks->TimingDisablesStartCbk, defrTimingDisablesStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 11146 "def.tab.cpp"
    break;

  case 865: /* timingdisables_rules: timingdisables_rules timingdisables_rule  */
#line 5561 "def.y"
      {}
#line 11152 "def.tab.cpp"
    break;

  case 866: /* $@160: %empty  */
#line 5563 "def.y"
                                   { defData->dumb_mode = 2; defData->no_num = 2;  }
#line 11158 "def.tab.cpp"
    break;

  case 867: /* $@161: %empty  */
#line 5564 "def.y"
                       { defData->dumb_mode = 2; defData->no_num = 2;  }
#line 11164 "def.tab.cpp"
    break;

  case 868: /* timingdisables_rule: '-' K_FROMPIN $@160 T_STRING T_STRING K_TOPIN $@161 T_STRING T_STRING ';'  */
#line 5565 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setFromTo((yyvsp[-6].string), (yyvsp[-5].string), (yyvsp[-2].string), (yyvsp[-1].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                &defData->TimingDisable);
        }
      }
#line 11176 "def.tab.cpp"
    break;

  case 869: /* $@162: %empty  */
#line 5572 "def.y"
                      {defData->dumb_mode = 2; defData->no_num = 2; }
#line 11182 "def.tab.cpp"
    break;

  case 870: /* timingdisables_rule: '-' K_THRUPIN $@162 T_STRING T_STRING ';'  */
#line 5573 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setThru((yyvsp[-2].string), (yyvsp[-1].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                   &defData->TimingDisable);
        }
      }
#line 11194 "def.tab.cpp"
    break;

  case 871: /* $@163: %empty  */
#line 5580 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 11200 "def.tab.cpp"
    break;

  case 872: /* timingdisables_rule: '-' K_MACRO $@163 T_STRING td_macro_option ';'  */
#line 5581 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setMacro((yyvsp[-2].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                &defData->TimingDisable);
        }
      }
#line 11212 "def.tab.cpp"
    break;

  case 873: /* timingdisables_rule: '-' K_REENTRANTPATHS ';'  */
#line 5589 "def.y"
      { if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setReentrantPathsFlag();
      }
#line 11220 "def.tab.cpp"
    break;

  case 874: /* $@164: %empty  */
#line 5594 "def.y"
                           {defData->dumb_mode = 1; defData->no_num = 1;}
#line 11226 "def.tab.cpp"
    break;

  case 875: /* $@165: %empty  */
#line 5595 "def.y"
      {defData->dumb_mode=1; defData->no_num = 1;}
#line 11232 "def.tab.cpp"
    break;

  case 876: /* td_macro_option: K_FROMPIN $@164 T_STRING K_TOPIN $@165 T_STRING  */
#line 5596 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setMacroFromTo((yyvsp[-3].string),(yyvsp[0].string));
      }
#line 11241 "def.tab.cpp"
    break;

  case 877: /* $@166: %empty  */
#line 5600 "def.y"
                         {defData->dumb_mode=1; defData->no_num = 1;}
#line 11247 "def.tab.cpp"
    break;

  case 878: /* td_macro_option: K_THRUPIN $@166 T_STRING  */
#line 5601 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setMacroThru((yyvsp[0].string));
      }
#line 11256 "def.tab.cpp"
    break;

  case 879: /* timingdisables_end: K_END K_TIMINGDISABLES  */
#line 5607 "def.y"
      { 
        if (defData->callbacks->TimingDisablesEndCbk)
          CALLBACK(defData->callbacks->TimingDisablesEndCbk, defrTimingDisablesEndCbkType, 0);
      }
#line 11265 "def.tab.cpp"
    break;

  case 881: /* partitions_start: K_PARTITIONS NUMBER ';'  */
#line 5617 "def.y"
      {
        if (defData->callbacks->PartitionsStartCbk)
          CALLBACK(defData->callbacks->PartitionsStartCbk, defrPartitionsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 11275 "def.tab.cpp"
    break;

  case 883: /* partition_rules: partition_rules partition_rule  */
#line 5625 "def.y"
      { }
#line 11281 "def.tab.cpp"
    break;

  case 884: /* partition_rule: start_partition partition_members ';'  */
#line 5628 "def.y"
      { 
        if (defData->callbacks->PartitionCbk)
          CALLBACK(defData->callbacks->PartitionCbk, defrPartitionCbkType, &defData->Partition);
      }
#line 11290 "def.tab.cpp"
    break;

  case 885: /* $@167: %empty  */
#line 5633 "def.y"
                     { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11296 "def.tab.cpp"
    break;

  case 886: /* start_partition: '-' $@167 T_STRING turnoff  */
#line 5634 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setName((yyvsp[-1].string));
      }
#line 11305 "def.tab.cpp"
    break;

  case 888: /* turnoff: K_TURNOFF turnoff_setup turnoff_hold  */
#line 5641 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.addTurnOff((yyvsp[-1].string), (yyvsp[0].string));
      }
#line 11314 "def.tab.cpp"
    break;

  case 889: /* turnoff_setup: %empty  */
#line 5647 "def.y"
      { (yyval.string) = (char*)" "; }
#line 11320 "def.tab.cpp"
    break;

  case 890: /* turnoff_setup: K_SETUPRISE  */
#line 5649 "def.y"
      { (yyval.string) = (char*)"R"; }
#line 11326 "def.tab.cpp"
    break;

  case 891: /* turnoff_setup: K_SETUPFALL  */
#line 5651 "def.y"
      { (yyval.string) = (char*)"F"; }
#line 11332 "def.tab.cpp"
    break;

  case 892: /* turnoff_hold: %empty  */
#line 5654 "def.y"
      { (yyval.string) = (char*)" "; }
#line 11338 "def.tab.cpp"
    break;

  case 893: /* turnoff_hold: K_HOLDRISE  */
#line 5656 "def.y"
      { (yyval.string) = (char*)"R"; }
#line 11344 "def.tab.cpp"
    break;

  case 894: /* turnoff_hold: K_HOLDFALL  */
#line 5658 "def.y"
      { (yyval.string) = (char*)"F"; }
#line 11350 "def.tab.cpp"
    break;

  case 897: /* $@168: %empty  */
#line 5664 "def.y"
                                     {defData->dumb_mode=2; defData->no_num = 2;}
#line 11356 "def.tab.cpp"
    break;

  case 898: /* partition_member: '+' K_FROMCLOCKPIN $@168 T_STRING T_STRING risefall minmaxpins  */
#line 5666 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromClockPin((yyvsp[-3].string), (yyvsp[-2].string));
      }
#line 11365 "def.tab.cpp"
    break;

  case 899: /* $@169: %empty  */
#line 5670 "def.y"
                          {defData->dumb_mode=2; defData->no_num = 2; }
#line 11371 "def.tab.cpp"
    break;

  case 900: /* partition_member: '+' K_FROMCOMPPIN $@169 T_STRING T_STRING risefallminmax2_list  */
#line 5672 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromCompPin((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 11380 "def.tab.cpp"
    break;

  case 901: /* $@170: %empty  */
#line 5676 "def.y"
                        {defData->dumb_mode=1; defData->no_num = 1; }
#line 11386 "def.tab.cpp"
    break;

  case 902: /* partition_member: '+' K_FROMIOPIN $@170 T_STRING risefallminmax1_list  */
#line 5678 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromIOPin((yyvsp[-1].string));
      }
#line 11395 "def.tab.cpp"
    break;

  case 903: /* $@171: %empty  */
#line 5682 "def.y"
                         {defData->dumb_mode=2; defData->no_num = 2; }
#line 11401 "def.tab.cpp"
    break;

  case 904: /* partition_member: '+' K_TOCLOCKPIN $@171 T_STRING T_STRING risefall minmaxpins  */
#line 5684 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToClockPin((yyvsp[-3].string), (yyvsp[-2].string));
      }
#line 11410 "def.tab.cpp"
    break;

  case 905: /* $@172: %empty  */
#line 5688 "def.y"
                        {defData->dumb_mode=2; defData->no_num = 2; }
#line 11416 "def.tab.cpp"
    break;

  case 906: /* partition_member: '+' K_TOCOMPPIN $@172 T_STRING T_STRING risefallminmax2_list  */
#line 5690 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToCompPin((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 11425 "def.tab.cpp"
    break;

  case 907: /* $@173: %empty  */
#line 5694 "def.y"
                      {defData->dumb_mode=1; defData->no_num = 2; }
#line 11431 "def.tab.cpp"
    break;

  case 908: /* partition_member: '+' K_TOIOPIN $@173 T_STRING risefallminmax1_list  */
#line 5695 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToIOPin((yyvsp[-1].string));
      }
#line 11440 "def.tab.cpp"
    break;

  case 909: /* partition_member: extension_stmt  */
#line 5700 "def.y"
      { 
        if (defData->callbacks->PartitionsExtCbk)
          CALLBACK(defData->callbacks->PartitionsExtCbk, defrPartitionsExtCbkType,
                   &defData->History_text[0]);
      }
#line 11450 "def.tab.cpp"
    break;

  case 910: /* $@174: %empty  */
#line 5707 "def.y"
      { defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT; }
#line 11456 "def.tab.cpp"
    break;

  case 911: /* minmaxpins: min_or_max_list K_PINS $@174 pin_list  */
#line 5708 "def.y"
      { defData->dumb_mode = 0; defData->no_num = 0; }
#line 11462 "def.tab.cpp"
    break;

  case 913: /* min_or_max_list: min_or_max_list min_or_max_member  */
#line 5712 "def.y"
      { }
#line 11468 "def.tab.cpp"
    break;

  case 914: /* min_or_max_member: K_MIN NUMBER NUMBER  */
#line 5715 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setMin((yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 11477 "def.tab.cpp"
    break;

  case 915: /* min_or_max_member: K_MAX NUMBER NUMBER  */
#line 5720 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setMax((yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 11486 "def.tab.cpp"
    break;

  case 917: /* pin_list: pin_list T_STRING  */
#line 5727 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addPin((yyvsp[0].string)); }
#line 11492 "def.tab.cpp"
    break;

  case 920: /* risefallminmax1: K_RISEMIN NUMBER  */
#line 5733 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addRiseMin((yyvsp[0].dval)); }
#line 11498 "def.tab.cpp"
    break;

  case 921: /* risefallminmax1: K_FALLMIN NUMBER  */
#line 5735 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addFallMin((yyvsp[0].dval)); }
#line 11504 "def.tab.cpp"
    break;

  case 922: /* risefallminmax1: K_RISEMAX NUMBER  */
#line 5737 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addRiseMax((yyvsp[0].dval)); }
#line 11510 "def.tab.cpp"
    break;

  case 923: /* risefallminmax1: K_FALLMAX NUMBER  */
#line 5739 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addFallMax((yyvsp[0].dval)); }
#line 11516 "def.tab.cpp"
    break;

  case 926: /* risefallminmax2: K_RISEMIN NUMBER NUMBER  */
#line 5747 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addRiseMinRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11523 "def.tab.cpp"
    break;

  case 927: /* risefallminmax2: K_FALLMIN NUMBER NUMBER  */
#line 5750 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addFallMinRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11530 "def.tab.cpp"
    break;

  case 928: /* risefallminmax2: K_RISEMAX NUMBER NUMBER  */
#line 5753 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addRiseMaxRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11537 "def.tab.cpp"
    break;

  case 929: /* risefallminmax2: K_FALLMAX NUMBER NUMBER  */
#line 5756 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addFallMaxRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11544 "def.tab.cpp"
    break;

  case 930: /* partitions_end: K_END K_PARTITIONS  */
#line 5760 "def.y"
      { if (defData->callbacks->PartitionsEndCbk)
          CALLBACK(defData->callbacks->PartitionsEndCbk, defrPartitionsEndCbkType, 0); }
#line 11551 "def.tab.cpp"
    break;

  case 932: /* comp_names: comp_names comp_name  */
#line 5765 "def.y"
      { }
#line 11557 "def.tab.cpp"
    break;

  case 933: /* $@175: %empty  */
#line 5767 "def.y"
               {defData->dumb_mode=2; defData->no_num = 2; }
#line 11563 "def.tab.cpp"
    break;

  case 934: /* comp_name: '(' $@175 T_STRING T_STRING subnet_opt_syn ')'  */
#line 5769 "def.y"
      {
        // note that the defData->first T_STRING could be the keyword VPIN 
        if (defData->callbacks->NetCbk)
          defData->Subnet->addPin((yyvsp[-3].string), (yyvsp[-2].string), (yyvsp[-1].integer));
      }
#line 11573 "def.tab.cpp"
    break;

  case 935: /* subnet_opt_syn: %empty  */
#line 5776 "def.y"
      { (yyval.integer) = 0; }
#line 11579 "def.tab.cpp"
    break;

  case 936: /* subnet_opt_syn: '+' K_SYNTHESIZED  */
#line 5778 "def.y"
      { 
        (yyval.integer) = 1; 
      }
#line 11587 "def.tab.cpp"
    break;

  case 939: /* $@176: %empty  */
#line 5786 "def.y"
      {
        if (defData->callbacks->NetCbk) {
            defData->Wire = new defiWire(defData);
            defData->Wire->Init((yyvsp[0].string), NULL);
            defData->Subnet->addWire(defData->Wire);
        }
      }
#line 11599 "def.tab.cpp"
    break;

  case 940: /* subnet_option: subnet_type $@176 paths  */
#line 5794 "def.y"
      {  
        defData->by_is_keyword = FALSE;
        defData->do_is_keyword = FALSE;
        defData->new_is_keyword = FALSE;
        defData->step_is_keyword = FALSE;
        defData->orient_is_keyword = FALSE;
        defData->needNPCbk = 0;
        defData->Wire = NULL;
      }
#line 11613 "def.tab.cpp"
    break;

  case 941: /* $@177: %empty  */
#line 5803 "def.y"
                         { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11619 "def.tab.cpp"
    break;

  case 942: /* subnet_option: K_NONDEFAULTRULE $@177 T_STRING  */
#line 5804 "def.y"
      { if (defData->callbacks->NetCbk) defData->Subnet->setNonDefault((yyvsp[0].string)); }
#line 11625 "def.tab.cpp"
    break;

  case 943: /* subnet_type: K_FIXED  */
#line 5807 "def.y"
      { (yyval.string) = (char*)"FIXED"; defData->dumb_mode = 1; }
#line 11631 "def.tab.cpp"
    break;

  case 944: /* subnet_type: K_COVER  */
#line 5809 "def.y"
      { (yyval.string) = (char*)"COVER"; defData->dumb_mode = 1; }
#line 11637 "def.tab.cpp"
    break;

  case 945: /* subnet_type: K_ROUTED  */
#line 5811 "def.y"
      { (yyval.string) = (char*)"ROUTED"; defData->dumb_mode = 1; }
#line 11643 "def.tab.cpp"
    break;

  case 946: /* subnet_type: K_NOSHIELD  */
#line 5813 "def.y"
      { (yyval.string) = (char*)"NOSHIELD"; defData->dumb_mode = 1; }
#line 11649 "def.tab.cpp"
    break;

  case 948: /* begin_pin_props: K_PINPROPERTIES NUMBER opt_semi  */
#line 5818 "def.y"
      { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("PINPROPERTIES num ; ... END PINPROPERTIES")) {
                CHKERR();
            }
        }

        if (defData->callbacks->PinPropStartCbk) {
            CALLBACK(defData->callbacks->PinPropStartCbk, defrPinPropStartCbkType, ROUND((yyvsp[-1].dval))); 
        }
      }
#line 11665 "def.tab.cpp"
    break;

  case 949: /* opt_semi: %empty  */
#line 5832 "def.y"
      { }
#line 11671 "def.tab.cpp"
    break;

  case 950: /* opt_semi: ';'  */
#line 5834 "def.y"
      { }
#line 11677 "def.tab.cpp"
    break;

  case 951: /* end_pin_props: K_END K_PINPROPERTIES  */
#line 5837 "def.y"
      { if (defData->callbacks->PinPropEndCbk)
          CALLBACK(defData->callbacks->PinPropEndCbk, defrPinPropEndCbkType, 0); }
#line 11684 "def.tab.cpp"
    break;

  case 954: /* $@178: %empty  */
#line 5844 "def.y"
                       { defData->dumb_mode = 2; defData->no_num = 2; }
#line 11690 "def.tab.cpp"
    break;

  case 955: /* $@179: %empty  */
#line 5845 "def.y"
      { if (defData->callbacks->PinPropCbk) defData->PinProp.setName((yyvsp[-1].string), (yyvsp[0].string)); }
#line 11696 "def.tab.cpp"
    break;

  case 956: /* pin_prop_terminal: '-' $@178 T_STRING T_STRING $@179 pin_prop_options ';'  */
#line 5847 "def.y"
      { if (defData->callbacks->PinPropCbk) {
          CALLBACK(defData->callbacks->PinPropCbk, defrPinPropCbkType, &defData->PinProp);
         // reset the property number
         defData->PinProp.clear();
        }
      }
#line 11707 "def.tab.cpp"
    break;

  case 959: /* $@180: %empty  */
#line 5857 "def.y"
                         { defData->dumb_mode = DEF_MAX_INT; }
#line 11713 "def.tab.cpp"
    break;

  case 960: /* pin_prop: '+' K_PROPERTY $@180 pin_prop_name_value_list  */
#line 5859 "def.y"
      { defData->dumb_mode = 0; }
#line 11719 "def.tab.cpp"
    break;

  case 963: /* pin_prop_name_value: T_STRING NUMBER  */
#line 5866 "def.y"
      {
        if (defData->callbacks->PinPropCbk) {
          char propTp;
          char* str = defData->ringCopy("                       ");
          propTp = defData->session->CompPinProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "PINPROPERTIES");
          sprintf(str, "%g", (yyvsp[0].dval));
          defData->PinProp.addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
        }
      }
#line 11734 "def.tab.cpp"
    break;

  case 964: /* pin_prop_name_value: T_STRING QSTRING  */
#line 5877 "def.y"
      {
        if (defData->callbacks->PinPropCbk) {
          char propTp;
          propTp = defData->session->CompPinProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "PINPROPERTIES");
          defData->PinProp.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 11747 "def.tab.cpp"
    break;

  case 965: /* pin_prop_name_value: T_STRING T_STRING  */
#line 5886 "def.y"
      {
        if (defData->callbacks->PinPropCbk) {
          char propTp;
          propTp = defData->session->CompPinProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "PINPROPERTIES");
          defData->PinProp.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 11760 "def.tab.cpp"
    break;

  case 967: /* blockage_start: K_BLOCKAGES NUMBER ';'  */
#line 5898 "def.y"
      { if (defData->callbacks->BlockageStartCbk)
          CALLBACK(defData->callbacks->BlockageStartCbk, defrBlockageStartCbkType, ROUND((yyvsp[-1].dval))); }
#line 11767 "def.tab.cpp"
    break;

  case 968: /* blockage_end: K_END K_BLOCKAGES  */
#line 5902 "def.y"
      { if (defData->callbacks->BlockageEndCbk)
          CALLBACK(defData->callbacks->BlockageEndCbk, defrBlockageEndCbkType, 0); }
#line 11774 "def.tab.cpp"
    break;

  case 971: /* blockage_def: blockage_rule rectPoly_blockage rectPoly_blockage_rules ';'  */
#line 5911 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
          CALLBACK(defData->callbacks->BlockageCbk, defrBlockageCbkType, &defData->Blockage);
          defData->Blockage.clear();
        }
      }
#line 11785 "def.tab.cpp"
    break;

  case 972: /* $@181: %empty  */
#line 5918 "def.y"
                           { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11791 "def.tab.cpp"
    break;

  case 973: /* $@182: %empty  */
#line 5919 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
          if (defData->Blockage.hasPlacement() != 0) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6539, "Invalid BLOCKAGE statement defined in the DEF file. The BLOCKAGE statment has both the LAYER and the PLACEMENT statements defined.\nUpdate your DEF file to have either BLOCKAGE or PLACEMENT statement only.");
              CHKERR();
            }
          }
          defData->Blockage.setLayer((yyvsp[0].string));
          defData->Blockage.clearPoly(); // free poly, if any
        }
        defData->hasBlkLayerComp = 0;
        defData->hasBlkLayerSpac = 0;
        defData->hasBlkLayerTypeComp = 0;
      }
#line 11811 "def.tab.cpp"
    break;

  case 975: /* $@183: %empty  */
#line 5938 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
          if (defData->Blockage.hasLayer() != 0) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6539, "Invalid BLOCKAGE statement defined in the DEF file. The BLOCKAGE statment has both the LAYER and the PLACEMENT statements defined.\nUpdate your DEF file to have either BLOCKAGE or PLACEMENT statement only.");
              CHKERR();
            }
          }
          defData->Blockage.setPlacement();
          defData->Blockage.clearPoly(); // free poly, if any
        }
        defData->hasBlkPlaceComp = 0;
        defData->hasBlkPlaceTypeComp = 0;
        defData->hasDef60BlkPlaceTypeComp = 0;
      }
#line 11831 "def.tab.cpp"
    break;

  case 979: /* layer_blockage_rule: '+' K_SPACING NUMBER  */
#line 5960 "def.y"
      {
        if (defData->VersionNum < 5.6) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defMsg = (char*)malloc(1000);
              sprintf (defData->defMsg,
                 "The SPACING statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
              defData->defError(6540, defData->defMsg);
              free(defData->defMsg);
              CHKERR();
            }
          }
        } else if (defData->hasBlkLayerSpac) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6541, "The SPACING statement is defined in the LAYER statement,\nbut there is already either a SPACING statement or DESIGNRULEWIDTH  statement has defined in the LAYER statement.\nUpdate your DEF file to have either SPACING statement or a DESIGNRULEWIDTH statement.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk)
            defData->Blockage.setSpacing((int)(yyvsp[0].dval));
          defData->hasBlkLayerSpac = 1;
        }
      }
#line 11861 "def.tab.cpp"
    break;

  case 980: /* layer_blockage_rule: '+' K_DESIGNRULEWIDTH NUMBER  */
#line 5986 "def.y"
      {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("BLOCKAGES ... - LAYER ... DESIGNRULEWIDTH effectiveWidth")) {
                CHKERR();
            }
        } else if (defData->VersionNum < 5.6) {
            if (defData->callbacks->BlockageCbk) {
                if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                    defData->defError(6541, "The SPACING statement is defined in the LAYER statement,\nbut there is already either a SPACING statement or DESIGNRULEWIDTH  statement has defined in the LAYER statement.\nUpdate your DEF file to have either SPACING statement or a DESIGNRULEWIDTH statement.");
                    CHKERR();
                }
            }
        } else if (defData->hasBlkLayerSpac) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6541, "The SPACING statement is defined in the LAYER statement,\nbut there is already either a SPACING statement or DESIGNRULEWIDTH  statement has defined in the LAYER statement.\nUpdate your DEF file to have either SPACING statement or a DESIGNRULEWIDTH statement.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk)
            defData->Blockage.setDesignRuleWidth((int)(yyvsp[0].dval));
          defData->hasBlkLayerSpac = 1;
        }
      }
#line 11891 "def.tab.cpp"
    break;

  case 981: /* layer_blockage_rule: '+' K_MASK NUMBER  */
#line 6013 "def.y"
      {      
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->blockageWarnings, defData->settings->BlockageWarnings)) {
          defData->Blockage.setMask((int)(yyvsp[0].dval));
        }
      }
#line 11901 "def.tab.cpp"
    break;

  case 982: /* $@184: %empty  */
#line 6019 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11907 "def.tab.cpp"
    break;

  case 983: /* layer_blockage_rule: '+' K_NAME $@184 T_STRING  */
#line 6020 "def.y"
      { 
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - LAYER layerName ... + NAME name")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->BlockageCbk) {
                defData->Blockage.setName((yyvsp[0].string));
            }
        }
      }
#line 11923 "def.tab.cpp"
    break;

  case 984: /* layer_blockage_rule: '+' K_PARTIAL NUMBER  */
#line 6032 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - LAYER layerName ... + PARTIAL density")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->BlockageCbk) {
                defData->Blockage.setPartial((yyvsp[0].dval));
            }
        }
      }
#line 11939 "def.tab.cpp"
    break;

  case 985: /* $@185: %empty  */
#line 6044 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 11947 "def.tab.cpp"
    break;

  case 986: /* layer_blockage_rule: '+' K_PROPERTY $@185 prop_name_value  */
#line 6048 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - LAYER layerName ... + PROPERTY propName propVal")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->BlockageCbk) {
                defData->setPropDataType((yyvsp[0].prop), "PIN", defData->session->BlockageProp);
                defData->Blockage.addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = 0;
            }
        }

        delete (yyvsp[0].prop);
      }
#line 11967 "def.tab.cpp"
    break;

  case 988: /* $@186: %empty  */
#line 6068 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11973 "def.tab.cpp"
    break;

  case 989: /* comp_blockage_rule: '+' K_COMPONENT $@186 T_STRING  */
#line 6069 "def.y"
      {
        if (defData->hasBlkLayerComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk) {
            defData->Blockage.setComponent((yyvsp[0].string));
          }
          if (defData->VersionNum < 5.8) {
            defData->hasBlkLayerComp = 1;
          }
        }
      }
#line 11995 "def.tab.cpp"
    break;

  case 990: /* comp_blockage_rule: '+' K_SLOTS  */
#line 6088 "def.y"
      {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("BLOCKAGES ... - LAYER ... + COMPONENT compName + SLOTS")) {
                CHKERR();
            }
        } else if (defData->hasBlkLayerComp || defData->hasBlkLayerTypeComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk) {
            defData->Blockage.setSlots();
          }
          if (defData->VersionNum < 5.8) {
            defData->hasBlkLayerComp = 1;
          } else {
            defData->hasBlkLayerTypeComp = 1;
          }
        }
      }
#line 12023 "def.tab.cpp"
    break;

  case 991: /* comp_blockage_rule: '+' K_FILLS  */
#line 6112 "def.y"
      {
        if (defData->hasBlkLayerComp || defData->hasBlkLayerTypeComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk){
            defData->Blockage.setFills();
          }

          if (defData->VersionNum < 5.8) {
            defData->hasBlkLayerComp = 1;
          } else {
            defData->hasBlkLayerTypeComp = 1;
          }
        }
      }
#line 12048 "def.tab.cpp"
    break;

  case 992: /* comp_blockage_rule: '+' K_PUSHDOWN  */
#line 6133 "def.y"
      {
        if (defData->hasBlkLayerComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk){
            defData->Blockage.setPushdown();
          }
          if (defData->VersionNum < 5.8) {
            defData->hasBlkLayerComp = 1;
          }
        }
      }
#line 12070 "def.tab.cpp"
    break;

  case 993: /* comp_blockage_rule: '+' K_EXCEPTPGNET  */
#line 6151 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->BlockageCbk) {
             if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
               defData->defMsg = (char*)malloc(10000);
               sprintf (defData->defMsg,
                 "The EXCEPTPGNET is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
               defData->defError(6549, defData->defMsg);
               free(defData->defMsg);
               CHKERR();
              }
           }
        } else {
           if (defData->hasBlkLayerComp) {
             if (defData->callbacks->BlockageCbk) {
               if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                 defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
                 CHKERR();
               }
             }
           } else {
             if (defData->callbacks->BlockageCbk){
               defData->Blockage.setExceptpgnet();
             }
             if (defData->VersionNum < 5.8){
               defData->hasBlkLayerComp = 1;
             }
           }
        }
      }
#line 12105 "def.tab.cpp"
    break;

  case 994: /* comp_blockage_rule: '+' K_ONLYPGNET  */
#line 6182 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - LAYER layerName ... + ONLYPGNET")) {
                CHKERR();
            }
        } else {
           if (defData->hasBlkLayerComp) {
             if (defData->callbacks->BlockageCbk) {
               if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                 defData->defError(6542, "The defined BLOCKAGES COMPONENT statement has either COMPONENT, SLOTS, FILLS, PUSHDOWN, ONLYPGNET or EXCEPTPGNET defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES COMPONENT statement per layer.");
                 CHKERR();
               }
             }
           } else {
             if (defData->callbacks->BlockageCbk) {
               defData->Blockage.setOnlypgnet();
             }
             
             if (defData->VersionNum < 5.8) {
               defData->hasBlkLayerComp = 1;
             }
           }
        }
      }
#line 12134 "def.tab.cpp"
    break;

  case 997: /* $@187: %empty  */
#line 6213 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12140 "def.tab.cpp"
    break;

  case 998: /* placement_comp_rule: '+' K_COMPONENT $@187 T_STRING  */
#line 6214 "def.y"
      {
        if (defData->hasBlkPlaceComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6543, "The defined BLOCKAGES PLACEMENT statement has either COMPONENT, PUSHDOWN, SOFT or PARTIAL defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES PLACEMENT statement.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk){
            defData->Blockage.setComponent((yyvsp[0].string));
          }
          if (defData->VersionNum < 5.8) {
            defData->hasBlkPlaceComp = 1;
          }
        }
      }
#line 12162 "def.tab.cpp"
    break;

  case 999: /* $@188: %empty  */
#line 6231 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12168 "def.tab.cpp"
    break;

  case 1000: /* placement_comp_rule: '+' K_NAME $@188 T_STRING  */
#line 6232 "def.y"
      { 
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - PLACEMENT ... + NAME name")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->BlockageCbk) {
                defData->Blockage.setName((yyvsp[0].string));
            }
        }
      }
#line 12184 "def.tab.cpp"
    break;

  case 1001: /* $@189: %empty  */
#line 6244 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 12192 "def.tab.cpp"
    break;

  case 1002: /* placement_comp_rule: '+' K_PROPERTY $@189 prop_name_value  */
#line 6248 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - PLACEMENT ... + PROPERTY propName propVal")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->BlockageCbk) {
                defData->setPropDataType((yyvsp[0].prop), "PIN", defData->session->BlockageProp);
                defData->Blockage.addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = 0;
            }
        }

        delete (yyvsp[0].prop);
      }
#line 12212 "def.tab.cpp"
    break;

  case 1003: /* placement_comp_rule: '+' K_ONLYBLOCKS  */
#line 6264 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - PLACEMENT ... + ONLYBLOCKS")) {
                CHKERR();
            }
        } else if (defData->hasDef60BlkPlaceTypeComp) {
             if (defData->def60ExclusiveStatementsError("BLOCKAGES ... - PLACEMENT", "+ ONLYBLOCKS, + SOFT, + PARTIAL")) {
                CHKERR();
             }
        } else {
            if (defData->callbacks->BlockageCbk){
                defData->Blockage.setOnlyblocks();
            }

            defData->hasDef60BlkPlaceTypeComp = 1;
        }
      }
#line 12234 "def.tab.cpp"
    break;

  case 1004: /* placement_comp_rule: '+' K_PUSHDOWN  */
#line 6282 "def.y"
      {
        if (defData->hasBlkPlaceComp) {
          if (defData->callbacks->BlockageCbk) {
            if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
              defData->defError(6543, "The defined BLOCKAGES PLACEMENT statement has either COMPONENT, PUSHDOWN, SOFT or PARTIAL defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES PLACEMENT statement.");
              CHKERR();
            }
          }
        } else {
          if (defData->callbacks->BlockageCbk){
            defData->Blockage.setPushdown();
          }
          if (defData->VersionNum < 5.8) {
            defData->hasBlkPlaceComp = 1;
          }
        }
      }
#line 12256 "def.tab.cpp"
    break;

  case 1005: /* placement_comp_rule: '+' K_SOFT  */
#line 6300 "def.y"
      {
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("BLOCKAGES ... - PLACEMENT ... + SOFT ...")) {
                CHKERR();
            }
        } else if (defData->VersionNum < 5.7) {
           if (defData->callbacks->BlockageCbk) {
             if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
               defData->defMsg = (char*)malloc(10000);
               sprintf (defData->defMsg,
                 "The PLACEMENT SOFT is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
               defData->defError(6547, defData->defMsg);
               free(defData->defMsg);
               CHKERR();
             }
           }
        } else {
           if (defData->hasBlkPlaceComp || defData->hasBlkPlaceTypeComp) {
             if (defData->callbacks->BlockageCbk) {
               if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                 defData->defError(6543, "The defined BLOCKAGES PLACEMENT statement has either COMPONENT, PUSHDOWN, SOFT or PARTIAL defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES PLACEMENT statement.");
                 CHKERR();
               }
             }
           } else {
             if (defData->callbacks->BlockageCbk){
               defData->Blockage.setSoft();
             }
             if (defData->VersionNum < 5.8) {
               defData->hasBlkPlaceComp = 1;
             }
             if (defData->VersionNum == 5.8) {
               defData->hasBlkPlaceTypeComp = 1;
             }
           }
        }
      }
#line 12298 "def.tab.cpp"
    break;

  case 1006: /* placement_comp_rule: '+' K_SOFT NUMBER  */
#line 6338 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - PLACEMENT ... + SOFT maxDensity")) {
                CHKERR();
            }
        } else if (defData->hasDef60BlkPlaceTypeComp) {
             if (defData->def60ExclusiveStatementsError("BLOCKAGES ... - PLACEMENT", "+ ONLYBLOCKS, + SOFT, + PARTIAL")) {
                CHKERR();
             }
        } else {
            if (defData->callbacks->BlockageCbk){
                defData->Blockage.setSoft((yyvsp[0].dval));
            }

            defData->hasDef60BlkPlaceTypeComp = 1;
        }      
      }
#line 12320 "def.tab.cpp"
    break;

  case 1007: /* placement_comp_rule: '+' K_PARTIAL NUMBER  */
#line 6357 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->BlockageCbk) {
             if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                defData->defMsg = (char*)malloc(10000);
                sprintf (defData->defMsg,
                  "The PARTIAL is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
                defData->defError(6548, defData->defMsg);
                free(defData->defMsg);
                CHKERR();
             }
           }
        } else {
           if (defData->hasBlkPlaceComp || defData->hasBlkPlaceTypeComp) {
             if (defData->callbacks->BlockageCbk) {
               if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                 defData->defError(6543, "The defined BLOCKAGES PLACEMENT statement has either COMPONENT, PUSHDOWN, SOFT or PARTIAL defined.\nOnly one of these statements is allowed per LAYER. Updated the DEF file to define a valid BLOCKAGES PLACEMENT statement.");
                 CHKERR();
               }
             }
           } else if (defData->VersionNum >= 6.0 - 0.00001 && defData->hasDef60BlkPlaceTypeComp) {
             if (defData->def60ExclusiveStatementsError("BLOCKAGES ... - PLACEMENT", "+ ONLYBLOCKS, + SOFT, + PARTIAL")) {
                CHKERR();
             } 
           } else {
             if (defData->callbacks->BlockageCbk){
               defData->Blockage.setPartial((yyvsp[0].dval));
             } 

             if (defData->VersionNum < 5.8) {
               defData->hasBlkPlaceComp = 1;
             } else if (defData->VersionNum == 5.8) {
               defData->hasBlkPlaceTypeComp = 1;
             } else if (defData->VersionNum >= 6.0 - 0.00001) {
                defData->hasDef60BlkPlaceTypeComp = 1;
             }
           }
         }
      }
#line 12364 "def.tab.cpp"
    break;

  case 1008: /* placement_comp_rule: '+' K_PARTIAL NUMBER K_NOFLOPS  */
#line 6398 "def.y"
      {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("BLOCKAGES ... - PLACEMENT ... + K_PARTIAL maxDensity K_NOFLOPS")) {
                CHKERR();
            }
        } else if (defData->hasDef60BlkPlaceTypeComp) {
            if (defData->def60ExclusiveStatementsError("BLOCKAGES ... - PLACEMENT", "+ ONLYBLOCKS, + SOFT, + PARTIAL")) {
                CHKERR();
            }
        } else if (defData->callbacks->BlockageCbk){
                defData->Blockage.setPartial((yyvsp[-1].dval), 1);
        }

        defData->hasDef60BlkPlaceTypeComp = 1;
      }
#line 12384 "def.tab.cpp"
    break;

  case 1011: /* rectPoly_blockage: K_RECT pt pt  */
#line 6419 "def.y"
      {
        if (defData->callbacks->BlockageCbk)
          defData->Blockage.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
      }
#line 12393 "def.tab.cpp"
    break;

  case 1012: /* $@190: %empty  */
#line 6424 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
            defData->Geometries.Reset();
        }
      }
#line 12403 "def.tab.cpp"
    break;

  case 1013: /* rectPoly_blockage: K_POLYGON $@190 firstPt nextPt nextPt otherPts  */
#line 6430 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
          if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond
            if (defData->Blockage.hasLayer()) {  // only in layer
              if (defData->callbacks->BlockageCbk)
                defData->Blockage.addPolygon(&defData->Geometries);
            } else {
              if (defData->callbacks->BlockageCbk) {
                if (defData->blockageWarnings++ < defData->settings->BlockageWarnings) {
                  defData->defError(6544, "A POLYGON statement is defined in the BLOCKAGE statement,\nbut it is not defined in the BLOCKAGE LAYER statement.\nUpdate your DEF file to either remove the POLYGON statement from the BLOCKAGE statement or\ndefine the POLYGON statement in a BLOCKAGE LAYER statement.");
                  CHKERR();
                }
              }
            }
          }
        }
      }
#line 12425 "def.tab.cpp"
    break;

  case 1015: /* slot_start: K_SLOTS NUMBER ';'  */
#line 6452 "def.y"
      { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SLOTS ... END SLOTS")) {
                CHKERR();
            }
        } else if (defData->callbacks->SlotStartCbk) {
            CALLBACK(defData->callbacks->SlotStartCbk, defrSlotStartCbkType, ROUND((yyvsp[-1].dval))); 
        }
      }
#line 12439 "def.tab.cpp"
    break;

  case 1016: /* slot_end: K_END K_SLOTS  */
#line 6463 "def.y"
      { if (defData->callbacks->SlotEndCbk)
          CALLBACK(defData->callbacks->SlotEndCbk, defrSlotEndCbkType, 0); 
      }
#line 12447 "def.tab.cpp"
    break;

  case 1019: /* slot_def: slot_rule geom_slot_rules ';'  */
#line 6472 "def.y"
      {
        if (defData->callbacks->SlotCbk) {
          CALLBACK(defData->callbacks->SlotCbk, defrSlotCbkType, &defData->Slot);
          defData->Slot.clear();
        }
      }
#line 12458 "def.tab.cpp"
    break;

  case 1020: /* $@191: %empty  */
#line 6479 "def.y"
                       { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12464 "def.tab.cpp"
    break;

  case 1021: /* $@192: %empty  */
#line 6480 "def.y"
      {
        if (defData->callbacks->SlotCbk) {
          defData->Slot.setLayer((yyvsp[0].string));
          defData->Slot.clearPoly();     // free poly, if any
        }
      }
#line 12475 "def.tab.cpp"
    break;

  case 1025: /* geom_slot: K_RECT pt pt  */
#line 6492 "def.y"
      {
        if (defData->callbacks->SlotCbk)
          defData->Slot.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
      }
#line 12484 "def.tab.cpp"
    break;

  case 1026: /* $@193: %empty  */
#line 6497 "def.y"
      {
          defData->Geometries.Reset();
      }
#line 12492 "def.tab.cpp"
    break;

  case 1027: /* geom_slot: K_POLYGON $@193 firstPt nextPt nextPt otherPts  */
#line 6501 "def.y"
      {
        if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond
          if (defData->callbacks->SlotCbk)
            defData->Slot.addPolygon(&defData->Geometries);
        }
      }
#line 12503 "def.tab.cpp"
    break;

  case 1029: /* fill_start: K_FILLS NUMBER ';'  */
#line 6512 "def.y"
      { if (defData->callbacks->FillStartCbk)
          CALLBACK(defData->callbacks->FillStartCbk, defrFillStartCbkType, ROUND((yyvsp[-1].dval))); }
#line 12510 "def.tab.cpp"
    break;

  case 1030: /* fill_end: K_END K_FILLS  */
#line 6516 "def.y"
      { if (defData->callbacks->FillEndCbk)
          CALLBACK(defData->callbacks->FillEndCbk, defrFillEndCbkType, 0); }
#line 12517 "def.tab.cpp"
    break;

  case 1033: /* $@194: %empty  */
#line 6523 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12523 "def.tab.cpp"
    break;

  case 1034: /* $@195: %empty  */
#line 6524 "def.y"
      {
        if (defData->callbacks->FillCbk) {
            defData->Fill.setLayer((yyvsp[0].string));
            defData->Fill.clearShapes();    // Free shapes, if any.
        }
      }
#line 12534 "def.tab.cpp"
    break;

  case 1035: /* fill_def: '-' K_LAYER $@194 T_STRING $@195 fill_layer_mask_opc_opt geom_fill geom_fill_rules ';'  */
#line 6531 "def.y"
      {
        if (defData->callbacks->FillCbk) {
            CALLBACK(defData->callbacks->FillCbk, defrFillCbkType, &defData->Fill);
            defData->Fill.Destroy();
            defData->Fill.Init();
        }
      }
#line 12546 "def.tab.cpp"
    break;

  case 1036: /* $@196: %empty  */
#line 6538 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12552 "def.tab.cpp"
    break;

  case 1037: /* $@197: %empty  */
#line 6539 "def.y"
      {
        if (defData->callbacks->FillCbk) {
          defData->Fill.setVia((yyvsp[0].string));
          defData->Fill.clearPts();
          defData->Geometries.Reset();
          defData->Orients.clear();
        }
      }
#line 12565 "def.tab.cpp"
    break;

  case 1038: /* fill_def: '-' K_VIA $@196 T_STRING $@197 fill_via_mask_opc_opt firstViaPt otherViaPts ';'  */
#line 6548 "def.y"
      {
        if (defData->callbacks->FillCbk) {
          defData->Fill.addPts(&defData->Geometries, &defData->Orients);
          CALLBACK(defData->callbacks->FillCbk, defrFillCbkType, &defData->Fill);
        }

        defData->Fill.clear();
        defData->Orients.clear();
      }
#line 12579 "def.tab.cpp"
    break;

  case 1041: /* geom_fill: K_RECT pt pt  */
#line 6563 "def.y"
      {
        if (defData->callbacks->FillCbk) {
            defData->Fill.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
        }

        if (defData->callbacks->FillPartialCbk
            && defData->Fill.numRectangles() == PARTIAL_CBK_THRESHOLD) {
            CALLBACK(defData->callbacks->FillPartialCbk,
                     defrFillPartialCbkType, &defData->Fill);
            defData->Fill.clearShapes();
        }
      }
#line 12596 "def.tab.cpp"
    break;

  case 1042: /* $@198: %empty  */
#line 6576 "def.y"
      {
        defData->Geometries.Reset();
      }
#line 12604 "def.tab.cpp"
    break;

  case 1043: /* geom_fill: K_POLYGON $@198 firstPt nextPt nextPt otherPts  */
#line 6580 "def.y"
      {
        if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond
            if (defData->callbacks->FillCbk) {
                defData->Fill.addPolygon(&defData->Geometries);
            }

            if (defData->callbacks->FillPartialCbk
                && defData->Fill.numPolygons() == PARTIAL_CBK_THRESHOLD) {
                CALLBACK(defData->callbacks->FillPartialCbk,
                         defrFillPartialCbkType, &defData->Fill);
                defData->Fill.clearShapes();
            }
        } else {
            defData->defMsg = (char*)malloc(DEF_MSGS);
            sprintf (defData->defMsg,
              "POLYGON statement in FILLS LAYER is a version 5.6 and later syntax.\nYour def file is defined with version %.2f.", defData->VersionNum);
            defData->defError(6564, defData->defMsg);
            free(defData->defMsg);
            CHKERR();
        }
      }
#line 12630 "def.tab.cpp"
    break;

  case 1049: /* fill_layer_opc: '+' K_OPC  */
#line 6613 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->FillCbk) {
             if (defData->fillWarnings++ < defData->settings->FillWarnings) {
               defData->defMsg = (char*)malloc(10000);
               sprintf (defData->defMsg,
                 "The LAYER OPC is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
               defData->defError(6553, defData->defMsg);
               free(defData->defMsg);
               CHKERR();
             }
           }
        } else {
           if (defData->callbacks->FillCbk)
             defData->Fill.setLayerOpc();
        }
      }
#line 12652 "def.tab.cpp"
    break;

  case 1050: /* firstViaPt: fill_via_orient pt  */
#line 6632 "def.y"
          { 
            defData->Geometries.startList((yyvsp[0].pt).x, (yyvsp[0].pt).y); 
            defData->Orients.push_back((yyvsp[-1].integer));
          }
#line 12661 "def.tab.cpp"
    break;

  case 1051: /* nextViaPt: fill_via_orient pt  */
#line 6638 "def.y"
          { 
            defData->Geometries.addToList((yyvsp[0].pt).x, (yyvsp[0].pt).y); 
            defData->Orients.push_back((yyvsp[-1].integer));
          }
#line 12670 "def.tab.cpp"
    break;

  case 1054: /* fill_via_orient: %empty  */
#line 6648 "def.y"
    {
        (yyval.integer) = 0;
    }
#line 12678 "def.tab.cpp"
    break;

  case 1055: /* fill_via_orient: orient  */
#line 6652 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("FILLS ... - VIA ... orient pt  ...")) {
                CHKERR();
            }
        } 

        (yyval.integer) = (yyvsp[0].integer);
    }
#line 12692 "def.tab.cpp"
    break;

  case 1061: /* $@199: %empty  */
#line 6672 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 12700 "def.tab.cpp"
    break;

  case 1062: /* fill_via_prop: '+' K_PROPERTY $@199 prop_name_value  */
#line 6676 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("FILLS: ... + VIA ... + PROPERTY propName propValue")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->FillCbk) {
                defData->setPropDataType((yyvsp[0].prop), "SPECIALROUTE", defData->session->SpecialRouteProp);
                defData->Fill.addViaProp((yyvsp[0].prop));
                (yyvsp[0].prop) = NULL;
            }
        }

        delete (yyvsp[0].prop);
    }
#line 12720 "def.tab.cpp"
    break;

  case 1063: /* $@200: %empty  */
#line 6693 "def.y"
    {
        defData->dumb_mode = 2; 
    }
#line 12728 "def.tab.cpp"
    break;

  case 1064: /* fill_layer_prop: '+' K_PROPERTY $@200 prop_name_value  */
#line 6697 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("FILLS: ... + LAYER ... + PROPERTY propName propValue")) {
                CHKERR();
            }
        } else {
            if (defData->callbacks->FillCbk) {
                defData->setPropDataType((yyvsp[0].prop), "SPECIALROUTE", defData->session->SpecialRouteProp);
                defData->Fill.addLayerProp((yyvsp[0].prop));
                (yyvsp[0].prop) = NULL;
            }
        }

        delete (yyvsp[0].prop);
    }
#line 12748 "def.tab.cpp"
    break;

  case 1065: /* fill_via_opc: '+' K_OPC  */
#line 6716 "def.y"
      {
        if (defData->VersionNum < 5.7) {
           if (defData->callbacks->FillCbk) {
             if (defData->fillWarnings++ < defData->settings->FillWarnings) {
               defData->defMsg = (char*)malloc(10000);
               sprintf (defData->defMsg,
                 "The VIA OPC is available in version 5.7 or later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
               defData->defError(6554, defData->defMsg);
               free(defData->defMsg);
               CHKERR();
             }
           }
        } else {
           if (defData->callbacks->FillCbk)
             defData->Fill.setViaOpc();
        }
      }
#line 12770 "def.tab.cpp"
    break;

  case 1066: /* fill_mask: '+' K_MASK NUMBER  */
#line 6736 "def.y"
      { 
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->fillWarnings, defData->settings->FillWarnings)) {
             if (defData->callbacks->FillCbk) {
                defData->Fill.setMask((int)(yyvsp[0].dval));
             }
        }
      }
#line 12782 "def.tab.cpp"
    break;

  case 1067: /* fill_viaMask: '+' K_MASK NUMBER  */
#line 6746 "def.y"
      { 
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->fillWarnings, defData->settings->FillWarnings)) {
             if (defData->callbacks->FillCbk) {
                defData->Fill.setMask((int)(yyvsp[0].dval));
             }
        }
      }
#line 12794 "def.tab.cpp"
    break;

  case 1069: /* nondefault_start: K_NONDEFAULTRULES NUMBER ';'  */
#line 6759 "def.y"
      { 
        if (defData->VersionNum < 5.6) {
          if (defData->callbacks->NonDefaultStartCbk) {
            if (defData->nonDefaultWarnings++ < defData->settings->NonDefaultWarnings) {
              defData->defMsg = (char*)malloc(1000);
              sprintf (defData->defMsg,
                 "The NONDEFAULTRULE statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f.", defData->VersionNum);
              defData->defError(6545, defData->defMsg);
              free(defData->defMsg);
              CHKERR();
            }
          }
        } else if (defData->callbacks->NonDefaultStartCbk)
          CALLBACK(defData->callbacks->NonDefaultStartCbk, defrNonDefaultStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 12815 "def.tab.cpp"
    break;

  case 1070: /* nondefault_end: K_END K_NONDEFAULTRULES  */
#line 6777 "def.y"
      { if (defData->callbacks->NonDefaultEndCbk)
          CALLBACK(defData->callbacks->NonDefaultEndCbk, defrNonDefaultEndCbkType, 0); }
#line 12822 "def.tab.cpp"
    break;

  case 1073: /* $@201: %empty  */
#line 6784 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12828 "def.tab.cpp"
    break;

  case 1074: /* $@202: %empty  */
#line 6785 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.clear(); 
          defData->NonDefault.setName((yyvsp[0].string));
        }
      }
#line 12839 "def.tab.cpp"
    break;

  case 1075: /* nondefault_def: '-' $@201 T_STRING $@202 nondefault_options ';'  */
#line 6792 "def.y"
      { if (defData->callbacks->NonDefaultCbk)
          CALLBACK(defData->callbacks->NonDefaultCbk, defrNonDefaultCbkType, &defData->NonDefault); }
#line 12846 "def.tab.cpp"
    break;

  case 1078: /* nondefault_option: '+' K_HARDSPACING  */
#line 6800 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk)
          defData->NonDefault.setHardspacing();
      }
#line 12855 "def.tab.cpp"
    break;

  case 1079: /* $@203: %empty  */
#line 6804 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12861 "def.tab.cpp"
    break;

  case 1080: /* $@204: %empty  */
#line 6806 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addLayer((yyvsp[-2].string));
          defData->NonDefault.addWidth((yyvsp[0].dval));
        }
      }
#line 12872 "def.tab.cpp"
    break;

  case 1082: /* $@205: %empty  */
#line 6813 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12878 "def.tab.cpp"
    break;

  case 1083: /* nondefault_option: '+' K_VIA $@205 T_STRING  */
#line 6814 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addVia((yyvsp[0].string));
        }
      }
#line 12888 "def.tab.cpp"
    break;

  case 1084: /* $@206: %empty  */
#line 6819 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12894 "def.tab.cpp"
    break;

  case 1085: /* nondefault_option: '+' K_VIARULE $@206 T_STRING  */
#line 6820 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addViaRule((yyvsp[0].string));
        }
      }
#line 12904 "def.tab.cpp"
    break;

  case 1086: /* $@207: %empty  */
#line 6825 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12910 "def.tab.cpp"
    break;

  case 1087: /* nondefault_option: '+' K_MINCUTS $@207 T_STRING NUMBER  */
#line 6826 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addMinCuts((yyvsp[-1].string), (int)(yyvsp[0].dval));
        }
      }
#line 12920 "def.tab.cpp"
    break;

  case 1091: /* nondefault_layer_option: K_DIAGWIDTH NUMBER  */
#line 6839 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addDiagWidth((yyvsp[0].dval));
        }
      }
#line 12930 "def.tab.cpp"
    break;

  case 1092: /* nondefault_layer_option: K_SPACING NUMBER  */
#line 6845 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addSpacing((yyvsp[0].dval));
        }
      }
#line 12940 "def.tab.cpp"
    break;

  case 1093: /* nondefault_layer_option: K_WIREEXT NUMBER  */
#line 6851 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addWireExt((yyvsp[0].dval));
        }
      }
#line 12950 "def.tab.cpp"
    break;

  case 1094: /* $@208: %empty  */
#line 6858 "def.y"
                                    { defData->dumb_mode = DEF_MAX_INT;  }
#line 12956 "def.tab.cpp"
    break;

  case 1095: /* nondefault_prop_opt: '+' K_PROPERTY $@208 nondefault_prop_list  */
#line 6860 "def.y"
      { defData->dumb_mode = 0; }
#line 12962 "def.tab.cpp"
    break;

  case 1098: /* nondefault_prop: T_STRING NUMBER  */
#line 6867 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          char propTp;
          char* str = defData->ringCopy("                       ");
          propTp = defData->session->NDefProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "NONDEFAULTRULE");
          sprintf(str, "%g", (yyvsp[0].dval));
          defData->NonDefault.addNumProperty((yyvsp[-1].string), (yyvsp[0].dval), str, propTp);
        }
      }
#line 12977 "def.tab.cpp"
    break;

  case 1099: /* nondefault_prop: T_STRING QSTRING  */
#line 6878 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          char propTp;
          propTp = defData->session->NDefProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "NONDEFAULTRULE");
          defData->NonDefault.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 12990 "def.tab.cpp"
    break;

  case 1100: /* nondefault_prop: T_STRING T_STRING  */
#line 6887 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          char propTp;
          propTp = defData->session->NDefProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "NONDEFAULTRULE");
          defData->NonDefault.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 13003 "def.tab.cpp"
    break;

  case 1102: /* styles_start: K_STYLES NUMBER ';'  */
#line 6900 "def.y"
      {
        if (defData->VersionNum < 5.6) {
          if (defData->callbacks->StylesStartCbk) {
            if (defData->stylesWarnings++ < defData->settings->StylesWarnings) {
              defData->defMsg = (char*)malloc(1000);
              sprintf (defData->defMsg,
                 "The STYLES statement is available in version 5.6 and later.\nHowever, your DEF file is defined with version %.2f", defData->VersionNum);
              defData->defError(6546, defData->defMsg);
              free(defData->defMsg);
              CHKERR();
            }
          }
        } else if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("STYLES ... END STYLES")) {
                CHKERR();
            }
        } else if (defData->callbacks->StylesStartCbk)
          CALLBACK(defData->callbacks->StylesStartCbk, defrStylesStartCbkType, ROUND((yyvsp[-1].dval)));
      }
#line 13027 "def.tab.cpp"
    break;

  case 1103: /* styles_end: K_END K_STYLES  */
#line 6921 "def.y"
      { if (defData->callbacks->StylesEndCbk)
          CALLBACK(defData->callbacks->StylesEndCbk, defrStylesEndCbkType, 0); }
#line 13034 "def.tab.cpp"
    break;

  case 1106: /* $@209: %empty  */
#line 6929 "def.y"
      {
        if (defData->callbacks->StylesCbk) defData->Styles.setStyle((int)(yyvsp[0].dval));
        defData->Geometries.Reset();
      }
#line 13043 "def.tab.cpp"
    break;

  case 1107: /* styles_rule: '-' K_STYLE NUMBER $@209 firstPt nextPt otherPts ';'  */
#line 6934 "def.y"
      {
        if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond will call the callback
          if (defData->callbacks->StylesCbk) {
            defData->Styles.setPolygon(&defData->Geometries);
            CALLBACK(defData->callbacks->StylesCbk, defrStylesCbkType, &defData->Styles);
          }
        }
      }
#line 13056 "def.tab.cpp"
    break;


#line 13060 "def.tab.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (defData, YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, defData);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, defData);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (defData, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, defData);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, defData);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 6944 "def.y"


END_LEFDEF_PARSER_NAMESPACE
