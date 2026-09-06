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
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1


/* Substitute the variable and function names.  */
#define yyparse         lefyyparse
#define yylex           lefyylex
#define yyerror         lefyyerror
#define yydebug         lefyydebug
#define yynerrs         lefyynerrs
#define yylval          lefyylval
#define yychar          lefyychar

/* First part of user prologue.  */
#line 52 "lef.y"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "lex.h"
#include "lefiDefs.hpp"
#include "lefiUser.hpp"
#include "lefiUtil.hpp"

#include "lefrData.hpp"
#include "lefrCallBacks.hpp"
#include "lefrSettings.hpp"

BEGIN_LEFDEF_PARSER_NAMESPACE

#define LYPROP_ECAP "EDGE_CAPACITANCE"

#define YYINITDEPTH 10000  // pcr 640902 - initialize the yystacksize to 300 
                           // this may need to increase in a design gets 
                           // larger and a polygon has around 300 sizes 
                           // 11/21/2003 - incrreased to 500, design from 
                           // Artisan is greater than 300, need to find a 
                           // way to dynamically increase the size 
                           // 2/10/2004 - increased to 1000 for pcr 686073 
                           // 3/22/2004 - increased to 2000 for pcr 695879 
                           // 9/29/2004 - double the size for pcr 746865 
                           // tried to overwrite the yyoverflow definition 
                           // it is impossible due to the union structure 
                           // 10/03/2006 - increased to 10000 for pcr 913695 

#define YYMAXDEPTH 300000  // 1/24/2008 - increased from 150000 
                           // This value has to be greater than YYINITDEPTH 


// Macro to describe how we handle a callback.
// If the function was set then call it.
// If the function returns non zero then there was an error
// so call the error routine and exit.
#define CALLBACK(func, typ, data) \
    if (func && !lefData->lef_errors) { \
      if (func) { \
        if ((lefData->lefRetVal = (*func)(typ, data, lefSettings->UserData)) == 0) { \
        } else { \
          return lefData->lefRetVal; \
        } \
      } \
    }

#define CHKERR() \
    if (lefData->lef_errors > 20) { \
      lefError(1020, "Too many syntax errors."); \
      lefData->lef_errors = 0; \
      return 1; \
    }

// **********************************************************************
// **********************************************************************

#define C_EQ 0
#define C_NE 1
#define C_LT 2
#define C_LE 3
#define C_GT 4
#define C_GE 5


int comp_str(char *s1, int op, char *s2)
{
    int k = strcmp(s1, s2);
    switch (op) {
        case C_EQ: return k == 0;
        case C_NE: return k != 0;
        case C_GT: return k >  0;
        case C_GE: return k >= 0;
        case C_LT: return k <  0;
        case C_LE: return k <= 0;
        }
    return 0;
}
int comp_num(double s1, int op, double s2)
{
    double k = s1 - s2;
    switch (op) {
        case C_EQ: return k == 0;
        case C_NE: return k != 0;
        case C_GT: return k >  0;
        case C_GE: return k >= 0;
        case C_LT: return k <  0;
        case C_LE: return k <= 0;
        }
    return 0;
}

int validNum(int values) {
    switch (values) {
        case 100:
        case 200:
        case 1000:
        case 2000:
             return 1;
        case 400:
        case 800:
        case 4000:
        case 8000:
        case 10000:
        case 20000:
             if (lefData->versionNum < 5.6) {
                if (lefCallbacks->UnitsCbk) {
                  if (lefData->unitsWarnings++ < lefSettings->UnitsWarnings) {
                    lefData->outMsg = (char*)lefMalloc(10000);
                    sprintf (lefData->outMsg,
                       "Error found when processing LEF file '%s'\nUnit %d is a version 5.6 or later syntax\nYour lef file is defined with version %.2f.",
                    lefData->lefrFileName, values, lefData->versionNum);
                    lefError(1501, lefData->outMsg);
                    lefFree(lefData->outMsg);
                  }
                }
                return 0;
             } else {
                return 1;
             }        
    }
    if (lefData->unitsWarnings++ < lefSettings->UnitsWarnings) {
       lefData->outMsg = (char*)lefMalloc(10000);
       sprintf (lefData->outMsg,
          "The value %d defined for LEF UNITS DATABASE MICRONS is invalid\n. Correct value is 100, 200, 400, 800, 1000, 2000, 4000, 8000, 10000, or 20000", values);
       lefError(1502, lefData->outMsg);
       lefFree(lefData->outMsg);
    }
    CHKERR();
    return 0;
}

int zeroOrGt(double values) {
    if (values < 0)
      return 0;
    return 1;
}


#line 220 "lef.tab.c"

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

#include "lef.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_K_HISTORY = 3,                  /* K_HISTORY  */
  YYSYMBOL_K_ABUT = 4,                     /* K_ABUT  */
  YYSYMBOL_K_ABUTMENT = 5,                 /* K_ABUTMENT  */
  YYSYMBOL_K_ACTIVE = 6,                   /* K_ACTIVE  */
  YYSYMBOL_K_ANALOG = 7,                   /* K_ANALOG  */
  YYSYMBOL_K_ARRAY = 8,                    /* K_ARRAY  */
  YYSYMBOL_K_AREA = 9,                     /* K_AREA  */
  YYSYMBOL_K_BLOCK = 10,                   /* K_BLOCK  */
  YYSYMBOL_K_BOTTOMLEFT = 11,              /* K_BOTTOMLEFT  */
  YYSYMBOL_K_BOTTOMRIGHT = 12,             /* K_BOTTOMRIGHT  */
  YYSYMBOL_K_BY = 13,                      /* K_BY  */
  YYSYMBOL_K_CAPACITANCE = 14,             /* K_CAPACITANCE  */
  YYSYMBOL_K_CAPMULTIPLIER = 15,           /* K_CAPMULTIPLIER  */
  YYSYMBOL_K_CLASS = 16,                   /* K_CLASS  */
  YYSYMBOL_K_CLOCK = 17,                   /* K_CLOCK  */
  YYSYMBOL_K_CLOCKTYPE = 18,               /* K_CLOCKTYPE  */
  YYSYMBOL_K_COLUMNMAJOR = 19,             /* K_COLUMNMAJOR  */
  YYSYMBOL_K_DESIGNRULEWIDTH = 20,         /* K_DESIGNRULEWIDTH  */
  YYSYMBOL_K_INFLUENCE = 21,               /* K_INFLUENCE  */
  YYSYMBOL_K_CORE = 22,                    /* K_CORE  */
  YYSYMBOL_K_CORNER = 23,                  /* K_CORNER  */
  YYSYMBOL_K_COVER = 24,                   /* K_COVER  */
  YYSYMBOL_K_CPERSQDIST = 25,              /* K_CPERSQDIST  */
  YYSYMBOL_K_CURRENT = 26,                 /* K_CURRENT  */
  YYSYMBOL_K_CURRENTSOURCE = 27,           /* K_CURRENTSOURCE  */
  YYSYMBOL_K_CUT = 28,                     /* K_CUT  */
  YYSYMBOL_K_DEFAULT = 29,                 /* K_DEFAULT  */
  YYSYMBOL_K_DATABASE = 30,                /* K_DATABASE  */
  YYSYMBOL_K_DATA = 31,                    /* K_DATA  */
  YYSYMBOL_K_DIELECTRIC = 32,              /* K_DIELECTRIC  */
  YYSYMBOL_K_DIRECTION = 33,               /* K_DIRECTION  */
  YYSYMBOL_K_DO = 34,                      /* K_DO  */
  YYSYMBOL_K_EDGECAPACITANCE = 35,         /* K_EDGECAPACITANCE  */
  YYSYMBOL_K_EEQ = 36,                     /* K_EEQ  */
  YYSYMBOL_K_END = 37,                     /* K_END  */
  YYSYMBOL_K_ENDCAP = 38,                  /* K_ENDCAP  */
  YYSYMBOL_K_FALL = 39,                    /* K_FALL  */
  YYSYMBOL_K_FALLCS = 40,                  /* K_FALLCS  */
  YYSYMBOL_K_FALLT0 = 41,                  /* K_FALLT0  */
  YYSYMBOL_K_FALLSATT1 = 42,               /* K_FALLSATT1  */
  YYSYMBOL_K_FALLRS = 43,                  /* K_FALLRS  */
  YYSYMBOL_K_FALLSATCUR = 44,              /* K_FALLSATCUR  */
  YYSYMBOL_K_FALLTHRESH = 45,              /* K_FALLTHRESH  */
  YYSYMBOL_K_FEEDTHRU = 46,                /* K_FEEDTHRU  */
  YYSYMBOL_K_FIXED = 47,                   /* K_FIXED  */
  YYSYMBOL_K_FOREIGN = 48,                 /* K_FOREIGN  */
  YYSYMBOL_K_FROMPIN = 49,                 /* K_FROMPIN  */
  YYSYMBOL_K_GENERATE = 50,                /* K_GENERATE  */
  YYSYMBOL_K_GENERATOR = 51,               /* K_GENERATOR  */
  YYSYMBOL_K_GROUND = 52,                  /* K_GROUND  */
  YYSYMBOL_K_HEIGHT = 53,                  /* K_HEIGHT  */
  YYSYMBOL_K_HORIZONTAL = 54,              /* K_HORIZONTAL  */
  YYSYMBOL_K_INOUT = 55,                   /* K_INOUT  */
  YYSYMBOL_K_INPUT = 56,                   /* K_INPUT  */
  YYSYMBOL_K_INPUTNOISEMARGIN = 57,        /* K_INPUTNOISEMARGIN  */
  YYSYMBOL_K_COMPONENTPIN = 58,            /* K_COMPONENTPIN  */
  YYSYMBOL_K_INTRINSIC = 59,               /* K_INTRINSIC  */
  YYSYMBOL_K_INVERT = 60,                  /* K_INVERT  */
  YYSYMBOL_K_IRDROP = 61,                  /* K_IRDROP  */
  YYSYMBOL_K_ITERATE = 62,                 /* K_ITERATE  */
  YYSYMBOL_K_IV_TABLES = 63,               /* K_IV_TABLES  */
  YYSYMBOL_K_LAYER = 64,                   /* K_LAYER  */
  YYSYMBOL_K_LEAKAGE = 65,                 /* K_LEAKAGE  */
  YYSYMBOL_K_LEQ = 66,                     /* K_LEQ  */
  YYSYMBOL_K_LIBRARY = 67,                 /* K_LIBRARY  */
  YYSYMBOL_K_MACRO = 68,                   /* K_MACRO  */
  YYSYMBOL_K_MATCH = 69,                   /* K_MATCH  */
  YYSYMBOL_K_MAXDELAY = 70,                /* K_MAXDELAY  */
  YYSYMBOL_K_MAXLOAD = 71,                 /* K_MAXLOAD  */
  YYSYMBOL_K_METALOVERHANG = 72,           /* K_METALOVERHANG  */
  YYSYMBOL_K_MILLIAMPS = 73,               /* K_MILLIAMPS  */
  YYSYMBOL_K_MILLIWATTS = 74,              /* K_MILLIWATTS  */
  YYSYMBOL_K_MINFEATURE = 75,              /* K_MINFEATURE  */
  YYSYMBOL_K_MUSTJOIN = 76,                /* K_MUSTJOIN  */
  YYSYMBOL_K_NAMESCASESENSITIVE = 77,      /* K_NAMESCASESENSITIVE  */
  YYSYMBOL_K_NANOSECONDS = 78,             /* K_NANOSECONDS  */
  YYSYMBOL_K_NETS = 79,                    /* K_NETS  */
  YYSYMBOL_K_NEW = 80,                     /* K_NEW  */
  YYSYMBOL_K_NONDEFAULTRULE = 81,          /* K_NONDEFAULTRULE  */
  YYSYMBOL_K_NONINVERT = 82,               /* K_NONINVERT  */
  YYSYMBOL_K_NONUNATE = 83,                /* K_NONUNATE  */
  YYSYMBOL_K_OBS = 84,                     /* K_OBS  */
  YYSYMBOL_K_OHMS = 85,                    /* K_OHMS  */
  YYSYMBOL_K_OFFSET = 86,                  /* K_OFFSET  */
  YYSYMBOL_K_ORIENTATION = 87,             /* K_ORIENTATION  */
  YYSYMBOL_K_ORIGIN = 88,                  /* K_ORIGIN  */
  YYSYMBOL_K_OUTPUT = 89,                  /* K_OUTPUT  */
  YYSYMBOL_K_OUTPUTNOISEMARGIN = 90,       /* K_OUTPUTNOISEMARGIN  */
  YYSYMBOL_K_OVERHANG = 91,                /* K_OVERHANG  */
  YYSYMBOL_K_OVERLAP = 92,                 /* K_OVERLAP  */
  YYSYMBOL_K_OFF = 93,                     /* K_OFF  */
  YYSYMBOL_K_ON = 94,                      /* K_ON  */
  YYSYMBOL_K_OVERLAPS = 95,                /* K_OVERLAPS  */
  YYSYMBOL_K_PAD = 96,                     /* K_PAD  */
  YYSYMBOL_K_PATH = 97,                    /* K_PATH  */
  YYSYMBOL_K_PATTERN = 98,                 /* K_PATTERN  */
  YYSYMBOL_K_PICOFARADS = 99,              /* K_PICOFARADS  */
  YYSYMBOL_K_PIN = 100,                    /* K_PIN  */
  YYSYMBOL_K_PITCH = 101,                  /* K_PITCH  */
  YYSYMBOL_K_PLACED = 102,                 /* K_PLACED  */
  YYSYMBOL_K_POLYGON = 103,                /* K_POLYGON  */
  YYSYMBOL_K_PORT = 104,                   /* K_PORT  */
  YYSYMBOL_K_POST = 105,                   /* K_POST  */
  YYSYMBOL_K_POWER = 106,                  /* K_POWER  */
  YYSYMBOL_K_PRE = 107,                    /* K_PRE  */
  YYSYMBOL_K_PULLDOWNRES = 108,            /* K_PULLDOWNRES  */
  YYSYMBOL_K_RECT = 109,                   /* K_RECT  */
  YYSYMBOL_K_RESISTANCE = 110,             /* K_RESISTANCE  */
  YYSYMBOL_K_RESISTIVE = 111,              /* K_RESISTIVE  */
  YYSYMBOL_K_RING = 112,                   /* K_RING  */
  YYSYMBOL_K_RISE = 113,                   /* K_RISE  */
  YYSYMBOL_K_RISECS = 114,                 /* K_RISECS  */
  YYSYMBOL_K_RISERS = 115,                 /* K_RISERS  */
  YYSYMBOL_K_RISESATCUR = 116,             /* K_RISESATCUR  */
  YYSYMBOL_K_RISETHRESH = 117,             /* K_RISETHRESH  */
  YYSYMBOL_K_RISESATT1 = 118,              /* K_RISESATT1  */
  YYSYMBOL_K_RISET0 = 119,                 /* K_RISET0  */
  YYSYMBOL_K_RISEVOLTAGETHRESHOLD = 120,   /* K_RISEVOLTAGETHRESHOLD  */
  YYSYMBOL_K_FALLVOLTAGETHRESHOLD = 121,   /* K_FALLVOLTAGETHRESHOLD  */
  YYSYMBOL_K_ROUTING = 122,                /* K_ROUTING  */
  YYSYMBOL_K_ROWMAJOR = 123,               /* K_ROWMAJOR  */
  YYSYMBOL_K_RPERSQ = 124,                 /* K_RPERSQ  */
  YYSYMBOL_K_SAMENET = 125,                /* K_SAMENET  */
  YYSYMBOL_K_SCANUSE = 126,                /* K_SCANUSE  */
  YYSYMBOL_K_SHAPE = 127,                  /* K_SHAPE  */
  YYSYMBOL_K_SHRINKAGE = 128,              /* K_SHRINKAGE  */
  YYSYMBOL_K_SIGNAL = 129,                 /* K_SIGNAL  */
  YYSYMBOL_K_SITE = 130,                   /* K_SITE  */
  YYSYMBOL_K_SIZE = 131,                   /* K_SIZE  */
  YYSYMBOL_K_SOURCE = 132,                 /* K_SOURCE  */
  YYSYMBOL_K_SPACER = 133,                 /* K_SPACER  */
  YYSYMBOL_K_SPACING = 134,                /* K_SPACING  */
  YYSYMBOL_K_SPECIALNETS = 135,            /* K_SPECIALNETS  */
  YYSYMBOL_K_STACK = 136,                  /* K_STACK  */
  YYSYMBOL_K_START = 137,                  /* K_START  */
  YYSYMBOL_K_STEP = 138,                   /* K_STEP  */
  YYSYMBOL_K_STOP = 139,                   /* K_STOP  */
  YYSYMBOL_K_STRUCTURE = 140,              /* K_STRUCTURE  */
  YYSYMBOL_K_SYMMETRY = 141,               /* K_SYMMETRY  */
  YYSYMBOL_K_TABLE = 142,                  /* K_TABLE  */
  YYSYMBOL_K_THICKNESS = 143,              /* K_THICKNESS  */
  YYSYMBOL_K_TIEHIGH = 144,                /* K_TIEHIGH  */
  YYSYMBOL_K_TIELOW = 145,                 /* K_TIELOW  */
  YYSYMBOL_K_TIEOFFR = 146,                /* K_TIEOFFR  */
  YYSYMBOL_K_TIME = 147,                   /* K_TIME  */
  YYSYMBOL_K_TIMING = 148,                 /* K_TIMING  */
  YYSYMBOL_K_TO = 149,                     /* K_TO  */
  YYSYMBOL_K_TOPIN = 150,                  /* K_TOPIN  */
  YYSYMBOL_K_TOPLEFT = 151,                /* K_TOPLEFT  */
  YYSYMBOL_K_TOPRIGHT = 152,               /* K_TOPRIGHT  */
  YYSYMBOL_K_TOPOFSTACKONLY = 153,         /* K_TOPOFSTACKONLY  */
  YYSYMBOL_K_PORTOBS = 154,                /* K_PORTOBS  */
  YYSYMBOL_K_TRISTATE = 155,               /* K_TRISTATE  */
  YYSYMBOL_K_TYPE = 156,                   /* K_TYPE  */
  YYSYMBOL_K_UNATENESS = 157,              /* K_UNATENESS  */
  YYSYMBOL_K_UNITS = 158,                  /* K_UNITS  */
  YYSYMBOL_K_USE = 159,                    /* K_USE  */
  YYSYMBOL_K_VARIABLE = 160,               /* K_VARIABLE  */
  YYSYMBOL_K_VERTICAL = 161,               /* K_VERTICAL  */
  YYSYMBOL_K_VHI = 162,                    /* K_VHI  */
  YYSYMBOL_K_VIA = 163,                    /* K_VIA  */
  YYSYMBOL_K_VIARULE = 164,                /* K_VIARULE  */
  YYSYMBOL_K_VLO = 165,                    /* K_VLO  */
  YYSYMBOL_K_VOLTAGE = 166,                /* K_VOLTAGE  */
  YYSYMBOL_K_VOLTS = 167,                  /* K_VOLTS  */
  YYSYMBOL_K_WIDTH = 168,                  /* K_WIDTH  */
  YYSYMBOL_K_X = 169,                      /* K_X  */
  YYSYMBOL_K_Y = 170,                      /* K_Y  */
  YYSYMBOL_T_STRING = 171,                 /* T_STRING  */
  YYSYMBOL_QSTRING = 172,                  /* QSTRING  */
  YYSYMBOL_NUMBER = 173,                   /* NUMBER  */
  YYSYMBOL_K_N = 174,                      /* K_N  */
  YYSYMBOL_K_S = 175,                      /* K_S  */
  YYSYMBOL_K_E = 176,                      /* K_E  */
  YYSYMBOL_K_W = 177,                      /* K_W  */
  YYSYMBOL_K_FN = 178,                     /* K_FN  */
  YYSYMBOL_K_FS = 179,                     /* K_FS  */
  YYSYMBOL_K_FE = 180,                     /* K_FE  */
  YYSYMBOL_K_FW = 181,                     /* K_FW  */
  YYSYMBOL_K_R0 = 182,                     /* K_R0  */
  YYSYMBOL_K_R90 = 183,                    /* K_R90  */
  YYSYMBOL_K_R180 = 184,                   /* K_R180  */
  YYSYMBOL_K_R270 = 185,                   /* K_R270  */
  YYSYMBOL_K_MX = 186,                     /* K_MX  */
  YYSYMBOL_K_MY = 187,                     /* K_MY  */
  YYSYMBOL_K_MXR90 = 188,                  /* K_MXR90  */
  YYSYMBOL_K_MYR90 = 189,                  /* K_MYR90  */
  YYSYMBOL_K_USER = 190,                   /* K_USER  */
  YYSYMBOL_K_MASTERSLICE = 191,            /* K_MASTERSLICE  */
  YYSYMBOL_K_ENDMACRO = 192,               /* K_ENDMACRO  */
  YYSYMBOL_K_ENDMACROPIN = 193,            /* K_ENDMACROPIN  */
  YYSYMBOL_K_ENDVIARULE = 194,             /* K_ENDVIARULE  */
  YYSYMBOL_K_ENDVIA = 195,                 /* K_ENDVIA  */
  YYSYMBOL_K_ENDLAYER = 196,               /* K_ENDLAYER  */
  YYSYMBOL_K_ENDSITE = 197,                /* K_ENDSITE  */
  YYSYMBOL_K_CANPLACE = 198,               /* K_CANPLACE  */
  YYSYMBOL_K_CANNOTOCCUPY = 199,           /* K_CANNOTOCCUPY  */
  YYSYMBOL_K_TRACKS = 200,                 /* K_TRACKS  */
  YYSYMBOL_K_FLOORPLAN = 201,              /* K_FLOORPLAN  */
  YYSYMBOL_K_GCELLGRID = 202,              /* K_GCELLGRID  */
  YYSYMBOL_K_DEFAULTCAP = 203,             /* K_DEFAULTCAP  */
  YYSYMBOL_K_MINPINS = 204,                /* K_MINPINS  */
  YYSYMBOL_K_WIRECAP = 205,                /* K_WIRECAP  */
  YYSYMBOL_K_STABLE = 206,                 /* K_STABLE  */
  YYSYMBOL_K_SETUP = 207,                  /* K_SETUP  */
  YYSYMBOL_K_HOLD = 208,                   /* K_HOLD  */
  YYSYMBOL_K_DEFINE = 209,                 /* K_DEFINE  */
  YYSYMBOL_K_DEFINES = 210,                /* K_DEFINES  */
  YYSYMBOL_K_DEFINEB = 211,                /* K_DEFINEB  */
  YYSYMBOL_K_IF = 212,                     /* K_IF  */
  YYSYMBOL_K_THEN = 213,                   /* K_THEN  */
  YYSYMBOL_K_ELSE = 214,                   /* K_ELSE  */
  YYSYMBOL_K_FALSE = 215,                  /* K_FALSE  */
  YYSYMBOL_K_TRUE = 216,                   /* K_TRUE  */
  YYSYMBOL_K_EQ = 217,                     /* K_EQ  */
  YYSYMBOL_K_NE = 218,                     /* K_NE  */
  YYSYMBOL_K_LE = 219,                     /* K_LE  */
  YYSYMBOL_K_LT = 220,                     /* K_LT  */
  YYSYMBOL_K_GE = 221,                     /* K_GE  */
  YYSYMBOL_K_GT = 222,                     /* K_GT  */
  YYSYMBOL_K_OR = 223,                     /* K_OR  */
  YYSYMBOL_K_AND = 224,                    /* K_AND  */
  YYSYMBOL_K_NOT = 225,                    /* K_NOT  */
  YYSYMBOL_K_DELAY = 226,                  /* K_DELAY  */
  YYSYMBOL_K_TABLEDIMENSION = 227,         /* K_TABLEDIMENSION  */
  YYSYMBOL_K_TABLEAXIS = 228,              /* K_TABLEAXIS  */
  YYSYMBOL_K_TABLEENTRIES = 229,           /* K_TABLEENTRIES  */
  YYSYMBOL_K_TRANSITIONTIME = 230,         /* K_TRANSITIONTIME  */
  YYSYMBOL_K_EXTENSION = 231,              /* K_EXTENSION  */
  YYSYMBOL_K_PROPDEF = 232,                /* K_PROPDEF  */
  YYSYMBOL_K_STRING = 233,                 /* K_STRING  */
  YYSYMBOL_K_INTEGER = 234,                /* K_INTEGER  */
  YYSYMBOL_K_REAL = 235,                   /* K_REAL  */
  YYSYMBOL_K_RANGE = 236,                  /* K_RANGE  */
  YYSYMBOL_K_PROPERTY = 237,               /* K_PROPERTY  */
  YYSYMBOL_K_VIRTUAL = 238,                /* K_VIRTUAL  */
  YYSYMBOL_K_BUSBITCHARS = 239,            /* K_BUSBITCHARS  */
  YYSYMBOL_K_VERSION = 240,                /* K_VERSION  */
  YYSYMBOL_K_BEGINEXT = 241,               /* K_BEGINEXT  */
  YYSYMBOL_K_ENDEXT = 242,                 /* K_ENDEXT  */
  YYSYMBOL_K_UNIVERSALNOISEMARGIN = 243,   /* K_UNIVERSALNOISEMARGIN  */
  YYSYMBOL_K_EDGERATETHRESHOLD1 = 244,     /* K_EDGERATETHRESHOLD1  */
  YYSYMBOL_K_CORRECTIONTABLE = 245,        /* K_CORRECTIONTABLE  */
  YYSYMBOL_K_EDGERATESCALEFACTOR = 246,    /* K_EDGERATESCALEFACTOR  */
  YYSYMBOL_K_EDGERATETHRESHOLD2 = 247,     /* K_EDGERATETHRESHOLD2  */
  YYSYMBOL_K_VICTIMNOISE = 248,            /* K_VICTIMNOISE  */
  YYSYMBOL_K_NOISETABLE = 249,             /* K_NOISETABLE  */
  YYSYMBOL_K_EDGERATE = 250,               /* K_EDGERATE  */
  YYSYMBOL_K_OUTPUTRESISTANCE = 251,       /* K_OUTPUTRESISTANCE  */
  YYSYMBOL_K_VICTIMLENGTH = 252,           /* K_VICTIMLENGTH  */
  YYSYMBOL_K_CORRECTIONFACTOR = 253,       /* K_CORRECTIONFACTOR  */
  YYSYMBOL_K_OUTPUTPINANTENNASIZE = 254,   /* K_OUTPUTPINANTENNASIZE  */
  YYSYMBOL_K_INPUTPINANTENNASIZE = 255,    /* K_INPUTPINANTENNASIZE  */
  YYSYMBOL_K_INOUTPINANTENNASIZE = 256,    /* K_INOUTPINANTENNASIZE  */
  YYSYMBOL_K_CURRENTDEN = 257,             /* K_CURRENTDEN  */
  YYSYMBOL_K_PWL = 258,                    /* K_PWL  */
  YYSYMBOL_K_ANTENNALENGTHFACTOR = 259,    /* K_ANTENNALENGTHFACTOR  */
  YYSYMBOL_K_TAPERRULE = 260,              /* K_TAPERRULE  */
  YYSYMBOL_K_DIVIDERCHAR = 261,            /* K_DIVIDERCHAR  */
  YYSYMBOL_K_ANTENNASIZE = 262,            /* K_ANTENNASIZE  */
  YYSYMBOL_K_ANTENNAMETALLENGTH = 263,     /* K_ANTENNAMETALLENGTH  */
  YYSYMBOL_K_ANTENNAMETALAREA = 264,       /* K_ANTENNAMETALAREA  */
  YYSYMBOL_K_RISESLEWLIMIT = 265,          /* K_RISESLEWLIMIT  */
  YYSYMBOL_K_FALLSLEWLIMIT = 266,          /* K_FALLSLEWLIMIT  */
  YYSYMBOL_K_FUNCTION = 267,               /* K_FUNCTION  */
  YYSYMBOL_K_BUFFER = 268,                 /* K_BUFFER  */
  YYSYMBOL_K_INVERTER = 269,               /* K_INVERTER  */
  YYSYMBOL_K_NAMEMAPSTRING = 270,          /* K_NAMEMAPSTRING  */
  YYSYMBOL_K_NOWIREEXTENSIONATPIN = 271,   /* K_NOWIREEXTENSIONATPIN  */
  YYSYMBOL_K_WIREEXTENSION = 272,          /* K_WIREEXTENSION  */
  YYSYMBOL_K_MESSAGE = 273,                /* K_MESSAGE  */
  YYSYMBOL_K_CREATEFILE = 274,             /* K_CREATEFILE  */
  YYSYMBOL_K_OPENFILE = 275,               /* K_OPENFILE  */
  YYSYMBOL_K_CLOSEFILE = 276,              /* K_CLOSEFILE  */
  YYSYMBOL_K_WARNING = 277,                /* K_WARNING  */
  YYSYMBOL_K_ERROR = 278,                  /* K_ERROR  */
  YYSYMBOL_K_FATALERROR = 279,             /* K_FATALERROR  */
  YYSYMBOL_K_RECOVERY = 280,               /* K_RECOVERY  */
  YYSYMBOL_K_SKEW = 281,                   /* K_SKEW  */
  YYSYMBOL_K_ANYEDGE = 282,                /* K_ANYEDGE  */
  YYSYMBOL_K_POSEDGE = 283,                /* K_POSEDGE  */
  YYSYMBOL_K_NEGEDGE = 284,                /* K_NEGEDGE  */
  YYSYMBOL_K_SDFCONDSTART = 285,           /* K_SDFCONDSTART  */
  YYSYMBOL_K_SDFCONDEND = 286,             /* K_SDFCONDEND  */
  YYSYMBOL_K_SDFCOND = 287,                /* K_SDFCOND  */
  YYSYMBOL_K_MPWH = 288,                   /* K_MPWH  */
  YYSYMBOL_K_MPWL = 289,                   /* K_MPWL  */
  YYSYMBOL_K_PERIOD = 290,                 /* K_PERIOD  */
  YYSYMBOL_K_ACCURRENTDENSITY = 291,       /* K_ACCURRENTDENSITY  */
  YYSYMBOL_K_DCCURRENTDENSITY = 292,       /* K_DCCURRENTDENSITY  */
  YYSYMBOL_K_AVERAGE = 293,                /* K_AVERAGE  */
  YYSYMBOL_K_PEAK = 294,                   /* K_PEAK  */
  YYSYMBOL_K_RMS = 295,                    /* K_RMS  */
  YYSYMBOL_K_FREQUENCY = 296,              /* K_FREQUENCY  */
  YYSYMBOL_K_CUTAREA = 297,                /* K_CUTAREA  */
  YYSYMBOL_K_MEGAHERTZ = 298,              /* K_MEGAHERTZ  */
  YYSYMBOL_K_USELENGTHTHRESHOLD = 299,     /* K_USELENGTHTHRESHOLD  */
  YYSYMBOL_K_LENGTHTHRESHOLD = 300,        /* K_LENGTHTHRESHOLD  */
  YYSYMBOL_K_ANTENNAINPUTGATEAREA = 301,   /* K_ANTENNAINPUTGATEAREA  */
  YYSYMBOL_K_ANTENNAINOUTDIFFAREA = 302,   /* K_ANTENNAINOUTDIFFAREA  */
  YYSYMBOL_K_ANTENNAOUTPUTDIFFAREA = 303,  /* K_ANTENNAOUTPUTDIFFAREA  */
  YYSYMBOL_K_ANTENNAAREARATIO = 304,       /* K_ANTENNAAREARATIO  */
  YYSYMBOL_K_ANTENNADIFFAREARATIO = 305,   /* K_ANTENNADIFFAREARATIO  */
  YYSYMBOL_K_ANTENNACUMAREARATIO = 306,    /* K_ANTENNACUMAREARATIO  */
  YYSYMBOL_K_ANTENNACUMDIFFAREARATIO = 307, /* K_ANTENNACUMDIFFAREARATIO  */
  YYSYMBOL_K_ANTENNAAREAFACTOR = 308,      /* K_ANTENNAAREAFACTOR  */
  YYSYMBOL_K_ANTENNASIDEAREARATIO = 309,   /* K_ANTENNASIDEAREARATIO  */
  YYSYMBOL_K_ANTENNADIFFSIDEAREARATIO = 310, /* K_ANTENNADIFFSIDEAREARATIO  */
  YYSYMBOL_K_ANTENNACUMSIDEAREARATIO = 311, /* K_ANTENNACUMSIDEAREARATIO  */
  YYSYMBOL_K_ANTENNACUMDIFFSIDEAREARATIO = 312, /* K_ANTENNACUMDIFFSIDEAREARATIO  */
  YYSYMBOL_K_ANTENNASIDEAREAFACTOR = 313,  /* K_ANTENNASIDEAREAFACTOR  */
  YYSYMBOL_K_DIFFUSEONLY = 314,            /* K_DIFFUSEONLY  */
  YYSYMBOL_K_MANUFACTURINGGRID = 315,      /* K_MANUFACTURINGGRID  */
  YYSYMBOL_K_FIXEDMASK = 316,              /* K_FIXEDMASK  */
  YYSYMBOL_K_ANTENNACELL = 317,            /* K_ANTENNACELL  */
  YYSYMBOL_K_CLEARANCEMEASURE = 318,       /* K_CLEARANCEMEASURE  */
  YYSYMBOL_K_EUCLIDEAN = 319,              /* K_EUCLIDEAN  */
  YYSYMBOL_K_MAXXY = 320,                  /* K_MAXXY  */
  YYSYMBOL_K_USEMINSPACING = 321,          /* K_USEMINSPACING  */
  YYSYMBOL_K_ROWMINSPACING = 322,          /* K_ROWMINSPACING  */
  YYSYMBOL_K_ROWABUTSPACING = 323,         /* K_ROWABUTSPACING  */
  YYSYMBOL_K_FLIP = 324,                   /* K_FLIP  */
  YYSYMBOL_K_NONE = 325,                   /* K_NONE  */
  YYSYMBOL_K_ANTENNAPARTIALMETALAREA = 326, /* K_ANTENNAPARTIALMETALAREA  */
  YYSYMBOL_K_ANTENNAPARTIALMETALSIDEAREA = 327, /* K_ANTENNAPARTIALMETALSIDEAREA  */
  YYSYMBOL_K_ANTENNAGATEAREA = 328,        /* K_ANTENNAGATEAREA  */
  YYSYMBOL_K_ANTENNADIFFAREA = 329,        /* K_ANTENNADIFFAREA  */
  YYSYMBOL_K_ANTENNAMAXAREACAR = 330,      /* K_ANTENNAMAXAREACAR  */
  YYSYMBOL_K_ANTENNAMAXSIDEAREACAR = 331,  /* K_ANTENNAMAXSIDEAREACAR  */
  YYSYMBOL_K_ANTENNAPARTIALCUTAREA = 332,  /* K_ANTENNAPARTIALCUTAREA  */
  YYSYMBOL_K_ANTENNAMAXCUTCAR = 333,       /* K_ANTENNAMAXCUTCAR  */
  YYSYMBOL_K_SLOTWIREWIDTH = 334,          /* K_SLOTWIREWIDTH  */
  YYSYMBOL_K_SLOTWIRELENGTH = 335,         /* K_SLOTWIRELENGTH  */
  YYSYMBOL_K_SLOTWIDTH = 336,              /* K_SLOTWIDTH  */
  YYSYMBOL_K_SLOTLENGTH = 337,             /* K_SLOTLENGTH  */
  YYSYMBOL_K_MAXADJACENTSLOTSPACING = 338, /* K_MAXADJACENTSLOTSPACING  */
  YYSYMBOL_K_MAXCOAXIALSLOTSPACING = 339,  /* K_MAXCOAXIALSLOTSPACING  */
  YYSYMBOL_K_MAXEDGESLOTSPACING = 340,     /* K_MAXEDGESLOTSPACING  */
  YYSYMBOL_K_SPLITWIREWIDTH = 341,         /* K_SPLITWIREWIDTH  */
  YYSYMBOL_K_MINIMUMDENSITY = 342,         /* K_MINIMUMDENSITY  */
  YYSYMBOL_K_MAXIMUMDENSITY = 343,         /* K_MAXIMUMDENSITY  */
  YYSYMBOL_K_DENSITYCHECKWINDOW = 344,     /* K_DENSITYCHECKWINDOW  */
  YYSYMBOL_K_DENSITYCHECKSTEP = 345,       /* K_DENSITYCHECKSTEP  */
  YYSYMBOL_K_FILLACTIVESPACING = 346,      /* K_FILLACTIVESPACING  */
  YYSYMBOL_K_MINIMUMCUT = 347,             /* K_MINIMUMCUT  */
  YYSYMBOL_K_ADJACENTCUTS = 348,           /* K_ADJACENTCUTS  */
  YYSYMBOL_K_ANTENNAMODEL = 349,           /* K_ANTENNAMODEL  */
  YYSYMBOL_K_BUMP = 350,                   /* K_BUMP  */
  YYSYMBOL_K_ENCLOSURE = 351,              /* K_ENCLOSURE  */
  YYSYMBOL_K_FROMABOVE = 352,              /* K_FROMABOVE  */
  YYSYMBOL_K_FROMBELOW = 353,              /* K_FROMBELOW  */
  YYSYMBOL_K_IMPLANT = 354,                /* K_IMPLANT  */
  YYSYMBOL_K_LENGTH = 355,                 /* K_LENGTH  */
  YYSYMBOL_K_MAXVIASTACK = 356,            /* K_MAXVIASTACK  */
  YYSYMBOL_K_AREAIO = 357,                 /* K_AREAIO  */
  YYSYMBOL_K_BLACKBOX = 358,               /* K_BLACKBOX  */
  YYSYMBOL_K_MAXWIDTH = 359,               /* K_MAXWIDTH  */
  YYSYMBOL_K_MINENCLOSEDAREA = 360,        /* K_MINENCLOSEDAREA  */
  YYSYMBOL_K_MINSTEP = 361,                /* K_MINSTEP  */
  YYSYMBOL_K_ORIENT = 362,                 /* K_ORIENT  */
  YYSYMBOL_K_OXIDE1 = 363,                 /* K_OXIDE1  */
  YYSYMBOL_K_OXIDE2 = 364,                 /* K_OXIDE2  */
  YYSYMBOL_K_OXIDE3 = 365,                 /* K_OXIDE3  */
  YYSYMBOL_K_OXIDE4 = 366,                 /* K_OXIDE4  */
  YYSYMBOL_K_OXIDE5 = 367,                 /* K_OXIDE5  */
  YYSYMBOL_K_OXIDE6 = 368,                 /* K_OXIDE6  */
  YYSYMBOL_K_OXIDE7 = 369,                 /* K_OXIDE7  */
  YYSYMBOL_K_OXIDE8 = 370,                 /* K_OXIDE8  */
  YYSYMBOL_K_OXIDE9 = 371,                 /* K_OXIDE9  */
  YYSYMBOL_K_OXIDE10 = 372,                /* K_OXIDE10  */
  YYSYMBOL_K_OXIDE11 = 373,                /* K_OXIDE11  */
  YYSYMBOL_K_OXIDE12 = 374,                /* K_OXIDE12  */
  YYSYMBOL_K_OXIDE13 = 375,                /* K_OXIDE13  */
  YYSYMBOL_K_OXIDE14 = 376,                /* K_OXIDE14  */
  YYSYMBOL_K_OXIDE15 = 377,                /* K_OXIDE15  */
  YYSYMBOL_K_OXIDE16 = 378,                /* K_OXIDE16  */
  YYSYMBOL_K_OXIDE17 = 379,                /* K_OXIDE17  */
  YYSYMBOL_K_OXIDE18 = 380,                /* K_OXIDE18  */
  YYSYMBOL_K_OXIDE19 = 381,                /* K_OXIDE19  */
  YYSYMBOL_K_OXIDE20 = 382,                /* K_OXIDE20  */
  YYSYMBOL_K_OXIDE21 = 383,                /* K_OXIDE21  */
  YYSYMBOL_K_OXIDE22 = 384,                /* K_OXIDE22  */
  YYSYMBOL_K_OXIDE23 = 385,                /* K_OXIDE23  */
  YYSYMBOL_K_OXIDE24 = 386,                /* K_OXIDE24  */
  YYSYMBOL_K_OXIDE25 = 387,                /* K_OXIDE25  */
  YYSYMBOL_K_OXIDE26 = 388,                /* K_OXIDE26  */
  YYSYMBOL_K_OXIDE27 = 389,                /* K_OXIDE27  */
  YYSYMBOL_K_OXIDE28 = 390,                /* K_OXIDE28  */
  YYSYMBOL_K_OXIDE29 = 391,                /* K_OXIDE29  */
  YYSYMBOL_K_OXIDE30 = 392,                /* K_OXIDE30  */
  YYSYMBOL_K_OXIDE31 = 393,                /* K_OXIDE31  */
  YYSYMBOL_K_OXIDE32 = 394,                /* K_OXIDE32  */
  YYSYMBOL_K_PARALLELRUNLENGTH = 395,      /* K_PARALLELRUNLENGTH  */
  YYSYMBOL_K_MINWIDTH = 396,               /* K_MINWIDTH  */
  YYSYMBOL_K_PROTRUSIONWIDTH = 397,        /* K_PROTRUSIONWIDTH  */
  YYSYMBOL_K_SPACINGTABLE = 398,           /* K_SPACINGTABLE  */
  YYSYMBOL_K_WITHIN = 399,                 /* K_WITHIN  */
  YYSYMBOL_K_ABOVE = 400,                  /* K_ABOVE  */
  YYSYMBOL_K_BELOW = 401,                  /* K_BELOW  */
  YYSYMBOL_K_CENTERTOCENTER = 402,         /* K_CENTERTOCENTER  */
  YYSYMBOL_K_CUTSIZE = 403,                /* K_CUTSIZE  */
  YYSYMBOL_K_CUTSPACING = 404,             /* K_CUTSPACING  */
  YYSYMBOL_K_DENSITY = 405,                /* K_DENSITY  */
  YYSYMBOL_K_DIAG45 = 406,                 /* K_DIAG45  */
  YYSYMBOL_K_DIAG135 = 407,                /* K_DIAG135  */
  YYSYMBOL_K_MASK = 408,                   /* K_MASK  */
  YYSYMBOL_K_DIAGMINEDGELENGTH = 409,      /* K_DIAGMINEDGELENGTH  */
  YYSYMBOL_K_DIAGSPACING = 410,            /* K_DIAGSPACING  */
  YYSYMBOL_K_DIAGPITCH = 411,              /* K_DIAGPITCH  */
  YYSYMBOL_K_DIAGWIDTH = 412,              /* K_DIAGWIDTH  */
  YYSYMBOL_K_GENERATED = 413,              /* K_GENERATED  */
  YYSYMBOL_K_GROUNDSENSITIVITY = 414,      /* K_GROUNDSENSITIVITY  */
  YYSYMBOL_K_HARDSPACING = 415,            /* K_HARDSPACING  */
  YYSYMBOL_K_INSIDECORNER = 416,           /* K_INSIDECORNER  */
  YYSYMBOL_K_LAYERS = 417,                 /* K_LAYERS  */
  YYSYMBOL_K_LENGTHSUM = 418,              /* K_LENGTHSUM  */
  YYSYMBOL_K_MICRONS = 419,                /* K_MICRONS  */
  YYSYMBOL_K_MINCUTS = 420,                /* K_MINCUTS  */
  YYSYMBOL_K_MINSIZE = 421,                /* K_MINSIZE  */
  YYSYMBOL_K_NETEXPR = 422,                /* K_NETEXPR  */
  YYSYMBOL_K_OUTSIDECORNER = 423,          /* K_OUTSIDECORNER  */
  YYSYMBOL_K_PREFERENCLOSURE = 424,        /* K_PREFERENCLOSURE  */
  YYSYMBOL_K_ROWCOL = 425,                 /* K_ROWCOL  */
  YYSYMBOL_K_ROWPATTERN = 426,             /* K_ROWPATTERN  */
  YYSYMBOL_K_SOFT = 427,                   /* K_SOFT  */
  YYSYMBOL_K_SUPPLYSENSITIVITY = 428,      /* K_SUPPLYSENSITIVITY  */
  YYSYMBOL_K_USEVIA = 429,                 /* K_USEVIA  */
  YYSYMBOL_K_USEVIARULE = 430,             /* K_USEVIARULE  */
  YYSYMBOL_K_WELLTAP = 431,                /* K_WELLTAP  */
  YYSYMBOL_K_ARRAYCUTS = 432,              /* K_ARRAYCUTS  */
  YYSYMBOL_K_ARRAYSPACING = 433,           /* K_ARRAYSPACING  */
  YYSYMBOL_K_ANTENNAAREADIFFREDUCEPWL = 434, /* K_ANTENNAAREADIFFREDUCEPWL  */
  YYSYMBOL_K_ANTENNAAREAMINUSDIFF = 435,   /* K_ANTENNAAREAMINUSDIFF  */
  YYSYMBOL_K_NOROUTE = 436,                /* K_NOROUTE  */
  YYSYMBOL_K_ABSTRACT = 437,               /* K_ABSTRACT  */
  YYSYMBOL_K_ANTENNACUMROUTINGPLUSCUT = 438, /* K_ANTENNACUMROUTINGPLUSCUT  */
  YYSYMBOL_K_ANTENNAGATEPLUSDIFF = 439,    /* K_ANTENNAGATEPLUSDIFF  */
  YYSYMBOL_K_ENDOFLINE = 440,              /* K_ENDOFLINE  */
  YYSYMBOL_K_ENDOFNOTCHWIDTH = 441,        /* K_ENDOFNOTCHWIDTH  */
  YYSYMBOL_K_EXCEPTEXTRACUT = 442,         /* K_EXCEPTEXTRACUT  */
  YYSYMBOL_K_EXCEPTSAMEPGNET = 443,        /* K_EXCEPTSAMEPGNET  */
  YYSYMBOL_K_EXCEPTPGNET = 444,            /* K_EXCEPTPGNET  */
  YYSYMBOL_K_OBSSPACING = 445,             /* K_OBSSPACING  */
  YYSYMBOL_K_FULLDRC = 446,                /* K_FULLDRC  */
  YYSYMBOL_K_MIN = 447,                    /* K_MIN  */
  YYSYMBOL_K_LONGARRAY = 448,              /* K_LONGARRAY  */
  YYSYMBOL_K_MAXEDGES = 449,               /* K_MAXEDGES  */
  YYSYMBOL_K_NOTCHLENGTH = 450,            /* K_NOTCHLENGTH  */
  YYSYMBOL_K_NOTCHSPACING = 451,           /* K_NOTCHSPACING  */
  YYSYMBOL_K_ORTHOGONAL = 452,             /* K_ORTHOGONAL  */
  YYSYMBOL_K_PARALLELEDGE = 453,           /* K_PARALLELEDGE  */
  YYSYMBOL_K_PARALLELOVERLAP = 454,        /* K_PARALLELOVERLAP  */
  YYSYMBOL_K_PGONLY = 455,                 /* K_PGONLY  */
  YYSYMBOL_K_PRL = 456,                    /* K_PRL  */
  YYSYMBOL_K_TWOEDGES = 457,               /* K_TWOEDGES  */
  YYSYMBOL_K_TWOWIDTHS = 458,              /* K_TWOWIDTHS  */
  YYSYMBOL_IF = 459,                       /* IF  */
  YYSYMBOL_LNOT = 460,                     /* LNOT  */
  YYSYMBOL_461_ = 461,                     /* '-'  */
  YYSYMBOL_462_ = 462,                     /* '+'  */
  YYSYMBOL_463_ = 463,                     /* '*'  */
  YYSYMBOL_464_ = 464,                     /* '/'  */
  YYSYMBOL_UMINUS = 465,                   /* UMINUS  */
  YYSYMBOL_466_ = 466,                     /* ';'  */
  YYSYMBOL_467_ = 467,                     /* '('  */
  YYSYMBOL_468_ = 468,                     /* ')'  */
  YYSYMBOL_469_ = 469,                     /* '='  */
  YYSYMBOL_470_n_ = 470,                   /* '\n'  */
  YYSYMBOL_471_ = 471,                     /* '<'  */
  YYSYMBOL_472_ = 472,                     /* '>'  */
  YYSYMBOL_YYACCEPT = 473,                 /* $accept  */
  YYSYMBOL_lef_file = 474,                 /* lef_file  */
  YYSYMBOL_version = 475,                  /* version  */
  YYSYMBOL_476_1 = 476,                    /* $@1  */
  YYSYMBOL_int_number = 477,               /* int_number  */
  YYSYMBOL_dividerchar = 478,              /* dividerchar  */
  YYSYMBOL_busbitchars = 479,              /* busbitchars  */
  YYSYMBOL_rules = 480,                    /* rules  */
  YYSYMBOL_end_library = 481,              /* end_library  */
  YYSYMBOL_rule = 482,                     /* rule  */
  YYSYMBOL_case_sensitivity = 483,         /* case_sensitivity  */
  YYSYMBOL_wireextension = 484,            /* wireextension  */
  YYSYMBOL_fixedmask = 485,                /* fixedmask  */
  YYSYMBOL_manufacturing = 486,            /* manufacturing  */
  YYSYMBOL_useminspacing = 487,            /* useminspacing  */
  YYSYMBOL_clearancemeasure = 488,         /* clearancemeasure  */
  YYSYMBOL_clearance_type = 489,           /* clearance_type  */
  YYSYMBOL_spacing_type = 490,             /* spacing_type  */
  YYSYMBOL_spacing_value = 491,            /* spacing_value  */
  YYSYMBOL_units_section = 492,            /* units_section  */
  YYSYMBOL_start_units = 493,              /* start_units  */
  YYSYMBOL_units_rules = 494,              /* units_rules  */
  YYSYMBOL_units_rule = 495,               /* units_rule  */
  YYSYMBOL_layer_rule = 496,               /* layer_rule  */
  YYSYMBOL_start_layer = 497,              /* start_layer  */
  YYSYMBOL_498_2 = 498,                    /* $@2  */
  YYSYMBOL_end_layer = 499,                /* end_layer  */
  YYSYMBOL_500_3 = 500,                    /* $@3  */
  YYSYMBOL_layer_options = 501,            /* layer_options  */
  YYSYMBOL_layer_option = 502,             /* layer_option  */
  YYSYMBOL_503_4 = 503,                    /* $@4  */
  YYSYMBOL_504_5 = 504,                    /* $@5  */
  YYSYMBOL_505_6 = 505,                    /* $@6  */
  YYSYMBOL_506_7 = 506,                    /* $@7  */
  YYSYMBOL_507_8 = 507,                    /* $@8  */
  YYSYMBOL_508_9 = 508,                    /* $@9  */
  YYSYMBOL_509_10 = 509,                   /* $@10  */
  YYSYMBOL_510_11 = 510,                   /* $@11  */
  YYSYMBOL_511_12 = 511,                   /* $@12  */
  YYSYMBOL_512_13 = 512,                   /* $@13  */
  YYSYMBOL_513_14 = 513,                   /* $@14  */
  YYSYMBOL_514_15 = 514,                   /* $@15  */
  YYSYMBOL_515_16 = 515,                   /* $@16  */
  YYSYMBOL_516_17 = 516,                   /* $@17  */
  YYSYMBOL_517_18 = 517,                   /* $@18  */
  YYSYMBOL_518_19 = 518,                   /* $@19  */
  YYSYMBOL_519_20 = 519,                   /* $@20  */
  YYSYMBOL_520_21 = 520,                   /* $@21  */
  YYSYMBOL_521_22 = 521,                   /* $@22  */
  YYSYMBOL_522_23 = 522,                   /* $@23  */
  YYSYMBOL_523_24 = 523,                   /* $@24  */
  YYSYMBOL_524_25 = 524,                   /* $@25  */
  YYSYMBOL_525_26 = 525,                   /* $@26  */
  YYSYMBOL_526_27 = 526,                   /* $@27  */
  YYSYMBOL_527_28 = 527,                   /* $@28  */
  YYSYMBOL_528_29 = 528,                   /* $@29  */
  YYSYMBOL_layer_arraySpacing_long = 529,  /* layer_arraySpacing_long  */
  YYSYMBOL_layer_arraySpacing_width = 530, /* layer_arraySpacing_width  */
  YYSYMBOL_layer_arraySpacing_arraycuts = 531, /* layer_arraySpacing_arraycuts  */
  YYSYMBOL_layer_arraySpacing_arraycut = 532, /* layer_arraySpacing_arraycut  */
  YYSYMBOL_sp_options = 533,               /* sp_options  */
  YYSYMBOL_534_30 = 534,                   /* $@30  */
  YYSYMBOL_535_31 = 535,                   /* $@31  */
  YYSYMBOL_536_32 = 536,                   /* $@32  */
  YYSYMBOL_537_33 = 537,                   /* $@33  */
  YYSYMBOL_538_34 = 538,                   /* $@34  */
  YYSYMBOL_539_35 = 539,                   /* $@35  */
  YYSYMBOL_540_36 = 540,                   /* $@36  */
  YYSYMBOL_layer_spacingtable_opts = 541,  /* layer_spacingtable_opts  */
  YYSYMBOL_layer_spacingtable_opt = 542,   /* layer_spacingtable_opt  */
  YYSYMBOL_layer_enclosure_type_opt = 543, /* layer_enclosure_type_opt  */
  YYSYMBOL_layer_enclosure_width_opt = 544, /* layer_enclosure_width_opt  */
  YYSYMBOL_545_37 = 545,                   /* $@37  */
  YYSYMBOL_layer_enclosure_width_except_opt = 546, /* layer_enclosure_width_except_opt  */
  YYSYMBOL_layer_preferenclosure_width_opt = 547, /* layer_preferenclosure_width_opt  */
  YYSYMBOL_layer_minimumcut_within = 548,  /* layer_minimumcut_within  */
  YYSYMBOL_layer_minimumcut_from = 549,    /* layer_minimumcut_from  */
  YYSYMBOL_layer_minimumcut_length = 550,  /* layer_minimumcut_length  */
  YYSYMBOL_layer_minstep_options = 551,    /* layer_minstep_options  */
  YYSYMBOL_layer_minstep_option = 552,     /* layer_minstep_option  */
  YYSYMBOL_layer_minstep_type = 553,       /* layer_minstep_type  */
  YYSYMBOL_layer_antenna_pwl = 554,        /* layer_antenna_pwl  */
  YYSYMBOL_555_38 = 555,                   /* $@38  */
  YYSYMBOL_layer_diffusion_ratios = 556,   /* layer_diffusion_ratios  */
  YYSYMBOL_layer_diffusion_ratio = 557,    /* layer_diffusion_ratio  */
  YYSYMBOL_layer_antenna_duo = 558,        /* layer_antenna_duo  */
  YYSYMBOL_layer_table_type = 559,         /* layer_table_type  */
  YYSYMBOL_layer_frequency = 560,          /* layer_frequency  */
  YYSYMBOL_561_39 = 561,                   /* $@39  */
  YYSYMBOL_562_40 = 562,                   /* $@40  */
  YYSYMBOL_563_41 = 563,                   /* $@41  */
  YYSYMBOL_ac_layer_table_opt = 564,       /* ac_layer_table_opt  */
  YYSYMBOL_565_42 = 565,                   /* $@42  */
  YYSYMBOL_566_43 = 566,                   /* $@43  */
  YYSYMBOL_dc_layer_table = 567,           /* dc_layer_table  */
  YYSYMBOL_568_44 = 568,                   /* $@44  */
  YYSYMBOL_int_number_list = 569,          /* int_number_list  */
  YYSYMBOL_number_list = 570,              /* number_list  */
  YYSYMBOL_layer_prop_list = 571,          /* layer_prop_list  */
  YYSYMBOL_layer_prop = 572,               /* layer_prop  */
  YYSYMBOL_current_density_pwl_list = 573, /* current_density_pwl_list  */
  YYSYMBOL_current_density_pwl = 574,      /* current_density_pwl  */
  YYSYMBOL_cap_points = 575,               /* cap_points  */
  YYSYMBOL_cap_point = 576,                /* cap_point  */
  YYSYMBOL_res_points = 577,               /* res_points  */
  YYSYMBOL_res_point = 578,                /* res_point  */
  YYSYMBOL_layer_type = 579,               /* layer_type  */
  YYSYMBOL_layer_direction = 580,          /* layer_direction  */
  YYSYMBOL_layer_minen_width = 581,        /* layer_minen_width  */
  YYSYMBOL_layer_oxide = 582,              /* layer_oxide  */
  YYSYMBOL_layer_sp_parallel_widths = 583, /* layer_sp_parallel_widths  */
  YYSYMBOL_layer_sp_parallel_width = 584,  /* layer_sp_parallel_width  */
  YYSYMBOL_585_45 = 585,                   /* $@45  */
  YYSYMBOL_layer_sp_TwoWidths = 586,       /* layer_sp_TwoWidths  */
  YYSYMBOL_layer_sp_TwoWidth = 587,        /* layer_sp_TwoWidth  */
  YYSYMBOL_588_46 = 588,                   /* $@46  */
  YYSYMBOL_layer_sp_TwoWidthsPRL = 589,    /* layer_sp_TwoWidthsPRL  */
  YYSYMBOL_layer_sp_influence_widths = 590, /* layer_sp_influence_widths  */
  YYSYMBOL_layer_sp_influence_width = 591, /* layer_sp_influence_width  */
  YYSYMBOL_maxstack_via = 592,             /* maxstack_via  */
  YYSYMBOL_593_47 = 593,                   /* $@47  */
  YYSYMBOL_via = 594,                      /* via  */
  YYSYMBOL_595_48 = 595,                   /* $@48  */
  YYSYMBOL_via_keyword = 596,              /* via_keyword  */
  YYSYMBOL_start_via = 597,                /* start_via  */
  YYSYMBOL_via_viarule = 598,              /* via_viarule  */
  YYSYMBOL_599_49 = 599,                   /* $@49  */
  YYSYMBOL_600_50 = 600,                   /* $@50  */
  YYSYMBOL_601_51 = 601,                   /* $@51  */
  YYSYMBOL_via_viarule_options = 602,      /* via_viarule_options  */
  YYSYMBOL_via_viarule_option = 603,       /* via_viarule_option  */
  YYSYMBOL_604_52 = 604,                   /* $@52  */
  YYSYMBOL_via_option = 605,               /* via_option  */
  YYSYMBOL_via_other_options = 606,        /* via_other_options  */
  YYSYMBOL_via_more_options = 607,         /* via_more_options  */
  YYSYMBOL_via_other_option = 608,         /* via_other_option  */
  YYSYMBOL_609_53 = 609,                   /* $@53  */
  YYSYMBOL_via_prop_list = 610,            /* via_prop_list  */
  YYSYMBOL_via_name_value_pair = 611,      /* via_name_value_pair  */
  YYSYMBOL_via_foreign = 612,              /* via_foreign  */
  YYSYMBOL_start_foreign = 613,            /* start_foreign  */
  YYSYMBOL_614_54 = 614,                   /* $@54  */
  YYSYMBOL_orientation = 615,              /* orientation  */
  YYSYMBOL_via_layer_rule = 616,           /* via_layer_rule  */
  YYSYMBOL_via_layer = 617,                /* via_layer  */
  YYSYMBOL_618_55 = 618,                   /* $@55  */
  YYSYMBOL_via_geometries = 619,           /* via_geometries  */
  YYSYMBOL_via_geometry = 620,             /* via_geometry  */
  YYSYMBOL_621_56 = 621,                   /* $@56  */
  YYSYMBOL_end_via = 622,                  /* end_via  */
  YYSYMBOL_623_57 = 623,                   /* $@57  */
  YYSYMBOL_viarule_keyword = 624,          /* viarule_keyword  */
  YYSYMBOL_625_58 = 625,                   /* $@58  */
  YYSYMBOL_viarule = 626,                  /* viarule  */
  YYSYMBOL_viarule_generate = 627,         /* viarule_generate  */
  YYSYMBOL_628_59 = 628,                   /* $@59  */
  YYSYMBOL_viarule_generate_default = 629, /* viarule_generate_default  */
  YYSYMBOL_viarule_layer_list = 630,       /* viarule_layer_list  */
  YYSYMBOL_opt_viarule_props = 631,        /* opt_viarule_props  */
  YYSYMBOL_viarule_props = 632,            /* viarule_props  */
  YYSYMBOL_viarule_prop = 633,             /* viarule_prop  */
  YYSYMBOL_634_60 = 634,                   /* $@60  */
  YYSYMBOL_viarule_prop_list = 635,        /* viarule_prop_list  */
  YYSYMBOL_viarule_layer = 636,            /* viarule_layer  */
  YYSYMBOL_via_names = 637,                /* via_names  */
  YYSYMBOL_via_name = 638,                 /* via_name  */
  YYSYMBOL_viarule_layer_name = 639,       /* viarule_layer_name  */
  YYSYMBOL_640_61 = 640,                   /* $@61  */
  YYSYMBOL_viarule_layer_options = 641,    /* viarule_layer_options  */
  YYSYMBOL_viarule_layer_option = 642,     /* viarule_layer_option  */
  YYSYMBOL_end_viarule = 643,              /* end_viarule  */
  YYSYMBOL_644_62 = 644,                   /* $@62  */
  YYSYMBOL_spacing_rule = 645,             /* spacing_rule  */
  YYSYMBOL_start_spacing = 646,            /* start_spacing  */
  YYSYMBOL_end_spacing = 647,              /* end_spacing  */
  YYSYMBOL_spacings = 648,                 /* spacings  */
  YYSYMBOL_spacing = 649,                  /* spacing  */
  YYSYMBOL_samenet_keyword = 650,          /* samenet_keyword  */
  YYSYMBOL_maskColor = 651,                /* maskColor  */
  YYSYMBOL_irdrop = 652,                   /* irdrop  */
  YYSYMBOL_start_irdrop = 653,             /* start_irdrop  */
  YYSYMBOL_end_irdrop = 654,               /* end_irdrop  */
  YYSYMBOL_ir_tables = 655,                /* ir_tables  */
  YYSYMBOL_ir_table = 656,                 /* ir_table  */
  YYSYMBOL_ir_table_values = 657,          /* ir_table_values  */
  YYSYMBOL_ir_table_value = 658,           /* ir_table_value  */
  YYSYMBOL_ir_tablename = 659,             /* ir_tablename  */
  YYSYMBOL_minfeature = 660,               /* minfeature  */
  YYSYMBOL_dielectric = 661,               /* dielectric  */
  YYSYMBOL_nondefault_rule = 662,          /* nondefault_rule  */
  YYSYMBOL_663_63 = 663,                   /* $@63  */
  YYSYMBOL_664_64 = 664,                   /* $@64  */
  YYSYMBOL_665_65 = 665,                   /* $@65  */
  YYSYMBOL_end_nd_rule = 666,              /* end_nd_rule  */
  YYSYMBOL_nd_hardspacing = 667,           /* nd_hardspacing  */
  YYSYMBOL_nd_rules = 668,                 /* nd_rules  */
  YYSYMBOL_nd_rule = 669,                  /* nd_rule  */
  YYSYMBOL_usevia = 670,                   /* usevia  */
  YYSYMBOL_671_66 = 671,                   /* $@66  */
  YYSYMBOL_useviarule = 672,               /* useviarule  */
  YYSYMBOL_673_67 = 673,                   /* $@67  */
  YYSYMBOL_mincuts = 674,                  /* mincuts  */
  YYSYMBOL_675_68 = 675,                   /* $@68  */
  YYSYMBOL_nd_prop = 676,                  /* nd_prop  */
  YYSYMBOL_677_69 = 677,                   /* $@69  */
  YYSYMBOL_nd_prop_list = 678,             /* nd_prop_list  */
  YYSYMBOL_nd_layer = 679,                 /* nd_layer  */
  YYSYMBOL_680_70 = 680,                   /* $@70  */
  YYSYMBOL_681_71 = 681,                   /* $@71  */
  YYSYMBOL_682_72 = 682,                   /* $@72  */
  YYSYMBOL_683_73 = 683,                   /* $@73  */
  YYSYMBOL_nd_layer_stmts = 684,           /* nd_layer_stmts  */
  YYSYMBOL_nd_layer_stmt = 685,            /* nd_layer_stmt  */
  YYSYMBOL_site = 686,                     /* site  */
  YYSYMBOL_start_site = 687,               /* start_site  */
  YYSYMBOL_688_74 = 688,                   /* $@74  */
  YYSYMBOL_end_site = 689,                 /* end_site  */
  YYSYMBOL_690_75 = 690,                   /* $@75  */
  YYSYMBOL_site_options = 691,             /* site_options  */
  YYSYMBOL_site_option = 692,              /* site_option  */
  YYSYMBOL_site_prop = 693,                /* site_prop  */
  YYSYMBOL_site_class = 694,               /* site_class  */
  YYSYMBOL_site_symmetry_statement = 695,  /* site_symmetry_statement  */
  YYSYMBOL_site_symmetries = 696,          /* site_symmetries  */
  YYSYMBOL_site_symmetry = 697,            /* site_symmetry  */
  YYSYMBOL_site_rowpattern_statement = 698, /* site_rowpattern_statement  */
  YYSYMBOL_699_76 = 699,                   /* $@76  */
  YYSYMBOL_site_rowpatterns = 700,         /* site_rowpatterns  */
  YYSYMBOL_site_rowpattern = 701,          /* site_rowpattern  */
  YYSYMBOL_702_77 = 702,                   /* $@77  */
  YYSYMBOL_pt = 703,                       /* pt  */
  YYSYMBOL_macro = 704,                    /* macro  */
  YYSYMBOL_705_78 = 705,                   /* $@78  */
  YYSYMBOL_start_macro = 706,              /* start_macro  */
  YYSYMBOL_707_79 = 707,                   /* $@79  */
  YYSYMBOL_end_macro = 708,                /* end_macro  */
  YYSYMBOL_709_80 = 709,                   /* $@80  */
  YYSYMBOL_macro_options = 710,            /* macro_options  */
  YYSYMBOL_macro_option = 711,             /* macro_option  */
  YYSYMBOL_712_81 = 712,                   /* $@81  */
  YYSYMBOL_macro_prop_list = 713,          /* macro_prop_list  */
  YYSYMBOL_macro_symmetry_statement = 714, /* macro_symmetry_statement  */
  YYSYMBOL_macro_symmetries = 715,         /* macro_symmetries  */
  YYSYMBOL_macro_symmetry = 716,           /* macro_symmetry  */
  YYSYMBOL_macro_name_value_pair = 717,    /* macro_name_value_pair  */
  YYSYMBOL_macro_class = 718,              /* macro_class  */
  YYSYMBOL_class_type = 719,               /* class_type  */
  YYSYMBOL_pad_type = 720,                 /* pad_type  */
  YYSYMBOL_core_type = 721,                /* core_type  */
  YYSYMBOL_endcap_type = 722,              /* endcap_type  */
  YYSYMBOL_macro_obsspacing = 723,         /* macro_obsspacing  */
  YYSYMBOL_obsspacing_opt = 724,           /* obsspacing_opt  */
  YYSYMBOL_obsspaicing_layers = 725,       /* obsspaicing_layers  */
  YYSYMBOL_obsspaicing_layer = 726,        /* obsspaicing_layer  */
  YYSYMBOL_727_82 = 727,                   /* $@82  */
  YYSYMBOL_macro_generator = 728,          /* macro_generator  */
  YYSYMBOL_macro_generate = 729,           /* macro_generate  */
  YYSYMBOL_macro_source = 730,             /* macro_source  */
  YYSYMBOL_macro_power = 731,              /* macro_power  */
  YYSYMBOL_macro_origin = 732,             /* macro_origin  */
  YYSYMBOL_macro_foreign = 733,            /* macro_foreign  */
  YYSYMBOL_macro_fixedMask = 734,          /* macro_fixedMask  */
  YYSYMBOL_macro_eeq = 735,                /* macro_eeq  */
  YYSYMBOL_736_83 = 736,                   /* $@83  */
  YYSYMBOL_macro_leq = 737,                /* macro_leq  */
  YYSYMBOL_738_84 = 738,                   /* $@84  */
  YYSYMBOL_macro_site = 739,               /* macro_site  */
  YYSYMBOL_macro_site_word = 740,          /* macro_site_word  */
  YYSYMBOL_site_word = 741,                /* site_word  */
  YYSYMBOL_macro_size = 742,               /* macro_size  */
  YYSYMBOL_macro_pin = 743,                /* macro_pin  */
  YYSYMBOL_start_macro_pin = 744,          /* start_macro_pin  */
  YYSYMBOL_745_85 = 745,                   /* $@85  */
  YYSYMBOL_end_macro_pin = 746,            /* end_macro_pin  */
  YYSYMBOL_747_86 = 747,                   /* $@86  */
  YYSYMBOL_macro_pin_options = 748,        /* macro_pin_options  */
  YYSYMBOL_macro_pin_option = 749,         /* macro_pin_option  */
  YYSYMBOL_750_87 = 750,                   /* $@87  */
  YYSYMBOL_751_88 = 751,                   /* $@88  */
  YYSYMBOL_752_89 = 752,                   /* $@89  */
  YYSYMBOL_753_90 = 753,                   /* $@90  */
  YYSYMBOL_754_91 = 754,                   /* $@91  */
  YYSYMBOL_755_92 = 755,                   /* $@92  */
  YYSYMBOL_756_93 = 756,                   /* $@93  */
  YYSYMBOL_757_94 = 757,                   /* $@94  */
  YYSYMBOL_758_95 = 758,                   /* $@95  */
  YYSYMBOL_759_96 = 759,                   /* $@96  */
  YYSYMBOL_760_97 = 760,                   /* $@97  */
  YYSYMBOL_pin_layer_oxide = 761,          /* pin_layer_oxide  */
  YYSYMBOL_pin_prop_list = 762,            /* pin_prop_list  */
  YYSYMBOL_pin_name_value_pair = 763,      /* pin_name_value_pair  */
  YYSYMBOL_electrical_direction = 764,     /* electrical_direction  */
  YYSYMBOL_start_macro_port = 765,         /* start_macro_port  */
  YYSYMBOL_macro_port_class_option = 766,  /* macro_port_class_option  */
  YYSYMBOL_macro_pin_use = 767,            /* macro_pin_use  */
  YYSYMBOL_macro_scan_use = 768,           /* macro_scan_use  */
  YYSYMBOL_pin_shape = 769,                /* pin_shape  */
  YYSYMBOL_geometries = 770,               /* geometries  */
  YYSYMBOL_geometry = 771,                 /* geometry  */
  YYSYMBOL_772_98 = 772,                   /* $@98  */
  YYSYMBOL_773_99 = 773,                   /* $@99  */
  YYSYMBOL_geometry_options = 774,         /* geometry_options  */
  YYSYMBOL_layer_exceptpgnet = 775,        /* layer_exceptpgnet  */
  YYSYMBOL_layer_real_abstract_noroute_opt = 776, /* layer_real_abstract_noroute_opt  */
  YYSYMBOL_real_abstract_noroute = 777,    /* real_abstract_noroute  */
  YYSYMBOL_opt_geometry_props = 778,       /* opt_geometry_props  */
  YYSYMBOL_geometry_props = 779,           /* geometry_props  */
  YYSYMBOL_geometry_prop = 780,            /* geometry_prop  */
  YYSYMBOL_opt_geometry_via_props = 781,   /* opt_geometry_via_props  */
  YYSYMBOL_geometry_via_props = 782,       /* geometry_via_props  */
  YYSYMBOL_geometry_via_prop = 783,        /* geometry_via_prop  */
  YYSYMBOL_prop_name_value = 784,          /* prop_name_value  */
  YYSYMBOL_785_100 = 785,                  /* $@100  */
  YYSYMBOL_prop_name_value_pair = 786,     /* prop_name_value_pair  */
  YYSYMBOL_prop_string_value = 787,        /* prop_string_value  */
  YYSYMBOL_layer_spacing = 788,            /* layer_spacing  */
  YYSYMBOL_firstPt = 789,                  /* firstPt  */
  YYSYMBOL_nextPt = 790,                   /* nextPt  */
  YYSYMBOL_otherPts = 791,                 /* otherPts  */
  YYSYMBOL_via_placement = 792,            /* via_placement  */
  YYSYMBOL_793_101 = 793,                  /* $@101  */
  YYSYMBOL_794_102 = 794,                  /* $@102  */
  YYSYMBOL_stepPattern = 795,              /* stepPattern  */
  YYSYMBOL_sitePattern = 796,              /* sitePattern  */
  YYSYMBOL_trackPattern = 797,             /* trackPattern  */
  YYSYMBOL_798_103 = 798,                  /* $@103  */
  YYSYMBOL_799_104 = 799,                  /* $@104  */
  YYSYMBOL_800_105 = 800,                  /* $@105  */
  YYSYMBOL_801_106 = 801,                  /* $@106  */
  YYSYMBOL_trackLayers = 802,              /* trackLayers  */
  YYSYMBOL_layer_name = 803,               /* layer_name  */
  YYSYMBOL_gcellPattern = 804,             /* gcellPattern  */
  YYSYMBOL_macro_obs = 805,                /* macro_obs  */
  YYSYMBOL_start_macro_obs = 806,          /* start_macro_obs  */
  YYSYMBOL_macro_density = 807,            /* macro_density  */
  YYSYMBOL_density_layers = 808,           /* density_layers  */
  YYSYMBOL_density_layer = 809,            /* density_layer  */
  YYSYMBOL_810_107 = 810,                  /* $@107  */
  YYSYMBOL_811_108 = 811,                  /* $@108  */
  YYSYMBOL_density_layer_rects = 812,      /* density_layer_rects  */
  YYSYMBOL_density_layer_rect = 813,       /* density_layer_rect  */
  YYSYMBOL_macro_clocktype = 814,          /* macro_clocktype  */
  YYSYMBOL_815_109 = 815,                  /* $@109  */
  YYSYMBOL_timing = 816,                   /* timing  */
  YYSYMBOL_start_timing = 817,             /* start_timing  */
  YYSYMBOL_end_timing = 818,               /* end_timing  */
  YYSYMBOL_timing_options = 819,           /* timing_options  */
  YYSYMBOL_timing_option = 820,            /* timing_option  */
  YYSYMBOL_821_110 = 821,                  /* $@110  */
  YYSYMBOL_822_111 = 822,                  /* $@111  */
  YYSYMBOL_823_112 = 823,                  /* $@112  */
  YYSYMBOL_one_pin_trigger = 824,          /* one_pin_trigger  */
  YYSYMBOL_two_pin_trigger = 825,          /* two_pin_trigger  */
  YYSYMBOL_from_pin_trigger = 826,         /* from_pin_trigger  */
  YYSYMBOL_to_pin_trigger = 827,           /* to_pin_trigger  */
  YYSYMBOL_delay_or_transition = 828,      /* delay_or_transition  */
  YYSYMBOL_list_of_table_entries = 829,    /* list_of_table_entries  */
  YYSYMBOL_table_entry = 830,              /* table_entry  */
  YYSYMBOL_list_of_table_axis_dnumbers = 831, /* list_of_table_axis_dnumbers  */
  YYSYMBOL_slew_spec = 832,                /* slew_spec  */
  YYSYMBOL_risefall = 833,                 /* risefall  */
  YYSYMBOL_unateness = 834,                /* unateness  */
  YYSYMBOL_list_of_from_strings = 835,     /* list_of_from_strings  */
  YYSYMBOL_list_of_to_strings = 836,       /* list_of_to_strings  */
  YYSYMBOL_array = 837,                    /* array  */
  YYSYMBOL_838_113 = 838,                  /* $@113  */
  YYSYMBOL_start_array = 839,              /* start_array  */
  YYSYMBOL_840_114 = 840,                  /* $@114  */
  YYSYMBOL_end_array = 841,                /* end_array  */
  YYSYMBOL_842_115 = 842,                  /* $@115  */
  YYSYMBOL_array_rules = 843,              /* array_rules  */
  YYSYMBOL_array_rule = 844,               /* array_rule  */
  YYSYMBOL_845_116 = 845,                  /* $@116  */
  YYSYMBOL_846_117 = 846,                  /* $@117  */
  YYSYMBOL_847_118 = 847,                  /* $@118  */
  YYSYMBOL_848_119 = 848,                  /* $@119  */
  YYSYMBOL_849_120 = 849,                  /* $@120  */
  YYSYMBOL_floorplan_start = 850,          /* floorplan_start  */
  YYSYMBOL_floorplan_list = 851,           /* floorplan_list  */
  YYSYMBOL_floorplan_element = 852,        /* floorplan_element  */
  YYSYMBOL_853_121 = 853,                  /* $@121  */
  YYSYMBOL_854_122 = 854,                  /* $@122  */
  YYSYMBOL_cap_list = 855,                 /* cap_list  */
  YYSYMBOL_one_cap = 856,                  /* one_cap  */
  YYSYMBOL_msg_statement = 857,            /* msg_statement  */
  YYSYMBOL_858_123 = 858,                  /* $@123  */
  YYSYMBOL_create_file_statement = 859,    /* create_file_statement  */
  YYSYMBOL_860_124 = 860,                  /* $@124  */
  YYSYMBOL_dtrm = 861,                     /* dtrm  */
  YYSYMBOL_then = 862,                     /* then  */
  YYSYMBOL_else = 863,                     /* else  */
  YYSYMBOL_expression = 864,               /* expression  */
  YYSYMBOL_b_expr = 865,                   /* b_expr  */
  YYSYMBOL_s_expr = 866,                   /* s_expr  */
  YYSYMBOL_relop = 867,                    /* relop  */
  YYSYMBOL_prop_def_section = 868,         /* prop_def_section  */
  YYSYMBOL_869_125 = 869,                  /* $@125  */
  YYSYMBOL_prop_stmts = 870,               /* prop_stmts  */
  YYSYMBOL_prop_stmt = 871,                /* prop_stmt  */
  YYSYMBOL_872_126 = 872,                  /* $@126  */
  YYSYMBOL_873_127 = 873,                  /* $@127  */
  YYSYMBOL_874_128 = 874,                  /* $@128  */
  YYSYMBOL_875_129 = 875,                  /* $@129  */
  YYSYMBOL_876_130 = 876,                  /* $@130  */
  YYSYMBOL_877_131 = 877,                  /* $@131  */
  YYSYMBOL_878_132 = 878,                  /* $@132  */
  YYSYMBOL_879_133 = 879,                  /* $@133  */
  YYSYMBOL_880_134 = 880,                  /* $@134  */
  YYSYMBOL_881_135 = 881,                  /* $@135  */
  YYSYMBOL_prop_define = 882,              /* prop_define  */
  YYSYMBOL_opt_range_second = 883,         /* opt_range_second  */
  YYSYMBOL_opt_endofline = 884,            /* opt_endofline  */
  YYSYMBOL_885_136 = 885,                  /* $@136  */
  YYSYMBOL_opt_endofline_twoedges = 886,   /* opt_endofline_twoedges  */
  YYSYMBOL_opt_samenetPGonly = 887,        /* opt_samenetPGonly  */
  YYSYMBOL_opt_def_range = 888,            /* opt_def_range  */
  YYSYMBOL_opt_def_value = 889,            /* opt_def_value  */
  YYSYMBOL_opt_def_dvalue = 890,           /* opt_def_dvalue  */
  YYSYMBOL_layer_spacing_opts = 891,       /* layer_spacing_opts  */
  YYSYMBOL_layer_spacing_opt = 892,        /* layer_spacing_opt  */
  YYSYMBOL_893_137 = 893,                  /* $@137  */
  YYSYMBOL_layer_spacing_cut_routing = 894, /* layer_spacing_cut_routing  */
  YYSYMBOL_895_138 = 895,                  /* $@138  */
  YYSYMBOL_896_139 = 896,                  /* $@139  */
  YYSYMBOL_897_140 = 897,                  /* $@140  */
  YYSYMBOL_898_141 = 898,                  /* $@141  */
  YYSYMBOL_899_142 = 899,                  /* $@142  */
  YYSYMBOL_spacing_cut_layer_opt = 900,    /* spacing_cut_layer_opt  */
  YYSYMBOL_opt_adjacentcuts_exceptsame = 901, /* opt_adjacentcuts_exceptsame  */
  YYSYMBOL_opt_layer_name = 902,           /* opt_layer_name  */
  YYSYMBOL_903_143 = 903,                  /* $@143  */
  YYSYMBOL_req_layer_name = 904,           /* req_layer_name  */
  YYSYMBOL_905_144 = 905,                  /* $@144  */
  YYSYMBOL_universalnoisemargin = 906,     /* universalnoisemargin  */
  YYSYMBOL_edgeratethreshold1 = 907,       /* edgeratethreshold1  */
  YYSYMBOL_edgeratethreshold2 = 908,       /* edgeratethreshold2  */
  YYSYMBOL_edgeratescalefactor = 909,      /* edgeratescalefactor  */
  YYSYMBOL_noisetable = 910,               /* noisetable  */
  YYSYMBOL_911_145 = 911,                  /* $@145  */
  YYSYMBOL_end_noisetable = 912,           /* end_noisetable  */
  YYSYMBOL_noise_table_list = 913,         /* noise_table_list  */
  YYSYMBOL_noise_table_entry = 914,        /* noise_table_entry  */
  YYSYMBOL_output_resistance_entry = 915,  /* output_resistance_entry  */
  YYSYMBOL_916_146 = 916,                  /* $@146  */
  YYSYMBOL_num_list = 917,                 /* num_list  */
  YYSYMBOL_victim_list = 918,              /* victim_list  */
  YYSYMBOL_victim = 919,                   /* victim  */
  YYSYMBOL_920_147 = 920,                  /* $@147  */
  YYSYMBOL_vnoiselist = 921,               /* vnoiselist  */
  YYSYMBOL_correctiontable = 922,          /* correctiontable  */
  YYSYMBOL_923_148 = 923,                  /* $@148  */
  YYSYMBOL_end_correctiontable = 924,      /* end_correctiontable  */
  YYSYMBOL_correction_table_list = 925,    /* correction_table_list  */
  YYSYMBOL_correction_table_item = 926,    /* correction_table_item  */
  YYSYMBOL_output_list = 927,              /* output_list  */
  YYSYMBOL_928_149 = 928,                  /* $@149  */
  YYSYMBOL_numo_list = 929,                /* numo_list  */
  YYSYMBOL_corr_victim_list = 930,         /* corr_victim_list  */
  YYSYMBOL_corr_victim = 931,              /* corr_victim  */
  YYSYMBOL_932_150 = 932,                  /* $@150  */
  YYSYMBOL_corr_list = 933,                /* corr_list  */
  YYSYMBOL_input_antenna = 934,            /* input_antenna  */
  YYSYMBOL_output_antenna = 935,           /* output_antenna  */
  YYSYMBOL_inout_antenna = 936,            /* inout_antenna  */
  YYSYMBOL_antenna_input = 937,            /* antenna_input  */
  YYSYMBOL_antenna_inout = 938,            /* antenna_inout  */
  YYSYMBOL_antenna_output = 939,           /* antenna_output  */
  YYSYMBOL_extension_opt = 940,            /* extension_opt  */
  YYSYMBOL_extension = 941                 /* extension  */
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
#define YYFINAL  4
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   2789

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  473
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  469
/* YYNRULES -- Number of rules.  */
#define YYNRULES  1109
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  2138

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   716


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
     470,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     467,   468,   463,   462,     2,   461,     2,   464,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,   466,
     471,   469,   472,     2,     2,     2,     2,     2,     2,     2,
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
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   439,   440,   441,   442,   443,   444,
     445,   446,   447,   448,   449,   450,   451,   452,   453,   454,
     455,   456,   457,   458,   459,   460,   465
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   345,   345,   369,   369,   420,   437,   450,   463,   464,
     465,   469,   475,   485,   485,   485,   485,   486,   486,   486,
     486,   486,   487,   487,   488,   488,   488,   488,   488,   488,
     488,   489,   489,   490,   490,   491,   491,   492,   492,   492,
     493,   493,   494,   494,   494,   494,   494,   495,   495,   495,
     496,   499,   513,   531,   541,   552,   564,   571,   586,   590,
     591,   594,   595,   598,   599,   601,   607,   630,   631,   634,
     636,   638,   640,   642,   644,   646,   653,   656,   663,   663,
     695,   695,   753,   754,   758,   769,   775,   768,   792,   798,
     813,   818,   823,   827,   831,   835,   839,   843,   847,   852,
     869,   868,   905,   904,   922,   936,   948,   959,   971,   982,
     994,  1006,  1018,  1030,  1042,  1055,  1082,  1101,  1119,  1138,
    1138,  1143,  1142,  1157,  1172,  1188,  1211,  1187,  1214,  1237,
    1213,  1241,  1280,  1279,  1318,  1357,  1356,  1396,  1395,  1410,
    1449,  1448,  1487,  1526,  1525,  1565,  1564,  1605,  1604,  1643,
    1664,  1685,  1707,  1728,  1706,  1744,  1768,  1792,  1816,  1840,
    1864,  1888,  1912,  1935,  1953,  1971,  1989,  2007,  2025,  2050,
    2076,  2075,  2093,  2092,  2108,  2107,  2114,  2131,  2130,  2157,
    2156,  2176,  2175,  2193,  2211,  2237,  2236,  2254,  2256,  2262,
    2264,  2270,  2272,  2275,  2292,  2313,  2318,  2324,  2291,  2338,
    2342,  2337,  2366,  2365,  2390,  2392,  2395,  2402,  2403,  2404,
    2406,  2408,  2407,  2414,  2430,  2431,  2447,  2448,  2455,  2456,
    2472,  2473,  2492,  2511,  2512,  2530,  2531,  2534,  2538,  2542,
    2556,  2557,  2558,  2561,  2565,  2564,  2584,  2585,  2589,  2594,
    2595,  2629,  2630,  2631,  2635,  2637,  2640,  2634,  2644,  2646,
    2645,  2660,  2659,  2676,  2675,  2680,  2681,  2684,  2685,  2689,
    2690,  2694,  2702,  2710,  2722,  2724,  2727,  2731,  2732,  2735,
    2739,  2740,  2743,  2747,  2748,  2749,  2750,  2751,  2752,  2755,
    2756,  2757,  2758,  2760,  2761,  2768,  2773,  2778,  2783,  2788,
    2793,  2798,  2803,  2808,  2813,  2818,  2823,  2828,  2833,  2838,
    2843,  2848,  2853,  2858,  2863,  2868,  2873,  2878,  2883,  2888,
    2893,  2898,  2903,  2908,  2913,  2918,  2923,  2930,  2931,  2935,
    2934,  2944,  2945,  2949,  2948,  2959,  2963,  2970,  2971,  2974,
    2977,  3013,  3013,  3040,  3040,  3050,  3053,  3062,  3070,  3079,
    3081,  3084,  3079,  3105,  3106,  3109,  3113,  3117,  3121,  3121,
    3127,  3128,  3130,  3133,  3134,  3138,  3140,  3142,  3144,  3144,
    3147,  3158,  3159,  3163,  3173,  3181,  3191,  3200,  3209,  3218,
    3228,  3228,  3232,  3233,  3234,  3235,  3236,  3237,  3238,  3239,
    3240,  3241,  3242,  3243,  3244,  3245,  3246,  3247,  3249,  3252,
    3252,  3259,  3261,  3265,  3279,  3278,  3302,  3302,  3336,  3336,
    3346,  3365,  3364,  3392,  3393,  3411,  3412,  3415,  3417,  3421,
    3422,  3425,  3425,  3430,  3431,  3435,  3443,  3451,  3462,  3493,
    3495,  3498,  3501,  3501,  3507,  3509,  3513,  3532,  3551,  3587,
    3589,  3592,  3594,  3596,  3626,  3654,  3654,  3688,  3691,  3713,
    3726,  3728,  3731,  3745,  3760,  3766,  3767,  3770,  3773,  3784,
    3793,  3795,  3798,  3806,  3808,  3811,  3814,  3817,  3831,  3842,
    3843,  3853,  3842,  3883,  3888,  3909,  3911,  3930,  3931,  3935,
    3936,  3937,  3938,  3939,  3940,  3941,  3946,  3945,  3968,  3967,
    3992,  3991,  4014,  4014,  4019,  4020,  4024,  4032,  4040,  4051,
    4052,  4061,  4065,  4051,  4107,  4109,  4113,  4118,  4121,  4145,
    4168,  4191,  4210,  4216,  4216,  4226,  4226,  4257,  4259,  4263,
    4269,  4271,  4276,  4278,  4281,  4297,  4298,  4299,  4301,  4304,
    4306,  4310,  4312,  4314,  4317,  4317,  4321,  4323,  4326,  4326,
    4330,  4332,  4336,  4335,  4343,  4343,  4361,  4361,  4383,  4385,
    4389,  4390,  4391,  4392,  4393,  4394,  4395,  4397,  4399,  4401,
    4403,  4404,  4405,  4407,  4409,  4411,  4413,  4415,  4417,  4419,
    4421,  4423,  4423,  4428,  4429,  4432,  4443,  4445,  4449,  4451,
    4453,  4457,  4467,  4475,  4484,  4492,  4493,  4512,  4513,  4514,
    4533,  4552,  4553,  4566,  4567,  4568,  4591,  4592,  4598,  4601,
    4606,  4607,  4608,  4609,  4610,  4611,  4614,  4615,  4616,  4617,
    4634,  4651,  4670,  4671,  4672,  4673,  4674,  4675,  4678,  4688,
    4694,  4700,  4707,  4708,  4711,  4711,  4716,  4719,  4723,  4732,
    4741,  4751,  4761,  4801,  4812,  4823,  4834,  4847,  4857,  4857,
    4860,  4860,  4871,  4882,  4898,  4902,  4905,  4921,  4928,  4928,
    4934,  4934,  4956,  4957,  4961,  4970,  4979,  4988,  4997,  5006,
    5015,  5015,  5024,  5033,  5035,  5037,  5039,  5048,  5057,  5066,
    5075,  5084,  5093,  5102,  5111,  5113,  5113,  5115,  5115,  5124,
    5124,  5133,  5133,  5142,  5151,  5153,  5155,  5164,  5173,  5182,
    5191,  5200,  5209,  5218,  5218,  5220,  5220,  5223,  5237,  5249,
    5270,  5291,  5312,  5314,  5316,  5346,  5376,  5406,  5436,  5466,
    5496,  5526,  5557,  5556,  5586,  5586,  5607,  5607,  5623,  5623,
    5641,  5646,  5651,  5656,  5661,  5666,  5671,  5676,  5681,  5686,
    5691,  5696,  5701,  5706,  5711,  5716,  5721,  5726,  5731,  5736,
    5741,  5746,  5751,  5756,  5761,  5766,  5771,  5776,  5781,  5786,
    5791,  5796,  5803,  5804,  5808,  5818,  5826,  5836,  5837,  5838,
    5839,  5840,  5842,  5854,  5855,  5860,  5861,  5862,  5863,  5864,
    5865,  5868,  5869,  5870,  5871,  5874,  5875,  5876,  5877,  5879,
    5882,  5883,  5882,  5901,  5913,  5934,  5955,  5975,  5995,  6017,
    6038,  6041,  6042,  6044,  6045,  6060,  6061,  6083,  6087,  6091,
    6096,  6097,  6100,  6101,  6110,  6121,  6122,  6125,  6126,  6129,
    6147,  6147,  6157,  6164,  6175,  6179,  6184,  6185,  6199,  6214,
    6218,  6222,  6224,  6228,  6228,  6241,  6241,  6256,  6260,  6271,
    6284,  6292,  6283,  6295,  6303,  6294,  6305,  6314,  6324,  6326,
    6329,  6332,  6341,  6351,  6362,  6374,  6386,  6407,  6408,  6411,
    6412,  6411,  6418,  6419,  6422,  6428,  6428,  6431,  6434,  6437,
    6451,  6453,  6458,  6457,  6468,  6468,  6471,  6470,  6474,  6483,
    6485,  6487,  6489,  6491,  6493,  6495,  6497,  6499,  6501,  6503,
    6505,  6507,  6509,  6511,  6513,  6515,  6517,  6521,  6523,  6525,
    6529,  6531,  6533,  6535,  6539,  6541,  6543,  6547,  6549,  6551,
    6555,  6557,  6561,  6563,  6566,  6570,  6572,  6577,  6578,  6580,
    6585,  6587,  6591,  6593,  6595,  6599,  6601,  6605,  6607,  6611,
    6610,  6620,  6620,  6630,  6630,  6654,  6655,  6659,  6659,  6666,
    6666,  6673,  6673,  6680,  6680,  6686,  6689,  6689,  6695,  6702,
    6707,  6708,  6712,  6712,  6719,  6719,  6729,  6730,  6733,  6737,
    6737,  6741,  6741,  6745,  6746,  6747,  6750,  6751,  6755,  6756,
    6760,  6761,  6762,  6763,  6764,  6765,  6766,  6768,  6771,  6772,
    6773,  6774,  6775,  6776,  6777,  6778,  6779,  6780,  6781,  6782,
    6783,  6785,  6786,  6789,  6795,  6797,  6806,  6810,  6811,  6812,
    6813,  6814,  6815,  6816,  6817,  6818,  6822,  6821,  6834,  6835,
    6839,  6839,  6848,  6848,  6857,  6857,  6867,  6867,  6876,  6876,
    6885,  6885,  6894,  6894,  6903,  6903,  6912,  6912,  6927,  6927,
    6943,  6948,  6953,  6958,  6963,  6971,  6972,  6977,  6984,  6991,
    6999,  7001,  7000,  7009,  7010,  7018,  7019,  7027,  7028,  7033,
    7034,  7039,  7040,  7043,  7045,  7047,  7072,  7071,  7096,  7120,
    7122,  7123,  7122,  7139,  7138,  7163,  7187,  7186,  7192,  7198,
    7206,  7205,  7221,  7235,  7252,  7253,  7261,  7262,  7279,  7280,
    7280,  7285,  7285,  7289,  7303,  7316,  7329,  7343,  7342,  7348,
    7361,  7362,  7366,  7373,  7377,  7376,  7382,  7385,  7390,  7391,
    7395,  7394,  7401,  7404,  7409,  7408,  7415,  7428,  7429,  7433,
    7440,  7444,  7443,  7450,  7453,  7458,  7459,  7464,  7463,  7470,
    7473,  7479,  7503,  7527,  7551,  7585,  7619,  7653,  7654,  7656
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
  "\"end of file\"", "error", "\"invalid token\"", "K_HISTORY", "K_ABUT",
  "K_ABUTMENT", "K_ACTIVE", "K_ANALOG", "K_ARRAY", "K_AREA", "K_BLOCK",
  "K_BOTTOMLEFT", "K_BOTTOMRIGHT", "K_BY", "K_CAPACITANCE",
  "K_CAPMULTIPLIER", "K_CLASS", "K_CLOCK", "K_CLOCKTYPE", "K_COLUMNMAJOR",
  "K_DESIGNRULEWIDTH", "K_INFLUENCE", "K_CORE", "K_CORNER", "K_COVER",
  "K_CPERSQDIST", "K_CURRENT", "K_CURRENTSOURCE", "K_CUT", "K_DEFAULT",
  "K_DATABASE", "K_DATA", "K_DIELECTRIC", "K_DIRECTION", "K_DO",
  "K_EDGECAPACITANCE", "K_EEQ", "K_END", "K_ENDCAP", "K_FALL", "K_FALLCS",
  "K_FALLT0", "K_FALLSATT1", "K_FALLRS", "K_FALLSATCUR", "K_FALLTHRESH",
  "K_FEEDTHRU", "K_FIXED", "K_FOREIGN", "K_FROMPIN", "K_GENERATE",
  "K_GENERATOR", "K_GROUND", "K_HEIGHT", "K_HORIZONTAL", "K_INOUT",
  "K_INPUT", "K_INPUTNOISEMARGIN", "K_COMPONENTPIN", "K_INTRINSIC",
  "K_INVERT", "K_IRDROP", "K_ITERATE", "K_IV_TABLES", "K_LAYER",
  "K_LEAKAGE", "K_LEQ", "K_LIBRARY", "K_MACRO", "K_MATCH", "K_MAXDELAY",
  "K_MAXLOAD", "K_METALOVERHANG", "K_MILLIAMPS", "K_MILLIWATTS",
  "K_MINFEATURE", "K_MUSTJOIN", "K_NAMESCASESENSITIVE", "K_NANOSECONDS",
  "K_NETS", "K_NEW", "K_NONDEFAULTRULE", "K_NONINVERT", "K_NONUNATE",
  "K_OBS", "K_OHMS", "K_OFFSET", "K_ORIENTATION", "K_ORIGIN", "K_OUTPUT",
  "K_OUTPUTNOISEMARGIN", "K_OVERHANG", "K_OVERLAP", "K_OFF", "K_ON",
  "K_OVERLAPS", "K_PAD", "K_PATH", "K_PATTERN", "K_PICOFARADS", "K_PIN",
  "K_PITCH", "K_PLACED", "K_POLYGON", "K_PORT", "K_POST", "K_POWER",
  "K_PRE", "K_PULLDOWNRES", "K_RECT", "K_RESISTANCE", "K_RESISTIVE",
  "K_RING", "K_RISE", "K_RISECS", "K_RISERS", "K_RISESATCUR",
  "K_RISETHRESH", "K_RISESATT1", "K_RISET0", "K_RISEVOLTAGETHRESHOLD",
  "K_FALLVOLTAGETHRESHOLD", "K_ROUTING", "K_ROWMAJOR", "K_RPERSQ",
  "K_SAMENET", "K_SCANUSE", "K_SHAPE", "K_SHRINKAGE", "K_SIGNAL", "K_SITE",
  "K_SIZE", "K_SOURCE", "K_SPACER", "K_SPACING", "K_SPECIALNETS",
  "K_STACK", "K_START", "K_STEP", "K_STOP", "K_STRUCTURE", "K_SYMMETRY",
  "K_TABLE", "K_THICKNESS", "K_TIEHIGH", "K_TIELOW", "K_TIEOFFR", "K_TIME",
  "K_TIMING", "K_TO", "K_TOPIN", "K_TOPLEFT", "K_TOPRIGHT",
  "K_TOPOFSTACKONLY", "K_PORTOBS", "K_TRISTATE", "K_TYPE", "K_UNATENESS",
  "K_UNITS", "K_USE", "K_VARIABLE", "K_VERTICAL", "K_VHI", "K_VIA",
  "K_VIARULE", "K_VLO", "K_VOLTAGE", "K_VOLTS", "K_WIDTH", "K_X", "K_Y",
  "T_STRING", "QSTRING", "NUMBER", "K_N", "K_S", "K_E", "K_W", "K_FN",
  "K_FS", "K_FE", "K_FW", "K_R0", "K_R90", "K_R180", "K_R270", "K_MX",
  "K_MY", "K_MXR90", "K_MYR90", "K_USER", "K_MASTERSLICE", "K_ENDMACRO",
  "K_ENDMACROPIN", "K_ENDVIARULE", "K_ENDVIA", "K_ENDLAYER", "K_ENDSITE",
  "K_CANPLACE", "K_CANNOTOCCUPY", "K_TRACKS", "K_FLOORPLAN", "K_GCELLGRID",
  "K_DEFAULTCAP", "K_MINPINS", "K_WIRECAP", "K_STABLE", "K_SETUP",
  "K_HOLD", "K_DEFINE", "K_DEFINES", "K_DEFINEB", "K_IF", "K_THEN",
  "K_ELSE", "K_FALSE", "K_TRUE", "K_EQ", "K_NE", "K_LE", "K_LT", "K_GE",
  "K_GT", "K_OR", "K_AND", "K_NOT", "K_DELAY", "K_TABLEDIMENSION",
  "K_TABLEAXIS", "K_TABLEENTRIES", "K_TRANSITIONTIME", "K_EXTENSION",
  "K_PROPDEF", "K_STRING", "K_INTEGER", "K_REAL", "K_RANGE", "K_PROPERTY",
  "K_VIRTUAL", "K_BUSBITCHARS", "K_VERSION", "K_BEGINEXT", "K_ENDEXT",
  "K_UNIVERSALNOISEMARGIN", "K_EDGERATETHRESHOLD1", "K_CORRECTIONTABLE",
  "K_EDGERATESCALEFACTOR", "K_EDGERATETHRESHOLD2", "K_VICTIMNOISE",
  "K_NOISETABLE", "K_EDGERATE", "K_OUTPUTRESISTANCE", "K_VICTIMLENGTH",
  "K_CORRECTIONFACTOR", "K_OUTPUTPINANTENNASIZE", "K_INPUTPINANTENNASIZE",
  "K_INOUTPINANTENNASIZE", "K_CURRENTDEN", "K_PWL",
  "K_ANTENNALENGTHFACTOR", "K_TAPERRULE", "K_DIVIDERCHAR", "K_ANTENNASIZE",
  "K_ANTENNAMETALLENGTH", "K_ANTENNAMETALAREA", "K_RISESLEWLIMIT",
  "K_FALLSLEWLIMIT", "K_FUNCTION", "K_BUFFER", "K_INVERTER",
  "K_NAMEMAPSTRING", "K_NOWIREEXTENSIONATPIN", "K_WIREEXTENSION",
  "K_MESSAGE", "K_CREATEFILE", "K_OPENFILE", "K_CLOSEFILE", "K_WARNING",
  "K_ERROR", "K_FATALERROR", "K_RECOVERY", "K_SKEW", "K_ANYEDGE",
  "K_POSEDGE", "K_NEGEDGE", "K_SDFCONDSTART", "K_SDFCONDEND", "K_SDFCOND",
  "K_MPWH", "K_MPWL", "K_PERIOD", "K_ACCURRENTDENSITY",
  "K_DCCURRENTDENSITY", "K_AVERAGE", "K_PEAK", "K_RMS", "K_FREQUENCY",
  "K_CUTAREA", "K_MEGAHERTZ", "K_USELENGTHTHRESHOLD", "K_LENGTHTHRESHOLD",
  "K_ANTENNAINPUTGATEAREA", "K_ANTENNAINOUTDIFFAREA",
  "K_ANTENNAOUTPUTDIFFAREA", "K_ANTENNAAREARATIO",
  "K_ANTENNADIFFAREARATIO", "K_ANTENNACUMAREARATIO",
  "K_ANTENNACUMDIFFAREARATIO", "K_ANTENNAAREAFACTOR",
  "K_ANTENNASIDEAREARATIO", "K_ANTENNADIFFSIDEAREARATIO",
  "K_ANTENNACUMSIDEAREARATIO", "K_ANTENNACUMDIFFSIDEAREARATIO",
  "K_ANTENNASIDEAREAFACTOR", "K_DIFFUSEONLY", "K_MANUFACTURINGGRID",
  "K_FIXEDMASK", "K_ANTENNACELL", "K_CLEARANCEMEASURE", "K_EUCLIDEAN",
  "K_MAXXY", "K_USEMINSPACING", "K_ROWMINSPACING", "K_ROWABUTSPACING",
  "K_FLIP", "K_NONE", "K_ANTENNAPARTIALMETALAREA",
  "K_ANTENNAPARTIALMETALSIDEAREA", "K_ANTENNAGATEAREA",
  "K_ANTENNADIFFAREA", "K_ANTENNAMAXAREACAR", "K_ANTENNAMAXSIDEAREACAR",
  "K_ANTENNAPARTIALCUTAREA", "K_ANTENNAMAXCUTCAR", "K_SLOTWIREWIDTH",
  "K_SLOTWIRELENGTH", "K_SLOTWIDTH", "K_SLOTLENGTH",
  "K_MAXADJACENTSLOTSPACING", "K_MAXCOAXIALSLOTSPACING",
  "K_MAXEDGESLOTSPACING", "K_SPLITWIREWIDTH", "K_MINIMUMDENSITY",
  "K_MAXIMUMDENSITY", "K_DENSITYCHECKWINDOW", "K_DENSITYCHECKSTEP",
  "K_FILLACTIVESPACING", "K_MINIMUMCUT", "K_ADJACENTCUTS",
  "K_ANTENNAMODEL", "K_BUMP", "K_ENCLOSURE", "K_FROMABOVE", "K_FROMBELOW",
  "K_IMPLANT", "K_LENGTH", "K_MAXVIASTACK", "K_AREAIO", "K_BLACKBOX",
  "K_MAXWIDTH", "K_MINENCLOSEDAREA", "K_MINSTEP", "K_ORIENT", "K_OXIDE1",
  "K_OXIDE2", "K_OXIDE3", "K_OXIDE4", "K_OXIDE5", "K_OXIDE6", "K_OXIDE7",
  "K_OXIDE8", "K_OXIDE9", "K_OXIDE10", "K_OXIDE11", "K_OXIDE12",
  "K_OXIDE13", "K_OXIDE14", "K_OXIDE15", "K_OXIDE16", "K_OXIDE17",
  "K_OXIDE18", "K_OXIDE19", "K_OXIDE20", "K_OXIDE21", "K_OXIDE22",
  "K_OXIDE23", "K_OXIDE24", "K_OXIDE25", "K_OXIDE26", "K_OXIDE27",
  "K_OXIDE28", "K_OXIDE29", "K_OXIDE30", "K_OXIDE31", "K_OXIDE32",
  "K_PARALLELRUNLENGTH", "K_MINWIDTH", "K_PROTRUSIONWIDTH",
  "K_SPACINGTABLE", "K_WITHIN", "K_ABOVE", "K_BELOW", "K_CENTERTOCENTER",
  "K_CUTSIZE", "K_CUTSPACING", "K_DENSITY", "K_DIAG45", "K_DIAG135",
  "K_MASK", "K_DIAGMINEDGELENGTH", "K_DIAGSPACING", "K_DIAGPITCH",
  "K_DIAGWIDTH", "K_GENERATED", "K_GROUNDSENSITIVITY", "K_HARDSPACING",
  "K_INSIDECORNER", "K_LAYERS", "K_LENGTHSUM", "K_MICRONS", "K_MINCUTS",
  "K_MINSIZE", "K_NETEXPR", "K_OUTSIDECORNER", "K_PREFERENCLOSURE",
  "K_ROWCOL", "K_ROWPATTERN", "K_SOFT", "K_SUPPLYSENSITIVITY", "K_USEVIA",
  "K_USEVIARULE", "K_WELLTAP", "K_ARRAYCUTS", "K_ARRAYSPACING",
  "K_ANTENNAAREADIFFREDUCEPWL", "K_ANTENNAAREAMINUSDIFF", "K_NOROUTE",
  "K_ABSTRACT", "K_ANTENNACUMROUTINGPLUSCUT", "K_ANTENNAGATEPLUSDIFF",
  "K_ENDOFLINE", "K_ENDOFNOTCHWIDTH", "K_EXCEPTEXTRACUT",
  "K_EXCEPTSAMEPGNET", "K_EXCEPTPGNET", "K_OBSSPACING", "K_FULLDRC",
  "K_MIN", "K_LONGARRAY", "K_MAXEDGES", "K_NOTCHLENGTH", "K_NOTCHSPACING",
  "K_ORTHOGONAL", "K_PARALLELEDGE", "K_PARALLELOVERLAP", "K_PGONLY",
  "K_PRL", "K_TWOEDGES", "K_TWOWIDTHS", "IF", "LNOT", "'-'", "'+'", "'*'",
  "'/'", "UMINUS", "';'", "'('", "')'", "'='", "'\\n'", "'<'", "'>'",
  "$accept", "lef_file", "version", "$@1", "int_number", "dividerchar",
  "busbitchars", "rules", "end_library", "rule", "case_sensitivity",
  "wireextension", "fixedmask", "manufacturing", "useminspacing",
  "clearancemeasure", "clearance_type", "spacing_type", "spacing_value",
  "units_section", "start_units", "units_rules", "units_rule",
  "layer_rule", "start_layer", "$@2", "end_layer", "$@3", "layer_options",
  "layer_option", "$@4", "$@5", "$@6", "$@7", "$@8", "$@9", "$@10", "$@11",
  "$@12", "$@13", "$@14", "$@15", "$@16", "$@17", "$@18", "$@19", "$@20",
  "$@21", "$@22", "$@23", "$@24", "$@25", "$@26", "$@27", "$@28", "$@29",
  "layer_arraySpacing_long", "layer_arraySpacing_width",
  "layer_arraySpacing_arraycuts", "layer_arraySpacing_arraycut",
  "sp_options", "$@30", "$@31", "$@32", "$@33", "$@34", "$@35", "$@36",
  "layer_spacingtable_opts", "layer_spacingtable_opt",
  "layer_enclosure_type_opt", "layer_enclosure_width_opt", "$@37",
  "layer_enclosure_width_except_opt", "layer_preferenclosure_width_opt",
  "layer_minimumcut_within", "layer_minimumcut_from",
  "layer_minimumcut_length", "layer_minstep_options",
  "layer_minstep_option", "layer_minstep_type", "layer_antenna_pwl",
  "$@38", "layer_diffusion_ratios", "layer_diffusion_ratio",
  "layer_antenna_duo", "layer_table_type", "layer_frequency", "$@39",
  "$@40", "$@41", "ac_layer_table_opt", "$@42", "$@43", "dc_layer_table",
  "$@44", "int_number_list", "number_list", "layer_prop_list",
  "layer_prop", "current_density_pwl_list", "current_density_pwl",
  "cap_points", "cap_point", "res_points", "res_point", "layer_type",
  "layer_direction", "layer_minen_width", "layer_oxide",
  "layer_sp_parallel_widths", "layer_sp_parallel_width", "$@45",
  "layer_sp_TwoWidths", "layer_sp_TwoWidth", "$@46",
  "layer_sp_TwoWidthsPRL", "layer_sp_influence_widths",
  "layer_sp_influence_width", "maxstack_via", "$@47", "via", "$@48",
  "via_keyword", "start_via", "via_viarule", "$@49", "$@50", "$@51",
  "via_viarule_options", "via_viarule_option", "$@52", "via_option",
  "via_other_options", "via_more_options", "via_other_option", "$@53",
  "via_prop_list", "via_name_value_pair", "via_foreign", "start_foreign",
  "$@54", "orientation", "via_layer_rule", "via_layer", "$@55",
  "via_geometries", "via_geometry", "$@56", "end_via", "$@57",
  "viarule_keyword", "$@58", "viarule", "viarule_generate", "$@59",
  "viarule_generate_default", "viarule_layer_list", "opt_viarule_props",
  "viarule_props", "viarule_prop", "$@60", "viarule_prop_list",
  "viarule_layer", "via_names", "via_name", "viarule_layer_name", "$@61",
  "viarule_layer_options", "viarule_layer_option", "end_viarule", "$@62",
  "spacing_rule", "start_spacing", "end_spacing", "spacings", "spacing",
  "samenet_keyword", "maskColor", "irdrop", "start_irdrop", "end_irdrop",
  "ir_tables", "ir_table", "ir_table_values", "ir_table_value",
  "ir_tablename", "minfeature", "dielectric", "nondefault_rule", "$@63",
  "$@64", "$@65", "end_nd_rule", "nd_hardspacing", "nd_rules", "nd_rule",
  "usevia", "$@66", "useviarule", "$@67", "mincuts", "$@68", "nd_prop",
  "$@69", "nd_prop_list", "nd_layer", "$@70", "$@71", "$@72", "$@73",
  "nd_layer_stmts", "nd_layer_stmt", "site", "start_site", "$@74",
  "end_site", "$@75", "site_options", "site_option", "site_prop",
  "site_class", "site_symmetry_statement", "site_symmetries",
  "site_symmetry", "site_rowpattern_statement", "$@76", "site_rowpatterns",
  "site_rowpattern", "$@77", "pt", "macro", "$@78", "start_macro", "$@79",
  "end_macro", "$@80", "macro_options", "macro_option", "$@81",
  "macro_prop_list", "macro_symmetry_statement", "macro_symmetries",
  "macro_symmetry", "macro_name_value_pair", "macro_class", "class_type",
  "pad_type", "core_type", "endcap_type", "macro_obsspacing",
  "obsspacing_opt", "obsspaicing_layers", "obsspaicing_layer", "$@82",
  "macro_generator", "macro_generate", "macro_source", "macro_power",
  "macro_origin", "macro_foreign", "macro_fixedMask", "macro_eeq", "$@83",
  "macro_leq", "$@84", "macro_site", "macro_site_word", "site_word",
  "macro_size", "macro_pin", "start_macro_pin", "$@85", "end_macro_pin",
  "$@86", "macro_pin_options", "macro_pin_option", "$@87", "$@88", "$@89",
  "$@90", "$@91", "$@92", "$@93", "$@94", "$@95", "$@96", "$@97",
  "pin_layer_oxide", "pin_prop_list", "pin_name_value_pair",
  "electrical_direction", "start_macro_port", "macro_port_class_option",
  "macro_pin_use", "macro_scan_use", "pin_shape", "geometries", "geometry",
  "$@98", "$@99", "geometry_options", "layer_exceptpgnet",
  "layer_real_abstract_noroute_opt", "real_abstract_noroute",
  "opt_geometry_props", "geometry_props", "geometry_prop",
  "opt_geometry_via_props", "geometry_via_props", "geometry_via_prop",
  "prop_name_value", "$@100", "prop_name_value_pair", "prop_string_value",
  "layer_spacing", "firstPt", "nextPt", "otherPts", "via_placement",
  "$@101", "$@102", "stepPattern", "sitePattern", "trackPattern", "$@103",
  "$@104", "$@105", "$@106", "trackLayers", "layer_name", "gcellPattern",
  "macro_obs", "start_macro_obs", "macro_density", "density_layers",
  "density_layer", "$@107", "$@108", "density_layer_rects",
  "density_layer_rect", "macro_clocktype", "$@109", "timing",
  "start_timing", "end_timing", "timing_options", "timing_option", "$@110",
  "$@111", "$@112", "one_pin_trigger", "two_pin_trigger",
  "from_pin_trigger", "to_pin_trigger", "delay_or_transition",
  "list_of_table_entries", "table_entry", "list_of_table_axis_dnumbers",
  "slew_spec", "risefall", "unateness", "list_of_from_strings",
  "list_of_to_strings", "array", "$@113", "start_array", "$@114",
  "end_array", "$@115", "array_rules", "array_rule", "$@116", "$@117",
  "$@118", "$@119", "$@120", "floorplan_start", "floorplan_list",
  "floorplan_element", "$@121", "$@122", "cap_list", "one_cap",
  "msg_statement", "$@123", "create_file_statement", "$@124", "dtrm",
  "then", "else", "expression", "b_expr", "s_expr", "relop",
  "prop_def_section", "$@125", "prop_stmts", "prop_stmt", "$@126", "$@127",
  "$@128", "$@129", "$@130", "$@131", "$@132", "$@133", "$@134", "$@135",
  "prop_define", "opt_range_second", "opt_endofline", "$@136",
  "opt_endofline_twoedges", "opt_samenetPGonly", "opt_def_range",
  "opt_def_value", "opt_def_dvalue", "layer_spacing_opts",
  "layer_spacing_opt", "$@137", "layer_spacing_cut_routing", "$@138",
  "$@139", "$@140", "$@141", "$@142", "spacing_cut_layer_opt",
  "opt_adjacentcuts_exceptsame", "opt_layer_name", "$@143",
  "req_layer_name", "$@144", "universalnoisemargin", "edgeratethreshold1",
  "edgeratethreshold2", "edgeratescalefactor", "noisetable", "$@145",
  "end_noisetable", "noise_table_list", "noise_table_entry",
  "output_resistance_entry", "$@146", "num_list", "victim_list", "victim",
  "$@147", "vnoiselist", "correctiontable", "$@148", "end_correctiontable",
  "correction_table_list", "correction_table_item", "output_list", "$@149",
  "numo_list", "corr_victim_list", "corr_victim", "$@150", "corr_list",
  "input_antenna", "output_antenna", "inout_antenna", "antenna_input",
  "antenna_inout", "antenna_output", "extension_opt", "extension", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-1600)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-828)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    2232, -1600,   134,  2433, -1600, -1600,   -34, -1600, -1600, -1600,
     -34,   270, -1600, -1600, -1600, -1600, -1600, -1600, -1600,    -1,
   -1600,   -34,   -34,   -34,   -34,   -34,   -34,   -34,   -34,   -34,
     156,   407, -1600, -1600,   -31,    12,    37,   -34,   -90,   -53,
     386,   -34, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   262, -1600,
     485, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600,    56,   284, -1600,    46,   413,   415,   -34,   140,   145,
     448,   462,   465, -1600,   181,   480,   -34,   191,   193,   200,
     223, -1600,   225,   227,   229,   233,   239,   246,   544,   548,
     264,   275,   299,   321, -1600, -1600, -1600,   325, -1600, -1600,
     458,  -151,   586,  2014,    13,   424,   479, -1600,   675, -1600,
   -1600,   144,   215,    25,    31,   557,   727, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,   334, -1600, -1600, -1600, -1600, -1600,
     560, -1600,   350,   368, -1600, -1600, -1600, -1600,   385, -1600,
   -1600, -1600, -1600, -1600, -1600,   390,   400, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,   417, -1600, -1600,   780,   822,   481,
     741,   829,   837,   845,   758,   639, -1600,   769,   918,   -34,
       6,   -34, -1600,   -34,   -34,   -34,   353,   -34,   -34,   -34,
      67,   -34, -1600,   -81,   -34,   -34,   495,   658,   -34, -1600,
     -34, -1600,   -34,   -34, -1600,   -34, -1600,   -34,   -34,   -34,
     -34,   -34,   -34,   -34,   -34,   -34,   -34,   -34,   -34,   -34,
     -34,   -34,   -34, -1600,   179,   -34,   779,   -34,   -34,   -34,
     502,   -34,   -34,   -34,   -34,   -34, -1600,   179, -1600,   501,
     -34,   504,   -34, -1600, -1600, -1600, -1600, -1600, -1600,   -34,
   -1600, -1600, -1600, -1600,   934, -1600, -1600, -1600,   387, -1600,
   -1600, -1600, -1600,   803, -1600,   356,    26,   863, -1600, -1600,
   -1600,   831,   943,   834, -1600, -1600, -1600,   100, -1600,   -34,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,    42,
   -1600, -1600,   838,   842, -1600, -1600,  -101, -1600,   -34, -1600,
     -34,   289, -1600, -1600, -1600,   323,   542,   951,    78,   497,
     980, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600,   850, -1600, -1600, -1600, -1600,
     545, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   876,
   -1600,   -34, -1600,  1016, -1600, -1600, -1600, -1600,   640,   824,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600,   344,   379,  -102,  -102, -1600,   883,   -34,
     -34,   -34, -1600,   -34,   -34,   -34,   -34,   888,   596,   -45,
     598, -1600, -1600, -1600, -1600,   599,   600,   896,   602,  -112,
     -95,   149,   603,   605, -1600,   606, -1600, -1600, -1600, -1600,
   -1600, -1600,   607,   608,   905,   611,   -34,   613,   614,   617,
   -1600, -1600, -1600,   -34,   341,   618,   173,   620,   173, -1600,
     623,   173,   635,   173, -1600,   636,   637,   638,   641,   678,
     679,   680,   681,   682,   683,   685,   -34,   686,   687,   917,
     748, -1600, -1600,   -34,   688, -1600, -1600,   689,   750,   707,
      15,   690,   692,   693,   -55,   694,  -101,   -34,   695,  -101,
     696, -1600,   697,   990,   993,   699,   995,   996, -1600, -1600,
     395, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   -34,   -34,
     702,   597,   430,   675,   703,   626, -1600,  1003,  1138,   -62,
   -1600, -1600,   110,   -34,   -34,  -101,   -34,   -34,   -34,   -34,
   -1600, -1600,  1008, -1600, -1600,   -49,   716,   717,   718,  1015,
    1174,   -93,   723,  1019, -1600,    89,     8, -1600,   841,   450,
      51, -1600, -1600, -1600, -1600,   726,  1022,  1023,  1024,   730,
    1026,   737,  1033,   742,  1198,   746,   747,   749,   -67,  1043,
     751,   752, -1600, -1600, -1600, -1600, -1600, -1600,  1152, -1600,
     753,   644, -1600, -1600,   -41,   754,  1365, -1600, -1600,   813,
     813,   813,    65,   -34,  1185, -1600, -1600,  2489,  1052,  1052,
     470, -1600,   552, -1600,  1052, -1600, -1600,   121,   763, -1600,
   -1600,  1063,  1064,  1065,  1066,  1067,  1069,  1072,  1073,  1074,
    1075,   -34, -1600,    76, -1600, -1600,   -34, -1600,   109, -1600,
   -1600, -1600,    92,  -102,   155,   155,  1076,   782,   784,   785,
     786,   787,   789,   790,   791, -1600,   792,   795, -1600, -1600,
   -1600, -1600, -1600, -1600,   796, -1600,   797,   793,   798, -1600,
   -1600,   -79, -1600, -1600, -1600,   642,  -108, -1600,   799,   -34,
   -1600, -1600, -1600,   804,   962,   -34,  1092,   805, -1600,   802,
   -1600,   808, -1600,   810,   958, -1600,   814, -1600,   815,   958,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600,   816, -1600, -1600,   -34, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600,   817,   -34, -1600,
    1111, -1600, -1600,   -34,   -34,  1116,   -34,  1117,   820, -1600,
   -1600, -1600, -1600,   821, -1600, -1600, -1600,   -34, -1600,  1120,
    -101, -1600, -1600, -1600,   823, -1600,   825,   674,   -88, -1600,
    1119, -1600,   -34, -1600, -1600, -1600,   826,   813,   813, -1600,
     245, -1600, -1600, -1600, -1600,   -62,   827, -1600, -1600, -1600,
     830,   832,   833,   835,  -101,   836,  1281,  1146,   -34,   -34,
   -1600,   -34, -1600, -1600, -1600, -1600, -1600,   -34, -1600, -1600,
   -1600, -1600, -1600, -1600,   677, -1600,   -75, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600,   840,   843,   846, -1600,   847, -1600, -1600, -1600,
     -34, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   700,
     -66, -1600, -1600, -1600,  1126,   266, -1600,   848,  1152, -1600,
   -1600,   851,  1129, -1600,   -34, -1600,   -34,   159,   612, -1600,
     -34,   -34, -1600,  1132,   -34, -1600,   -34,   -34, -1600, -1600,
   -1600,   -34,   -34,   -34,   -34,   -34,   -34,   -34,   525,   128,
     -34,   582,   -34,   -34, -1600, -1600, -1600,   -34,   -34,  1131,
     -34,   -34,  1134,  1135,  1145,  1147,  1148,  1150,  1151,  1153,
   -1600, -1600, -1600, -1600,   -87, -1600, -1600, -1600,   494,  1140,
     -34,   -25,   -23,   -22,   813, -1600,   813,  1082, -1600,   859,
   -1600,   534,  1186, -1600,   -34,   -34,   -34,   -34, -1600, -1600,
     -34,   -34,   -34,   -34, -1600,   562,  1128, -1600, -1600,   -34,
     866,   871, -1600, -1600,  1170,  1171,  1172, -1600, -1600, -1600,
   -1600, -1600,  1118,   652,   302,   -34,   880,   881,   -34,   -34,
     882,   -34,   -34,   885,   133,   886,  1178,  1183, -1600, -1600,
   -1600, -1600,   116,   483,   483,   483,   483,   483,   483,   483,
     483,   483,   483,   889,   -34,  1112,    88, -1600,   890,   -34,
    1109,    88, -1600,    92, -1600, -1600,    92,   -43,    92, -1600,
     738,    60,   806,  -319,  -102, -1600, -1600, -1600, -1600,   899,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   901, -1600,
   -1600, -1600,   902, -1600, -1600, -1600, -1600,    24,   -79, -1600,
   -1600, -1600, -1600, -1600,   -34,   258, -1600,   898, -1600,  1197,
   -1600, -1600, -1600, -1600,  -101, -1600, -1600, -1600,   906, -1600,
   -1600,   907, -1600, -1600, -1600, -1600,   -34,   908,    40,  1203,
    1241,   -34, -1600,   -34, -1600, -1600,   -69, -1600,   -34,   972,
   -1600, -1600,   974, -1600, -1600, -1600, -1600, -1600, -1600,   910,
   -1600, -1600,  -101,  1138, -1600,  -116, -1600,  1209, -1600, -1600,
   -1600, -1600,   915, -1600,   -34,   -34,   916,   -92, -1600,   920,
   -1600, -1600, -1600, -1600,   857, -1600, -1600, -1600, -1600, -1600,
   -1600,   921, -1600, -1600, -1600, -1600, -1600,   922, -1600, -1600,
    1213, -1600, -1600, -1600, -1600,   857,   927,   928,   929,   930,
     931,   937,  -110,  1229,   938,   939,   -34,  1235,   941,  1237,
     945,   946,  1243,   -34,   949,   950,   952,   953,   954,   955,
     957, -1600, -1600, -1600, -1600,   959, -1600, -1600, -1600,   960,
     961, -1600, -1600, -1600, -1600, -1600, -1600,   963,   967,   968,
    1246,   -34,  1253,  1373,  1373,  1373,   976,   977,  1373,  1373,
    1373,  1373,  1374,  1374,  1373,  1374,  1820,  1269,  1272,  1274,
     -65, -1600,   732,    42, -1600,   534, -1600, -1600,  -101, -1600,
    -101,  -101,  -101,  -101,  -101, -1600,  -101, -1600, -1600, -1600,
   -1600,   -34,   -34,   -34,   -34,  1275,   -34,   -34,   -34,   -34,
    1276, -1600, -1600, -1600,   982,   -34, -1600,   -28,   -34,   337,
   -1600, -1600,   983,   984,   985,   -34, -1600, -1600, -1600,   666,
     -34, -1600, -1600,  1295, -1600, -1600,  1420,  1423, -1600,  1424,
    1425, -1600,  1257,   -34, -1600, -1600, -1600, -1600,  1052,  1052,
   -1600,   794, -1600, -1600, -1600, -1600, -1600, -1600,  1426, -1600,
   -1600, -1600, -1600, -1600, -1600,  1289,  1226,  1226,  1293,   999,
    1000,  1001,  1002,  1004,  1006,  1010,  1011,  1012,  1013, -1600,
   -1600,     3, -1600, -1600, -1600, -1600,     9, -1600, -1600,    60,
   -1600,    92,   -43, -1600,   709,    72,   619, -1600, -1600, -1600,
   -1600, -1600, -1600,   -43,   -43,   -43,   -43,   -43,   -43, -1600,
   -1600, -1600,   -43, -1600,    92,    92,    92,    92,  1261,  -102,
    -102,  -102,  -102, -1600, -1600, -1600,   -34,   301, -1600,   -34,
     388, -1600,  1025,  1310, -1600,   -34,   -34,   -34,   -34,   -34,
     -34,  1018, -1600,   -34,  1021, -1600,  1027, -1600, -1600, -1600,
    -101, -1600, -1600,  1089,   -74, -1600, -1600, -1600, -1600,   -34,
   -1600,   -34, -1600, -1600, -1600,   -34,   -34,  1090, -1600,  1034,
   -1600, -1600, -1600,  1326, -1600,   -34, -1600,   -34, -1600,  -101,
    -101, -1600, -1600, -1600, -1600, -1600,  1029,  1030, -1600,  1031,
   -1600, -1600, -1600, -1600, -1600, -1600,  1464, -1600, -1600, -1600,
   -1600, -1600, -1600,  1035, -1600, -1600, -1600, -1600,   -34,  1036,
   -1600,  1037, -1600, -1600,  1038,   -34, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,   828,
     -60, -1600,   -34,  1039, -1600,  1040,  1041,  1042, -1600, -1600,
    1044,  1046,  1047,  1048, -1600,  1049,  1050,  1051,  1053, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600,  1054,  1055,  1056,  1057, -1600,   807, -1600,  1059,  1060,
    1462,  1263, -1600,   -44,  -101,  -101,  -101,  1062, -1600, -1600,
    1068,  1070,  1071,  1077, -1600,   -57,  1078,  1079,  1081,  1083,
   -1600,   -51, -1600,  1301, -1600, -1600,   -34, -1600, -1600, -1600,
   -1600, -1600,   -34, -1600, -1600, -1600,  1291,   -34,   562,   -34,
     -34,   -34,   -34, -1600,  1324,  1084,  1086,  1360, -1600, -1600,
   -1600,   310,  1361,  1362,  1364,  1367, -1600, -1600,   -34,   -34,
    1366, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600,  1290, -1600,  1296, -1600,    92,    60,   286, -1600,
   -1600,   300,   300,   402,   402, -1600, -1600,   300, -1600, -1600,
     659,   486, -1600,   -10,  1095,  1095,  1095,   -34,  1093, -1600,
     -34,  1094, -1600, -1600, -1600, -1600,  1370,   -34,  1322,  1162,
    1163,  1113, -1600, -1600,  1097, -1600, -1600, -1600,    14,    17,
   -1600,   -34,   540,   -34,   -34,  1100, -1600, -1600,  1101, -1600,
     -34,   -34,   -34,   -34,   -34,  1102, -1600,   -98,   -34,  -101,
    1103, -1600, -1600, -1600, -1600,  1454,   -34, -1600,  1104, -1600,
   -1600, -1600,  1105, -1600, -1600, -1600, -1600, -1600,  1108, -1600,
    1404, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  1405, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  1114, -1600,
   -1600, -1600, -1600,  1133,  1263, -1600,     0, -1600,  -101, -1600,
    1544, -1600,  1408,  1410, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600,   -34,   -34,   -34,   -34,
   -1600,  1355,  1445,  1446,  1447,  1448,   -34, -1600, -1600, -1600,
   -1600,  -114,   -34,  1121,  1122, -1600,   -34, -1600, -1600, -1600,
   -1600,   -34,  1290, -1600,   -34,  1296, -1600,   588,    68,   273,
     -43, -1600,  1375,  -102,  1123, -1600,  1124, -1600, -1600, -1600,
     -34,   -34,   -34,   -34, -1600,    18, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,  1238, -1600, -1600, -1600, -1600,  1195,
    1461,  1428, -1600, -1600, -1600, -1600,  1165,  1137, -1600, -1600,
    1139,  -101, -1600,  -101, -1600,  1585, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,  -136, -1600,   -34,  1141, -1600,   -26,
    1142,  1544,  1143,   235,  1136,  1144,   -34,   -34,   -34,   -34,
     -34,   -34,   -34,  1149,  1432, -1600, -1600,  1154, -1600, -1600,
   -1600,  1156, -1600,  1157, -1600,   -43,    92,    80, -1600,  1095,
   -1600, -1600,  1465,    52,   -34, -1600, -1600,  1167, -1600,  1377,
    1377,   -86,   -34,  1158,  1176,   -34,  1160,  1195,   -34,   -34,
   -1600,   -34,  1168,  1165, -1600,  1196, -1600,  -101,  1454,   -34,
   -1600, -1600, -1600,    81, -1600,  1599, -1600,     0, -1600, -1600,
    1173, -1600,  1175, -1600, -1600,   -34,   -34,  1459,   -34,  1177,
    1184, -1600, -1600, -1600,   -34, -1600, -1600, -1600,   300,   432,
   -1600, -1600,   -34,   -34, -1600, -1600, -1600,  1190,  1187,   -34,
     -42,   -34, -1600, -1600, -1600,  1250, -1600,   -34, -1600,  1502,
   -1600, -1600, -1600, -1600,   -34,  1508, -1600, -1600, -1600, -1600,
     -18,   -34, -1600,  1513,   -34,   -34,  1188,   -34,  1189, -1600,
   -1600,  1192,   -34,   -34,   -34,  1589,  1592,  1193,  1409,  1413,
    1427,   -34, -1600, -1600,   -34, -1600, -1600,   -34,  1491,  1436,
   -1600,   -34, -1600,   -34, -1600, -1600,  1498,   -34,  1496, -1600,
    1202,   -34, -1600, -1600, -1600,  1531, -1600, -1600,   -34,   -34,
    1204, -1600, -1600, -1600,   -34,   -34,   -34, -1600,  1273, -1600,
   -1600,  1501, -1600, -1600, -1600,  1503,   -34,   -34, -1600,  1498,
   -1600,  1504, -1600,   -34,   -34,   -34,  1210, -1600, -1600, -1600,
   -1600, -1600,    19, -1600,    33,   -34,   -34, -1600, -1600, -1600,
      38,   -34, -1600, -1600,  1034, -1600,  1506, -1600,   -34,   -34,
   -1600,  1507,  1507,    34, -1600, -1600, -1600, -1600, -1600, -1600,
      45,    47, -1600, -1600,  1280,  1512,   -34,  1215, -1600,   -34,
   -1600, -1600,  1657,   -34, -1600,  1559,   -34,   -34,   -34, -1600,
    1227, -1600, -1600,    71,   -34,   -34, -1600, -1600,  1282, -1600,
     -34,  1219,  1516,   -34,  1222,  1223,  1224, -1600, -1600, -1600,
    1565, -1600, -1600,   -34,  1234, -1600, -1600,  1236, -1600, -1600,
   -1600,   -34, -1600,   -34,   -34, -1600, -1600, -1600,   -34,  1239,
    1350,   -34,   -34,   -34,   -34,  1240, -1600, -1600,   -30,   -34,
     -34, -1600,   -34, -1600,   -34,   -34,  1532,   -34,   -34,  1242,
    1244,  1245,   -34, -1600, -1600, -1600,  1247, -1600
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int16 yydefact[] =
{
       0,    10,     0,  1107,     1,   911,     0,   448,    78,   534,
       0,     0,   459,   503,   438,    66,   335,   398,   986,     0,
       3,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   939,   941,     0,     0,     0,     0,     0,     0,
       0,     0,    13,    21,    14,     9,    15,    22,    46,    45,
      47,    48,    16,    67,    17,    82,    49,    18,     0,   333,
       0,    19,    20,    24,   440,    27,   450,    26,    25,    31,
      28,   507,    29,   538,    30,   915,    23,    50,    32,    33,
      34,    36,    35,    37,    38,    39,    40,    41,    42,    43,
      44,    11,     0,     5,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   988,     0,     0,     0,     0,     0,     0,
       0,  1067,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    55,    60,    59,     0,    61,    62,
       0,     0,     0,     0,   336,     0,   403,   422,   419,   405,
     424,     0,     0,     0,   532,   909,     0,  1109,     2,  1108,
     912,   458,    79,   535,     0,    52,    51,   460,   504,   399,
       0,     7,     0,     0,  1064,  1084,  1066,  1065,     0,  1102,
    1101,  1103,     6,    54,    53,     0,     0,  1104,  1105,  1106,
      56,    58,    64,    63,     0,   331,   330,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    68,     0,     0,     0,
       0,     0,    80,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   119,     0,     0,     0,     0,     0,     0,   132,
       0,   135,     0,     0,   140,     0,   143,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   147,   207,     0,     0,     0,     0,     0,
     177,     0,     0,     0,     0,     0,   185,   207,    85,     0,
       0,     0,     0,    77,    83,   337,   338,   370,   389,     0,
     360,   339,   358,   350,     0,   351,   353,   355,     0,   356,
     391,   404,   401,     0,   406,   407,   418,     0,   444,   437,
     441,     0,     0,     0,   447,   451,   453,     0,   505,     0,
     519,   800,   524,   502,   508,   513,   511,   510,   512,     0,
     845,   628,     0,     0,   630,   835,     0,   638,     0,   634,
       0,     0,   566,   848,   561,     0,     0,     0,     0,     0,
       0,   539,   545,   540,   541,   542,   543,   544,   548,   547,
     549,   546,   550,   551,   553,     0,   552,   554,   642,   557,
       0,   558,   559,   560,   850,   635,   919,   921,   923,     0,
     926,     0,   917,     0,   916,   930,    12,   457,   465,     0,
     992,  1002,   990,   996,  1004,   994,  1008,  1006,   998,  1000,
     989,     4,  1063,     0,     0,     0,     0,    57,     0,     0,
       0,     0,    65,     0,     0,     0,     0,     0,     0,     0,
       0,   279,   280,   281,   282,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   100,     0,   274,   275,   273,   276,
     277,   278,     0,     0,     0,     0,     0,     0,     0,     0,
     242,   241,   243,   121,     0,     0,     0,     0,     0,   137,
       0,     0,     0,     0,   145,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   208,   209,     0,     0,   170,   174,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   187,     0,
       0,   149,     0,     0,     0,     0,     0,     0,   396,   334,
     352,   372,   374,   375,   373,   376,   378,   379,   377,   380,
     381,   382,   383,   386,   384,   387,   385,   366,     0,     0,
       0,     0,   388,     0,     0,     0,   411,     0,     0,   408,
     409,   420,     0,     0,     0,     0,     0,     0,     0,     0,
     425,   439,     0,   449,   456,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   526,   578,   586,   587,   575,     0,
     583,   577,   584,   581,   582,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   627,   839,   837,   611,   609,   610,   612,   623,
       0,     0,   536,   533,     0,     0,     0,   834,   770,   445,
     445,   445,   795,     0,     0,   781,   780,     0,     0,     0,
       0,   929,     0,   936,     0,   913,   910,     0,     0,   467,
     987,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,  1091,     0,  1087,  1090,     0,  1074,     0,  1070,
    1073,   976,     0,     0,   943,   943,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    99,     0,     0,   113,   104,
     114,    81,   109,    94,     0,    90,     0,     0,     0,   183,
     112,  1033,   111,    88,    98,     0,     0,   259,     0,     0,
     116,   115,   110,     0,     0,     0,     0,     0,   131,     0,
     233,     0,   134,     0,   239,   139,     0,   142,     0,   239,
      84,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,     0,   166,   167,     0,   285,   286,   287,   288,   289,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   313,   314,   315,   316,     0,     0,   168,
     283,   225,   169,     0,     0,     0,     0,     0,     0,    89,
     184,    97,    92,     0,    96,   809,   811,     0,   188,   189,
       0,   151,   150,   371,     0,   357,     0,     0,     0,   361,
       0,   354,     0,   530,   369,   367,     0,   445,   445,   392,
     407,   423,   415,   416,   417,     0,     0,   435,   400,   410,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     452,     0,   454,   516,   515,   517,   506,     0,   521,   522,
     523,   518,   520,   514,     0,   801,     0,   579,   580,   596,
     599,   597,   598,   600,   601,   588,   576,   606,   607,   603,
     602,   604,   605,   589,   592,   590,   591,   593,   594,   595,
     585,   574,     0,     0,     0,   616,     0,   622,   639,   621,
       0,   620,   619,   618,   568,   569,   570,   565,   567,     0,
       0,   563,   555,   556,     0,     0,   614,     0,   612,   626,
     624,     0,     0,   632,     0,   633,     0,     0,     0,   640,
       0,     0,   671,     0,     0,   650,     0,     0,   665,   667,
     752,     0,     0,     0,     0,     0,     0,     0,     0,   765,
       0,     0,     0,     0,   685,   669,   683,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     702,   708,   704,   706,     0,   637,   643,   653,   753,     0,
       0,     0,     0,     0,   445,   800,   445,   796,   797,     0,
     833,   769,     0,   901,     0,     0,     0,     0,   852,   900,
       0,     0,     0,     0,   854,     0,     0,   880,   881,     0,
       0,     0,   882,   883,     0,     0,     0,   877,   878,   879,
     847,   851,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   932,   934,
     931,   466,   461,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   943,  1088,     0,     0,
       0,   943,  1071,     0,   972,   971,     0,     0,     0,   957,
       0,     0,     0,     0,     0,   944,   945,   940,   942,     0,
      70,    73,    75,    72,    71,    69,    74,    76,     0,   107,
      95,    91,     0,   105,  1036,  1035,  1038,  1039,  1033,   261,
     262,   263,   120,   260,     0,     0,   264,     0,   123,     0,
     122,   128,   125,   124,     0,   133,   136,   240,     0,   141,
     144,     0,   165,   172,   148,   179,     0,     0,     0,     0,
       0,     0,   194,     0,   178,    93,     0,   181,     0,     0,
     152,   390,     0,   365,   364,   363,   359,   362,   397,     0,
     368,   394,     0,     0,   413,     0,   421,     0,   426,   427,
     434,   433,     0,   432,     0,     0,     0,     0,   455,     0,
     804,   805,   803,   802,     0,   525,   527,   846,   629,   617,
     631,     0,   573,   572,   571,   562,   564,     0,   836,   838,
       0,   608,   613,   625,   537,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   761,   762,   763,   764,     0,   766,   768,   767,     0,
       0,   756,   759,   760,   758,   757,   755,     0,     0,     0,
       0,     0,     0,  1058,  1058,  1058,     0,     0,  1058,  1058,
    1058,  1058,     0,     0,  1058,     0,     0,     0,     0,     0,
       0,   644,     0,     0,   688,     0,   771,   446,     0,   811,
       0,     0,     0,     0,     0,   799,     0,   798,   773,   782,
     849,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   902,   903,   904,     0,     0,   895,     0,     0,     0,
     892,   876,     0,     0,     0,     0,   884,   885,   886,     0,
       0,   890,   891,     0,   920,   922,     0,     0,   924,     0,
       0,   927,     0,     0,   937,   918,   914,   925,     0,     0,
     489,     0,   482,   480,   476,   478,   470,   471,     0,   468,
     473,   474,   475,   472,   469,  1012,  1027,  1027,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,  1089,
    1093,     0,  1086,  1085,  1072,  1076,     0,  1069,  1068,     0,
     968,     0,     0,   954,     0,     0,     0,   981,   982,   977,
     978,   979,   980,     0,     0,     0,     0,     0,     0,   983,
     984,   985,     0,   946,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   974,   973,   332,     0,     0,   267,     0,
       0,   270,  1025,     0,  1040,     0,     0,     0,     0,     0,
       0,     0,  1034,     0,     0,   265,     0,   244,   255,   257,
       0,   138,   146,   218,   210,   284,   171,   232,   230,     0,
     231,     0,   175,   226,   227,     0,     0,     0,   255,   325,
     186,   810,   812,   216,   190,     0,   236,     0,   531,     0,
       0,   402,   412,   414,   436,   430,     0,     0,   428,     0,
     442,   509,   528,   636,   840,   615,   819,   673,   678,   679,
     751,   750,   747,     0,   748,   641,   660,   658,     0,     0,
     656,     0,   674,   675,     0,     0,   652,   677,   676,   659,
     657,   680,   681,   655,   664,   663,   654,   662,   661,     0,
       0,   742,     0,     0,  1059,     0,     0,     0,   692,   693,
       0,     0,     0,     0,  1061,     0,     0,     0,     0,   710,
     711,   712,   713,   714,   715,   716,   717,   718,   719,   720,
     721,   722,   723,   724,   725,   726,   727,   728,   729,   730,
     731,   732,   733,   734,   735,   736,   737,   738,   739,   740,
     741,     0,     0,     0,     0,   647,     0,   645,     0,     0,
       0,   790,   811,     0,     0,     0,     0,     0,   815,   813,
       0,     0,     0,     0,   905,     0,     0,     0,     0,     0,
     907,     0,   869,     0,   859,   896,     0,   860,   893,   873,
     874,   875,     0,   887,   888,   889,     0,     0,     0,     0,
       0,     0,     0,   928,     0,     0,     0,     0,   486,   487,
     488,     0,     0,     0,     0,   463,   462,  1013,     0,  1031,
    1029,  1014,   993,  1003,   991,   997,  1005,   995,  1009,  1007,
     999,  1001,     0,  1094,     0,  1077,     0,     0,     0,   955,
     969,   960,   959,   951,   950,   952,   953,   958,   964,   965,
     967,   966,   947,     0,   963,   962,   961,     0,     0,   268,
       0,     0,   271,  1026,  1037,  1045,     0,     0,  1048,     0,
       0,     0,  1052,   101,     0,   117,   118,   257,     0,     0,
     234,     0,   220,     0,     0,     0,   228,   229,     0,   102,
       0,   195,     0,     0,     0,     0,    86,     0,     0,     0,
       0,   431,   429,   443,   529,     0,     0,   749,     0,   682,
     651,   666,     0,   746,   745,   744,   686,   743,     0,   684,
       0,   689,   691,   690,   694,   695,   698,   697,     0,   699,
     700,   696,   701,   703,   709,   705,   707,   648,     0,   646,
     754,   687,   800,   783,   791,   792,     0,   774,     0,   811,
       0,   776,     0,     0,   864,   868,   866,   862,   906,   853,
     863,   861,   865,   867,   908,   855,     0,     0,     0,     0,
     856,     0,     0,     0,     0,     0,     0,   933,   935,   490,
     484,     0,     0,     0,     0,   464,     0,  1032,  1010,  1030,
    1011,     0,  1092,  1095,     0,  1075,  1078,     0,     0,     0,
       0,   948,     0,     0,     0,   108,     0,   106,  1041,  1046,
       0,     0,     0,     0,   266,     0,   129,   256,   258,   126,
     236,   219,   221,   222,   223,   211,   213,   180,   176,   204,
       0,     0,   326,   199,   217,   182,   191,     0,   237,   238,
       0,     0,   393,     0,   842,     0,   672,   668,   670,  1060,
    1062,   649,   794,   784,   785,   793,     0,     0,   811,     0,
       0,     0,     0,     0,     0,     0,     0,   897,     0,     0,
       0,     0,     0,     0,     0,   483,   485,     0,   477,   479,
    1028,     0,  1096,     0,  1079,     0,     0,     0,   949,   975,
     269,   272,  1054,  1015,     0,  1043,  1050,     0,   245,     0,
       0,     0,     0,     0,   214,     0,     0,   204,     0,     0,
     255,     0,     0,   191,   153,     0,   811,     0,   841,     0,
     787,   789,   788,   806,   786,     0,   775,     0,   778,   777,
       0,   814,     0,   894,   872,     0,     0,     0,     0,   820,
     823,   831,   832,   938,     0,   481,  1097,  1080,   956,   970,
    1055,  1042,     0,     0,  1016,  1047,  1049,  1056,  1020,     0,
     248,     0,   130,   127,   235,     0,   173,     0,   212,     0,
     103,   205,   202,   196,   200,     0,    87,   192,   154,   340,
       0,     0,   843,     0,     0,     0,     0,     0,     0,   816,
     870,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1017,     0,  1057,  1044,     0,  1051,  1053,     0,     0,     0,
     253,     0,   215,     0,   327,   255,   321,     0,     0,   395,
       0,     0,   808,   807,   772,     0,   779,   871,     0,     0,
       0,   821,   824,   491,     0,     0,     0,  1019,     0,   251,
     249,     0,   255,   224,   206,   203,   197,     0,   201,   321,
     193,     0,   844,     0,     0,   898,     0,   858,   828,   828,
     494,  1099,     0,  1082,     0,     0,     0,   255,   257,   246,
       0,     0,   328,   317,   325,   322,     0,   818,     0,     0,
     857,   822,   825,     0,  1098,  1100,  1081,  1083,  1018,  1021,
       0,     0,   257,   254,     0,   198,     0,     0,   817,     0,
     830,   829,     0,     0,   492,     0,     0,     0,     0,   495,
    1023,   252,   250,     0,     0,     0,   318,   323,     0,   899,
       0,     0,     0,     0,     0,     0,     0,  1024,  1022,   247,
       0,   319,   255,     0,     0,   500,   493,     0,   496,   497,
     501,     0,   255,   324,     0,   499,   498,   329,   320,     0,
       0,     0,     0,     0,     0,     0,   341,   343,   342,     0,
       0,   348,     0,   344,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   346,   349,   345,     0,   347
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
   -1600, -1600, -1600, -1600,    -6, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  -169, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  -160, -1600,
    1452, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600,   293, -1600,   -68, -1600,  1028, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,  -145, -1600, -1392, -1599, -1600,  1058,
   -1600,   661, -1600,   361, -1600,   359, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600,  -284, -1600, -1600,  -308, -1600, -1600, -1600,
   -1600,   735, -1600,  1443, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600,  1248, -1600, -1600,   964, -1600,
    -132, -1600,  -328, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600,  1216,   966, -1600,
    -509, -1600, -1600,  -127, -1600, -1600, -1600, -1600, -1600, -1600,
     628, -1600,   743, -1600, -1600, -1600, -1600, -1600,  -540, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1482, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
    -204, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600,   873, -1600,   521, -1600, -1600, -1600,
   -1600, -1600,   869, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
     279, -1600, -1600, -1600, -1600, -1600, -1600,   526,   801, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600,    39, -1600, -1600,
     812,  -932, -1600, -1600, -1600, -1600,  -902, -1193, -1211, -1600,
   -1600, -1600, -1453,  -572, -1600, -1600, -1600, -1600, -1600,  -275,
   -1600, -1600, -1600, -1600, -1600, -1600,   887, -1600, -1600, -1600,
    -131, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600,   506, -1600, -1600,
     -73,   195, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  -592, -1235,
   -1153,  -993, -1011,  -372, -1013, -1600, -1600, -1600, -1600, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,    98,
   -1600, -1600, -1600, -1600, -1600,   454, -1600, -1600,   708, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,  -337,
   -1600, -1017, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600,
   -1600,  1130, -1600, -1600, -1600, -1600,     2, -1600, -1600, -1600,
   -1600, -1600, -1600,  1155, -1600, -1600, -1600, -1600,     7, -1600,
   -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600, -1600
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     2,    42,   105,   509,    43,    44,     3,   148,    45,
      46,    47,    48,    49,    50,    51,   127,   130,   184,    52,
      53,   132,   196,    54,    55,    95,   263,   407,   133,   264,
     478,  1796,   661,  1789,   424,   674,  1379,  1860,  1378,  1859,
     436,   438,   684,   441,   443,   689,   460,  1406,  1938,   740,
    1383,   741,   470,  1384,  1403,   476,   759,  1089,  1872,  1873,
     748,  1398,  1791,  1975,  2033,  1870,  1976,  1974,  1866,  1867,
     463,  1645,  1864,  1928,  1655,  1642,  1784,  1863,  1078,  1393,
    1394,   681,  1780,  1657,  1798,  1068,   433,  1060,  1637,  1920,
    2052,  1969,  2028,  2027,  1922,  2002,  1638,  1639,   666,   667,
    1055,  1056,  1357,  1358,  1360,  1361,   422,   405,  1077,   737,
    2055,  2076,  2102,  2008,  2009,  2092,  1653,  2005,  2032,    56,
     388,    57,   135,    58,    59,   273,   486,  1978,  2117,  2118,
    2123,  2126,   274,   275,   490,   276,   487,   768,   769,   277,
     278,   483,   510,   279,   280,   484,   512,   779,  1409,   489,
     770,    60,   102,    61,    62,   513,   282,   138,   518,   519,
     520,   785,  1105,   139,   285,   521,   140,   283,   286,   530,
     788,  1107,    63,    64,   289,   141,   290,   291,   931,    65,
      66,   294,   142,   295,   535,   802,   296,    67,    68,    69,
     100,   368,  1288,  1576,   609,   992,  1289,  1290,  1573,  1291,
    1574,  1292,  1572,  1293,  1571,  1741,  1294,  1567,  1834,  2020,
    2082,  2043,  2069,    70,    71,   101,   303,   539,   143,   304,
     305,   306,   307,   541,   812,   308,   544,   816,  1126,  1664,
    1401,    72,   330,    73,    96,   583,   872,   144,   331,   569,
     860,   332,   568,   858,   861,   333,   555,   840,   825,   833,
     334,   578,   867,   868,  1140,   335,   336,   337,   338,   339,
     340,   341,   342,   557,   343,   560,   344,   345,   362,   346,
     347,   348,   562,   925,  1153,   586,   926,  1159,  1162,  1163,
    1191,  1156,  1192,  1190,  1206,  1208,  1209,  1207,  1511,  1460,
    1461,   927,   928,  1215,  1187,  1175,  1179,   594,   595,   929,
    1521,   941,  1814,  1883,  1884,  1703,  1704,  1705,   936,   937,
     938,   542,   543,   815,  1123,  1946,   756,  1402,  1086,   596,
    1713,  1712,  1817,   585,   980,  1955,  2018,  1956,  2019,  2041,
    2061,   983,   349,   350,   351,   865,   574,   864,  1665,  1878,
    1804,   352,   556,   353,   354,   970,   597,   971,  1235,  1240,
    1827,   972,   973,  1259,  1556,  1263,  1249,  1250,  1247,  1897,
     974,  1244,  1535,  1541,    74,   363,    75,    92,   606,   986,
     145,   364,   604,   598,   599,   600,   602,   365,   607,   990,
    1278,  1279,   984,  1274,    76,   118,    77,   119,  1027,  1349,
    1763,  1020,  1021,  1022,  1342,    78,   103,   160,   380,   613,
     611,   616,   614,   619,   620,   612,   615,   618,   617,  1299,
    1915,  1965,  2070,  2088,  1624,  1579,  1750,  1748,  1047,  1048,
    1362,  1371,  1626,  1852,  1917,  1853,  1918,  1911,  1963,  1465,
    1680,  1475,  1688,    79,    80,    81,    82,    83,   168,  1011,
     628,   629,   630,  1009,  1316,  1755,  1756,  1959,  2024,    84,
     383,  1006,   623,   624,   625,  1004,  1311,  1752,  1753,  1958,
    2022,    85,    86,    87,    88,    89,    90,    91,   149
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      94,   580,  1319,  1225,    97,  1320,  1651,  1325,  1523,  1352,
     789,   284,   329,   634,   635,   106,   107,   108,   109,   110,
     111,   112,   113,   114,  1323,  1324,   976,   977,  1525,  1219,
    1221,   123,   985,  1363,  1816,   131,   745,  1218,  1775,  1220,
    1222,   297,   265,  1028,  1419,  1433,  1044,   309,  2062,   310,
     932,   933,   545,  1210,   819,   515,  2119,  1281,  2120,   522,
     401,    93,   298,   665,   546,   547,   548,   311,  2121,  2063,
     631,  2064,    93,  1912,   511,    93,   808,   809,    93,   267,
     549,   312,   313,   767,  1596,   185,    93,    93,  1364,  1740,
     810,   154,    93,   146,  1643,   416,  1124,   314,   523,  1880,
     163,  1944,   854,   855,    93,   859,   834,   835,    93,   515,
     632,  1459,   561,  1005,  1718,   315,   856,   524,    93,   316,
    1724,   516,   536,  1282,    93,   581,  1967,   934,    93,    93,
      93,   317,    93,  1176,     4,   525,   526,   318,   550,    93,
     836,   820,   120,  1024,  2065,    93,  1010,    93,    93,  1353,
      93,    93,   821,   822,   551,    93,   299,   837,   987,   417,
     527,   319,   320,   321,   790,  1147,   300,   402,  2066,  1321,
    1272,   104,   322,    93,  1177,   516,    93,   425,  1387,   323,
    1280,   287,    93,   776,   838,   121,  1476,    93,  1478,   418,
    1778,  1778,    93,   400,   528,   406,   537,   408,   409,   410,
     412,   413,   414,   415,  1761,   423,    93,   427,   428,   429,
     122,    93,   435,   646,   437,  1945,   439,   440,    93,   442,
    1778,   444,   445,   446,   447,   448,   449,   450,   451,   452,
     453,   454,   455,   456,   457,   458,   459,  1101,  1102,   464,
    1178,   466,   467,   468,  1778,   471,   472,   473,   474,   475,
      14,   575,   292,   871,   480,  1968,   482,  1820,   419,  1836,
    1365,  1023,   301,   485,   631,    93,   125,   126,   324,   288,
    1148,   791,   755,  1343,   943,   760,  1104,  1344,  1345,    16,
     552,  1644,  1761,  1346,  1347,  1344,  1345,  1281,  1913,  1344,
    1345,  1346,  1347,   540,  1761,  1346,  1347,   147,   325,   565,
    1881,  1882,   935,  1138,  1013,   420,  2067,  1014,  1015,   137,
    1597,  1706,   563,  1352,   564,   186,  1522,  1016,  1524,   988,
     989,   794,    93,  1045,  1366,   823,   621,   622,   115,  1598,
     573,  1708,  1709,  1608,  1609,  1610,  1611,  1273,   538,   566,
    1601,  1602,  1603,  1604,  1605,  1606,    93,   326,   949,  1607,
    1412,  1914,  1835,  1282,   653,   603,  1434,   293,  1052,   626,
     627,  1260,  1760,    98,    99,   633,   508,   553,  1890,   508,
    1797,   655,  1367,   811,  1420,  1046,   124,   529,  1096,  1211,
     508,   508,  1924,   637,   638,   639,   426,   640,   641,   642,
     643,  1125,   554,   647,  1224,  2122,  1226,  1400,   508,   857,
    1135,  1515,   508,   654,   656,   658,  1676,   657,   839,  1719,
     746,   752,   403,   404,  1313,  1725,   515,   800,  1017,  1318,
     669,   421,  1707,   508,  1322,   873,   266,   673,   677,  2051,
     680,   679,   680,   134,  1948,   680,   327,   680,  1544,   824,
    1888,   508,   508,   267,   508,   508,  2068,   817,  1979,   508,
     701,   302,  1024,  2073,   924,   150,  1388,   738,  1389,   268,
    1762,   827,   828,  1390,  1368,  1369,  1801,   508,   753,  1592,
     128,   757,   267,   747,  1370,  1594,   328,   411,  1934,   567,
    1776,  1281,   516,  1779,  1858,  2044,   129,  1761,   268,  1391,
    1327,  1328,  1329,  1330,  1331,  1332,  1350,  1351,  1819,  2046,
     116,   117,   772,   773,  2053,   269,  1392,  1659,   281,   675,
    1213,  2071,   151,  2072,    93,  1818,   818,   792,   793,    16,
     795,   796,   797,   798,   576,   577,    93,   515,  1261,   801,
    1348,  1214,  1262,   777,   269,   136,  1283,  2089,  1762,   778,
    1600,  1335,  1336,  1337,  1338,  1284,  1285,  1282,   270,   137,
    1762,   182,   183,  1017,  1025,   829,  1090,   830,  1026,  1018,
      93,   491,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   270,   874,   461,
     462,  1171,   587,  2006,   152,  1758,   153,   939,   271,  1181,
    1112,   570,   571,   516,   621,   622,  1413,   369,   588,  1182,
     187,   831,   832,  1757,  1845,  1846,   155,  1887,  1876,   588,
    2030,   156,   188,  1183,  1172,  1003,   189,  1024,   370,   157,
    1008,  1025,  1241,   190,   371,  1026,  1019,   372,   373,   626,
     627,   589,   272,   158,  1184,  2050,   159,   590,   676,   978,
     979,   374,   589,   591,  1242,  1243,  1326,   161,   590,  1344,
    1345,   162,  1354,   284,   591,  1346,  1347,   164,  1149,   165,
     375,   272,  1173,  1057,  1174,  1940,   166,  1150,  1151,  1061,
      93,   491,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   355,  1185,   167,
     376,   169,   191,   170,  1845,   171,   192,   592,  1073,   172,
    2103,  1152,   593,  1344,  1345,   173,  1565,  1566,   592,  1346,
    2108,  1186,   174,   593,   377,   175,  1295,  1296,  1297,   176,
    1212,   981,   982,   378,   379,  1054,  1374,   755,   755,  1223,
     177,   683,  1075,   193,   686,  1024,   688,  1079,  1080,   137,
    1082,   178,  1339,  1762,  1340,  1341,  1352,  1335,  1336,  1337,
    1338,  1087,   194,  1298,  1599,   356,   357,   358,   359,   360,
     361,  1335,  1336,  1337,  1338,   179,  1099,  1847,  1356,  1618,
    1812,   491,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   180,   430,   431,
     432,   181,  1116,  1117,   366,  1118,  1422,   782,   783,   784,
     367,  1119,  1761,  1547,  1248,  1327,  1328,  1329,  1330,  1331,
    1332,  1333,  1334,  1049,  1050,  1051,   381,  1426,   491,   492,
     493,   494,   495,   496,   497,   498,   499,   500,   501,   502,
     503,   504,   505,   506,   382,  1909,  1327,  1328,  1329,  1330,
    1331,  1332,  1350,  1351,  1131,  1093,  1094,  1095,  1120,  1121,
    1122,   384,  1908,   507,   508,  1359,  1621,  1466,  1467,   385,
    1380,  1470,  1471,  1472,  1473,  1337,  1338,  1477,  1145,   386,
    1146,  1132,  1133,  1134,  1154,  1155,  1344,  1345,  1158,   389,
    1160,  1161,   195,   387,  1518,  1164,  1165,  1166,  1167,  1168,
    1169,  1170,  1782,  1783,  1180,   390,  1188,  1189,  1410,   392,
     391,  1193,  1194,   393,  1196,  1197,   491,   492,   493,   494,
     495,   496,   497,   498,   499,   500,   501,   502,   503,   504,
     505,   506,   394,   395,  1217,   396,  1327,  1328,  1329,  1330,
    1331,  1332,  1333,  1334,  1256,  1257,  1258,   397,  1231,  1232,
    1233,  1234,   398,   399,  1236,  1237,  1238,  1239,  1553,  1554,
    1555,   434,   465,  1246,   469,  1327,  1328,  1329,  1330,  1331,
    1332,  1333,  1334,   579,   508,  1568,  1569,  1570,   479,   874,
     481,   488,  1266,  1267,   514,  1269,  1270,  1613,  1614,  1615,
    1616,   491,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   531,  1310,  1673,
    1674,  1675,   532,  1315,   533,   534,  1516,  1019,   572,   558,
    1019,  1019,  1019,   559,   755,   573,   755,   582,  1526,  1527,
    1528,   584,  1529,  1327,  1328,  1329,  1330,  1331,  1332,  1350,
    1351,   491,   492,   493,   494,   495,   496,   497,   498,   499,
     500,   501,   502,   503,   504,   505,   506,   601,  1373,  1335,
    1336,  1337,  1338,   605,   636,   608,   610,  1339,  1762,  1340,
    1341,   644,   645,   775,   648,   649,   650,   651,   652,   659,
    1385,   660,   662,   663,   664,  1397,   665,  1399,   668,   670,
     671,  1024,  1404,   672,   678,   704,   682,  1353,  1339,   685,
    1340,  1341,  1300,  1301,  1302,  1303,  1304,  1305,  1306,  1307,
    1308,   687,   690,   691,   692,   743,   744,   693,  1416,  1417,
     870,   705,   706,   707,   708,   709,   710,   711,   712,   713,
     714,   715,   716,   717,   718,   719,   720,   721,   722,   723,
     724,   725,   726,   727,   728,   729,   730,   731,   732,   733,
     734,   735,   736,   758,   694,   695,   696,   697,   698,   699,
    1438,   700,   702,   703,   739,   742,   749,  1445,   750,   751,
     754,   763,   761,   762,   764,   765,   766,   767,   774,   781,
    1335,  1336,  1337,  1338,   786,   787,  1640,  1599,  1339,   799,
    1340,  1341,   803,   804,   805,  1462,   806,   807,  1698,   813,
     814,   826,   841,   842,   843,   844,   845,   846,  1517,  1335,
    1336,  1337,  1338,   847,   848,   755,  1660,  1339,   849,  1340,
    1341,   850,   851,   852,   859,   853,   866,   862,   863,   869,
     875,   930,   940,   975,  1759,  1530,  1531,  1532,  1533,   991,
    1536,  1537,  1538,  1539,   993,   994,   995,   996,   997,  1543,
     998,  1545,  1546,   999,  1000,  1001,  1002,  1029,  1030,  1552,
    1031,  1032,  1033,  1034,  1557,  1035,  1036,  1037,  1059,  1038,
    1042,  1039,  1040,  1041,  1043,  1062,  1054,  1564,  1024,  1064,
    1058,  1063,  1067,  1697,  1065,  1339,  1066,  1340,  1341,  1076,
    1069,  1070,  1072,  1074,  1081,  1083,  1084,  1085,  1088,  1091,
    1098,  1092,  1100,  1106,  1114,  1115,  1108,  1137,  1109,  1110,
    1144,  1111,  1113,  1157,  1195,  1593,  1127,  1198,  1199,  1128,
    1595,  1216,  1129,  1130,  1141,  1019,  1019,  1143,  1200,   935,
    1201,  1202,  1710,  1203,  1204,  1228,  1205,  1019,  1019,  1019,
    1019,  1019,  1019,  1248,  1230,  1245,  1019,  1251,  1019,  1019,
    1019,  1019,  1252,  1253,  1254,  1255,  1264,  1265,  1268,  1276,
    1617,  1271,  1275,  1620,  1277,  1309,  1314,  1312,  1317,  1627,
    1628,  1629,  1630,  1631,  1632,  1355,  1376,  1634,  1356,  1359,
    1377,  1395,  1381,  1382,  1386,  1396,  1405,  1407,  1408,   876,
    1414,  1415,  1418,  1646,  1425,  1647,  1421,  1423,  1424,  1648,
    1649,  1849,   877,  1427,  1428,  1429,  1430,  1431,   878,  1656,
    1435,  1658,   879,  1432,  1436,  1437,  1439,  1440,  1441,   880,
     881,  1442,  1443,   267,  1444,  1446,  1447,  1459,  1448,  1449,
    1450,  1451,   882,  1452,  1463,  1453,  1454,  1455,   883,  1456,
     884,   885,  1668,  1457,  1458,   886,   887,  1464,  1474,  1672,
    1512,   888,  1468,  1469,  1513,  1514,  1534,  1540,  1542,  1549,
    1550,  1551,  1558,  1799,  1559,   889,  1678,  1560,  1561,  1562,
    1563,  1577,  1578,  1575,  1581,  1582,  1583,  1584,  1585,   890,
    1586,   891,  1587,   892,  1612,   893,  1588,  1589,  1590,  1591,
    1623,   894,   895,  1625,  1633,   896,   897,  1635,  1641,  1650,
    1652,   898,   899,  1636,  1654,  1661,  1662,  1663,  1666,  1701,
    1702,  1667,  1669,  1670,  1671,  1679,  1681,  1682,  1683,  1726,
    1684,   900,  1685,  1686,  1687,  1689,  1690,  1691,  1729,  1692,
    1693,  1694,  1695,  1696,   901,  1699,  1700,   902,  1711,  1736,
     903,  1739,  1742,  1743,  1714,  1744,  1715,  1716,  1745,  1749,
    1727,  1768,  1751,  1717,  1720,  1721,  1728,  1722,  1754,  1723,
    1737,  1730,  1738,  1732,  1733,  1734,  1735,  1024,  1770,  1765,
    1767,  1771,  1772,  1803,  1773,  1774,  1787,  1788,  1795,  1802,
    1806,  1807,  1746,  1747,  1808,  1809,  1810,  1813,  1816,  1821,
    1811,  1822,  1828,  1829,  1830,  1831,  1832,  1838,  1839,  1848,
    1019,  1850,  1851,  1862,  1865,  1868,  1869,  1871,  1879,  1877,
    1904,  1910,   904,  1874,  1893,  1875,  1921,  1886,  1889,  1891,
    1894,  1764,  1947,  1939,  1766,  1903,   905,  1919,  1927,  1953,
    1905,  1769,  1906,  1907,  1926,   906,  1930,   907,   908,   909,
     910,   911,  1777,  1962,  1936,  1781,  1973,  1785,  1786,  1949,
    1964,  1950,  1977,  -826,  1790,  1777,  1792,  1793,  1794,  1971,
    -827,  1981,  1800,  1991,  1984,  1986,  1992,  1799,  1987,  1993,
    1805,  1995,  1994,  1996,  2000,  2001,  2007,  2011,  2012,  2014,
    2017,  2031,  2026,  1941,  2029,  2036,  2040,  2057,  2060,  2074,
    2075,  2078,  2080,  2083,  2087,  2095,  2093,  2096,  2098,  2099,
    2100,   912,   913,   914,   915,   916,   917,   918,   919,  2101,
    2105,  2111,  2106,  2130,  1937,  2110,  2116,  1931,  2133,   477,
    2134,  2135,  1861,  2137,   920,  1923,  1375,  1071,  1619,  1622,
    1823,  1824,  1825,  1826,  1053,  2035,  2056,  1286,   517,   780,
    1833,  1411,  1097,  1136,  1519,  1287,  1837,  1142,   771,  1677,
    1840,  1520,  1229,  1815,  2042,  1841,  1103,  1942,  1843,  1227,
    1892,  1580,  1139,  1731,  1019,  1548,  1372,  1844,  1012,  1842,
       0,     0,     0,     0,  1854,  1855,  1856,  1857,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,  1007,   921,
       0,     0,     0,     0,     0,     0,     0,   922,     0,     0,
       0,     0,     0,   923,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1885,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1895,  1896,  1898,  1899,  1900,  1901,  1902,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,  1019,
    1019,     0,     0,     0,     0,     0,     0,     0,  1916,     0,
       0,     0,     0,     0,     0,     0,  1925,     0,     0,  1929,
       0,     0,  1932,  1933,     0,  1935,     0,     0,     0,     0,
       0,     0,     0,  1943,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,  1951,
    1952,     0,  1954,     0,     0,     0,     0,     0,  1957,     0,
       0,     0,     0,     0,     0,     0,  1960,  1961,     0,     0,
       0,     0,     0,  1966,     0,  1970,     0,     0,     0,     0,
       0,  1972,     0,     0,     0,     0,     0,     0,  1777,     0,
       0,     0,     0,     0,     0,  1980,     0,     0,  1982,  1983,
       0,  1985,     0,     0,     0,     0,  1988,  1989,  1990,     0,
       0,     0,     0,     0,     0,  1997,     0,     0,  1998,     0,
       0,  1999,     0,     0,     0,  2003,     0,  2004,     0,     0,
       0,  2010,     0,     0,     0,  2013,     0,     0,     0,     0,
       0,     0,  2015,  2016,     0,     0,     0,     0,  2021,  2023,
    2025,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1777,  2034,     0,     0,     0,     0,     0,  2037,  2038,  2039,
       0,     0,     0,     0,     0,     0,  2045,     0,  2047,  2048,
    2049,     0,     0,   197,  1777,  2054,     0,     0,   198,   199,
       0,     0,  2058,  2059,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,  1777,     0,     0,   200,     0,   201,
    2077,   202,     0,  2079,     0,     0,     0,  2081,     0,     0,
    2084,  2085,  2086,     0,     0,     0,     0,   203,  2090,  2091,
       0,     0,     0,     0,  2094,     0,     0,  2097,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  2104,     0,     0,
       0,     0,     0,     0,     0,  2107,     0,  1777,  2109,     0,
     204,     0,  1777,     0,     0,  2112,  2113,  2114,  2115,     0,
       0,     0,     0,  2124,  2125,   205,  2127,     0,  2128,  2129,
       0,  2131,  2132,     0,   206,     0,  2136,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   207,     0,     0,     0,     0,     0,   208,     0,
       0,     0,     0,     0,     0,     0,     0,   209,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     210,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   211,  1479,  1480,  1481,  1482,  1483,  1484,  1485,
    1486,  1487,  1488,  1489,  1490,  1491,  1492,  1493,  1494,  1495,
    1496,  1497,  1498,  1499,  1500,  1501,  1502,  1503,  1504,  1505,
    1506,  1507,  1508,  1509,  1510,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    -8,     1,     0,     0,     0,     0,     0,     0,
      -8,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   212,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    -8,     0,     0,     0,     0,    -8,
       0,   213,     0,   214,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   215,     0,     0,     0,
       0,     0,     0,    -8,     0,     0,    -8,     0,     0,     0,
      -8,     0,     0,     0,     0,   216,   217,    -8,     0,    -8,
       0,     0,     0,    -8,     0,     0,     0,     0,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,     0,   228,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   229,   230,
     231,   232,   233,   234,   235,   236,   237,   238,   239,   240,
     241,   242,    -8,   243,     0,   244,    -8,     0,     0,     0,
       0,     0,     0,   245,   246,   247,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      -8,     0,     0,     0,     0,    -8,    -8,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     248,   249,   250,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   251,   252,   253,   254,   255,     0,     0,     0,
       0,     0,     0,     0,     0,   256,     0,     0,   257,     0,
       0,     5,     0,     0,     0,     0,     0,   258,   259,   260,
       0,     0,   261,   262,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    -8,     6,     0,     0,     0,     0,
       0,    -8,    -8,    -8,     0,    -8,    -8,    -8,    -8,    -8,
       0,    -8,     0,     0,     0,     0,    -8,    -8,    -8,     0,
       0,     0,     0,    -8,     7,     0,     0,     8,     0,     0,
       0,     9,     0,    -8,     0,    -8,    -8,     0,    10,     0,
      11,     0,     0,     0,    12,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   942,     0,   943,   944,
     945,   946,   947,    -8,    -8,    -8,     0,     0,   948,     0,
       0,     0,     0,     0,     0,     0,     0,    -8,    -8,     0,
      -8,     0,     0,    -8,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    13,     0,     0,     0,    14,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    -8,     0,
       0,    15,     0,     0,     0,     0,    16,    17,     0,     0,
       0,     0,   949,   950,   951,     0,     0,   952,   953,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   954,
       0,     0,     0,     0,     0,     0,   955,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    18,     0,     0,     0,     0,
       0,     0,    19,    20,     0,     0,    21,    22,    23,    24,
      25,     0,    26,     0,     0,     0,     0,    27,    28,    29,
       0,     0,     0,     0,    30,   956,   957,   958,     0,     0,
       0,     0,     0,     0,    31,     0,    32,    33,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   959,   960,     0,
     961,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    34,    35,    36,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    37,    38,
       0,    39,     0,     0,    40,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   962,
     963,     0,     0,     0,   964,   965,   966,   967,   968,   969,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    41
};

static const yytype_int16 yycheck[] =
{
       6,   329,  1013,   935,    10,  1016,  1398,  1018,  1219,  1022,
     519,   138,   144,   385,   386,    21,    22,    23,    24,    25,
      26,    27,    28,    29,  1017,  1018,   598,   599,  1221,   931,
     932,    37,   604,     9,    34,    41,    21,    62,  1637,    62,
      62,    16,    29,   635,   136,   155,   125,    16,    14,    18,
     590,   591,    10,   140,    46,   171,    86,   171,    88,    33,
      54,   173,    37,   171,    22,    23,    24,    36,    98,    35,
     172,    37,   173,    21,   278,   173,   169,   170,   173,    48,
      38,    50,    51,   171,  1319,   236,   173,   173,    64,  1571,
     183,    97,   173,    37,   168,    28,   171,    66,    72,   235,
     106,    20,   169,   170,   173,   171,    55,    56,   173,   171,
     212,   171,   316,    37,   171,    84,   183,    91,   173,    88,
     171,   237,    22,   237,   173,   329,   168,    62,   173,   173,
     173,   100,   173,     5,     0,   109,   110,   106,    96,   173,
      89,   133,   173,   462,   110,   173,    37,   173,   173,   468,
     173,   173,   144,   145,   112,   173,   131,   106,    37,    92,
     134,   130,   131,   132,    54,     6,   141,   161,   134,   212,
      37,   172,   141,   173,    46,   237,   173,   258,   138,   148,
      64,    37,   173,   511,   133,   173,  1203,   173,  1205,   122,
     173,   173,   173,   199,   168,   201,    96,   203,   204,   205,
     206,   207,   208,   209,   214,   211,   173,   213,   214,   215,
     173,   173,   218,   258,   220,   134,   222,   223,   173,   225,
     173,   227,   228,   229,   230,   231,   232,   233,   234,   235,
     236,   237,   238,   239,   240,   241,   242,   777,   778,   245,
     112,   247,   248,   249,   173,   251,   252,   253,   254,   255,
     134,   173,    37,   581,   260,   297,   262,  1710,   191,  1741,
     236,   633,   237,   269,   172,   173,   319,   320,   237,   125,
     111,   161,   476,   213,    39,   479,   785,   217,   218,   163,
     238,   355,   214,   223,   224,   217,   218,   171,   236,   217,
     218,   223,   224,   299,   214,   223,   224,   241,   267,    10,
     436,   437,   237,    37,   212,   238,   272,   215,   216,    64,
    1321,  1522,   318,  1326,   320,   466,  1218,   225,  1220,   198,
     199,   525,   173,   402,   300,   317,   250,   251,   172,  1322,
      64,  1524,  1525,  1344,  1345,  1346,  1347,   204,   238,    50,
    1333,  1334,  1335,  1336,  1337,  1338,   173,   316,   113,  1342,
     466,   299,   466,   237,   466,   361,   466,   142,   466,   250,
     251,    59,  1597,    93,    94,   467,   467,   325,  1821,   467,
     468,   466,   348,   466,   466,   454,   466,   351,   466,   466,
     467,   467,   468,   389,   390,   391,   467,   393,   394,   395,
     396,   466,   350,   399,   934,   425,   936,   466,   467,   466,
     466,   466,   467,   409,   410,   411,   466,   258,   357,   466,
     395,   466,   406,   407,  1006,   466,   171,   466,   461,  1011,
     426,   354,   466,   467,   467,   466,   413,   433,   434,  2028,
     436,   258,   438,   171,  1887,   441,   405,   443,   466,   431,
     466,   467,   467,    48,   467,   467,   412,   358,   466,   467,
     456,   426,   462,  2052,   586,   171,   416,   463,   418,    64,
     470,    11,    12,   423,   440,   441,  1659,   467,   474,   466,
      84,   477,    48,   458,   450,   466,   445,   124,  1870,   190,
     466,   171,   237,   466,   466,   466,   100,   214,    64,   449,
     217,   218,   219,   220,   221,   222,   223,   224,  1709,   466,
      93,    94,   508,   509,   466,   110,   466,  1409,    29,   168,
      16,   466,   466,   466,   173,  1708,   427,   523,   524,   163,
     526,   527,   528,   529,   446,   447,   173,   171,   226,   535,
     470,    37,   230,   103,   110,    50,   420,   466,   470,   109,
     468,   461,   462,   463,   464,   429,   430,   237,   153,    64,
     470,    93,    94,   461,   466,   105,   760,   107,   470,   467,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   153,   584,   400,
     401,    56,    37,  1975,   171,  1596,   171,   593,   164,     7,
     794,   268,   269,   237,   250,   251,  1105,    37,    64,    17,
      14,   151,   152,  1596,  1757,  1758,   466,  1818,  1801,    64,
    2002,   466,    26,    31,    89,   621,    30,   462,    58,   171,
     626,   466,    60,    37,    64,   470,   632,    67,    68,   250,
     251,    97,   237,   171,    52,  2027,   171,   103,   297,   169,
     170,    81,    97,   109,    82,    83,  1018,   466,   103,   217,
     218,   171,  1024,   780,   109,   223,   224,   466,    46,   466,
     100,   237,   137,   669,   139,  1876,   466,    55,    56,   675,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   130,   106,   466,
     130,   466,   106,   466,  1847,   466,   110,   163,   704,   466,
    2092,    89,   168,   217,   218,   466,  1278,  1279,   163,   223,
    2102,   129,   466,   168,   154,   171,   233,   234,   235,   171,
     924,   169,   170,   163,   164,   467,   468,   931,   932,   933,
     466,   438,   738,   147,   441,   462,   443,   743,   744,    64,
     746,   466,   469,   470,   471,   472,  1759,   461,   462,   463,
     464,   757,   166,   270,   468,   198,   199,   200,   201,   202,
     203,   461,   462,   463,   464,   466,   772,  1760,   467,   468,
    1702,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   466,   293,   294,
     295,   466,   798,   799,    67,   801,  1124,   171,   172,   173,
     466,   807,   214,   466,   467,   217,   218,   219,   220,   221,
     222,   223,   224,   171,   172,   173,   466,  1145,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   184,   185,
     186,   187,   188,   189,   466,  1846,   217,   218,   219,   220,
     221,   222,   223,   224,   850,   171,   172,   173,   171,   172,
     173,   466,  1845,   466,   467,   467,   468,  1194,  1195,   469,
    1064,  1198,  1199,  1200,  1201,   463,   464,  1204,   874,   469,
     876,   171,   172,   173,   880,   881,   217,   218,   884,    99,
     886,   887,   296,   466,  1212,   891,   892,   893,   894,   895,
     896,   897,   352,   353,   900,    73,   902,   903,  1102,   158,
     419,   907,   908,    74,   910,   911,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,   185,   186,   187,
     188,   189,    85,    78,   930,   167,   217,   218,   219,   220,
     221,   222,   223,   224,   282,   283,   284,   298,   944,   945,
     946,   947,   173,    25,   950,   951,   952,   953,   282,   283,
     284,   293,   173,   959,   452,   217,   218,   219,   220,   221,
     222,   223,   224,   466,   467,   171,   172,   173,   467,   975,
     466,    37,   978,   979,   171,   981,   982,  1349,  1350,  1351,
    1352,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   134,  1004,   171,
     172,   173,   171,  1009,    61,   171,  1210,  1013,   466,   171,
    1016,  1017,  1018,   171,  1218,    64,  1220,    37,  1222,  1223,
    1224,   171,  1226,   217,   218,   219,   220,   221,   222,   223,
     224,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   171,  1054,   461,
     462,   463,   464,    37,   171,   415,   232,   469,   470,   471,
     472,   173,   466,   466,   466,   466,   466,   171,   466,   466,
    1076,   466,   466,   466,   466,  1081,   171,  1083,   467,   466,
     466,   462,  1088,   466,   466,   168,   466,   468,   469,   466,
     471,   472,   994,   995,   996,   997,   998,   999,  1000,  1001,
    1002,   466,   466,   466,   466,   355,   399,   466,  1114,  1115,
     466,   363,   364,   365,   366,   367,   368,   369,   370,   371,
     372,   373,   374,   375,   376,   377,   378,   379,   380,   381,
     382,   383,   384,   385,   386,   387,   388,   389,   390,   391,
     392,   393,   394,   448,   466,   466,   466,   466,   466,   466,
    1156,   466,   466,   466,   466,   466,   466,  1163,   466,   466,
     466,   171,   466,   466,   171,   466,   171,   171,   466,   466,
     461,   462,   463,   464,   171,    37,  1380,   468,   469,   171,
     471,   472,   466,   466,   466,  1191,   171,    13,  1516,   466,
     171,   350,   466,   171,   171,   171,   466,   171,   466,   461,
     462,   463,   464,   466,   171,  1409,  1410,   469,   466,   471,
     472,    13,   466,   466,   171,   466,    64,   466,   466,   466,
     466,   408,    37,   171,  1596,  1231,  1232,  1233,  1234,   466,
    1236,  1237,  1238,  1239,   171,   171,   171,   171,   171,  1245,
     171,  1247,  1248,   171,   171,   171,   171,   171,   466,  1255,
     466,   466,   466,   466,  1260,   466,   466,   466,   296,   467,
     467,   466,   466,   466,   466,   173,   467,  1273,   462,   467,
     466,   466,   314,   466,   466,   469,   466,   471,   472,   168,
     466,   466,   466,   466,   168,   168,   466,   466,   168,   466,
     171,   466,   466,   466,    13,   149,   466,   171,   466,   466,
     171,   466,   466,   171,   173,  1311,   466,   173,   173,   466,
    1316,   171,   466,   466,   466,  1321,  1322,   466,   173,   237,
     173,   173,  1526,   173,   173,   466,   173,  1333,  1334,  1335,
    1336,  1337,  1338,   467,   148,   207,  1342,   466,  1344,  1345,
    1346,  1347,   172,   172,   172,   227,   466,   466,   466,   171,
    1356,   466,   466,  1359,   171,   466,   466,   245,   249,  1365,
    1366,  1367,  1368,  1369,  1370,   466,   468,  1373,   467,   467,
     173,   168,   466,   466,   466,   134,   404,   403,   468,    14,
     171,   466,   466,  1389,   171,  1391,   466,   466,   466,  1395,
    1396,  1763,    27,   466,   466,   466,   466,   466,    33,  1405,
     171,  1407,    37,   466,   466,   466,   171,   466,   171,    44,
      45,   466,   466,    48,   171,   466,   466,   171,   466,   466,
     466,   466,    57,   466,   171,   466,   466,   466,    63,   466,
      65,    66,  1438,   466,   466,    70,    71,    64,    64,  1445,
     171,    76,   466,   466,   172,   171,   171,   171,   466,   466,
     466,   466,   157,  1657,    34,    90,  1462,    34,    34,    34,
     203,   172,   236,    37,   171,   466,   466,   466,   466,   104,
     466,   106,   466,   108,   213,   110,   466,   466,   466,   466,
     455,   116,   117,   173,   466,   120,   121,   466,   399,   399,
     456,   126,   127,   466,   168,   466,   466,   466,    34,    37,
     237,   466,   466,   466,   466,   466,   466,   466,   466,   208,
     466,   146,   466,   466,   466,   466,   466,   466,   227,   466,
     466,   466,   466,   466,   159,   466,   466,   162,   466,   205,
     165,   171,   171,   171,   466,   171,   466,   466,   171,   173,
    1546,   171,   252,   466,   466,   466,  1552,   466,   252,   466,
     466,  1557,   466,  1559,  1560,  1561,  1562,   462,   236,   466,
     466,   399,   399,   109,   451,   468,   466,   466,   466,   466,
     466,   466,  1578,  1579,   466,   171,   171,   444,    34,   171,
     466,   171,   227,   138,   138,   138,   138,   466,   466,   214,
    1596,   468,   468,   355,   399,   134,   168,   432,    13,  1803,
     168,   136,   237,   466,   468,   466,   229,   466,   466,   466,
     466,  1617,    13,   417,  1620,   466,   251,   450,   442,   160,
     466,  1627,   466,   466,   466,   260,   466,   262,   263,   264,
     265,   266,  1638,   443,   466,  1641,   134,  1643,  1644,   466,
     453,   466,   134,   466,  1650,  1651,  1652,  1653,  1654,   399,
     466,   138,  1658,    64,   466,   466,    64,  1861,   466,   466,
    1666,   248,   253,   236,   173,   229,   168,   171,   466,   138,
     466,   168,   399,  1877,   173,   171,   466,   171,   171,   399,
     168,   466,    25,   124,   457,   466,   404,   171,   466,   466,
     466,   326,   327,   328,   329,   330,   331,   332,   333,   134,
     466,   351,   466,   171,  1873,   466,   466,  1867,   466,   257,
     466,   466,  1780,   466,   349,  1860,  1055,   689,  1357,  1360,
    1726,  1727,  1728,  1729,   666,  2009,  2034,   992,   285,   513,
    1736,  1103,   768,   860,  1213,   992,  1742,   868,   490,  1460,
    1746,  1215,   941,  1704,  2019,  1751,   780,  1878,  1754,   937,
    1823,  1297,   865,  1558,  1760,  1249,  1048,  1755,   628,  1752,
      -1,    -1,    -1,    -1,  1770,  1771,  1772,  1773,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   623,   414,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   422,    -1,    -1,
      -1,    -1,    -1,   428,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    1816,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    1826,  1827,  1828,  1829,  1830,  1831,  1832,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1845,
    1846,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1854,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,  1862,    -1,    -1,  1865,
      -1,    -1,  1868,  1869,    -1,  1871,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,  1879,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1895,
    1896,    -1,  1898,    -1,    -1,    -1,    -1,    -1,  1904,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,  1912,  1913,    -1,    -1,
      -1,    -1,    -1,  1919,    -1,  1921,    -1,    -1,    -1,    -1,
      -1,  1927,    -1,    -1,    -1,    -1,    -1,    -1,  1934,    -1,
      -1,    -1,    -1,    -1,    -1,  1941,    -1,    -1,  1944,  1945,
      -1,  1947,    -1,    -1,    -1,    -1,  1952,  1953,  1954,    -1,
      -1,    -1,    -1,    -1,    -1,  1961,    -1,    -1,  1964,    -1,
      -1,  1967,    -1,    -1,    -1,  1971,    -1,  1973,    -1,    -1,
      -1,  1977,    -1,    -1,    -1,  1981,    -1,    -1,    -1,    -1,
      -1,    -1,  1988,  1989,    -1,    -1,    -1,    -1,  1994,  1995,
    1996,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    2006,  2007,    -1,    -1,    -1,    -1,    -1,  2013,  2014,  2015,
      -1,    -1,    -1,    -1,    -1,    -1,  2022,    -1,  2024,  2025,
    2026,    -1,    -1,     9,  2030,  2031,    -1,    -1,    14,    15,
      -1,    -1,  2038,  2039,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,  2050,    -1,    -1,    33,    -1,    35,
    2056,    37,    -1,  2059,    -1,    -1,    -1,  2063,    -1,    -1,
    2066,  2067,  2068,    -1,    -1,    -1,    -1,    53,  2074,  2075,
      -1,    -1,    -1,    -1,  2080,    -1,    -1,  2083,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  2093,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  2101,    -1,  2103,  2104,    -1,
      86,    -1,  2108,    -1,    -1,  2111,  2112,  2113,  2114,    -1,
      -1,    -1,    -1,  2119,  2120,   101,  2122,    -1,  2124,  2125,
      -1,  2127,  2128,    -1,   110,    -1,  2132,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   128,    -1,    -1,    -1,    -1,    -1,   134,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   143,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     156,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   168,   363,   364,   365,   366,   367,   368,   369,
     370,   371,   372,   373,   374,   375,   376,   377,   378,   379,
     380,   381,   382,   383,   384,   385,   386,   387,   388,   389,
     390,   391,   392,   393,   394,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     0,     1,    -1,    -1,    -1,    -1,    -1,    -1,
       8,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   237,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    -1,    -1,    37,
      -1,   257,    -1,   259,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   272,    -1,    -1,    -1,
      -1,    -1,    -1,    61,    -1,    -1,    64,    -1,    -1,    -1,
      68,    -1,    -1,    -1,    -1,   291,   292,    75,    -1,    77,
      -1,    -1,    -1,    81,    -1,    -1,    -1,    -1,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   313,    -1,   315,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   334,   335,
     336,   337,   338,   339,   340,   341,   342,   343,   344,   345,
     346,   347,   130,   349,    -1,   351,   134,    -1,    -1,    -1,
      -1,    -1,    -1,   359,   360,   361,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     158,    -1,    -1,    -1,    -1,   163,   164,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     396,   397,   398,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   408,   409,   410,   411,   412,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   421,    -1,    -1,   424,    -1,
      -1,     8,    -1,    -1,    -1,    -1,    -1,   433,   434,   435,
      -1,    -1,   438,   439,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   232,    32,    -1,    -1,    -1,    -1,
      -1,   239,   240,   241,    -1,   243,   244,   245,   246,   247,
      -1,   249,    -1,    -1,    -1,    -1,   254,   255,   256,    -1,
      -1,    -1,    -1,   261,    61,    -1,    -1,    64,    -1,    -1,
      -1,    68,    -1,   271,    -1,   273,   274,    -1,    75,    -1,
      77,    -1,    -1,    -1,    81,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,    39,    40,
      41,    42,    43,   301,   302,   303,    -1,    -1,    49,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   315,   316,    -1,
     318,    -1,    -1,   321,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   130,    -1,    -1,    -1,   134,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   356,    -1,
      -1,   158,    -1,    -1,    -1,    -1,   163,   164,    -1,    -1,
      -1,    -1,   113,   114,   115,    -1,    -1,   118,   119,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   150,
      -1,    -1,    -1,    -1,    -1,    -1,   157,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   232,    -1,    -1,    -1,    -1,
      -1,    -1,   239,   240,    -1,    -1,   243,   244,   245,   246,
     247,    -1,   249,    -1,    -1,    -1,    -1,   254,   255,   256,
      -1,    -1,    -1,    -1,   261,   206,   207,   208,    -1,    -1,
      -1,    -1,    -1,    -1,   271,    -1,   273,   274,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   228,   229,    -1,
     231,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   301,   302,   303,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   315,   316,
      -1,   318,    -1,    -1,   321,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   280,
     281,    -1,    -1,    -1,   285,   286,   287,   288,   289,   290,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   356
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int16 yystos[] =
{
       0,     1,   474,   480,     0,     8,    32,    61,    64,    68,
      75,    77,    81,   130,   134,   158,   163,   164,   232,   239,
     240,   243,   244,   245,   246,   247,   249,   254,   255,   256,
     261,   271,   273,   274,   301,   302,   303,   315,   316,   318,
     321,   356,   475,   478,   479,   482,   483,   484,   485,   486,
     487,   488,   492,   493,   496,   497,   592,   594,   596,   597,
     624,   626,   627,   645,   646,   652,   653,   660,   661,   662,
     686,   687,   704,   706,   837,   839,   857,   859,   868,   906,
     907,   908,   909,   910,   922,   934,   935,   936,   937,   938,
     939,   940,   840,   173,   477,   498,   707,   477,    93,    94,
     663,   688,   625,   869,   172,   476,   477,   477,   477,   477,
     477,   477,   477,   477,   477,   172,    93,    94,   858,   860,
     173,   173,   173,   477,   466,   319,   320,   489,    84,   100,
     490,   477,   494,   501,   171,   595,    50,    64,   630,   636,
     639,   648,   655,   691,   710,   843,    37,   241,   481,   941,
     171,   466,   171,   171,   477,   466,   466,   171,   171,   171,
     870,   466,   171,   477,   466,   466,   466,   466,   911,   466,
     466,   466,   466,   466,   466,   171,   171,   466,   466,   466,
     466,   466,    93,    94,   491,   236,   466,    14,    26,    30,
      37,   106,   110,   147,   166,   296,   495,     9,    14,    15,
      33,    35,    37,    53,    86,   101,   110,   128,   134,   143,
     156,   168,   237,   257,   259,   272,   291,   292,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   313,   315,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   349,   351,   359,   360,   361,   396,   397,
     398,   408,   409,   410,   411,   412,   421,   424,   433,   434,
     435,   438,   439,   499,   502,    29,   413,    48,    64,   110,
     153,   164,   237,   598,   605,   606,   608,   612,   613,   616,
     617,    29,   629,   640,   636,   637,   641,    37,   125,   647,
     649,   650,    37,   142,   654,   656,   659,    16,    37,   131,
     141,   237,   426,   689,   692,   693,   694,   695,   698,    16,
      18,    36,    50,    51,    66,    84,    88,   100,   106,   130,
     131,   132,   141,   148,   237,   267,   316,   405,   445,   613,
     705,   711,   714,   718,   723,   728,   729,   730,   731,   732,
     733,   734,   735,   737,   739,   740,   742,   743,   744,   805,
     806,   807,   814,   816,   817,   130,   198,   199,   200,   201,
     202,   203,   741,   838,   844,   850,    67,   466,   664,    37,
      58,    64,    67,    68,    81,   100,   130,   154,   163,   164,
     871,   466,   466,   923,   466,   469,   469,   466,   593,    99,
      73,   419,   158,    74,    85,    78,   167,   298,   173,    25,
     477,    54,   161,   406,   407,   580,   477,   500,   477,   477,
     477,   124,   477,   477,   477,   477,    28,    92,   122,   191,
     238,   354,   579,   477,   507,   258,   467,   477,   477,   477,
     293,   294,   295,   559,   293,   477,   513,   477,   514,   477,
     477,   516,   477,   517,   477,   477,   477,   477,   477,   477,
     477,   477,   477,   477,   477,   477,   477,   477,   477,   477,
     519,   400,   401,   543,   477,   173,   477,   477,   477,   452,
     525,   477,   477,   477,   477,   477,   528,   543,   503,   467,
     477,   466,   477,   614,   618,   477,   599,   609,    37,   622,
     607,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   466,   467,   477,
     615,   703,   619,   628,   171,   171,   237,   596,   631,   632,
     633,   638,    33,    72,    91,   109,   110,   134,   168,   351,
     642,   134,   171,    61,   171,   657,    22,    96,   238,   690,
     477,   696,   784,   785,   699,    10,    22,    23,    24,    38,
      96,   112,   238,   325,   350,   719,   815,   736,   171,   171,
     738,   703,   745,   477,   477,    10,    50,   190,   715,   712,
     268,   269,   466,    64,   809,   173,   446,   447,   724,   466,
     615,   703,    37,   708,   171,   796,   748,    37,    64,    97,
     103,   109,   163,   168,   770,   771,   792,   819,   846,   847,
     848,   171,   849,   477,   845,    37,   841,   851,   415,   667,
     232,   873,   878,   872,   875,   879,   874,   881,   880,   876,
     877,   250,   251,   925,   926,   927,   250,   251,   913,   914,
     915,   172,   212,   467,   866,   866,   171,   477,   477,   477,
     477,   477,   477,   477,   173,   466,   258,   477,   466,   466,
     466,   171,   466,   466,   477,   466,   477,   258,   477,   466,
     466,   505,   466,   466,   466,   171,   571,   572,   467,   477,
     466,   466,   466,   477,   508,   168,   297,   477,   466,   258,
     477,   554,   466,   554,   515,   466,   554,   466,   554,   518,
     466,   466,   466,   466,   466,   466,   466,   466,   466,   466,
     466,   477,   466,   466,   168,   363,   364,   365,   366,   367,
     368,   369,   370,   371,   372,   373,   374,   375,   376,   377,
     378,   379,   380,   381,   382,   383,   384,   385,   386,   387,
     388,   389,   390,   391,   392,   393,   394,   582,   477,   466,
     522,   524,   466,   355,   399,    21,   395,   458,   533,   466,
     466,   466,   466,   477,   466,   703,   789,   477,   448,   529,
     703,   466,   466,   171,   171,   466,   171,   171,   610,   611,
     623,   608,   477,   477,   466,   466,   615,   103,   109,   620,
     630,   466,   171,   172,   173,   634,   171,    37,   643,   633,
      54,   161,   477,   477,   703,   477,   477,   477,   477,   171,
     466,   477,   658,   466,   466,   466,   171,    13,   169,   170,
     183,   466,   697,   466,   171,   786,   700,   358,   427,    46,
     133,   144,   145,   317,   431,   721,   350,    11,    12,   105,
     107,   151,   152,   722,    55,    56,    89,   106,   133,   357,
     720,   466,   171,   171,   171,   466,   171,   466,   171,   466,
      13,   466,   466,   466,   169,   170,   183,   466,   716,   171,
     713,   717,   466,   466,   810,   808,    64,   725,   726,   466,
     466,   615,   709,   466,   477,   466,    14,    27,    33,    37,
      44,    45,    57,    63,    65,    66,    70,    71,    76,    90,
     104,   106,   108,   110,   116,   117,   120,   121,   126,   127,
     146,   159,   162,   165,   237,   251,   260,   262,   263,   264,
     265,   266,   326,   327,   328,   329,   330,   331,   332,   333,
     349,   414,   422,   428,   613,   746,   749,   764,   765,   772,
     408,   651,   651,   651,    62,   237,   781,   782,   783,   477,
      37,   774,    37,    39,    40,    41,    42,    43,    49,   113,
     114,   115,   118,   119,   150,   157,   206,   207,   208,   228,
     229,   231,   280,   281,   285,   286,   287,   288,   289,   290,
     818,   820,   824,   825,   833,   171,   796,   796,   169,   170,
     797,   169,   170,   804,   855,   796,   842,    37,   198,   199,
     852,   466,   668,   171,   171,   171,   171,   171,   171,   171,
     171,   171,   171,   477,   928,    37,   924,   926,   477,   916,
      37,   912,   914,   212,   215,   216,   225,   461,   467,   477,
     864,   865,   866,   866,   462,   466,   470,   861,   861,   171,
     466,   466,   466,   466,   466,   466,   466,   466,   467,   466,
     466,   466,   467,   466,   125,   402,   454,   891,   892,   171,
     172,   173,   466,   572,   467,   573,   574,   477,   466,   296,
     560,   477,   173,   466,   467,   466,   466,   314,   558,   466,
     466,   558,   466,   477,   466,   477,   168,   581,   551,   477,
     477,   168,   477,   168,   466,   466,   791,   477,   168,   530,
     703,   466,   466,   171,   172,   173,   466,   611,   171,   477,
     466,   651,   651,   631,   633,   635,   466,   644,   466,   466,
     466,   466,   703,   466,    13,   149,   477,   477,   477,   477,
     171,   172,   173,   787,   171,   466,   701,   466,   466,   466,
     466,   477,   171,   172,   173,   466,   717,   171,    37,   809,
     727,   466,   725,   466,   171,   477,   477,     6,   111,    46,
      55,    56,    89,   747,   477,   477,   754,   171,   477,   750,
     477,   477,   751,   752,   477,   477,   477,   477,   477,   477,
     477,    56,    89,   137,   139,   768,     5,    46,   112,   769,
     477,     7,    17,    31,    52,   106,   129,   767,   477,   477,
     756,   753,   755,   477,   477,   173,   477,   477,   173,   173,
     173,   173,   173,   173,   173,   173,   757,   760,   758,   759,
     140,   466,   703,    16,    37,   766,   171,   477,    62,   789,
      62,   789,    62,   703,   651,   784,   651,   783,   466,   771,
     148,   477,   477,   477,   477,   821,   477,   477,   477,   477,
     822,    60,    82,    83,   834,   207,   477,   831,   467,   829,
     830,   466,   172,   172,   172,   227,   282,   283,   284,   826,
      59,   226,   230,   828,   466,   466,   477,   477,   466,   477,
     477,   466,    37,   204,   856,   466,   171,   171,   853,   854,
      64,   171,   237,   420,   429,   430,   594,   645,   665,   669,
     670,   672,   674,   676,   679,   233,   234,   235,   270,   882,
     882,   882,   882,   882,   882,   882,   882,   882,   882,   466,
     477,   929,   245,   861,   466,   477,   917,   249,   861,   865,
     865,   212,   467,   864,   864,   865,   866,   217,   218,   219,
     220,   221,   222,   223,   224,   461,   462,   463,   464,   469,
     471,   472,   867,   213,   217,   218,   223,   224,   470,   862,
     223,   224,   867,   468,   866,   466,   467,   575,   576,   467,
     577,   578,   893,     9,    64,   236,   300,   348,   440,   441,
     450,   894,   891,   477,   468,   574,   468,   173,   511,   509,
     703,   466,   466,   523,   526,   477,   466,   138,   416,   418,
     423,   449,   466,   552,   553,   168,   134,   477,   534,   477,
     466,   703,   790,   527,   477,   404,   520,   403,   468,   621,
     703,   643,   466,   633,   171,   466,   477,   477,   466,   136,
     466,   466,   615,   466,   466,   171,   615,   466,   466,   466,
     466,   466,   466,   155,   466,   171,   466,   466,   477,   171,
     466,   171,   466,   466,   171,   477,   466,   466,   466,   466,
     466,   466,   466,   466,   466,   466,   466,   466,   466,   171,
     762,   763,   477,   171,    64,   902,   902,   902,   466,   466,
     902,   902,   902,   902,    64,   904,   904,   902,   904,   363,
     364,   365,   366,   367,   368,   369,   370,   371,   372,   373,
     374,   375,   376,   377,   378,   379,   380,   381,   382,   383,
     384,   385,   386,   387,   388,   389,   390,   391,   392,   393,
     394,   761,   171,   172,   171,   466,   703,   466,   615,   719,
     770,   773,   789,   791,   789,   790,   703,   703,   703,   703,
     477,   477,   477,   477,   171,   835,   477,   477,   477,   477,
     171,   836,   466,   477,   466,   477,   477,   466,   830,   466,
     466,   466,   477,   282,   283,   284,   827,   477,   157,    34,
      34,    34,    34,   203,   477,   796,   796,   680,   171,   172,
     173,   677,   675,   671,   673,    37,   666,   172,   236,   888,
     888,   171,   466,   466,   466,   466,   466,   466,   466,   466,
     466,   466,   466,   477,   466,   477,   862,   865,   864,   468,
     468,   864,   864,   864,   864,   864,   864,   864,   865,   865,
     865,   865,   213,   866,   866,   866,   866,   477,   468,   576,
     477,   468,   578,   455,   887,   173,   895,   477,   477,   477,
     477,   477,   477,   466,   477,   466,   466,   561,   569,   570,
     703,   399,   548,   168,   355,   544,   477,   477,   477,   477,
     399,   569,   456,   589,   168,   547,   477,   556,   477,   789,
     703,   466,   466,   466,   702,   811,    34,   466,   477,   466,
     466,   466,   477,   171,   172,   173,   466,   763,   477,   466,
     903,   466,   466,   466,   466,   466,   466,   466,   905,   466,
     466,   466,   466,   466,   466,   466,   466,   466,   615,   466,
     466,    37,   237,   778,   779,   780,   791,   466,   790,   790,
     703,   466,   794,   793,   466,   466,   466,   466,   171,   466,
     466,   466,   466,   466,   171,   466,   208,   477,   477,   227,
     477,   834,   477,   477,   477,   477,   205,   466,   466,   171,
     676,   678,   171,   171,   171,   171,   477,   477,   890,   173,
     889,   252,   930,   931,   252,   918,   919,   864,   865,   866,
     862,   214,   470,   863,   477,   466,   477,   466,   171,   477,
     236,   399,   399,   451,   468,   570,   466,   477,   173,   466,
     555,   477,   352,   353,   549,   477,   477,   466,   466,   506,
     477,   535,   477,   477,   477,   466,   504,   468,   557,   703,
     477,   790,   466,   109,   813,   477,   466,   466,   466,   171,
     171,   466,   784,   444,   775,   780,    34,   795,   790,   791,
     795,   171,   171,   477,   477,   477,   477,   823,   227,   138,
     138,   138,   138,   477,   681,   466,   676,   477,   466,   466,
     477,   477,   931,   477,   919,   863,   863,   864,   214,   866,
     468,   468,   896,   898,   477,   477,   477,   477,   466,   512,
     510,   556,   355,   550,   545,   399,   541,   542,   134,   168,
     538,   432,   531,   532,   466,   466,   790,   703,   812,    13,
     235,   436,   437,   776,   777,   477,   466,   791,   466,   466,
     795,   466,   833,   468,   466,   477,   477,   832,   477,   477,
     477,   477,   477,   466,   168,   466,   466,   466,   864,   865,
     136,   900,    21,   236,   299,   883,   477,   897,   899,   450,
     562,   229,   567,   567,   468,   477,   466,   442,   546,   477,
     466,   541,   477,   477,   569,   477,   466,   531,   521,   417,
     791,   703,   813,   477,    20,   134,   788,    13,   795,   466,
     466,   477,   477,   160,   477,   798,   800,   477,   932,   920,
     477,   477,   443,   901,   453,   884,   477,   168,   297,   564,
     477,   399,   477,   134,   540,   536,   539,   134,   600,   466,
     477,   138,   477,   477,   466,   477,   466,   466,   477,   477,
     477,    64,    64,   466,   253,   248,   236,   477,   477,   477,
     173,   229,   568,   477,   477,   590,   569,   168,   586,   587,
     477,   171,   466,   477,   138,   477,   477,   466,   799,   801,
     682,   477,   933,   477,   921,   477,   399,   566,   565,   173,
     569,   168,   591,   537,   477,   586,   171,   477,   477,   477,
     466,   802,   802,   684,   466,   477,   466,   477,   477,   477,
     569,   570,   563,   466,   477,   583,   589,   171,   477,   477,
     171,   803,    14,    35,    37,   110,   134,   272,   412,   685,
     885,   466,   466,   570,   399,   168,   584,   477,   466,   477,
      25,   477,   683,   124,   477,   477,   477,   457,   886,   466,
     477,   477,   588,   404,   477,   466,   171,   477,   466,   466,
     466,   134,   585,   569,   477,   466,   466,   477,   569,   477,
     466,   351,   477,   477,   477,   477,   466,   601,   602,    86,
      88,    98,   425,   603,   477,   477,   604,   477,   477,   477,
     171,   477,   477,   466,   466,   466,   477,   466
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int16 yyr1[] =
{
       0,   473,   474,   476,   475,   477,   478,   479,   480,   480,
     480,   481,   481,   482,   482,   482,   482,   482,   482,   482,
     482,   482,   482,   482,   482,   482,   482,   482,   482,   482,
     482,   482,   482,   482,   482,   482,   482,   482,   482,   482,
     482,   482,   482,   482,   482,   482,   482,   482,   482,   482,
     482,   483,   483,   484,   484,   485,   486,   487,   488,   489,
     489,   490,   490,   491,   491,   492,   493,   494,   494,   495,
     495,   495,   495,   495,   495,   495,   495,   496,   498,   497,
     500,   499,   501,   501,   502,   503,   504,   502,   502,   502,
     502,   502,   502,   502,   502,   502,   502,   502,   502,   502,
     505,   502,   506,   502,   502,   502,   502,   502,   502,   502,
     502,   502,   502,   502,   502,   502,   502,   502,   502,   507,
     502,   508,   502,   502,   502,   509,   510,   502,   511,   512,
     502,   502,   513,   502,   502,   514,   502,   515,   502,   502,
     516,   502,   502,   517,   502,   518,   502,   519,   502,   502,
     502,   502,   520,   521,   502,   502,   502,   502,   502,   502,
     502,   502,   502,   502,   502,   502,   502,   502,   502,   502,
     522,   502,   523,   502,   524,   502,   502,   525,   502,   526,
     502,   527,   502,   502,   502,   528,   502,   529,   529,   530,
     530,   531,   531,   532,   534,   535,   536,   537,   533,   538,
     539,   533,   540,   533,   541,   541,   542,   543,   543,   543,
     544,   545,   544,   544,   546,   546,   547,   547,   548,   548,
     549,   549,   549,   550,   550,   551,   551,   552,   552,   552,
     553,   553,   553,   554,   555,   554,   556,   556,   557,   558,
     558,   559,   559,   559,   561,   562,   563,   560,   564,   565,
     564,   566,   564,   568,   567,   569,   569,   570,   570,   571,
     571,   572,   572,   572,   573,   573,   574,   575,   575,   576,
     577,   577,   578,   579,   579,   579,   579,   579,   579,   580,
     580,   580,   580,   581,   581,   582,   582,   582,   582,   582,
     582,   582,   582,   582,   582,   582,   582,   582,   582,   582,
     582,   582,   582,   582,   582,   582,   582,   582,   582,   582,
     582,   582,   582,   582,   582,   582,   582,   583,   583,   585,
     584,   586,   586,   588,   587,   589,   589,   590,   590,   591,
     592,   593,   592,   595,   594,   596,   597,   597,   597,   599,
     600,   601,   598,   602,   602,   603,   603,   603,   604,   603,
     605,   605,   606,   607,   607,   608,   608,   608,   609,   608,
     608,   610,   610,   611,   611,   611,   612,   612,   612,   612,
     614,   613,   615,   615,   615,   615,   615,   615,   615,   615,
     615,   615,   615,   615,   615,   615,   615,   615,   616,   618,
     617,   619,   619,   620,   621,   620,   623,   622,   625,   624,
     626,   628,   627,   629,   629,   630,   630,   631,   631,   632,
     632,   634,   633,   635,   635,   633,   633,   633,   636,   637,
     637,   638,   640,   639,   641,   641,   642,   642,   642,   642,
     642,   642,   642,   642,   642,   644,   643,   645,   646,   647,
     648,   648,   649,   649,   650,   651,   651,   652,   653,   654,
     655,   655,   656,   657,   657,   658,   659,   660,   661,   663,
     664,   665,   662,   666,   666,   667,   667,   668,   668,   669,
     669,   669,   669,   669,   669,   669,   671,   670,   673,   672,
     675,   674,   677,   676,   678,   678,   676,   676,   676,   680,
     681,   682,   683,   679,   684,   684,   685,   685,   685,   685,
     685,   685,   686,   688,   687,   690,   689,   691,   691,   692,
     692,   692,   692,   692,   693,   694,   694,   694,   695,   696,
     696,   697,   697,   697,   699,   698,   700,   700,   702,   701,
     703,   703,   705,   704,   707,   706,   709,   708,   710,   710,
     711,   711,   711,   711,   711,   711,   711,   711,   711,   711,
     711,   711,   711,   711,   711,   711,   711,   711,   711,   711,
     711,   712,   711,   713,   713,   714,   715,   715,   716,   716,
     716,   717,   717,   717,   718,   719,   719,   719,   719,   719,
     719,   719,   719,   719,   719,   719,   719,   719,   719,   719,
     720,   720,   720,   720,   720,   720,   721,   721,   721,   721,
     721,   721,   722,   722,   722,   722,   722,   722,   723,   724,
     724,   724,   725,   725,   727,   726,   728,   729,   730,   730,
     730,   731,   732,   733,   733,   733,   733,   734,   736,   735,
     738,   737,   739,   739,   740,   741,   742,   743,   745,   744,
     747,   746,   748,   748,   749,   749,   749,   749,   749,   749,
     750,   749,   749,   749,   749,   749,   749,   749,   749,   749,
     749,   749,   749,   749,   749,   751,   749,   752,   749,   753,
     749,   754,   749,   749,   749,   749,   749,   749,   749,   749,
     749,   749,   749,   755,   749,   756,   749,   749,   749,   749,
     749,   749,   749,   749,   749,   749,   749,   749,   749,   749,
     749,   749,   757,   749,   758,   749,   759,   749,   760,   749,
     761,   761,   761,   761,   761,   761,   761,   761,   761,   761,
     761,   761,   761,   761,   761,   761,   761,   761,   761,   761,
     761,   761,   761,   761,   761,   761,   761,   761,   761,   761,
     761,   761,   762,   762,   763,   763,   763,   764,   764,   764,
     764,   764,   765,   766,   766,   767,   767,   767,   767,   767,
     767,   768,   768,   768,   768,   769,   769,   769,   769,   770,
     772,   773,   771,   771,   771,   771,   771,   771,   771,   771,
     771,   774,   774,   775,   775,   776,   776,   777,   777,   777,
     778,   778,   779,   779,   780,   781,   781,   782,   782,   783,
     785,   784,   786,   786,   787,   787,   788,   788,   788,   789,
     790,   791,   791,   793,   792,   794,   792,   795,   796,   796,
     798,   799,   797,   800,   801,   797,   797,   797,   802,   802,
     803,   804,   804,   805,   805,   806,   807,   808,   808,   810,
     811,   809,   812,   812,   813,   815,   814,   816,   817,   818,
     819,   819,   821,   820,   822,   820,   823,   820,   820,   820,
     820,   820,   820,   820,   820,   820,   820,   820,   820,   820,
     820,   820,   820,   820,   820,   820,   820,   824,   824,   824,
     825,   825,   825,   825,   826,   826,   826,   827,   827,   827,
     828,   828,   829,   829,   830,   831,   831,   832,   832,   832,
     833,   833,   834,   834,   834,   835,   835,   836,   836,   838,
     837,   840,   839,   842,   841,   843,   843,   845,   844,   846,
     844,   847,   844,   848,   844,   844,   849,   844,   844,   850,
     851,   851,   853,   852,   854,   852,   855,   855,   856,   858,
     857,   860,   859,   861,   861,   861,   862,   862,   863,   863,
     864,   864,   864,   864,   864,   864,   864,   864,   865,   865,
     865,   865,   865,   865,   865,   865,   865,   865,   865,   865,
     865,   865,   865,   866,   866,   866,   866,   867,   867,   867,
     867,   867,   867,   867,   867,   867,   869,   868,   870,   870,
     872,   871,   873,   871,   874,   871,   875,   871,   876,   871,
     877,   871,   878,   871,   879,   871,   880,   871,   881,   871,
     882,   882,   882,   882,   882,   883,   883,   883,   883,   883,
     884,   885,   884,   886,   886,   887,   887,   888,   888,   889,
     889,   890,   890,   891,   891,   892,   893,   892,   892,   894,
     895,   896,   894,   897,   894,   894,   898,   894,   894,   894,
     899,   894,   894,   894,   900,   900,   901,   901,   902,   903,
     902,   905,   904,   906,   907,   908,   909,   911,   910,   912,
     913,   913,   914,   914,   916,   915,   917,   917,   918,   918,
     920,   919,   921,   921,   923,   922,   924,   925,   925,   926,
     926,   928,   927,   929,   929,   930,   930,   932,   931,   933,
     933,   934,   935,   936,   937,   938,   939,   940,   940,   941
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     3,     0,     4,     1,     3,     3,     0,     2,
       1,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     3,     3,     3,     2,     3,     4,     3,     1,
       1,     1,     1,     1,     1,     4,     1,     0,     2,     4,
       4,     4,     4,     4,     4,     4,     4,     3,     0,     3,
       0,     3,     0,     2,     3,     0,     0,     9,     3,     3,
       3,     4,     3,     4,     3,     4,     3,     3,     3,     3,
       0,     6,     0,     9,     3,     4,     7,     4,     7,     3,
       3,     3,     3,     3,     3,     3,     3,     6,     6,     0,
       4,     0,     4,     4,     4,     0,     0,     9,     0,     0,
       9,     3,     0,     4,     3,     0,     4,     0,     5,     3,
       0,     4,     3,     0,     4,     0,     5,     0,     4,     2,
       3,     3,     0,     0,     9,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     4,     3,     3,     3,     3,
       0,     5,     0,     9,     0,     5,     7,     0,     4,     0,
       7,     0,     7,     3,     3,     0,     5,     0,     1,     0,
       2,     0,     2,     4,     0,     0,     0,     0,    11,     0,
       0,     9,     0,     9,     0,     2,     4,     0,     1,     1,
       0,     0,     4,     2,     0,     2,     0,     2,     0,     2,
       0,     1,     1,     0,     4,     0,     2,     1,     2,     2,
       1,     1,     1,     1,     0,     7,     0,     2,     1,     0,
       1,     1,     1,     1,     0,     0,     0,    12,     0,     0,
       5,     0,     5,     0,     5,     0,     2,     0,     2,     1,
       2,     2,     2,     2,     1,     2,     4,     1,     2,     4,
       1,     2,     4,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     0,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     0,     2,     0,
       4,     0,     2,     0,     6,     0,     2,     0,     2,     6,
       3,     0,     7,     0,     4,     1,     2,     3,     3,     0,
       0,     0,    26,     0,     2,     4,     4,     6,     0,     4,
       1,     1,     2,     0,     2,     1,     1,     3,     0,     4,
       1,     1,     2,     2,     2,     2,     2,     3,     4,     3,
       0,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     0,
       4,     0,     2,     5,     0,     8,     0,     3,     0,     3,
       5,     0,     7,     0,     1,     1,     2,     0,     1,     1,
       2,     0,     4,     1,     2,     2,     2,     2,     2,     0,
       2,     3,     0,     4,     0,     2,     3,     3,     4,     5,
       4,     5,     3,     3,     3,     0,     3,     3,     1,     2,
       0,     2,     5,     6,     1,     0,     2,     3,     1,     2,
       0,     2,     3,     0,     2,     2,     2,     4,     3,     0,
       0,     0,     8,     1,     2,     0,     2,     0,     2,     1,
       1,     1,     1,     1,     1,     1,     0,     4,     0,     4,
       0,     5,     0,     4,     1,     2,     2,     2,     2,     0,
       0,     0,     0,    12,     0,     2,     3,     3,     4,     4,
       3,     3,     3,     0,     3,     0,     3,     0,     2,     5,
       1,     1,     1,     1,     3,     3,     3,     3,     3,     0,
       2,     1,     1,     1,     0,     4,     0,     2,     0,     3,
       2,     4,     0,     4,     0,     3,     0,     3,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     3,     1,     1,     1,
       1,     0,     4,     1,     2,     3,     0,     2,     1,     1,
       1,     2,     2,     2,     3,     1,     2,     1,     1,     2,
       2,     1,     1,     1,     1,     2,     1,     1,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     4,     1,
       1,     1,     0,     2,     0,     3,     3,     4,     3,     3,
       3,     3,     3,     2,     3,     4,     3,     2,     0,     4,
       0,     4,     3,     3,     1,     1,     5,     3,     0,     3,
       0,     3,     0,     2,     2,     3,     4,     3,     4,     5,
       0,     4,     3,     1,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     0,     4,     0,     5,     0,
       5,     0,     5,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     4,     0,     4,     0,     4,     4,     2,     4,
       4,     4,     3,     3,     4,     4,     4,     4,     4,     4,
       4,     4,     0,     4,     0,     4,     0,     4,     0,     4,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     2,     2,     2,     3,     3,     4,
       3,     3,     1,     0,     3,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     0,     1,     1,     1,     2,
       0,     0,     9,     3,     5,     7,     5,     7,     7,     9,
       1,     0,     2,     0,     1,     0,     1,     1,     1,     1,
       0,     1,     1,     2,     2,     0,     1,     1,     2,     2,
       0,     2,     2,     2,     1,     1,     0,     2,     2,     1,
       1,     0,     2,     0,     7,     0,     8,     7,    11,     4,
       0,     0,    10,     0,     0,    10,     6,     6,     0,     2,
       1,     6,     6,     3,     2,     1,     4,     0,     2,     0,
       0,     7,     0,     2,     5,     0,     4,     3,     1,     2,
       0,     2,     0,     4,     0,     4,     0,    10,     9,     3,
       3,     4,     4,     4,     4,     4,     4,     4,     4,     3,
       7,     8,     6,     3,     3,     3,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     5,     1,     2,     0,     4,     7,
       1,     1,     1,     1,     1,     1,     2,     1,     2,     0,
       4,     0,     3,     0,     3,     0,     2,     0,     4,     0,
       4,     0,     4,     0,     4,     4,     0,     4,     5,     2,
       0,     2,     0,     4,     0,     4,     0,     2,     5,     0,
       6,     0,     6,     0,     1,     1,     1,     2,     1,     2,
       3,     3,     3,     3,     2,     3,     6,     1,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     2,     3,
       6,     1,     1,     3,     3,     6,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     0,     5,     0,     2,
       0,     5,     0,     5,     0,     5,     0,     5,     0,     5,
       0,     5,     0,     5,     0,     5,     0,     5,     0,     5,
       3,     3,     1,     2,     2,     0,     1,     2,     5,     3,
       0,     0,     6,     0,     1,     0,     1,     0,     3,     0,
       1,     0,     1,     0,     2,     1,     0,     3,     1,     0,
       0,     0,     5,     0,     6,     2,     0,     5,     2,     5,
       0,     6,     2,     6,     0,     1,     0,     1,     0,     0,
       3,     0,     3,     4,     3,     3,     3,     0,     7,     2,
       1,     2,     3,     1,     0,     5,     1,     2,     1,     2,
       0,     7,     1,     2,     0,     7,     2,     1,     2,     3,
       1,     0,     5,     1,     2,     1,     2,     0,     7,     1,
       2,     3,     3,     3,     3,     3,     3,     0,     2,     1
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
        yyerror (YY_("syntax error: cannot back up")); \
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
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
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
                 int yyrule)
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
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
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
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
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
      yychar = yylex ();
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
  case 2: /* lef_file: rules extension_opt end_library  */
#line 346 "lef.y"
      {
        // 11/16/2001 - Wanda da Rosa - pcr 408334
        // Return 1 if there are errors
        if (lefData->lef_errors)
           return 1;

        double parserVersion = (lefSettings->AllowVer60Plus)? lefData->versionNum : 5.8 ;
        
        if (!lefData->hasVer) {
              char temp[300];
              sprintf(temp, "No VERSION statement found, using the default value %2g.", parserVersion);
              lefWarning(2001, temp);            
        }        
        //only pre 5.6, 5.6 it is obsolete
        if (!lefData->hasNameCase && lefData->versionNum < 5.6)
           lefWarning(2002, "NAMESCASESENSITIVE is a required statement in LEF files with version 5.5 and earlier.\nWithout NAMESCASESENSITIVE defined, the LEF file is technically incorrect.\nRefer to the LEF/DEF 5.5 or earlier Language Reference manual on how to define this statement.");
        if (!lefData->hasBusBit && lefData->versionNum < 5.6)
           lefWarning(2003, "BUSBITCHARS is a required statement in LEF files with version 5.5 and earlier.\nWithout BUSBITCHARS defined, the LEF file is technically incorrect.\nRefer to the LEF/DEF 5.5 or earlier Language Reference manual on how to define this statement.");
        if (!lefData->hasDivChar && lefData->versionNum < 5.6)
           lefWarning(2004, "DIVIDERCHAR is a required statement in LEF files with version 5.5 and earlier.\nWithout DIVIDECHAR defined, the LEF file is technically incorrect.\nRefer to the LEF/DEF 5.5 or earlier Language Reference manual on how to define this statement.");

      }
#line 4025 "lef.tab.c"
    break;

  case 3: /* $@1: %empty  */
#line 369 "lef.y"
                   { lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 4031 "lef.tab.c"
    break;

  case 4: /* version: K_VERSION $@1 T_STRING ';'  */
#line 370 "lef.y"
      { 
		 // More than 1 VERSION in lef file within the open file - It's wrong syntax, 
		 // but copy old behavior - initialize lef reading.
         if (lefData->hasVer)     
         {
			lefData->initRead();
		 }

         lefData->versionNum = convert_name2num((yyvsp[-1].string));
         if (lefData->versionNum == 0.0 || lefData->versionNum > CURRENT_VERSION + 0.0001) {
            char temp[300];
            sprintf(temp,
                    "The execution has been stopped because the DEF parser %.2f does not support LEF file with version '%s'. Update your DEF file to version  %.2f or earlier.",
                    CURRENT_VERSION, 
                    (yyvsp[-1].string), 
                    CURRENT_VERSION);
                    lefError(1503, temp);
            return 1;
         }

         if (!lefSettings->AllowVer60Plus && lefData->versionNum > 5.8001) {
            char temp[300];
            sprintf(temp,
                  "The execution has been stopped because LEF parser can process %.2f version LEF files, but the translator software does not support processing of LEF files with version greater than 5.8. Update the translator software.",
                  CURRENT_VERSION);
            lefError(1703, temp);
            return 1;
         }

         if (lefCallbacks->VersionStrCbk) {
            CALLBACK(lefCallbacks->VersionStrCbk, lefrVersionStrCbkType, (yyvsp[-1].string));
         } else {
            if (lefCallbacks->VersionCbk)
               CALLBACK(lefCallbacks->VersionCbk, lefrVersionCbkType, lefData->versionNum);
         }
         if (lefData->versionNum > 5.3 && lefData->versionNum < 5.4) {
            lefData->ignoreVersion = 1;
         }
         lefData->use5_3 = lefData->use5_4 = 0;
         lefData->lef_errors = 0;
         lefData->hasVer = 1;
         if (lefData->versionNum < 5.6) {
            lefData->doneLib = 0;
            lefData->namesCaseSensitive = lefSettings->CaseSensitive;
         } else {
            lefData->doneLib = 1;
            lefData->namesCaseSensitive = 1;
         }
      }
#line 4085 "lef.tab.c"
    break;

  case 5: /* int_number: NUMBER  */
#line 421 "lef.y"
      {
         // int_number represent 'integer-like' type. It can have fraction and exponent part 
         // but the value shouldn't exceed the 64-bit integer limit. 
         if (!(( yylval.dval >= lefData->leflVal) && ( yylval.dval <= lefData->lefrVal))) { // YES, it isn't really a number 
            char *str = (char*) lefMalloc(strlen(lefData->current_token) + strlen(lefData->lefrFileName) + 350);
            sprintf(str, "ERROR (LEFPARS-203) Number has exceeded the limit for an integer. See file %s at line %d.\n",
                    lefData->lefrFileName, lefData->lef_nlines);
            fflush(stdout);
            lefiError(0, 203, str);
            free(str);
            lefData->lef_errors++;
        }

        (yyval.dval) = yylval.dval ;
      }
#line 4105 "lef.tab.c"
    break;

  case 6: /* dividerchar: K_DIVIDERCHAR QSTRING ';'  */
#line 438 "lef.y"
      {
        if (lefCallbacks->DividerCharCbk) {
          if (strcmp((yyvsp[-1].string), "") != 0) {
             CALLBACK(lefCallbacks->DividerCharCbk, lefrDividerCharCbkType, (yyvsp[-1].string));
          } else {
             CALLBACK(lefCallbacks->DividerCharCbk, lefrDividerCharCbkType, "/");
             lefWarning(2005, "DIVIDERCHAR has an invalid null value. Value is set to default /");
          }
        }
        lefData->hasDivChar = 1;
      }
#line 4121 "lef.tab.c"
    break;

  case 7: /* busbitchars: K_BUSBITCHARS QSTRING ';'  */
#line 451 "lef.y"
      {
        if (lefCallbacks->BusBitCharsCbk) {
          if (strcmp((yyvsp[-1].string), "") != 0) {
             CALLBACK(lefCallbacks->BusBitCharsCbk, lefrBusBitCharsCbkType, (yyvsp[-1].string)); 
          } else {
             CALLBACK(lefCallbacks->BusBitCharsCbk, lefrBusBitCharsCbkType, "[]"); 
             lefWarning(2006, "BUSBITCHAR has an invalid null value. Value is set to default []");
          }
        }
        lefData->hasBusBit = 1;
      }
#line 4137 "lef.tab.c"
    break;

  case 10: /* rules: error  */
#line 466 "lef.y"
            { }
#line 4143 "lef.tab.c"
    break;

  case 11: /* end_library: %empty  */
#line 469 "lef.y"
      {
        if (lefData->versionNum >= 5.6) {
           lefData->doneLib = 1;
           lefData->ge56done = 1;
        }
      }
#line 4154 "lef.tab.c"
    break;

  case 12: /* end_library: K_END K_LIBRARY  */
#line 476 "lef.y"
      {
        lefData->doneLib = 1;
        lefData->ge56done = 1;
        if (lefCallbacks->LibraryEndCbk)
          CALLBACK(lefCallbacks->LibraryEndCbk, lefrLibraryEndCbkType, 0);
        // 11/16/2001 - Wanda da Rosa - pcr 408334
        // Return 1 if there are errors
      }
#line 4167 "lef.tab.c"
    break;

  case 51: /* case_sensitivity: K_NAMESCASESENSITIVE K_ON ';'  */
#line 500 "lef.y"
          {
            if (lefData->versionNum < 5.6) {
              lefData->namesCaseSensitive = TRUE;
              if (lefCallbacks->CaseSensitiveCbk)
                CALLBACK(lefCallbacks->CaseSensitiveCbk, 
                         lefrCaseSensitiveCbkType,
                         lefData->namesCaseSensitive);
              lefData->hasNameCase = 1;
            } else
              if (lefCallbacks->CaseSensitiveCbk) // write warning only if cbk is set 
                 if (lefData->caseSensitiveWarnings++ < lefSettings->CaseSensitiveWarnings)
                   lefWarning(2007, "NAMESCASESENSITIVE statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
          }
#line 4185 "lef.tab.c"
    break;

  case 52: /* case_sensitivity: K_NAMESCASESENSITIVE K_OFF ';'  */
#line 514 "lef.y"
          {
            if (lefData->versionNum < 5.6) {
              lefData->namesCaseSensitive = FALSE;
              if (lefCallbacks->CaseSensitiveCbk)
                CALLBACK(lefCallbacks->CaseSensitiveCbk, lefrCaseSensitiveCbkType,
                               lefData->namesCaseSensitive);
              lefData->hasNameCase = 1;
            } else {
              if (lefCallbacks->CaseSensitiveCbk) { // write error only if cbk is set 
                if (lefData->caseSensitiveWarnings++ < lefSettings->CaseSensitiveWarnings) {
                  lefError(1504, "NAMESCASESENSITIVE statement is set with OFF.\nStarting version 5.6, NAMESCASENSITIVE is obsolete,\nif it is defined, it has to have the ON value.\nParser will stop processing.");
                  CHKERR();
                }
              }
            }
          }
#line 4206 "lef.tab.c"
    break;

  case 53: /* wireextension: K_NOWIREEXTENSIONATPIN K_ON ';'  */
#line 532 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->NoWireExtensionCbk)
          CALLBACK(lefCallbacks->NoWireExtensionCbk, lefrNoWireExtensionCbkType, "ON");
      } else
        if (lefCallbacks->NoWireExtensionCbk) // write warning only if cbk is set 
           if (lefData->noWireExtensionWarnings++ < lefSettings->NoWireExtensionWarnings)
             lefWarning(2008, "NOWIREEXTENSIONATPIN statement is obsolete in version 5.6 or later.\nThe NOWIREEXTENSIONATPIN statement will be ignored.");
    }
#line 4220 "lef.tab.c"
    break;

  case 54: /* wireextension: K_NOWIREEXTENSIONATPIN K_OFF ';'  */
#line 542 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->NoWireExtensionCbk)
          CALLBACK(lefCallbacks->NoWireExtensionCbk, lefrNoWireExtensionCbkType, "OFF");
      } else
        if (lefCallbacks->NoWireExtensionCbk) // write warning only if cbk is set 
           if (lefData->noWireExtensionWarnings++ < lefSettings->NoWireExtensionWarnings)
             lefWarning(2008, "NOWIREEXTENSIONATPIN statement is obsolete in version 5.6 or later.\nThe NOWIREEXTENSIONATPIN statement will be ignored.");
    }
#line 4234 "lef.tab.c"
    break;

  case 55: /* fixedmask: K_FIXEDMASK ';'  */
#line 553 "lef.y"
    { 
       if (lefData->versionNum >= 5.8) {
          if (lefCallbacks->FixedMaskCbk) {
            lefData->lefFixedMask = 1;
            CALLBACK(lefCallbacks->FixedMaskCbk, lefrFixedMaskCbkType, lefData->lefFixedMask);
          }
          
          lefData->hasFixedMask = 1;
       }
    }
#line 4249 "lef.tab.c"
    break;

  case 56: /* manufacturing: K_MANUFACTURINGGRID int_number ';'  */
#line 565 "lef.y"
    {
      if (lefCallbacks->ManufacturingCbk)
        CALLBACK(lefCallbacks->ManufacturingCbk, lefrManufacturingCbkType, (yyvsp[-1].dval));
      lefData->hasManufactur = 1;
    }
#line 4259 "lef.tab.c"
    break;

  case 57: /* useminspacing: K_USEMINSPACING spacing_type spacing_value ';'  */
#line 572 "lef.y"
  {
    if ((strcmp((yyvsp[-2].string), "PIN") == 0) && (lefData->versionNum >= 5.6)) {
      if (lefCallbacks->UseMinSpacingCbk) // write warning only if cbk is set 
         if (lefData->useMinSpacingWarnings++ < lefSettings->UseMinSpacingWarnings)
            lefWarning(2009, "USEMINSPACING PIN statement is obsolete in version 5.6 or later.\n The USEMINSPACING PIN statement will be ignored.");
    } else {
        if (lefCallbacks->UseMinSpacingCbk) {
          lefData->lefrUseMinSpacing.set((yyvsp[-2].string), (yyvsp[-1].integer));
          CALLBACK(lefCallbacks->UseMinSpacingCbk, lefrUseMinSpacingCbkType,
                   &lefData->lefrUseMinSpacing);
      }
    }
  }
#line 4277 "lef.tab.c"
    break;

  case 58: /* clearancemeasure: K_CLEARANCEMEASURE clearance_type ';'  */
#line 587 "lef.y"
    { CALLBACK(lefCallbacks->ClearanceMeasureCbk, lefrClearanceMeasureCbkType, (yyvsp[-1].string)); }
#line 4283 "lef.tab.c"
    break;

  case 59: /* clearance_type: K_MAXXY  */
#line 590 "lef.y"
            {(yyval.string) = (char*)"MAXXY";}
#line 4289 "lef.tab.c"
    break;

  case 60: /* clearance_type: K_EUCLIDEAN  */
#line 591 "lef.y"
                  {(yyval.string) = (char*)"EUCLIDEAN";}
#line 4295 "lef.tab.c"
    break;

  case 61: /* spacing_type: K_OBS  */
#line 594 "lef.y"
            {(yyval.string) = (char*)"OBS";}
#line 4301 "lef.tab.c"
    break;

  case 62: /* spacing_type: K_PIN  */
#line 595 "lef.y"
            {(yyval.string) = (char*)"PIN";}
#line 4307 "lef.tab.c"
    break;

  case 63: /* spacing_value: K_ON  */
#line 598 "lef.y"
            {(yyval.integer) = 1;}
#line 4313 "lef.tab.c"
    break;

  case 64: /* spacing_value: K_OFF  */
#line 599 "lef.y"
            {(yyval.integer) = 0;}
#line 4319 "lef.tab.c"
    break;

  case 65: /* units_section: start_units units_rules K_END K_UNITS  */
#line 602 "lef.y"
    { 
      if (lefCallbacks->UnitsCbk)
        CALLBACK(lefCallbacks->UnitsCbk, lefrUnitsCbkType, &lefData->lefrUnits);
    }
#line 4328 "lef.tab.c"
    break;

  case 66: /* start_units: K_UNITS  */
#line 608 "lef.y"
    {
      lefData->lefrUnits.clear();
      if (lefData->hasManufactur) {
        if (lefData->unitsWarnings++ < lefSettings->UnitsWarnings) {
          lefError(1505, "MANUFACTURINGGRID statement was defined before UNITS.\nRefer the LEF Language Reference manual for the order of LEF statements.");
          CHKERR();
        }
      }
      if (lefData->hasMinfeature) {
        if (lefData->unitsWarnings++ < lefSettings->UnitsWarnings) {
          lefError(1712, "MINFEATURE statement was defined before UNITS.\nRefer the LEF Language Reference manual for the order of LEF statements.");
          CHKERR();
        }
      }
      if (lefData->versionNum < 5.6) {
        if (lefData->hasSite) {//SITE is defined before UNIT and is illegal in pre 5.6
          lefError(1713, "SITE statement was defined before UNITS.\nRefer the LEF Language Reference manual for the order of LEF statements.");
          CHKERR();
        }
      }
    }
#line 4354 "lef.tab.c"
    break;

  case 69: /* units_rule: K_TIME K_NANOSECONDS int_number ';'  */
#line 635 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setTime((yyvsp[-1].dval)); }
#line 4360 "lef.tab.c"
    break;

  case 70: /* units_rule: K_CAPACITANCE K_PICOFARADS int_number ';'  */
#line 637 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setCapacitance((yyvsp[-1].dval)); }
#line 4366 "lef.tab.c"
    break;

  case 71: /* units_rule: K_RESISTANCE K_OHMS int_number ';'  */
#line 639 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setResistance((yyvsp[-1].dval)); }
#line 4372 "lef.tab.c"
    break;

  case 72: /* units_rule: K_POWER K_MILLIWATTS int_number ';'  */
#line 641 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setPower((yyvsp[-1].dval)); }
#line 4378 "lef.tab.c"
    break;

  case 73: /* units_rule: K_CURRENT K_MILLIAMPS int_number ';'  */
#line 643 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setCurrent((yyvsp[-1].dval)); }
#line 4384 "lef.tab.c"
    break;

  case 74: /* units_rule: K_VOLTAGE K_VOLTS int_number ';'  */
#line 645 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setVoltage((yyvsp[-1].dval)); }
#line 4390 "lef.tab.c"
    break;

  case 75: /* units_rule: K_DATABASE K_MICRONS int_number ';'  */
#line 647 "lef.y"
    { 
      if(validNum((int)(yyvsp[-1].dval))) {
         if (lefCallbacks->UnitsCbk)
            lefData->lefrUnits.setDatabase("MICRONS", (yyvsp[-1].dval));
      }
    }
#line 4401 "lef.tab.c"
    break;

  case 76: /* units_rule: K_FREQUENCY K_MEGAHERTZ NUMBER ';'  */
#line 654 "lef.y"
    { if (lefCallbacks->UnitsCbk) lefData->lefrUnits.setFrequency((yyvsp[-1].dval)); }
#line 4407 "lef.tab.c"
    break;

  case 77: /* layer_rule: start_layer layer_options end_layer  */
#line 658 "lef.y"
    { 
      if (lefCallbacks->LayerCbk)
        CALLBACK(lefCallbacks->LayerCbk, lefrLayerCbkType, &lefData->lefrLayer);
    }
#line 4416 "lef.tab.c"
    break;

  case 78: /* $@2: %empty  */
#line 663 "lef.y"
                     {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 4422 "lef.tab.c"
    break;

  case 79: /* start_layer: K_LAYER $@2 T_STRING  */
#line 664 "lef.y"
    { 
      if (lefData->lefrHasMaxVS) {   // 5.5 
        if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
          if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
            lefError(1506, "A MAXVIASTACK statement is defined before the LAYER statement.\nRefer to the LEF Language Reference manual for the order of LEF statements.");
            CHKERR();
          }
        }
      }
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setName((yyvsp[0].string));
      lefData->useLenThr = 0;
      lefData->layerCut = 0;
      lefData->layerMastOver = 0;
      lefData->layerRout = 0;
      lefData->layerDir = 0;
      lefData->lefrHasLayer = 1;
      //strcpy(lefData->layerName, $3);
      lefData->layerName = strdup((yyvsp[0].string));
      lefData->hasType = 0;
      lefData->hasMask = 0;
      lefData->hasPitch = 0;
      lefData->hasWidth = 0;
      lefData->hasDirection = 0;
      lefData->hasParallel = 0;
      lefData->hasInfluence = 0;
      lefData->hasTwoWidths = 0;
      lefData->lefrHasSpacingTbl = 0;
      lefData->lefrHasSpacing = 0;
    }
#line 4457 "lef.tab.c"
    break;

  case 80: /* $@3: %empty  */
#line 695 "lef.y"
                 {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 4463 "lef.tab.c"
    break;

  case 81: /* end_layer: K_END $@3 T_STRING  */
#line 696 "lef.y"
    { 
      if (strcmp(lefData->layerName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
          if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
             lefData->outMsg = (char*)lefMalloc(10000);
             sprintf (lefData->outMsg,
                "END LAYER name %s is different from the LAYER name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->layerName);
             lefError(1507, lefData->outMsg);
             lefFree(lefData->outMsg);
             lefFree(lefData->layerName);
             CHKERR(); 
          } else
             lefFree(lefData->layerName);
        } else
          lefFree(lefData->layerName);
      } else
        lefFree(lefData->layerName);
      if (!lefSettings->RelaxMode) {
        if (lefData->hasType == 0) {
          if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1508, "TYPE statement is a required statement in a LAYER and it is not defined.");
               CHKERR(); 
            }
          }
        }
        if ((lefData->layerRout == 1) && (lefData->hasPitch == 0)) {
          if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1509, "PITCH statement is a required statement in a LAYER with type ROUTING and it is not defined.");
              CHKERR(); 
            }
          }
        }
        if ((lefData->layerRout == 1) && (lefData->hasWidth == 0)) {
          if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1510, "WIDTH statement is a required statement in a LAYER with type ROUTING and it is not defined.");
              CHKERR(); 
            }
          }
        }
        if ((lefData->layerRout == 1) && (lefData->hasDirection == 0)) {
          if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg, "The DIRECTION statement which is required in a LAYER with TYPE ROUTING is not defined in LAYER %s.\nUpdate your lef file and add the DIRECTION statement for layer %s.", (yyvsp[0].string), (yyvsp[0].string));
              lefError(1511, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR(); 
            }
          }
        }
      }
    }
#line 4523 "lef.tab.c"
    break;

  case 82: /* layer_options: %empty  */
#line 753 "lef.y"
    { }
#line 4529 "lef.tab.c"
    break;

  case 83: /* layer_options: layer_options layer_option  */
#line 755 "lef.y"
    { }
#line 4535 "lef.tab.c"
    break;

  case 84: /* layer_option: K_MANUFACTURINGGRID int_number ';'  */
#line 759 "lef.y"
    {
      if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("LAYER ... MANUFACTORINGGRID value ;")) {
            CHKERR();
        }
      } else if (lefCallbacks->LayerCbk) {
          lefData->lefrLayer.setManufacturingGrid((yyvsp[-1].dval));
      }
    }
#line 4549 "lef.tab.c"
    break;

  case 85: /* $@4: %empty  */
#line 769 "lef.y"
    {
       // let setArraySpacingCutSpacing to set the data 
    }
#line 4557 "lef.tab.c"
    break;

  case 86: /* $@5: %empty  */
#line 775 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.setArraySpacingCut((yyvsp[0].dval));
         lefData->arrayCutsVal = 0;
      }
    }
#line 4568 "lef.tab.c"
    break;

  case 87: /* layer_option: K_ARRAYSPACING $@4 layer_arraySpacing_long layer_arraySpacing_width K_CUTSPACING int_number $@5 layer_arraySpacing_arraycuts ';'  */
#line 782 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "ARRAYSPACING is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1685, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      }
    }
#line 4583 "lef.tab.c"
    break;

  case 88: /* layer_option: K_TYPE layer_type ';'  */
#line 793 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.setType((yyvsp[-1].string));
      lefData->hasType = 1;
    }
#line 4593 "lef.tab.c"
    break;

  case 89: /* layer_option: K_MASK int_number ';'  */
#line 799 "lef.y"
    {
      if (lefData->versionNum < 5.8) {
          if (lefData->layerWarnings++ < lefSettings->ViaWarnings) {
              lefError(2081, "MASK information can only be defined with version 5.8");
              CHKERR(); 
          }           
      } else {
          if (lefCallbacks->LayerCbk) {
            lefData->lefrLayer.setMask((int)(yyvsp[-1].dval));
          }
          
          lefData->hasMask = 1;
      }
    }
#line 4612 "lef.tab.c"
    break;

  case 90: /* layer_option: K_PITCH int_number ';'  */
#line 814 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setPitch((yyvsp[-1].dval));
      lefData->hasPitch = 1;  
    }
#line 4621 "lef.tab.c"
    break;

  case 91: /* layer_option: K_PITCH int_number int_number ';'  */
#line 819 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setPitchXY((yyvsp[-2].dval), (yyvsp[-1].dval));
      lefData->hasPitch = 1;  
    }
#line 4630 "lef.tab.c"
    break;

  case 92: /* layer_option: K_DIAGPITCH int_number ';'  */
#line 824 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDiagPitch((yyvsp[-1].dval));
    }
#line 4638 "lef.tab.c"
    break;

  case 93: /* layer_option: K_DIAGPITCH int_number int_number ';'  */
#line 828 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDiagPitchXY((yyvsp[-2].dval), (yyvsp[-1].dval));
    }
#line 4646 "lef.tab.c"
    break;

  case 94: /* layer_option: K_OFFSET int_number ';'  */
#line 832 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setOffset((yyvsp[-1].dval));
    }
#line 4654 "lef.tab.c"
    break;

  case 95: /* layer_option: K_OFFSET int_number int_number ';'  */
#line 836 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setOffsetXY((yyvsp[-2].dval), (yyvsp[-1].dval));
    }
#line 4662 "lef.tab.c"
    break;

  case 96: /* layer_option: K_DIAGWIDTH int_number ';'  */
#line 840 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDiagWidth((yyvsp[-1].dval));
    }
#line 4670 "lef.tab.c"
    break;

  case 97: /* layer_option: K_DIAGSPACING int_number ';'  */
#line 844 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDiagSpacing((yyvsp[-1].dval));
    }
#line 4678 "lef.tab.c"
    break;

  case 98: /* layer_option: K_WIDTH int_number ';'  */
#line 848 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setWidth((yyvsp[-1].dval));
      lefData->hasWidth = 1;  
    }
#line 4687 "lef.tab.c"
    break;

  case 99: /* layer_option: K_AREA NUMBER ';'  */
#line 853 "lef.y"
    {
      // Issue an error is this is defined in masterslice
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1715, "It is incorrect to define an AREA statement in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
               CHKERR();
            }
         }
      }

      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.setArea((yyvsp[-1].dval));
      }
    }
#line 4707 "lef.tab.c"
    break;

  case 100: /* $@6: %empty  */
#line 869 "lef.y"
    {
      lefData->hasSpCenter = 0;       // reset to 0, only once per spacing is allowed 
      lefData->hasSpSamenet = 0;
      lefData->hasSpParallel = 0;
      lefData->hasSpLayer = 0;
      lefData->layerCutSpacing = (yyvsp[0].dval);  // for error message purpose
      // 11/22/99 - Wanda da Rosa, PCR 283762
      //            Issue an error is this is defined in masterslice
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1512, "It is incorrect to define a SPACING statement in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      // 5.5 either SPACING or SPACINGTABLE, not both for routing layer only
      if (lefData->layerRout) {
        if (lefData->lefrHasSpacingTbl && lefData->versionNum < 5.7) {
           if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
              if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                lefWarning(2010, "It is incorrect to have both SPACING rules & SPACINGTABLE rules within a ROUTING layer");
              }
           }
        }
        if (lefCallbacks->LayerCbk)
           lefData->lefrLayer.setSpacingMin((yyvsp[0].dval));
        lefData->lefrHasSpacing = 1;
      } else { 
        if (lefCallbacks->LayerCbk)
           lefData->lefrLayer.setSpacingMin((yyvsp[0].dval));
      }
    }
#line 4745 "lef.tab.c"
    break;

  case 101: /* layer_option: K_SPACING int_number $@6 layer_spacing_opts layer_spacing_cut_routing ';'  */
#line 903 "lef.y"
                                  {}
#line 4751 "lef.tab.c"
    break;

  case 102: /* $@7: %empty  */
#line 905 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.setSpacingTableOrtho();
      if (lefCallbacks->LayerCbk) // due to converting to C, else, convertor produce 
         lefData->lefrLayer.addSpacingTableOrthoWithin((yyvsp[-2].dval), (yyvsp[0].dval));//bad code
    }
#line 4762 "lef.tab.c"
    break;

  case 103: /* layer_option: K_SPACINGTABLE K_ORTHOGONAL K_WITHIN int_number K_SPACING int_number $@7 layer_spacingtable_opts ';'  */
#line 912 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "SPACINGTABLE ORTHOGONAL is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1694, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      }
    }
#line 4777 "lef.tab.c"
    break;

  case 104: /* layer_option: K_DIRECTION layer_direction ';'  */
#line 923 "lef.y"
    {
      lefData->layerDir = 1;
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1513, "DIRECTION statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDirection((yyvsp[-1].string));
      lefData->hasDirection = 1;  
    }
#line 4795 "lef.tab.c"
    break;

  case 105: /* layer_option: K_RESISTANCE K_RPERSQ int_number ';'  */
#line 937 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1514, "RESISTANCE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setResistance((yyvsp[-1].dval));
    }
#line 4811 "lef.tab.c"
    break;

  case 106: /* layer_option: K_RESISTANCE K_RPERSQ K_PWL '(' res_points ')' ';'  */
#line 949 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1515, "RESISTANCE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
    }
#line 4826 "lef.tab.c"
    break;

  case 107: /* layer_option: K_CAPACITANCE K_CPERSQDIST int_number ';'  */
#line 960 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1516, "CAPACITANCE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCapacitance((yyvsp[-1].dval));
    }
#line 4842 "lef.tab.c"
    break;

  case 108: /* layer_option: K_CAPACITANCE K_CPERSQDIST K_PWL '(' cap_points ')' ';'  */
#line 972 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1517, "CAPACITANCE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
    }
#line 4857 "lef.tab.c"
    break;

  case 109: /* layer_option: K_HEIGHT int_number ';'  */
#line 983 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1518, "HEIGHT statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setHeight((yyvsp[-1].dval));
    }
#line 4873 "lef.tab.c"
    break;

  case 110: /* layer_option: K_WIREEXTENSION int_number ';'  */
#line 995 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1519, "WIREEXTENSION statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setWireExtension((yyvsp[-1].dval));
    }
#line 4889 "lef.tab.c"
    break;

  case 111: /* layer_option: K_THICKNESS int_number ';'  */
#line 1007 "lef.y"
    {
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1520, "THICKNESS statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setThickness((yyvsp[-1].dval));
    }
#line 4905 "lef.tab.c"
    break;

  case 112: /* layer_option: K_SHRINKAGE int_number ';'  */
#line 1019 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1521, "SHRINKAGE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setShrinkage((yyvsp[-1].dval));
    }
#line 4921 "lef.tab.c"
    break;

  case 113: /* layer_option: K_CAPMULTIPLIER int_number ';'  */
#line 1031 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1522, "CAPMULTIPLIER statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCapMultiplier((yyvsp[-1].dval));
    }
#line 4937 "lef.tab.c"
    break;

  case 114: /* layer_option: K_EDGECAPACITANCE int_number ';'  */
#line 1043 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1523, "EDGECAPACITANCE statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setEdgeCap((yyvsp[-1].dval));
    }
#line 4953 "lef.tab.c"
    break;

  case 115: /* layer_option: K_ANTENNALENGTHFACTOR int_number ';'  */
#line 1056 "lef.y"
    { // 5.3 syntax 
      lefData->use5_3 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1525, "ANTENNALENGTHFACTOR statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      } else if (lefData->versionNum >= 5.4) {
         if (lefData->use5_4) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                  lefData->outMsg = (char*)lefMalloc(10000);
                  sprintf (lefData->outMsg,
                    "ANTENNALENGTHFACTOR statement is a version 5.3 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNALENGTHFACTOR syntax, which is incorrect.", lefData->versionNum);
                  lefError(1526, lefData->outMsg);
                  lefFree(lefData->outMsg);
                  CHKERR();
               }
            }
         }
      }

      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaLength((yyvsp[-1].dval));
    }
#line 4984 "lef.tab.c"
    break;

  case 116: /* layer_option: K_CURRENTDEN int_number ';'  */
#line 1083 "lef.y"
    {
      if (lefData->versionNum < 5.2) {
         if (!lefData->layerRout) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefError(1702, "CURRENTDEN statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
                 CHKERR();
               }
            }
         }
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCurrentDensity((yyvsp[-1].dval));
      } else {
         if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
            lefWarning(2079, "CURRENTDEN statement is obsolete in version 5.2 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.2 or later.");
            CHKERR();
         }
      }
    }
#line 5007 "lef.tab.c"
    break;

  case 117: /* layer_option: K_CURRENTDEN K_PWL '(' current_density_pwl_list ')' ';'  */
#line 1102 "lef.y"
    { 
      if (lefData->versionNum < 5.2) {
         if (!lefData->layerRout) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefError(1702, "CURRENTDEN statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
                 CHKERR();
               }
            }
         }
      } else {
         if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
            lefWarning(2079, "CURRENTDEN statement is obsolete in version 5.2 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.2 or later.");
            CHKERR();
         }
      }
    }
#line 5029 "lef.tab.c"
    break;

  case 118: /* layer_option: K_CURRENTDEN '(' int_number int_number ')' ';'  */
#line 1120 "lef.y"
    {
      if (lefData->versionNum < 5.2) {
         if (!lefData->layerRout) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefError(1702, "CURRENTDEN statement can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
                 CHKERR();
               }
            }
         }
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCurrentPoint((yyvsp[-3].dval), (yyvsp[-2].dval));
      } else {
         if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
            lefWarning(2079, "CURRENTDEN statement is obsolete in version 5.2 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.2 or later.");
            CHKERR();
         }
      }
    }
#line 5052 "lef.tab.c"
    break;

  case 119: /* $@8: %empty  */
#line 1138 "lef.y"
               { lefData->lefDumbMode = 10000000;}
#line 5058 "lef.tab.c"
    break;

  case 120: /* layer_option: K_PROPERTY $@8 layer_prop_list ';'  */
#line 1139 "lef.y"
    {
      lefData->lefDumbMode = 0;
    }
#line 5066 "lef.tab.c"
    break;

  case 121: /* $@9: %empty  */
#line 1143 "lef.y"
    {
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1527, "ACCURRENTDENSITY statement can't be defined in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addAccurrentDensity((yyvsp[0].string));      
    }
#line 5082 "lef.tab.c"
    break;

  case 122: /* layer_option: K_ACCURRENTDENSITY layer_table_type $@9 layer_frequency  */
#line 1154 "lef.y"
                    {

    }
#line 5090 "lef.tab.c"
    break;

  case 123: /* layer_option: K_ACCURRENTDENSITY layer_table_type int_number ';'  */
#line 1158 "lef.y"
    {
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1527, "ACCURRENTDENSITY statement can't be defined in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) {
           lefData->lefrLayer.addAccurrentDensity((yyvsp[-2].string));
           lefData->lefrLayer.setAcOneEntry((yyvsp[-1].dval));
      }
    }
#line 5109 "lef.tab.c"
    break;

  case 124: /* layer_option: K_DCCURRENTDENSITY K_AVERAGE int_number ';'  */
#line 1173 "lef.y"
    {
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1528, "DCCURRENTDENSITY statement can't be defined in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addDccurrentDensity("AVERAGE");
         lefData->lefrLayer.setDcOneEntry((yyvsp[-1].dval));
      }
    }
#line 5128 "lef.tab.c"
    break;

  case 125: /* $@10: %empty  */
#line 1188 "lef.y"
    {
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1528, "DCCURRENTDENSITY statement can't be defined in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (!lefData->layerCut) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1529, "CUTAREA statement can only be defined in LAYER with type CUT. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addDccurrentDensity("AVERAGE");
         lefData->lefrLayer.addNumber((yyvsp[0].dval));
      }
    }
#line 5155 "lef.tab.c"
    break;

  case 126: /* $@11: %empty  */
#line 1211 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addDcCutarea(); }
#line 5161 "lef.tab.c"
    break;

  case 127: /* layer_option: K_DCCURRENTDENSITY K_AVERAGE K_CUTAREA NUMBER $@10 number_list ';' $@11 dc_layer_table  */
#line 1212 "lef.y"
                   {}
#line 5167 "lef.tab.c"
    break;

  case 128: /* $@12: %empty  */
#line 1214 "lef.y"
    {
      if (lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1528, "DCCURRENTDENSITY can't be defined in LAYER with TYPE MASTERSLICE or OVERLAP. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1530, "WIDTH statement can only be defined in LAYER with type ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addDccurrentDensity("AVERAGE");
         lefData->lefrLayer.addNumber((yyvsp[0].dval));
      }
    }
#line 5194 "lef.tab.c"
    break;

  case 129: /* $@13: %empty  */
#line 1237 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addDcWidth(); }
#line 5200 "lef.tab.c"
    break;

  case 130: /* layer_option: K_DCCURRENTDENSITY K_AVERAGE K_WIDTH int_number $@12 int_number_list ';' $@13 dc_layer_table  */
#line 1238 "lef.y"
                   {}
#line 5206 "lef.tab.c"
    break;

  case 131: /* layer_option: K_ANTENNAAREARATIO int_number ';'  */
#line 1242 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNAAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1531, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNADIFFAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNAAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1704, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1533, "ANTENNAAREARATIO statement can only be defined in LAYER with TYPE ROUTING or CUT. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaAreaRatio((yyvsp[-1].dval));
    }
#line 5248 "lef.tab.c"
    break;

  case 132: /* $@14: %empty  */
#line 1280 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNADIFFAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1532, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNADIFFAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNADIFFAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1704, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1534, "ANTENNADIFFAREARATIO statement can only be defined in LAYER with TYPE ROUTING or CUT. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      lefData->antennaType = lefiAntennaDAR; 
    }
#line 5290 "lef.tab.c"
    break;

  case 133: /* layer_option: K_ANTENNADIFFAREARATIO $@14 layer_antenna_pwl ';'  */
#line 1317 "lef.y"
                          {}
#line 5296 "lef.tab.c"
    break;

  case 134: /* layer_option: K_ANTENNACUMAREARATIO int_number ';'  */
#line 1319 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNACUMAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1535, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNACUMAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNACUMAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1536, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1537, "ANTENNACUMAREARATIO statement can only be defined in LAYER with TYPE ROUTING or CUT. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaCumAreaRatio((yyvsp[-1].dval));
    }
#line 5338 "lef.tab.c"
    break;

  case 135: /* $@15: %empty  */
#line 1357 "lef.y"
    {  // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNACUMDIFFAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1538, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNACUMDIFFAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNACUMDIFFAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1539, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1540, "ANTENNACUMDIFFAREARATIO statement can only be defined in LAYER with TYPE ROUTING or CUT. Parser will stop processing.");
              CHKERR();
            }
         }
      }
      lefData->antennaType = lefiAntennaCDAR;
    }
#line 5380 "lef.tab.c"
    break;

  case 136: /* layer_option: K_ANTENNACUMDIFFAREARATIO $@15 layer_antenna_pwl ';'  */
#line 1394 "lef.y"
                          {}
#line 5386 "lef.tab.c"
    break;

  case 137: /* $@16: %empty  */
#line 1396 "lef.y"
    { // both 5.3  & 5.4 syntax 
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1541, "ANTENNAAREAFACTOR can only be defined in LAYER with TYPE ROUTING or CUT. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      // this does not need to check, since syntax is in both 5.3 & 5.4 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaAreaFactor((yyvsp[0].dval));
      lefData->antennaType = lefiAntennaAF;
    }
#line 5404 "lef.tab.c"
    break;

  case 138: /* layer_option: K_ANTENNAAREAFACTOR int_number $@16 layer_antenna_duo ';'  */
#line 1409 "lef.y"
                          {}
#line 5410 "lef.tab.c"
    break;

  case 139: /* layer_option: K_ANTENNASIDEAREARATIO int_number ';'  */
#line 1411 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1542, "ANTENNASIDEAREARATIO can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNASIDEAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1543, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNASIDEAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNASIDEAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1544, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaSideAreaRatio((yyvsp[-1].dval));
    }
#line 5452 "lef.tab.c"
    break;

  case 140: /* $@17: %empty  */
#line 1449 "lef.y"
    {  // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1545, "ANTENNADIFFSIDEAREARATIO can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNADIFFSIDEAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1546, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNADIFFSIDEAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNADIFFSIDEAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1547, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      lefData->antennaType = lefiAntennaDSAR;
    }
#line 5494 "lef.tab.c"
    break;

  case 141: /* layer_option: K_ANTENNADIFFSIDEAREARATIO $@17 layer_antenna_pwl ';'  */
#line 1486 "lef.y"
                          {}
#line 5500 "lef.tab.c"
    break;

  case 142: /* layer_option: K_ANTENNACUMSIDEAREARATIO int_number ';'  */
#line 1488 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1548, "ANTENNACUMSIDEAREARATIO can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNACUMSIDEAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1549, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNACUMSIDEAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNACUMSIDEAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1550, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaCumSideAreaRatio((yyvsp[-1].dval));
    }
#line 5542 "lef.tab.c"
    break;

  case 143: /* $@18: %empty  */
#line 1526 "lef.y"
    {  // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1551, "ANTENNACUMDIFFSIDEAREARATIO can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNACUMDIFFSIDEAREARATIO statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1552, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNACUMDIFFSIDEAREARATIO statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNACUMDIFFSIDEAREARATIO syntax, which is incorrect.", lefData->versionNum);
               lefError(1553, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      lefData->antennaType = lefiAntennaCDSAR;
    }
#line 5584 "lef.tab.c"
    break;

  case 144: /* layer_option: K_ANTENNACUMDIFFSIDEAREARATIO $@18 layer_antenna_pwl ';'  */
#line 1563 "lef.y"
                          {}
#line 5590 "lef.tab.c"
    break;

  case 145: /* $@19: %empty  */
#line 1565 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && (lefData->layerCut || lefData->layerMastOver)) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1554, "ANTENNASIDEAREAFACTOR can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNASIDEAREAFACTOR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1555, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNASIDEAREAFACTOR statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNASIDEAREAFACTOR syntax, which is incorrect.", lefData->versionNum);
               lefError(1556, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaSideAreaFactor((yyvsp[0].dval));
      lefData->antennaType = lefiAntennaSAF;
    }
#line 5633 "lef.tab.c"
    break;

  case 146: /* layer_option: K_ANTENNASIDEAREAFACTOR int_number $@19 layer_antenna_duo ';'  */
#line 1603 "lef.y"
                          {}
#line 5639 "lef.tab.c"
    break;

  case 147: /* $@20: %empty  */
#line 1605 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (!lefData->layerRout && !lefData->layerCut && lefData->layerMastOver) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1557, "ANTENNAMODEL can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNAMODEL statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1558, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->use5_3) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "ANTENNAMODEL statement is a version 5.4 or earlier syntax.\nYour lef file with version %.2f, has both old and new ANTENNAMODEL syntax, which is incorrect.", lefData->versionNum);
               lefError(1559, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      lefData->antennaType = lefiAntennaO;
    }
#line 5681 "lef.tab.c"
    break;

  case 148: /* layer_option: K_ANTENNAMODEL $@20 layer_oxide ';'  */
#line 1642 "lef.y"
                    {}
#line 5687 "lef.tab.c"
    break;

  case 149: /* layer_option: K_ANTENNACUMROUTINGPLUSCUT ';'  */
#line 1644 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "ANTENNACUMROUTINGPLUSCUT is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1686, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      } else {
         if (!lefData->layerRout && !lefData->layerCut) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                  lefError(1560, "ANTENNACUMROUTINGPLUSCUT can only be defined in LAYER with type ROUTING or CUT. Parser will stop processing.");
                  CHKERR();
               }
            }
         }
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaCumRoutingPlusCut();
      }
    }
#line 5712 "lef.tab.c"
    break;

  case 150: /* layer_option: K_ANTENNAGATEPLUSDIFF int_number ';'  */
#line 1665 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "ANTENNAGATEPLUSDIFF is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1687, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      } else {
         if (!lefData->layerRout && !lefData->layerCut) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                  lefError(1561, "ANTENNAGATEPLUSDIFF can only be defined in LAYER with type ROUTING or CUT. Parser will stop processing.");
                  CHKERR();
               }
            }
         }
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaGatePlusDiff((yyvsp[-1].dval));
      }
    }
#line 5737 "lef.tab.c"
    break;

  case 151: /* layer_option: K_ANTENNAAREAMINUSDIFF int_number ';'  */
#line 1686 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "ANTENNAAREAMINUSDIFF is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1688, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      } else {
         if (!lefData->layerRout && !lefData->layerCut) {
            if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                  lefError(1562, "ANTENNAAREAMINUSDIFF can only be defined in LAYER with type ROUTING or CUT. Parser will stop processing.");
                  CHKERR();
               }
            }
         }
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setAntennaAreaMinusDiff((yyvsp[-1].dval));
      }
    }
#line 5762 "lef.tab.c"
    break;

  case 152: /* $@21: %empty  */
#line 1707 "lef.y"
    {
      if (!lefData->layerRout && !lefData->layerCut) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1563, "ANTENNAAREADIFFREDUCEPWL can only be defined in LAYER with type ROUTING or CUT. Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) { // require min 2 points, set the 1st 2 
         if (lefData->lefrAntennaPWLPtr) {
            lefData->lefrAntennaPWLPtr->Destroy();
            lefFree(lefData->lefrAntennaPWLPtr);
         }

         lefData->lefrAntennaPWLPtr = lefiAntennaPWL::create();
         lefData->lefrAntennaPWLPtr->addAntennaPWL((yyvsp[-1].pt).x, (yyvsp[-1].pt).y);
         lefData->lefrAntennaPWLPtr->addAntennaPWL((yyvsp[0].pt).x, (yyvsp[0].pt).y);
      }
    }
#line 5787 "lef.tab.c"
    break;

  case 153: /* $@22: %empty  */
#line 1728 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        lefData->lefrLayer.setAntennaPWL(lefiAntennaADR, lefData->lefrAntennaPWLPtr);
        lefData->lefrAntennaPWLPtr = NULL;
      }
    }
#line 5798 "lef.tab.c"
    break;

  case 154: /* layer_option: K_ANTENNAAREADIFFREDUCEPWL '(' pt pt $@21 layer_diffusion_ratios ')' ';' $@22  */
#line 1734 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "ANTENNAAREADIFFREDUCEPWL is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1689, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      }
    }
#line 5813 "lef.tab.c"
    break;

  case 155: /* layer_option: K_SLOTWIREWIDTH int_number ';'  */
#line 1745 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWireWidth((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2011, "SLOTWIREWIDTH statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SLOTWIREWIDTH statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1564, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWireWidth((yyvsp[-1].dval));
    }
#line 5841 "lef.tab.c"
    break;

  case 156: /* layer_option: K_SLOTWIRELENGTH int_number ';'  */
#line 1769 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWireLength((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2012, "SLOTWIRELENGTH statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SLOTWIRELENGTH statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1565, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWireLength((yyvsp[-1].dval));
    }
#line 5869 "lef.tab.c"
    break;

  case 157: /* layer_option: K_SLOTWIDTH int_number ';'  */
#line 1793 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWidth((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2013, "SLOTWIDTH statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SLOTWIDTH statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1566, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotWidth((yyvsp[-1].dval));
    }
#line 5897 "lef.tab.c"
    break;

  case 158: /* layer_option: K_SLOTLENGTH int_number ';'  */
#line 1817 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotLength((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2014, "SLOTLENGTH statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SLOTLENGTH statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1567, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSlotLength((yyvsp[-1].dval));
    }
#line 5925 "lef.tab.c"
    break;

  case 159: /* layer_option: K_MAXADJACENTSLOTSPACING int_number ';'  */
#line 1841 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxAdjacentSlotSpacing((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2015, "MAXADJACENTSLOTSPACING statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MAXADJACENTSLOTSPACING statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1568, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxAdjacentSlotSpacing((yyvsp[-1].dval));
    }
#line 5953 "lef.tab.c"
    break;

  case 160: /* layer_option: K_MAXCOAXIALSLOTSPACING int_number ';'  */
#line 1865 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxCoaxialSlotSpacing((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
                lefWarning(2016, "MAXCOAXIALSLOTSPACING statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MAXCOAXIALSLOTSPACING statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1569, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxCoaxialSlotSpacing((yyvsp[-1].dval));
    }
#line 5981 "lef.tab.c"
    break;

  case 161: /* layer_option: K_MAXEDGESLOTSPACING int_number ';'  */
#line 1889 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxEdgeSlotSpacing((yyvsp[-1].dval));
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2017, "MAXEDGESLOTSPACING statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MAXEDGESLOTSPACING statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1570, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else
         if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxEdgeSlotSpacing((yyvsp[-1].dval));
    }
#line 6009 "lef.tab.c"
    break;

  case 162: /* layer_option: K_SPLITWIREWIDTH int_number ';'  */
#line 1913 "lef.y"
    { // 5.4 syntax 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum >= 5.7) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
               lefWarning(2018, "SPLITWIREWIDTH statement is obsolete in version 5.7 or later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.7 or later.");
         }
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SPLITWIREWIDTH statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1571, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setSplitWireWidth((yyvsp[-1].dval));
    }
#line 6036 "lef.tab.c"
    break;

  case 163: /* layer_option: K_MINIMUMDENSITY int_number ';'  */
#line 1936 "lef.y"
    { // 5.4 syntax, pcr 394389 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MINIMUMDENSITY statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1572, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMinimumDensity((yyvsp[-1].dval));
    }
#line 6058 "lef.tab.c"
    break;

  case 164: /* layer_option: K_MAXIMUMDENSITY int_number ';'  */
#line 1954 "lef.y"
    { // 5.4 syntax, pcr 394389 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MAXIMUMDENSITY statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1573, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaximumDensity((yyvsp[-1].dval));
    }
#line 6080 "lef.tab.c"
    break;

  case 165: /* layer_option: K_DENSITYCHECKWINDOW int_number int_number ';'  */
#line 1972 "lef.y"
    { // 5.4 syntax, pcr 394389 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "DENSITYCHECKWINDOW statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1574, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDensityCheckWindow((yyvsp[-2].dval), (yyvsp[-1].dval));
    }
#line 6102 "lef.tab.c"
    break;

  case 166: /* layer_option: K_DENSITYCHECKSTEP int_number ';'  */
#line 1990 "lef.y"
    { // 5.4 syntax, pcr 394389 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "DENSITYCHECKSTEP statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1575, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setDensityCheckStep((yyvsp[-1].dval));
    }
#line 6124 "lef.tab.c"
    break;

  case 167: /* layer_option: K_FILLACTIVESPACING int_number ';'  */
#line 2008 "lef.y"
    { // 5.4 syntax, pcr 394389 
      if (lefData->ignoreVersion) {
         // do nothing 
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "FILLACTIVESPACING statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1576, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setFillActiveSpacing((yyvsp[-1].dval));
    }
#line 6146 "lef.tab.c"
    break;

  case 168: /* layer_option: K_MAXWIDTH int_number ';'  */
#line 2026 "lef.y"
    {
      // 5.5 MAXWIDTH, is for routing layer only
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefError(1577, "MAXWIDTH statement can only be defined in LAYER with TYPE ROUTING.  Parser will stop processing.");
               CHKERR();
            }
         }
      }
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MAXWIDTH statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1578, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMaxwidth((yyvsp[-1].dval));
    }
#line 6175 "lef.tab.c"
    break;

  case 169: /* layer_option: K_MINWIDTH int_number ';'  */
#line 2051 "lef.y"
    {
      // 5.5 MINWIDTH, is for routing layer only
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1579, "MINWIDTH statement can only be defined in LAYER with TYPE ROUTING.  Parser will stop processing.");
              CHKERR();
            }
         }
      }
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MINWIDTH statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1580, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setMinwidth((yyvsp[-1].dval));
    }
#line 6204 "lef.tab.c"
    break;

  case 170: /* $@23: %empty  */
#line 2076 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "MINENCLOSEDAREA statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1581, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addMinenclosedarea((yyvsp[0].dval));
    }
#line 6224 "lef.tab.c"
    break;

  case 171: /* layer_option: K_MINENCLOSEDAREA NUMBER $@23 layer_minen_width ';'  */
#line 2091 "lef.y"
                          {}
#line 6230 "lef.tab.c"
    break;

  case 172: /* $@24: %empty  */
#line 2093 "lef.y"
    { // pcr 409334 
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addMinimumcut((int)(yyvsp[-2].dval), (yyvsp[0].dval)); 
      lefData->hasLayerMincut = 0;
    }
#line 6240 "lef.tab.c"
    break;

  case 173: /* layer_option: K_MINIMUMCUT int_number K_WIDTH int_number $@24 layer_minimumcut_within layer_minimumcut_from layer_minimumcut_length ';'  */
#line 2101 "lef.y"
    {
      if (!lefData->hasLayerMincut) {   // FROMABOVE nor FROMBELOW is set 
         if (lefCallbacks->LayerCbk)
             lefData->lefrLayer.addMinimumcutConnect((char*)"");
      }
    }
#line 6251 "lef.tab.c"
    break;

  case 174: /* $@25: %empty  */
#line 2108 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addMinstep((yyvsp[0].dval));
    }
#line 6259 "lef.tab.c"
    break;

  case 175: /* layer_option: K_MINSTEP int_number $@25 layer_minstep_options ';'  */
#line 2112 "lef.y"
    {
    }
#line 6266 "lef.tab.c"
    break;

  case 176: /* layer_option: K_PROTRUSIONWIDTH int_number K_LENGTH int_number K_WIDTH int_number ';'  */
#line 2115 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "PROTRUSION RULE statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1582, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.setProtrusion((yyvsp[-5].dval), (yyvsp[-3].dval), (yyvsp[-1].dval));
    }
#line 6286 "lef.tab.c"
    break;

  case 177: /* $@26: %empty  */
#line 2131 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "SPACINGTABLE statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1583, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      // 5.5 either SPACING or SPACINGTABLE in a layer, not both
      if (lefData->lefrHasSpacing && lefData->layerRout && lefData->versionNum < 5.7) {
         if (lefCallbacks->LayerCbk)  // write warning only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefWarning(2010, "It is incorrect to have both SPACING rules & SPACINGTABLE rules within a ROUTING layer");
            }
      } 
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpacingTable();
      lefData->lefrHasSpacingTbl = 1;
    }
#line 6314 "lef.tab.c"
    break;

  case 178: /* layer_option: K_SPACINGTABLE $@26 sp_options ';'  */
#line 2154 "lef.y"
                   {}
#line 6320 "lef.tab.c"
    break;

  case 179: /* $@27: %empty  */
#line 2157 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ENCLOSURE statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1584, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addEnclosure((yyvsp[-2].string), (yyvsp[-1].dval), (yyvsp[0].dval));
    }
#line 6341 "lef.tab.c"
    break;

  case 180: /* layer_option: K_ENCLOSURE layer_enclosure_type_opt int_number int_number $@27 layer_enclosure_width_opt ';'  */
#line 2173 "lef.y"
                                  {}
#line 6347 "lef.tab.c"
    break;

  case 181: /* $@28: %empty  */
#line 2176 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "PREFERENCLOSURE statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1585, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addPreferEnclosure((yyvsp[-2].string), (yyvsp[-1].dval), (yyvsp[0].dval));
    }
#line 6368 "lef.tab.c"
    break;

  case 182: /* layer_option: K_PREFERENCLOSURE layer_enclosure_type_opt int_number int_number $@28 layer_preferenclosure_width_opt ';'  */
#line 2192 "lef.y"
                                        {}
#line 6374 "lef.tab.c"
    break;

  case 183: /* layer_option: K_RESISTANCE int_number ';'  */
#line 2194 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "RESISTANCE statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1586, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else {
         if (lefCallbacks->LayerCbk)
            lefData->lefrLayer.setResPerCut((yyvsp[-1].dval));
      }
    }
#line 6396 "lef.tab.c"
    break;

  case 184: /* layer_option: K_DIAGMINEDGELENGTH int_number ';'  */
#line 2212 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1587, "DIAGMINEDGELENGTH can only be defined in LAYER with TYPE ROUTING. Parser will stop processing.");
              CHKERR();
            }
         }
      } else if (lefData->versionNum < 5.6) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "DIAGMINEDGELENGTH statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1588, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else {
         if (lefCallbacks->LayerCbk)
            lefData->lefrLayer.setDiagMinEdgeLength((yyvsp[-1].dval));
      }
    }
#line 6425 "lef.tab.c"
    break;

  case 185: /* $@29: %empty  */
#line 2237 "lef.y"
    {
      // Use the polygon code to retrieve the points for MINSIZE
      lefData->lefrGeometriesPtr = (lefiGeometries*)lefMalloc(sizeof(lefiGeometries));
      lefData->lefrGeometriesPtr->Init();
      lefData->lefrDoGeometries = 1;
    }
#line 6436 "lef.tab.c"
    break;

  case 186: /* layer_option: K_MINSIZE $@29 firstPt otherPts ';'  */
#line 2244 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
         lefData->lefrGeometriesPtr->addPolygon();
         lefData->lefrLayer.setMinSize(lefData->lefrGeometriesPtr);
      }
     lefData->lefrDoGeometries = 0;
      lefData->lefrGeometriesPtr->Destroy();
      lefFree(lefData->lefrGeometriesPtr);
    }
#line 6450 "lef.tab.c"
    break;

  case 188: /* layer_arraySpacing_long: K_LONGARRAY  */
#line 2257 "lef.y"
    {
        if (lefCallbacks->LayerCbk)
           lefData->lefrLayer.setArraySpacingLongArray();
    }
#line 6459 "lef.tab.c"
    break;

  case 190: /* layer_arraySpacing_width: K_WIDTH int_number  */
#line 2265 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.setArraySpacingWidth((yyvsp[0].dval));
    }
#line 6468 "lef.tab.c"
    break;

  case 193: /* layer_arraySpacing_arraycut: K_ARRAYCUTS int_number K_SPACING int_number  */
#line 2276 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addArraySpacingArray((int)(yyvsp[-2].dval), (yyvsp[0].dval));
         if (lefData->arrayCutsVal > (int)(yyvsp[-2].dval)) {
            // Mulitiple ARRAYCUTS value needs to me in ascending order 
            if (!lefData->arrayCutsWar) {
               if (lefData->layerWarnings++ < lefSettings->LayerWarnings)
                  lefWarning(2080, "The number of cut values in multiple ARRAYSPACING ARRAYCUTS are not in increasing order.\nTo be consistent with the documentation, update the cut values to increasing order.");
               lefData->arrayCutsWar = 1;
            }
         }
         lefData->arrayCutsVal = (int)(yyvsp[-2].dval);
    }
#line 6486 "lef.tab.c"
    break;

  case 194: /* $@30: %empty  */
#line 2292 "lef.y"
    { 
      if (lefData->hasInfluence) {  // 5.5 - INFLUENCE table must follow a PARALLEL
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1589, "An INFLUENCE table statement was defined before the PARALLELRUNLENGTH table statement.\nINFLUENCE table statement should be defined following the PARALLELRUNLENGTH.\nChange the LEF file and rerun the parser.");
              CHKERR();
            }
         }
      }
      if (lefData->hasParallel) { // 5.5 - Only one PARALLEL table is allowed per layer
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1590, "There is multiple PARALLELRUNLENGTH table statements are defined within a layer.\nAccording to the LEF Reference Manual, only one PARALLELRUNLENGTH table statement is allowed per layer.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval));
      lefData->hasParallel = 1;
    }
#line 6511 "lef.tab.c"
    break;

  case 195: /* $@31: %empty  */
#line 2313 "lef.y"
    {
      lefData->spParallelLength = lefData->lefrLayer.getNumber();
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpParallelLength();
    }
#line 6520 "lef.tab.c"
    break;

  case 196: /* $@32: %empty  */
#line 2318 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addSpParallelWidth((yyvsp[0].dval));
      }
    }
#line 6530 "lef.tab.c"
    break;

  case 197: /* $@33: %empty  */
#line 2324 "lef.y"
    { 
      if (lefData->lefrLayer.getNumber() != lefData->spParallelLength) {
         if (lefCallbacks->LayerCbk) {
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1591, "The number of length in the PARALLELRUNLENGTH statement is not equal to\nthe total number of spacings defined in the WIDTH statement in the SPACINGTABLE.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpParallelWidthSpacing();
    }
#line 6546 "lef.tab.c"
    break;

  case 199: /* $@34: %empty  */
#line 2338 "lef.y"
    {
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval));
    }
#line 6554 "lef.tab.c"
    break;

  case 200: /* $@35: %empty  */
#line 2342 "lef.y"
    {
      if (lefData->hasParallel) { // 5.7 - Either PARALLEL OR TWOWIDTHS per layer
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1592, "A PARALLELRUNLENGTH statement was already defined in the layer.\nIt is PARALLELRUNLENGTH or TWOWIDTHS is allowed per layer.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpTwoWidths((yyvsp[-4].dval), (yyvsp[-3].dval));
      lefData->hasTwoWidths = 1;
    }
#line 6571 "lef.tab.c"
    break;

  case 201: /* sp_options: K_TWOWIDTHS K_WIDTH int_number layer_sp_TwoWidthsPRL int_number $@34 int_number_list $@35 layer_sp_TwoWidths  */
#line 2355 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "TWOWIDTHS is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1697, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } 
    }
#line 6586 "lef.tab.c"
    break;

  case 202: /* $@36: %empty  */
#line 2366 "lef.y"
    {
      if (lefData->hasInfluence) {  // 5.5 - INFLUENCE table must follow a PARALLEL
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1594, "A INFLUENCE table statement was already defined in the layer.\nOnly one INFLUENCE statement is allowed per layer.");
              CHKERR();
            }
         }
      }
      if (!lefData->hasParallel) {  // 5.5 - INFLUENCE must follow a PARALLEL
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1595, "An INFLUENCE table statement was already defined before the layer.\nINFLUENCE statement has to be defined after the PARALLELRUNLENGTH table statement in the layer.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.setInfluence();
         lefData->lefrLayer.addSpInfluence((yyvsp[-4].dval), (yyvsp[-2].dval), (yyvsp[0].dval));
      }
    }
#line 6613 "lef.tab.c"
    break;

  case 206: /* layer_spacingtable_opt: K_WITHIN int_number K_SPACING int_number  */
#line 2396 "lef.y"
  {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addSpacingTableOrthoWithin((yyvsp[-2].dval), (yyvsp[0].dval));
  }
#line 6622 "lef.tab.c"
    break;

  case 207: /* layer_enclosure_type_opt: %empty  */
#line 2402 "lef.y"
    {(yyval.string) = (char*)"NULL";}
#line 6628 "lef.tab.c"
    break;

  case 208: /* layer_enclosure_type_opt: K_ABOVE  */
#line 2403 "lef.y"
             {(yyval.string) = (char*)"ABOVE";}
#line 6634 "lef.tab.c"
    break;

  case 209: /* layer_enclosure_type_opt: K_BELOW  */
#line 2404 "lef.y"
             {(yyval.string) = (char*)"BELOW";}
#line 6640 "lef.tab.c"
    break;

  case 211: /* $@37: %empty  */
#line 2408 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addEnclosureWidth((yyvsp[0].dval));
      }
    }
#line 6650 "lef.tab.c"
    break;

  case 213: /* layer_enclosure_width_opt: K_LENGTH int_number  */
#line 2415 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "LENGTH is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1691, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      } else {
         if (lefCallbacks->LayerCbk) {
            lefData->lefrLayer.addEnclosureLength((yyvsp[0].dval));
         }
      }
    }
#line 6669 "lef.tab.c"
    break;

  case 215: /* layer_enclosure_width_except_opt: K_EXCEPTEXTRACUT int_number  */
#line 2432 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
         lefData->outMsg = (char*)lefMalloc(10000);
         sprintf(lefData->outMsg,
           "EXCEPTEXTRACUT is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
         lefError(1690, lefData->outMsg);
         lefFree(lefData->outMsg);
         CHKERR();
      } else {
         if (lefCallbacks->LayerCbk) {
            lefData->lefrLayer.addEnclosureExceptEC((yyvsp[0].dval));
         }
      }
    }
#line 6688 "lef.tab.c"
    break;

  case 217: /* layer_preferenclosure_width_opt: K_WIDTH int_number  */
#line 2449 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addPreferEnclosureWidth((yyvsp[0].dval));
      }
    }
#line 6698 "lef.tab.c"
    break;

  case 219: /* layer_minimumcut_within: K_WITHIN int_number  */
#line 2457 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "MINIMUMCUT WITHIN is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1700, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
         if (lefCallbacks->LayerCbk) {
            lefData->lefrLayer.addMinimumcutWithin((yyvsp[0].dval));
         }
      }
    }
#line 6717 "lef.tab.c"
    break;

  case 221: /* layer_minimumcut_from: K_FROMABOVE  */
#line 2474 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "FROMABOVE statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1596, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      lefData->hasLayerMincut = 1;
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addMinimumcutConnect((char*)"FROMABOVE");

    }
#line 6740 "lef.tab.c"
    break;

  case 222: /* layer_minimumcut_from: K_FROMBELOW  */
#line 2493 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "FROMBELOW statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1597, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      }
      lefData->hasLayerMincut = 1;
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addMinimumcutConnect((char*)"FROMBELOW");
    }
#line 6762 "lef.tab.c"
    break;

  case 224: /* layer_minimumcut_length: K_LENGTH int_number K_WITHIN int_number  */
#line 2513 "lef.y"
    {   
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "LENGTH WITHIN statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1598, lefData->outMsg);
               lefFree(lefData->outMsg);
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addMinimumcutLengDis((yyvsp[-2].dval), (yyvsp[0].dval));
    }
#line 6783 "lef.tab.c"
    break;

  case 227: /* layer_minstep_option: layer_minstep_type  */
#line 2535 "lef.y"
  {
    if (lefCallbacks->LayerCbk) lefData->lefrLayer.addMinstepType((yyvsp[0].string));
  }
#line 6791 "lef.tab.c"
    break;

  case 228: /* layer_minstep_option: K_LENGTHSUM int_number  */
#line 2539 "lef.y"
  {
    if (lefCallbacks->LayerCbk) lefData->lefrLayer.addMinstepLengthsum((yyvsp[0].dval));
  }
#line 6799 "lef.tab.c"
    break;

  case 229: /* layer_minstep_option: K_MAXEDGES int_number  */
#line 2543 "lef.y"
  {
    if (lefData->versionNum < 5.7) {
      lefData->outMsg = (char*)lefMalloc(10000);
      sprintf(lefData->outMsg,
        "MAXEDGES is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
      lefError(1710, lefData->outMsg);
      lefFree(lefData->outMsg);
      CHKERR();
    } else
       if (lefCallbacks->LayerCbk) lefData->lefrLayer.addMinstepMaxedges((int)(yyvsp[0].dval));
  }
#line 6815 "lef.tab.c"
    break;

  case 230: /* layer_minstep_type: K_INSIDECORNER  */
#line 2556 "lef.y"
                 {(yyval.string) = (char*)"INSIDECORNER";}
#line 6821 "lef.tab.c"
    break;

  case 231: /* layer_minstep_type: K_OUTSIDECORNER  */
#line 2557 "lef.y"
                    {(yyval.string) = (char*)"OUTSIDECORNER";}
#line 6827 "lef.tab.c"
    break;

  case 232: /* layer_minstep_type: K_STEP  */
#line 2558 "lef.y"
           {(yyval.string) = (char*)"STEP";}
#line 6833 "lef.tab.c"
    break;

  case 233: /* layer_antenna_pwl: int_number  */
#line 2562 "lef.y"
      { if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setAntennaValue(lefData->antennaType, (yyvsp[0].dval)); }
#line 6840 "lef.tab.c"
    break;

  case 234: /* $@38: %empty  */
#line 2565 "lef.y"
      { if (lefCallbacks->LayerCbk) { // require min 2 points, set the 1st 2 
          if (lefData->lefrAntennaPWLPtr) {
            lefData->lefrAntennaPWLPtr->Destroy();
            lefFree(lefData->lefrAntennaPWLPtr);
          }

          lefData->lefrAntennaPWLPtr = lefiAntennaPWL::create();
          lefData->lefrAntennaPWLPtr->addAntennaPWL((yyvsp[-1].pt).x, (yyvsp[-1].pt).y);
          lefData->lefrAntennaPWLPtr->addAntennaPWL((yyvsp[0].pt).x, (yyvsp[0].pt).y);
        }
      }
#line 6856 "lef.tab.c"
    break;

  case 235: /* layer_antenna_pwl: K_PWL '(' pt pt $@38 layer_diffusion_ratios ')'  */
#line 2577 "lef.y"
      { 
        if (lefCallbacks->LayerCbk) {
          lefData->lefrLayer.setAntennaPWL(lefData->antennaType, lefData->lefrAntennaPWLPtr);
          lefData->lefrAntennaPWLPtr = NULL;
        }
      }
#line 6867 "lef.tab.c"
    break;

  case 238: /* layer_diffusion_ratio: pt  */
#line 2590 "lef.y"
  { if (lefCallbacks->LayerCbk)
      lefData->lefrAntennaPWLPtr->addAntennaPWL((yyvsp[0].pt).x, (yyvsp[0].pt).y);
  }
#line 6875 "lef.tab.c"
    break;

  case 240: /* layer_antenna_duo: K_DIFFUSEONLY  */
#line 2596 "lef.y"
      { 
        lefData->use5_4 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        }
        else if ((lefData->antennaType == lefiAntennaAF) && (lefData->versionNum <= 5.3)) {
           if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
              if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                   "ANTENNAAREAFACTOR with DIFFUSEONLY statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1599, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        } else if (lefData->use5_3) {
           if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
              if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                   "ANTENNAAREAFACTOR with DIFFUSEONLY statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1599, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setAntennaDUO(lefData->antennaType);
      }
#line 6911 "lef.tab.c"
    break;

  case 241: /* layer_table_type: K_PEAK  */
#line 2629 "lef.y"
               {(yyval.string) = (char*)"PEAK";}
#line 6917 "lef.tab.c"
    break;

  case 242: /* layer_table_type: K_AVERAGE  */
#line 2630 "lef.y"
               {(yyval.string) = (char*)"AVERAGE";}
#line 6923 "lef.tab.c"
    break;

  case 243: /* layer_table_type: K_RMS  */
#line 2631 "lef.y"
               {(yyval.string) = (char*)"RMS";}
#line 6929 "lef.tab.c"
    break;

  case 244: /* $@39: %empty  */
#line 2635 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval)); }
#line 6935 "lef.tab.c"
    break;

  case 245: /* $@40: %empty  */
#line 2637 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addAcFrequency(); }
#line 6941 "lef.tab.c"
    break;

  case 246: /* $@41: %empty  */
#line 2640 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval)); }
#line 6947 "lef.tab.c"
    break;

  case 247: /* layer_frequency: K_FREQUENCY NUMBER $@39 number_list ';' $@40 ac_layer_table_opt K_TABLEENTRIES NUMBER $@41 number_list ';'  */
#line 2642 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addAcTableEntry(); }
#line 6953 "lef.tab.c"
    break;

  case 249: /* $@42: %empty  */
#line 2646 "lef.y"
    {
      if (!lefData->layerCut) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1600, "CUTAREA statement can only be defined in LAYER with TYPE CUT.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval));
    }
#line 6969 "lef.tab.c"
    break;

  case 250: /* ac_layer_table_opt: K_CUTAREA NUMBER $@42 number_list ';'  */
#line 2658 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addAcCutarea(); }
#line 6975 "lef.tab.c"
    break;

  case 251: /* $@43: %empty  */
#line 2660 "lef.y"
    {
      if (!lefData->layerRout) {
         if (lefCallbacks->LayerCbk) { // write error only if cbk is set 
            if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1601, "WIDTH can only be defined in LAYER with TYPE ROUTING.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval));
    }
#line 6991 "lef.tab.c"
    break;

  case 252: /* ac_layer_table_opt: K_WIDTH int_number $@43 int_number_list ';'  */
#line 2672 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addAcWidth(); }
#line 6997 "lef.tab.c"
    break;

  case 253: /* $@44: %empty  */
#line 2676 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval)); }
#line 7003 "lef.tab.c"
    break;

  case 254: /* dc_layer_table: K_TABLEENTRIES int_number $@44 int_number_list ';'  */
#line 2678 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addDcTableEntry(); }
#line 7009 "lef.tab.c"
    break;

  case 256: /* int_number_list: int_number_list int_number  */
#line 2682 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval)); }
#line 7015 "lef.tab.c"
    break;

  case 258: /* number_list: number_list NUMBER  */
#line 2686 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval)); }
#line 7021 "lef.tab.c"
    break;

  case 261: /* layer_prop: T_STRING T_STRING  */
#line 2695 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        char propTp;
        propTp = lefSettings->lefProps.lefrLayerProp.propType((yyvsp[-1].string));
        lefData->lefrLayer.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 7033 "lef.tab.c"
    break;

  case 262: /* layer_prop: T_STRING QSTRING  */
#line 2703 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        char propTp;
        propTp = lefSettings->lefProps.lefrLayerProp.propType((yyvsp[-1].string));
        lefData->lefrLayer.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 7045 "lef.tab.c"
    break;

  case 263: /* layer_prop: T_STRING NUMBER  */
#line 2711 "lef.y"
    {
      char temp[32];
      sprintf(temp, "%.11g", (yyvsp[0].dval));
      if (lefCallbacks->LayerCbk) {
        char propTp;
        propTp = lefSettings->lefProps.lefrLayerProp.propType((yyvsp[-1].string));
        lefData->lefrLayer.addNumProp((yyvsp[-1].string), (yyvsp[0].dval), temp, propTp);
      }
    }
#line 7059 "lef.tab.c"
    break;

  case 264: /* current_density_pwl_list: current_density_pwl  */
#line 2723 "lef.y"
    { }
#line 7065 "lef.tab.c"
    break;

  case 265: /* current_density_pwl_list: current_density_pwl_list current_density_pwl  */
#line 2725 "lef.y"
    { }
#line 7071 "lef.tab.c"
    break;

  case 266: /* current_density_pwl: '(' int_number int_number ')'  */
#line 2728 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCurrentPoint((yyvsp[-2].dval), (yyvsp[-1].dval)); }
#line 7077 "lef.tab.c"
    break;

  case 269: /* cap_point: '(' int_number int_number ')'  */
#line 2736 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.setCapacitancePoint((yyvsp[-2].dval), (yyvsp[-1].dval)); }
#line 7083 "lef.tab.c"
    break;

  case 271: /* res_points: res_points res_point  */
#line 2741 "lef.y"
    { }
#line 7089 "lef.tab.c"
    break;

  case 272: /* res_point: '(' int_number int_number ')'  */
#line 2744 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.setResistancePoint((yyvsp[-2].dval), (yyvsp[-1].dval)); }
#line 7095 "lef.tab.c"
    break;

  case 273: /* layer_type: K_ROUTING  */
#line 2747 "lef.y"
                  {(yyval.string) = (char*)"ROUTING"; lefData->layerRout = 1;}
#line 7101 "lef.tab.c"
    break;

  case 274: /* layer_type: K_CUT  */
#line 2748 "lef.y"
                  {(yyval.string) = (char*)"CUT"; lefData->layerCut = 1;}
#line 7107 "lef.tab.c"
    break;

  case 275: /* layer_type: K_OVERLAP  */
#line 2749 "lef.y"
                  {(yyval.string) = (char*)"OVERLAP"; lefData->layerMastOver = 1;}
#line 7113 "lef.tab.c"
    break;

  case 276: /* layer_type: K_MASTERSLICE  */
#line 2750 "lef.y"
                  {(yyval.string) = (char*)"MASTERSLICE"; lefData->layerMastOver = 1;}
#line 7119 "lef.tab.c"
    break;

  case 277: /* layer_type: K_VIRTUAL  */
#line 2751 "lef.y"
                  {(yyval.string) = (char*)"VIRTUAL";}
#line 7125 "lef.tab.c"
    break;

  case 278: /* layer_type: K_IMPLANT  */
#line 2752 "lef.y"
                  {(yyval.string) = (char*)"IMPLANT";}
#line 7131 "lef.tab.c"
    break;

  case 279: /* layer_direction: K_HORIZONTAL  */
#line 2755 "lef.y"
                    {(yyval.string) = (char*)"HORIZONTAL";}
#line 7137 "lef.tab.c"
    break;

  case 280: /* layer_direction: K_VERTICAL  */
#line 2756 "lef.y"
                    {(yyval.string) = (char*)"VERTICAL";}
#line 7143 "lef.tab.c"
    break;

  case 281: /* layer_direction: K_DIAG45  */
#line 2757 "lef.y"
                    {(yyval.string) = (char*)"DIAG45";}
#line 7149 "lef.tab.c"
    break;

  case 282: /* layer_direction: K_DIAG135  */
#line 2758 "lef.y"
                    {(yyval.string) = (char*)"DIAG135";}
#line 7155 "lef.tab.c"
    break;

  case 284: /* layer_minen_width: K_WIDTH int_number  */
#line 2762 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addMinenclosedareaWidth((yyvsp[0].dval));
    }
#line 7164 "lef.tab.c"
    break;

  case 285: /* layer_oxide: K_OXIDE1  */
#line 2769 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(1);
    }
#line 7173 "lef.tab.c"
    break;

  case 286: /* layer_oxide: K_OXIDE2  */
#line 2774 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(2);
    }
#line 7182 "lef.tab.c"
    break;

  case 287: /* layer_oxide: K_OXIDE3  */
#line 2779 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(3);
    }
#line 7191 "lef.tab.c"
    break;

  case 288: /* layer_oxide: K_OXIDE4  */
#line 2784 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(4);
    }
#line 7200 "lef.tab.c"
    break;

  case 289: /* layer_oxide: K_OXIDE5  */
#line 2789 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(5);
    }
#line 7209 "lef.tab.c"
    break;

  case 290: /* layer_oxide: K_OXIDE6  */
#line 2794 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(6);
    }
#line 7218 "lef.tab.c"
    break;

  case 291: /* layer_oxide: K_OXIDE7  */
#line 2799 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(7);
    }
#line 7227 "lef.tab.c"
    break;

  case 292: /* layer_oxide: K_OXIDE8  */
#line 2804 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(8);
    }
#line 7236 "lef.tab.c"
    break;

  case 293: /* layer_oxide: K_OXIDE9  */
#line 2809 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(9);
    }
#line 7245 "lef.tab.c"
    break;

  case 294: /* layer_oxide: K_OXIDE10  */
#line 2814 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(10);
    }
#line 7254 "lef.tab.c"
    break;

  case 295: /* layer_oxide: K_OXIDE11  */
#line 2819 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(11);
    }
#line 7263 "lef.tab.c"
    break;

  case 296: /* layer_oxide: K_OXIDE12  */
#line 2824 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(12);
    }
#line 7272 "lef.tab.c"
    break;

  case 297: /* layer_oxide: K_OXIDE13  */
#line 2829 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(13);
    }
#line 7281 "lef.tab.c"
    break;

  case 298: /* layer_oxide: K_OXIDE14  */
#line 2834 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(14);
    }
#line 7290 "lef.tab.c"
    break;

  case 299: /* layer_oxide: K_OXIDE15  */
#line 2839 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(15);
    }
#line 7299 "lef.tab.c"
    break;

  case 300: /* layer_oxide: K_OXIDE16  */
#line 2844 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(16);
    }
#line 7308 "lef.tab.c"
    break;

  case 301: /* layer_oxide: K_OXIDE17  */
#line 2849 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(17);
    }
#line 7317 "lef.tab.c"
    break;

  case 302: /* layer_oxide: K_OXIDE18  */
#line 2854 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(18);
    }
#line 7326 "lef.tab.c"
    break;

  case 303: /* layer_oxide: K_OXIDE19  */
#line 2859 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(19);
    }
#line 7335 "lef.tab.c"
    break;

  case 304: /* layer_oxide: K_OXIDE20  */
#line 2864 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(20);
    }
#line 7344 "lef.tab.c"
    break;

  case 305: /* layer_oxide: K_OXIDE21  */
#line 2869 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(21);
    }
#line 7353 "lef.tab.c"
    break;

  case 306: /* layer_oxide: K_OXIDE22  */
#line 2874 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(22);
    }
#line 7362 "lef.tab.c"
    break;

  case 307: /* layer_oxide: K_OXIDE23  */
#line 2879 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(23);
    }
#line 7371 "lef.tab.c"
    break;

  case 308: /* layer_oxide: K_OXIDE24  */
#line 2884 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(24);
    }
#line 7380 "lef.tab.c"
    break;

  case 309: /* layer_oxide: K_OXIDE25  */
#line 2889 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(25);
    }
#line 7389 "lef.tab.c"
    break;

  case 310: /* layer_oxide: K_OXIDE26  */
#line 2894 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(26);
    }
#line 7398 "lef.tab.c"
    break;

  case 311: /* layer_oxide: K_OXIDE27  */
#line 2899 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(27);
    }
#line 7407 "lef.tab.c"
    break;

  case 312: /* layer_oxide: K_OXIDE28  */
#line 2904 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(28);
    }
#line 7416 "lef.tab.c"
    break;

  case 313: /* layer_oxide: K_OXIDE29  */
#line 2909 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(29);
    }
#line 7425 "lef.tab.c"
    break;

  case 314: /* layer_oxide: K_OXIDE30  */
#line 2914 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(30);
    }
#line 7434 "lef.tab.c"
    break;

  case 315: /* layer_oxide: K_OXIDE31  */
#line 2919 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(31);
    }
#line 7443 "lef.tab.c"
    break;

  case 316: /* layer_oxide: K_OXIDE32  */
#line 2924 "lef.y"
    {
    if (lefCallbacks->LayerCbk)
       lefData->lefrLayer.addAntennaModel(32);
    }
#line 7452 "lef.tab.c"
    break;

  case 317: /* layer_sp_parallel_widths: %empty  */
#line 2930 "lef.y"
    { }
#line 7458 "lef.tab.c"
    break;

  case 318: /* layer_sp_parallel_widths: layer_sp_parallel_widths layer_sp_parallel_width  */
#line 2932 "lef.y"
    { }
#line 7464 "lef.tab.c"
    break;

  case 319: /* $@45: %empty  */
#line 2935 "lef.y"
    { 
      if (lefCallbacks->LayerCbk) {
         lefData->lefrLayer.addSpParallelWidth((yyvsp[0].dval));
      }
    }
#line 7474 "lef.tab.c"
    break;

  case 320: /* layer_sp_parallel_width: K_WIDTH int_number $@45 int_number_list  */
#line 2941 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpParallelWidthSpacing(); }
#line 7480 "lef.tab.c"
    break;

  case 321: /* layer_sp_TwoWidths: %empty  */
#line 2944 "lef.y"
    { }
#line 7486 "lef.tab.c"
    break;

  case 322: /* layer_sp_TwoWidths: layer_sp_TwoWidth layer_sp_TwoWidths  */
#line 2946 "lef.y"
    { }
#line 7492 "lef.tab.c"
    break;

  case 323: /* $@46: %empty  */
#line 2949 "lef.y"
    {
       if (lefCallbacks->LayerCbk) lefData->lefrLayer.addNumber((yyvsp[0].dval));
    }
#line 7500 "lef.tab.c"
    break;

  case 324: /* layer_sp_TwoWidth: K_WIDTH int_number layer_sp_TwoWidthsPRL int_number $@46 int_number_list  */
#line 2953 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
         lefData->lefrLayer.addSpTwoWidths((yyvsp[-4].dval), (yyvsp[-3].dval));
    }
#line 7509 "lef.tab.c"
    break;

  case 325: /* layer_sp_TwoWidthsPRL: %empty  */
#line 2959 "lef.y"
    { 
        (yyval.dval) = -1; // cannot use 0, since PRL number can be 0 
        lefData->lefrLayer.setSpTwoWidthsHasPRL(0);
    }
#line 7518 "lef.tab.c"
    break;

  case 326: /* layer_sp_TwoWidthsPRL: K_PRL int_number  */
#line 2964 "lef.y"
    { 
        (yyval.dval) = (yyvsp[0].dval); 
        lefData->lefrLayer.setSpTwoWidthsHasPRL(1);
    }
#line 7527 "lef.tab.c"
    break;

  case 327: /* layer_sp_influence_widths: %empty  */
#line 2970 "lef.y"
    { }
#line 7533 "lef.tab.c"
    break;

  case 328: /* layer_sp_influence_widths: layer_sp_influence_widths layer_sp_influence_width  */
#line 2972 "lef.y"
    { }
#line 7539 "lef.tab.c"
    break;

  case 329: /* layer_sp_influence_width: K_WIDTH int_number K_WITHIN int_number K_SPACING int_number  */
#line 2975 "lef.y"
    { if (lefCallbacks->LayerCbk) lefData->lefrLayer.addSpInfluence((yyvsp[-4].dval), (yyvsp[-2].dval), (yyvsp[0].dval)); }
#line 7545 "lef.tab.c"
    break;

  case 330: /* maxstack_via: K_MAXVIASTACK int_number ';'  */
#line 2978 "lef.y"
    {
      if (!lefData->lefrHasLayer) {  // 5.5 
        if (lefCallbacks->MaxStackViaCbk) { // write error only if cbk is set 
           if (lefData->maxStackViaWarnings++ < lefSettings->MaxStackViaWarnings) {
             lefError(1602, "MAXVIASTACK statement has to be defined after the LAYER statement.");
             CHKERR();
           }
        }
      } else if (lefData->lefrHasMaxVS) {
        if (lefCallbacks->MaxStackViaCbk) { // write error only if cbk is set 
           if (lefData->maxStackViaWarnings++ < lefSettings->MaxStackViaWarnings) {
             lefError(1603, "A MAXVIASTACK was already defined.\nOnly one MAXVIASTACK is allowed per lef file.");
             CHKERR();
           }
        }
      } else {
        if (lefCallbacks->MaxStackViaCbk) {
           lefData->lefrMaxStackVia.setMaxStackVia((int)(yyvsp[-1].dval));
           CALLBACK(lefCallbacks->MaxStackViaCbk, lefrMaxStackViaCbkType, &lefData->lefrMaxStackVia);
        }
      }
      if (lefData->versionNum < 5.5) {
        if (lefCallbacks->MaxStackViaCbk) { // write error only if cbk is set 
           if (lefData->maxStackViaWarnings++ < lefSettings->MaxStackViaWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "MAXVIASTACK statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1604, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      lefData->lefrHasMaxVS = 1;
    }
#line 7585 "lef.tab.c"
    break;

  case 331: /* $@47: %empty  */
#line 3013 "lef.y"
                                     {lefData->lefDumbMode = 2; lefData->lefNoNum= 2;}
#line 7591 "lef.tab.c"
    break;

  case 332: /* maxstack_via: K_MAXVIASTACK int_number K_RANGE $@47 T_STRING T_STRING ';'  */
#line 3015 "lef.y"
    {
      if (!lefData->lefrHasLayer) {  // 5.5 
        if (lefCallbacks->MaxStackViaCbk) { // write error only if cbk is set 
           if (lefData->maxStackViaWarnings++ < lefSettings->MaxStackViaWarnings) {
              lefError(1602, "MAXVIASTACK statement has to be defined after the LAYER statement.");
              CHKERR();
           }
        }
      } else if (lefData->lefrHasMaxVS) {
        if (lefCallbacks->MaxStackViaCbk) { // write error only if cbk is set 
           if (lefData->maxStackViaWarnings++ < lefSettings->MaxStackViaWarnings) {
             lefError(1603, "A MAXVIASTACK was already defined.\nOnly one MAXVIASTACK is allowed per lef file.");
             CHKERR();
           }
        }
      } else {
        if (lefCallbacks->MaxStackViaCbk) {
           lefData->lefrMaxStackVia.setMaxStackVia((int)(yyvsp[-5].dval));
           lefData->lefrMaxStackVia.setMaxStackViaRange((yyvsp[-2].string), (yyvsp[-1].string));
           CALLBACK(lefCallbacks->MaxStackViaCbk, lefrMaxStackViaCbkType, &lefData->lefrMaxStackVia);
        }
      }
      lefData->lefrHasMaxVS = 1;
    }
#line 7620 "lef.tab.c"
    break;

  case 333: /* $@48: %empty  */
#line 3040 "lef.y"
                { lefData->hasViaRule_layer = 0; }
#line 7626 "lef.tab.c"
    break;

  case 334: /* via: start_via $@48 via_option end_via  */
#line 3041 "lef.y"
    { 
      if (lefCallbacks->ViaCbk) {
        if (lefData->ndRule) 
            lefData->nd->addViaRule(&lefData->lefrVia);
         else 
            CALLBACK(lefCallbacks->ViaCbk, lefrViaCbkType, &lefData->lefrVia);
       }
    }
#line 7639 "lef.tab.c"
    break;

  case 335: /* via_keyword: K_VIA  */
#line 3051 "lef.y"
     { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 7645 "lef.tab.c"
    break;

  case 336: /* start_via: via_keyword T_STRING  */
#line 3054 "lef.y"
    {
      // 0 is nodefault 
      if (lefCallbacks->ViaCbk) lefData->lefrVia.setName((yyvsp[0].string), 0);
      lefData->viaLayer = 0;
      lefData->numVia++;
      //strcpy(lefData->viaName, $2);
      lefData->viaName = strdup((yyvsp[0].string));
    }
#line 7658 "lef.tab.c"
    break;

  case 337: /* start_via: via_keyword T_STRING K_DEFAULT  */
#line 3063 "lef.y"
    {
      // 1 is default 
      if (lefCallbacks->ViaCbk) lefData->lefrVia.setName((yyvsp[-1].string), 1);
      lefData->viaLayer = 0;
      //strcpy(lefData->viaName, $2);
      lefData->viaName = strdup((yyvsp[-1].string));
    }
#line 7670 "lef.tab.c"
    break;

  case 338: /* start_via: via_keyword T_STRING K_GENERATED  */
#line 3071 "lef.y"
    {
      // 2 is generated 
      if (lefCallbacks->ViaCbk) lefData->lefrVia.setName((yyvsp[-1].string), 2);
      lefData->viaLayer = 0;
      //strcpy(lefData->viaName, $2);
      lefData->viaName = strdup((yyvsp[-1].string));
    }
#line 7682 "lef.tab.c"
    break;

  case 339: /* $@49: %empty  */
#line 3079 "lef.y"
                       {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 7688 "lef.tab.c"
    break;

  case 340: /* $@50: %empty  */
#line 3081 "lef.y"
           {lefData->lefDumbMode = 3; lefData->lefNoNum = 1; }
#line 7694 "lef.tab.c"
    break;

  case 341: /* $@51: %empty  */
#line 3084 "lef.y"
    {
       if (lefData->versionNum < 5.6) {
         if (lefCallbacks->ViaCbk) { // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "VIARULE statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1709, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
            }
         }
       }  else
          if (lefCallbacks->ViaCbk) lefData->lefrVia.setViaRule((yyvsp[-21].string), (yyvsp[-18].dval), (yyvsp[-17].dval), (yyvsp[-13].string), (yyvsp[-12].string), (yyvsp[-11].string),
                          (yyvsp[-8].dval), (yyvsp[-7].dval), (yyvsp[-4].dval), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval));
       lefData->viaLayer++;
       lefData->hasViaRule_layer = 1;
    }
#line 7717 "lef.tab.c"
    break;

  case 345: /* via_viarule_option: K_ROWCOL int_number int_number ';'  */
#line 3110 "lef.y"
    {
       if (lefCallbacks->ViaCbk) lefData->lefrVia.setRowCol((int)(yyvsp[-2].dval), (int)(yyvsp[-1].dval));
    }
#line 7725 "lef.tab.c"
    break;

  case 346: /* via_viarule_option: K_ORIGIN int_number int_number ';'  */
#line 3114 "lef.y"
    {
       if (lefCallbacks->ViaCbk) lefData->lefrVia.setOrigin((yyvsp[-2].dval), (yyvsp[-1].dval));
    }
#line 7733 "lef.tab.c"
    break;

  case 347: /* via_viarule_option: K_OFFSET int_number int_number int_number int_number ';'  */
#line 3118 "lef.y"
    {
       if (lefCallbacks->ViaCbk) lefData->lefrVia.setOffset((yyvsp[-4].dval), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval));
    }
#line 7741 "lef.tab.c"
    break;

  case 348: /* $@52: %empty  */
#line 3121 "lef.y"
              {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 7747 "lef.tab.c"
    break;

  case 349: /* via_viarule_option: K_PATTERN $@52 T_STRING ';'  */
#line 3122 "lef.y"
    {
       if (lefCallbacks->ViaCbk) lefData->lefrVia.setPattern((yyvsp[-1].string));
    }
#line 7755 "lef.tab.c"
    break;

  case 355: /* via_other_option: via_foreign  */
#line 3139 "lef.y"
    { }
#line 7761 "lef.tab.c"
    break;

  case 356: /* via_other_option: via_layer_rule  */
#line 3141 "lef.y"
    { }
#line 7767 "lef.tab.c"
    break;

  case 357: /* via_other_option: K_RESISTANCE int_number ';'  */
#line 3143 "lef.y"
    { if (lefCallbacks->ViaCbk) lefData->lefrVia.setResistance((yyvsp[-1].dval)); }
#line 7773 "lef.tab.c"
    break;

  case 358: /* $@53: %empty  */
#line 3144 "lef.y"
               { lefData->lefDumbMode = 1000000; }
#line 7779 "lef.tab.c"
    break;

  case 359: /* via_other_option: K_PROPERTY $@53 via_prop_list ';'  */
#line 3145 "lef.y"
    { lefData->lefDumbMode = 0;
    }
#line 7786 "lef.tab.c"
    break;

  case 360: /* via_other_option: K_TOPOFSTACKONLY  */
#line 3148 "lef.y"
    { 
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaCbk) lefData->lefrVia.setTopOfStack();
      } else
        if (lefCallbacks->ViaCbk)  // write warning only if cbk is set 
           if (lefData->viaWarnings++ < lefSettings->ViaWarnings)
              lefWarning(2019, "TOPOFSTACKONLY statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later");
    }
#line 7799 "lef.tab.c"
    break;

  case 363: /* via_name_value_pair: T_STRING NUMBER  */
#line 3164 "lef.y"
    { 
      char temp[32];
      sprintf(temp, "%.11g", (yyvsp[0].dval));
      if (lefCallbacks->ViaCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaProp.propType((yyvsp[-1].string));
         lefData->lefrVia.addNumProp((yyvsp[-1].string), (yyvsp[0].dval), temp, propTp);
      }
    }
#line 7813 "lef.tab.c"
    break;

  case 364: /* via_name_value_pair: T_STRING QSTRING  */
#line 3174 "lef.y"
    {
      if (lefCallbacks->ViaCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaProp.propType((yyvsp[-1].string));
         lefData->lefrVia.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 7825 "lef.tab.c"
    break;

  case 365: /* via_name_value_pair: T_STRING T_STRING  */
#line 3182 "lef.y"
    {
      if (lefCallbacks->ViaCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaProp.propType((yyvsp[-1].string));
         lefData->lefrVia.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 7837 "lef.tab.c"
    break;

  case 366: /* via_foreign: start_foreign ';'  */
#line 3192 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaCbk) lefData->lefrVia.setForeign((yyvsp[-1].string), 0, 0.0, 0.0, -1);
      } else
        if (lefCallbacks->ViaCbk)  // write warning only if cbk is set 
           if (lefData->viaWarnings++ < lefSettings->ViaWarnings)
             lefWarning(2020, "FOREIGN statement in VIA is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 7850 "lef.tab.c"
    break;

  case 367: /* via_foreign: start_foreign pt ';'  */
#line 3201 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaCbk) lefData->lefrVia.setForeign((yyvsp[-2].string), 1, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, -1);
      } else
        if (lefCallbacks->ViaCbk)  // write warning only if cbk is set 
           if (lefData->viaWarnings++ < lefSettings->ViaWarnings)
             lefWarning(2020, "FOREIGN statement in VIA is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 7863 "lef.tab.c"
    break;

  case 368: /* via_foreign: start_foreign pt orientation ';'  */
#line 3210 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaCbk) lefData->lefrVia.setForeign((yyvsp[-3].string), 1, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].integer));
      } else
        if (lefCallbacks->ViaCbk)  // write warning only if cbk is set 
           if (lefData->viaWarnings++ < lefSettings->ViaWarnings)
             lefWarning(2020, "FOREIGN statement in VIA is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 7876 "lef.tab.c"
    break;

  case 369: /* via_foreign: start_foreign orientation ';'  */
#line 3219 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaCbk) lefData->lefrVia.setForeign((yyvsp[-2].string), 0, 0.0, 0.0, (yyvsp[-1].integer));
      } else
        if (lefCallbacks->ViaCbk)  // write warning only if cbk is set 
           if (lefData->viaWarnings++ < lefSettings->ViaWarnings)
             lefWarning(2020, "FOREIGN statement in VIA is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 7889 "lef.tab.c"
    break;

  case 370: /* $@54: %empty  */
#line 3228 "lef.y"
                                {lefData->lefDumbMode = 1; lefData->lefNoNum= 1;}
#line 7895 "lef.tab.c"
    break;

  case 371: /* start_foreign: K_FOREIGN $@54 T_STRING  */
#line 3229 "lef.y"
    { (yyval.string) = (yyvsp[0].string); }
#line 7901 "lef.tab.c"
    break;

  case 372: /* orientation: K_N  */
#line 3232 "lef.y"
              {(yyval.integer) = 0;}
#line 7907 "lef.tab.c"
    break;

  case 373: /* orientation: K_W  */
#line 3233 "lef.y"
              {(yyval.integer) = 1;}
#line 7913 "lef.tab.c"
    break;

  case 374: /* orientation: K_S  */
#line 3234 "lef.y"
              {(yyval.integer) = 2;}
#line 7919 "lef.tab.c"
    break;

  case 375: /* orientation: K_E  */
#line 3235 "lef.y"
              {(yyval.integer) = 3;}
#line 7925 "lef.tab.c"
    break;

  case 376: /* orientation: K_FN  */
#line 3236 "lef.y"
              {(yyval.integer) = 4;}
#line 7931 "lef.tab.c"
    break;

  case 377: /* orientation: K_FW  */
#line 3237 "lef.y"
              {(yyval.integer) = 5;}
#line 7937 "lef.tab.c"
    break;

  case 378: /* orientation: K_FS  */
#line 3238 "lef.y"
              {(yyval.integer) = 6;}
#line 7943 "lef.tab.c"
    break;

  case 379: /* orientation: K_FE  */
#line 3239 "lef.y"
              {(yyval.integer) = 7;}
#line 7949 "lef.tab.c"
    break;

  case 380: /* orientation: K_R0  */
#line 3240 "lef.y"
              {(yyval.integer) = 0;}
#line 7955 "lef.tab.c"
    break;

  case 381: /* orientation: K_R90  */
#line 3241 "lef.y"
              {(yyval.integer) = 1;}
#line 7961 "lef.tab.c"
    break;

  case 382: /* orientation: K_R180  */
#line 3242 "lef.y"
              {(yyval.integer) = 2;}
#line 7967 "lef.tab.c"
    break;

  case 383: /* orientation: K_R270  */
#line 3243 "lef.y"
              {(yyval.integer) = 3;}
#line 7973 "lef.tab.c"
    break;

  case 384: /* orientation: K_MY  */
#line 3244 "lef.y"
              {(yyval.integer) = 4;}
#line 7979 "lef.tab.c"
    break;

  case 385: /* orientation: K_MYR90  */
#line 3245 "lef.y"
              {(yyval.integer) = 5;}
#line 7985 "lef.tab.c"
    break;

  case 386: /* orientation: K_MX  */
#line 3246 "lef.y"
              {(yyval.integer) = 6;}
#line 7991 "lef.tab.c"
    break;

  case 387: /* orientation: K_MXR90  */
#line 3247 "lef.y"
              {(yyval.integer) = 7;}
#line 7997 "lef.tab.c"
    break;

  case 388: /* via_layer_rule: via_layer via_geometries  */
#line 3250 "lef.y"
    { }
#line 8003 "lef.tab.c"
    break;

  case 389: /* $@55: %empty  */
#line 3252 "lef.y"
                   {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 8009 "lef.tab.c"
    break;

  case 390: /* via_layer: K_LAYER $@55 T_STRING ';'  */
#line 3253 "lef.y"
    {
      if (lefCallbacks->ViaCbk) lefData->lefrVia.addLayer((yyvsp[-1].string));
      lefData->viaLayer++;
      lefData->hasViaRule_layer = 1;
    }
#line 8019 "lef.tab.c"
    break;

  case 393: /* via_geometry: K_RECT maskColor pt pt ';'  */
#line 3266 "lef.y"
    { 
      if (lefCallbacks->ViaCbk) {
        if (lefData->versionNum < 5.8 && (int)(yyvsp[-3].integer) > 0) {
          if (lefData->viaWarnings++ < lefSettings->ViaWarnings) {
              lefError(2081, "MASK information can only be defined with version 5.8");
              CHKERR(); 
            }           
        } else {
          lefData->lefrVia.addRectToLayer((int)(yyvsp[-3].integer), (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y);
        }
      }
    }
#line 8036 "lef.tab.c"
    break;

  case 394: /* $@56: %empty  */
#line 3279 "lef.y"
    {
      lefData->lefrGeometriesPtr = (lefiGeometries*)lefMalloc(sizeof(lefiGeometries));
      lefData->lefrGeometriesPtr->Init();
      lefData->lefrDoGeometries = 1;
    }
#line 8046 "lef.tab.c"
    break;

  case 395: /* via_geometry: K_POLYGON maskColor $@56 firstPt nextPt nextPt otherPts ';'  */
#line 3285 "lef.y"
    { 
      if (lefCallbacks->ViaCbk) {
        if (lefData->versionNum < 5.8 && (yyvsp[-6].integer) > 0) {
          if (lefData->viaWarnings++ < lefSettings->ViaWarnings) {
              lefError(2083, "Color mask information can only be defined with version 5.8.");
              CHKERR(); 
            }           
        } else {
            lefData->lefrGeometriesPtr->addPolygon((int)(yyvsp[-6].integer));
            lefData->lefrVia.addPolyToLayer((int)(yyvsp[-6].integer), lefData->lefrGeometriesPtr);   // 5.6
        }
      }
      lefData->lefrGeometriesPtr->clearPolyItems(); // free items fields
      lefFree((char*)(lefData->lefrGeometriesPtr)); // Don't need anymore, poly data has
      lefData->lefrDoGeometries = 0;                // copied
    }
#line 8067 "lef.tab.c"
    break;

  case 396: /* $@57: %empty  */
#line 3302 "lef.y"
               {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8073 "lef.tab.c"
    break;

  case 397: /* end_via: K_END $@57 T_STRING  */
#line 3303 "lef.y"
    { 
      // 10/17/2001 - Wanda da Rosa, PCR 404149
      //              Error if no layer in via
      if (!lefData->viaLayer) {
         if (lefCallbacks->ViaCbk) {  // write error only if cbk is set 
            if (lefData->viaWarnings++ < lefSettings->ViaWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "A LAYER statement is missing in the VIA %s.\nAt least one LAYERis required per VIA statement.", (yyvsp[0].string));
              lefError(1606, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
            }
         }
      }
      if (strcmp(lefData->viaName, (yyvsp[0].string)) != 0) {
         if (lefCallbacks->ViaCbk) { // write error only if cbk is set 
            if (lefData->viaWarnings++ < lefSettings->ViaWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "END VIA name %s is different from the VIA name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->viaName);
              lefError(1607, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->viaName);
              CHKERR();
            } else
              lefFree(lefData->viaName);
         } else
            lefFree(lefData->viaName);
      } else
         lefFree(lefData->viaName);
    }
#line 8110 "lef.tab.c"
    break;

  case 398: /* $@58: %empty  */
#line 3336 "lef.y"
                            { lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8116 "lef.tab.c"
    break;

  case 399: /* viarule_keyword: K_VIARULE $@58 T_STRING  */
#line 3337 "lef.y"
    { 
      if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setName((yyvsp[0].string));
      lefData->viaRuleLayer = 0;
      //strcpy(lefData->viaRuleName, $3);
      lefData->viaRuleName = strdup((yyvsp[0].string));
      lefData->isGenerate = 0;
    }
#line 8128 "lef.tab.c"
    break;

  case 400: /* viarule: viarule_keyword viarule_layer_list via_names opt_viarule_props end_viarule  */
#line 3347 "lef.y"
    {
      if (lefData->viaRuleLayer == 0 || lefData->viaRuleLayer > 2) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1608, "A VIARULE statement requires two layers.");
              CHKERR();
            }
         }
      }
      if (lefCallbacks->ViaRuleCbk)
        CALLBACK(lefCallbacks->ViaRuleCbk, lefrViaRuleCbkType, &lefData->lefrViaRule);
      // 2/19/2004 - reset the ENCLOSURE overhang values which may be
      // set by the old syntax OVERHANG -- Not necessary, but just incase
      if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.clearLayerOverhang();
    }
#line 8148 "lef.tab.c"
    break;

  case 401: /* $@59: %empty  */
#line 3365 "lef.y"
    {
      lefData->isGenerate = 1;
    }
#line 8156 "lef.tab.c"
    break;

  case 402: /* viarule_generate: viarule_keyword K_GENERATE viarule_generate_default $@59 viarule_layer_list opt_viarule_props end_viarule  */
#line 3369 "lef.y"
    {
      if (lefData->viaRuleLayer == 0) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1708, "A VIARULE GENERATE requires three layers.");
              CHKERR();
            }
         }
      } else if ((lefData->viaRuleLayer < 3) && (lefData->versionNum >= 5.6)) {
         if (lefCallbacks->ViaRuleCbk)  // write warning only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings)
              lefWarning(2021, "turn-via is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      } else {
         if (lefCallbacks->ViaRuleCbk) {
            lefData->lefrViaRule.setGenerate();
            CALLBACK(lefCallbacks->ViaRuleCbk, lefrViaRuleCbkType, &lefData->lefrViaRule);
         }
      }
      // 2/19/2004 - reset the ENCLOSURE overhang values which may be
      // set by the old syntax OVERHANG
      if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.clearLayerOverhang();
    }
#line 8183 "lef.tab.c"
    break;

  case 404: /* viarule_generate_default: K_DEFAULT  */
#line 3394 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "DEFAULT statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1605, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
            }
         }
      } else
        if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setDefault();
    }
#line 8203 "lef.tab.c"
    break;

  case 411: /* $@60: %empty  */
#line 3425 "lef.y"
                         { lefData->lefDumbMode = 10000000;}
#line 8209 "lef.tab.c"
    break;

  case 412: /* viarule_prop: K_PROPERTY $@60 viarule_prop_list ';'  */
#line 3426 "lef.y"
    { lefData->lefDumbMode = 0;
    }
#line 8216 "lef.tab.c"
    break;

  case 415: /* viarule_prop: T_STRING T_STRING  */
#line 3436 "lef.y"
    {
      if (lefCallbacks->ViaRuleCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaRuleProp.propType((yyvsp[-1].string));
         lefData->lefrViaRule.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 8228 "lef.tab.c"
    break;

  case 416: /* viarule_prop: T_STRING QSTRING  */
#line 3444 "lef.y"
    {
      if (lefCallbacks->ViaRuleCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaRuleProp.propType((yyvsp[-1].string));
         lefData->lefrViaRule.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 8240 "lef.tab.c"
    break;

  case 417: /* viarule_prop: T_STRING NUMBER  */
#line 3452 "lef.y"
    {
      char temp[32];
      sprintf(temp, "%.11g", (yyvsp[0].dval));
      if (lefCallbacks->ViaRuleCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrViaRuleProp.propType((yyvsp[-1].string));
         lefData->lefrViaRule.addNumProp((yyvsp[-1].string), (yyvsp[0].dval), temp, propTp);
      }
    }
#line 8254 "lef.tab.c"
    break;

  case 418: /* viarule_layer: viarule_layer_name viarule_layer_options  */
#line 3463 "lef.y"
    {
      // 10/18/2001 - Wanda da Rosa PCR 404181
      //              Make sure the 1st 2 layers in viarule has direction
      // 04/28/2004 - PCR 704072 - DIRECTION in viarule generate is
      //              obsolete in 5.6
      if (lefData->versionNum >= 5.6) {
         if (lefData->viaRuleLayer < 2 && !lefData->viaRuleHasDir && !lefData->viaRuleHasEnc &&
             !lefData->isGenerate) {
            if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
               if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
                  lefError(1705, "VIARULE statement in a layer, requires a DIRECTION construct statement.");
                  CHKERR(); 
               }
            }
         }
      } else {
         if (lefData->viaRuleLayer < 2 && !lefData->viaRuleHasDir && !lefData->viaRuleHasEnc &&
             lefData->isGenerate) {
            if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
               if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
                  lefError(1705, "VIARULE statement in a layer, requires a DIRECTION construct statement.");
                  CHKERR(); 
               }
            }
         }
      }
      lefData->viaRuleLayer++;
    }
#line 8287 "lef.tab.c"
    break;

  case 421: /* via_name: via_keyword T_STRING ';'  */
#line 3499 "lef.y"
    { if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.addViaName((yyvsp[-1].string)); }
#line 8293 "lef.tab.c"
    break;

  case 422: /* $@61: %empty  */
#line 3501 "lef.y"
                            {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 8299 "lef.tab.c"
    break;

  case 423: /* viarule_layer_name: K_LAYER $@61 T_STRING ';'  */
#line 3502 "lef.y"
    { if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setLayer((yyvsp[-1].string));
      lefData->viaRuleHasDir = 0;
      lefData->viaRuleHasEnc = 0;
    }
#line 8308 "lef.tab.c"
    break;

  case 426: /* viarule_layer_option: K_DIRECTION K_HORIZONTAL ';'  */
#line 3514 "lef.y"
    {
      if (lefData->viaRuleHasEnc) {
        if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1706, "An ENCLOSRE statement was already defined in the layer.\nIt is DIRECTION or ENCLOSURE can be specified in a layer.");
              CHKERR();
           }
        }
      } else {
        if ((lefData->versionNum < 5.6) || (!lefData->isGenerate)) {
          if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setHorizontal();
        } else
          if (lefCallbacks->ViaRuleCbk)  // write warning only if cbk is set 
             if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings)
               lefWarning(2022, "DIRECTION statement in VIARULE is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      }
      lefData->viaRuleHasDir = 1;
    }
#line 8331 "lef.tab.c"
    break;

  case 427: /* viarule_layer_option: K_DIRECTION K_VERTICAL ';'  */
#line 3533 "lef.y"
    { 
      if (lefData->viaRuleHasEnc) {
        if (lefCallbacks->ViaRuleCbk) { // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1706, "An ENCLOSRE statement was already defined in the layer.\nIt is DIRECTION or ENCLOSURE can be specified in a layer.");
              CHKERR();
           }
        }
      } else {
        if ((lefData->versionNum < 5.6) || (!lefData->isGenerate)) {
          if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setVertical();
        } else
          if (lefCallbacks->ViaRuleCbk) // write warning only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings)
              lefWarning(2022, "DIRECTION statement in VIARULE is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      }
      lefData->viaRuleHasDir = 1;
    }
#line 8354 "lef.tab.c"
    break;

  case 428: /* viarule_layer_option: K_ENCLOSURE int_number int_number ';'  */
#line 3552 "lef.y"
    {
      if (lefData->versionNum < 5.5) {
         if (lefCallbacks->ViaRuleCbk) { // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                "ENCLOSURE statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1707, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
         }
      }
      // 2/19/2004 - Enforced the rule that ENCLOSURE can only be defined
      // in VIARULE GENERATE
      if (!lefData->isGenerate) {
         if (lefCallbacks->ViaRuleCbk) { // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1614, "An ENCLOSURE statement is defined in a VIARULE statement only.\nOVERHANG statement can only be defined in VIARULE GENERATE.");
              CHKERR();
           }
         }
      }
      if (lefData->viaRuleHasDir) {
         if (lefCallbacks->ViaRuleCbk) { // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefError(1609, "A DIRECTION statement was already defined in the layer.\nIt is DIRECTION or ENCLOSURE can be specified in a layer.");
              CHKERR();
           }
         }
      } else {
         if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setEnclosure((yyvsp[-2].dval), (yyvsp[-1].dval));
      }
      lefData->viaRuleHasEnc = 1;
    }
#line 8394 "lef.tab.c"
    break;

  case 429: /* viarule_layer_option: K_WIDTH int_number K_TO int_number ';'  */
#line 3588 "lef.y"
    { if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setWidth((yyvsp[-3].dval),(yyvsp[-1].dval)); }
#line 8400 "lef.tab.c"
    break;

  case 430: /* viarule_layer_option: K_RECT pt pt ';'  */
#line 3590 "lef.y"
    { if (lefCallbacks->ViaRuleCbk)
        lefData->lefrViaRule.setRect((yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y); }
#line 8407 "lef.tab.c"
    break;

  case 431: /* viarule_layer_option: K_SPACING int_number K_BY int_number ';'  */
#line 3593 "lef.y"
    { if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setSpacing((yyvsp[-3].dval),(yyvsp[-1].dval)); }
#line 8413 "lef.tab.c"
    break;

  case 432: /* viarule_layer_option: K_RESISTANCE int_number ';'  */
#line 3595 "lef.y"
    { if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setResistance((yyvsp[-1].dval)); }
#line 8419 "lef.tab.c"
    break;

  case 433: /* viarule_layer_option: K_OVERHANG int_number ';'  */
#line 3597 "lef.y"
    {
      if (!lefData->viaRuleHasDir) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
               lefError(1610, "An OVERHANG statement is defined, but the required DIRECTION statement is not yet defined.\nUpdate the LEF file to define the DIRECTION statement before the OVERHANG.");
               CHKERR();
            }
         }
      }
      // 2/19/2004 - Enforced the rule that OVERHANG can only be defined
      // in VIARULE GENERATE after 5.3
      if ((lefData->versionNum > 5.3) && (!lefData->isGenerate)) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
               lefError(1611, "An OVERHANG statement is defined in a VIARULE statement only.\nOVERHANG statement can only be defined in VIARULE GENERATE.");
               CHKERR();
            }
         }
      }
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setOverhang((yyvsp[-1].dval));
      } else {
        if (lefCallbacks->ViaRuleCbk)  // write warning only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings)
              lefWarning(2023, "OVERHANG statement will be translated into similar ENCLOSURE rule");
        // In 5.6 & later, set it to either ENCLOSURE overhang1 or overhang2
        if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setOverhangToEnclosure((yyvsp[-1].dval));
      }
    }
#line 8453 "lef.tab.c"
    break;

  case 434: /* viarule_layer_option: K_METALOVERHANG int_number ';'  */
#line 3627 "lef.y"
    {
      // 2/19/2004 - Enforced the rule that METALOVERHANG can only be defined
      // in VIARULE GENERATE
      if ((lefData->versionNum > 5.3) && (!lefData->isGenerate)) {
         if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
            if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
               lefError(1612, "An METALOVERHANG statement is defined in a VIARULE statement only.\nOVERHANG statement can only be defined in VIARULE GENERATE.");
               CHKERR();
            }
         }
      }
      if (lefData->versionNum < 5.6) {
        if (!lefData->viaRuleHasDir) {
           if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
             if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
                lefError(1613, "An METALOVERHANG statement is defined, but the required DIRECTION statement is not yet defined.\nUpdate the LEF file to define the DIRECTION statement before the OVERHANG.");
                CHKERR();
             } 
           }
        }
        if (lefCallbacks->ViaRuleCbk) lefData->lefrViaRule.setMetalOverhang((yyvsp[-1].dval));
      } else
        if (lefCallbacks->ViaRuleCbk)  // write warning only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings)
             lefWarning(2024, "METALOVERHANG statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 8484 "lef.tab.c"
    break;

  case 435: /* $@62: %empty  */
#line 3654 "lef.y"
                   {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8490 "lef.tab.c"
    break;

  case 436: /* end_viarule: K_END $@62 T_STRING  */
#line 3655 "lef.y"
    {
      if ((lefData->isGenerate) && (lefCallbacks->ViaRuleCbk) && lefData->lefrViaRule.numLayers() >= 3) {         
        if (!lefData->lefrViaRule.layer(0)->hasRect() &&
            !lefData->lefrViaRule.layer(1)->hasRect() &&
            !lefData->lefrViaRule.layer(2)->hasRect()) {
            lefData->outMsg = (char*)lefMalloc(10000);
            sprintf (lefData->outMsg, 
                     "VIARULE GENERATE '%s' cut layer definition should have RECT statement.\nCorrect the LEF file before rerunning it through the LEF parser.", 
                      lefData->viaRuleName);
            lefWarning(1714, lefData->outMsg); 
            lefFree(lefData->outMsg);            
            CHKERR();                
        }
      }

      if (strcmp(lefData->viaRuleName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->ViaRuleCbk) {  // write error only if cbk is set 
           if (lefData->viaRuleWarnings++ < lefSettings->ViaRuleWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "END VIARULE name %s is different from the VIARULE name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->viaRuleName);
              lefError(1615, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->viaRuleName);
              CHKERR();
           } else
              lefFree(lefData->viaRuleName);
        } else
           lefFree(lefData->viaRuleName);
      } else
        lefFree(lefData->viaRuleName);
    }
#line 8527 "lef.tab.c"
    break;

  case 437: /* spacing_rule: start_spacing spacings end_spacing  */
#line 3689 "lef.y"
    { }
#line 8533 "lef.tab.c"
    break;

  case 438: /* start_spacing: K_SPACING  */
#line 3692 "lef.y"
    {
      lefData->hasSamenet = 0;
      if ((lefData->versionNum < 5.6) || (!lefData->ndRule)) {
        // if 5.6 and in nondefaultrule, it should not get in here, 
        // it should go to the else statement to write out a warning 
        // if 5.6, not in nondefaultrule, it will get in here 
        // if 5.5 and earlier in nondefaultrule is ok to get in here 
        if (lefData->versionNum >= 5.7) { // will get to this if statement if  
                           // lefData->versionNum is 5.6 and higher but lefData->ndRule = 0 
           if (lefData->spacingWarnings == 0) {  // only print once 
              lefWarning(2077, "A SPACING SAMENET section is defined but it is not legal in a LEF 5.7 version file.\nIt will be ignored which will probably cause real DRC violations to be ignored, and may\ncause false DRC violations to occur.\n\nTo avoid this warning, and correctly handle these DRC rules, you should modify your\nLEF to use the appropriate SAMENET keywords as described in the LEF/DEF 5.7\nmanual under the SPACING statements in the LAYER (Routing) and LAYER (Cut)\nsections listed in the LEF Table of Contents.");
              lefData->spacingWarnings++;
           }
        } else if (lefCallbacks->SpacingBeginCbk && !lefData->ndRule)
          CALLBACK(lefCallbacks->SpacingBeginCbk, lefrSpacingBeginCbkType, 0);
      } else
        if (lefCallbacks->SpacingBeginCbk && !lefData->ndRule)  // write warning only if cbk is set 
           if (lefData->spacingWarnings++ < lefSettings->SpacingWarnings)
             lefWarning(2025, "SAMENET statement in NONDEFAULTRULE is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 8558 "lef.tab.c"
    break;

  case 439: /* end_spacing: K_END K_SPACING  */
#line 3714 "lef.y"
    {
      if ((lefData->versionNum < 5.6) || (!lefData->ndRule)) {
        if ((lefData->versionNum <= 5.4) && (!lefData->hasSamenet)) {
           lefError(1616, "SAMENET statement is required inside SPACING for any lef file with version 5.4 and earlier, but is not defined in the parsed lef file.");
           CHKERR();
        } else if (lefData->versionNum < 5.7) { // obsolete in 5.7 and later 
           if (lefCallbacks->SpacingEndCbk && !lefData->ndRule)
             CALLBACK(lefCallbacks->SpacingEndCbk, lefrSpacingEndCbkType, 0);
        }
      }
    }
#line 8574 "lef.tab.c"
    break;

  case 442: /* spacing: samenet_keyword T_STRING T_STRING int_number ';'  */
#line 3732 "lef.y"
    {
      if ((lefData->versionNum < 5.6) || (!lefData->ndRule)) {
        if (lefData->versionNum < 5.7) {
          if (lefCallbacks->SpacingCbk) {
            lefData->lefrSpacing.set((yyvsp[-3].string), (yyvsp[-2].string), (yyvsp[-1].dval), 0);
            if (lefData->ndRule)
                lefData->nd->addSpacingRule(&lefData->lefrSpacing);
            else 
                CALLBACK(lefCallbacks->SpacingCbk, lefrSpacingCbkType, &lefData->lefrSpacing);            
          }
        }
      }
    }
#line 8592 "lef.tab.c"
    break;

  case 443: /* spacing: samenet_keyword T_STRING T_STRING int_number K_STACK ';'  */
#line 3746 "lef.y"
    {
      if ((lefData->versionNum < 5.6) || (!lefData->ndRule)) {
        if (lefData->versionNum < 5.7) {
          if (lefCallbacks->SpacingCbk) {
            lefData->lefrSpacing.set((yyvsp[-4].string), (yyvsp[-3].string), (yyvsp[-2].dval), 1);
            if (lefData->ndRule)
                lefData->nd->addSpacingRule(&lefData->lefrSpacing);
            else 
                CALLBACK(lefCallbacks->SpacingCbk, lefrSpacingCbkType, &lefData->lefrSpacing);    
          }
        }
      }
    }
#line 8610 "lef.tab.c"
    break;

  case 444: /* samenet_keyword: K_SAMENET  */
#line 3762 "lef.y"
    { lefData->lefDumbMode = 2; lefData->lefNoNum = 2; lefData->hasSamenet = 1; }
#line 8616 "lef.tab.c"
    break;

  case 445: /* maskColor: %empty  */
#line 3766 "lef.y"
    { (yyval.integer) = 0; }
#line 8622 "lef.tab.c"
    break;

  case 446: /* maskColor: K_MASK int_number  */
#line 3768 "lef.y"
    { (yyval.integer) = (int)(yyvsp[0].dval); }
#line 8628 "lef.tab.c"
    break;

  case 447: /* irdrop: start_irdrop ir_tables end_irdrop  */
#line 3771 "lef.y"
    { }
#line 8634 "lef.tab.c"
    break;

  case 448: /* start_irdrop: K_IRDROP  */
#line 3774 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->IRDropBeginCbk) 
          CALLBACK(lefCallbacks->IRDropBeginCbk, lefrIRDropBeginCbkType, 0);
      } else
        if (lefCallbacks->IRDropBeginCbk) // write warning only if cbk is set 
          if (lefData->iRDropWarnings++ < lefSettings->IRDropWarnings)
            lefWarning(2026, "IRDROP statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 8648 "lef.tab.c"
    break;

  case 449: /* end_irdrop: K_END K_IRDROP  */
#line 3785 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->IRDropEndCbk)
          CALLBACK(lefCallbacks->IRDropEndCbk, lefrIRDropEndCbkType, 0);
      }
    }
#line 8659 "lef.tab.c"
    break;

  case 452: /* ir_table: ir_tablename ir_table_values ';'  */
#line 3799 "lef.y"
    { 
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->IRDropCbk)
          CALLBACK(lefCallbacks->IRDropCbk, lefrIRDropCbkType, &lefData->lefrIRDrop);
      }
    }
#line 8670 "lef.tab.c"
    break;

  case 455: /* ir_table_value: int_number int_number  */
#line 3812 "lef.y"
  { if (lefCallbacks->IRDropCbk) lefData->lefrIRDrop.setValues((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 8676 "lef.tab.c"
    break;

  case 456: /* ir_tablename: K_TABLE T_STRING  */
#line 3815 "lef.y"
  { if (lefCallbacks->IRDropCbk) lefData->lefrIRDrop.setTableName((yyvsp[0].string)); }
#line 8682 "lef.tab.c"
    break;

  case 457: /* minfeature: K_MINFEATURE int_number int_number ';'  */
#line 3818 "lef.y"
  {
    lefData->hasMinfeature = 1;
    if (lefData->versionNum < 5.4) {
       if (lefCallbacks->MinFeatureCbk) {
         lefData->lefrMinFeature.set((yyvsp[-2].dval), (yyvsp[-1].dval));
         CALLBACK(lefCallbacks->MinFeatureCbk, lefrMinFeatureCbkType, &lefData->lefrMinFeature);
       }
    } else
       if (lefCallbacks->MinFeatureCbk) // write warning only if cbk is set 
          if (lefData->minFeatureWarnings++ < lefSettings->MinFeatureWarnings)
            lefWarning(2027, "MINFEATURE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
  }
#line 8699 "lef.tab.c"
    break;

  case 458: /* dielectric: K_DIELECTRIC int_number ';'  */
#line 3832 "lef.y"
  {
    if (lefData->versionNum < 5.4) {
       if (lefCallbacks->DielectricCbk)
         CALLBACK(lefCallbacks->DielectricCbk, lefrDielectricCbkType, (yyvsp[-1].dval));
    } else
       if (lefCallbacks->DielectricCbk) // write warning only if cbk is set 
         if (lefData->dielectricWarnings++ < lefSettings->DielectricWarnings)
           lefWarning(2028, "DIELECTRIC statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
  }
#line 8713 "lef.tab.c"
    break;

  case 459: /* $@63: %empty  */
#line 3842 "lef.y"
                                  {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8719 "lef.tab.c"
    break;

  case 460: /* $@64: %empty  */
#line 3843 "lef.y"
  {
    (void)lefSetNonDefault((yyvsp[0].string));
    if (lefCallbacks->NonDefaultCbk) lefData->lefrNonDefault.setName((yyvsp[0].string));
    lefData->ndLayer = 0;
    lefData->ndRule = 1;
    lefData->numVia = 0;
    //strcpy(lefData->nonDefaultRuleName, $3);
    lefData->nonDefaultRuleName = strdup((yyvsp[0].string));
  }
#line 8733 "lef.tab.c"
    break;

  case 461: /* $@65: %empty  */
#line 3853 "lef.y"
           {lefData->lefNdRule = 1;}
#line 8739 "lef.tab.c"
    break;

  case 462: /* nondefault_rule: K_NONDEFAULTRULE $@63 T_STRING $@64 nd_hardspacing nd_rules $@65 end_nd_rule  */
#line 3854 "lef.y"
  {
    // 10/18/2001 - Wanda da Rosa, PCR 404189
    //              At least 1 layer is required
    if ((!lefData->ndLayer) && (!lefSettings->RelaxMode)) {
       if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
         if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
            lefError(1617, "NONDEFAULTRULE statement requires at least one LAYER statement.");
            CHKERR();
         }
       }
    }
    if ((!lefData->numVia) && (!lefSettings->RelaxMode) && (lefData->versionNum < 5.6)) {
       // VIA is no longer a required statement in 5.6
       if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
         if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
            lefError(1618, "NONDEFAULTRULE statement requires at least one VIA statement.");
            CHKERR();
         }
       }
    }
    if (lefCallbacks->NonDefaultCbk) {
      lefData->lefrNonDefault.end();
      CALLBACK(lefCallbacks->NonDefaultCbk, lefrNonDefaultCbkType, &lefData->lefrNonDefault);
    }
    lefData->ndRule = 0;
    lefData->lefDumbMode = 0;
    (void)lefUnsetNonDefault();
  }
#line 8772 "lef.tab.c"
    break;

  case 463: /* end_nd_rule: K_END  */
#line 3884 "lef.y"
    {
      if ((lefData->nonDefaultRuleName) && (*lefData->nonDefaultRuleName != '\0'))
        lefFree(lefData->nonDefaultRuleName);
    }
#line 8781 "lef.tab.c"
    break;

  case 464: /* end_nd_rule: K_END T_STRING  */
#line 3889 "lef.y"
    {
      if (strcmp(lefData->nonDefaultRuleName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
          if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
             lefData->outMsg = (char*)lefMalloc(10000);
             sprintf (lefData->outMsg,
                "END NONDEFAULTRULE name %s is different from the NONDEFAULTRULE name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->nonDefaultRuleName);
             lefError(1619, lefData->outMsg);
             lefFree(lefData->nonDefaultRuleName);
             lefFree(lefData->outMsg);
             CHKERR();
          } else
             lefFree(lefData->nonDefaultRuleName);
        } else
           lefFree(lefData->nonDefaultRuleName);
      } else
        lefFree(lefData->nonDefaultRuleName);
    }
#line 8804 "lef.tab.c"
    break;

  case 466: /* nd_hardspacing: K_HARDSPACING ';'  */
#line 3912 "lef.y"
    {
       if (lefData->versionNum < 5.6) {
          if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "HARDSPACING statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1620, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
          }
       } else 
          if (lefCallbacks->NonDefaultCbk)
             lefData->lefrNonDefault.setHardspacing();
    }
#line 8825 "lef.tab.c"
    break;

  case 476: /* $@66: %empty  */
#line 3946 "lef.y"
    {
        lefData->lefDumbMode = 1;
    }
#line 8833 "lef.tab.c"
    break;

  case 477: /* usevia: K_USEVIA $@66 T_STRING ';'  */
#line 3950 "lef.y"
    {
       if (lefData->versionNum < 5.6) {
          if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
             lefData->outMsg = (char*)lefMalloc(10000);
             sprintf (lefData->outMsg,
               "USEVIA statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
             lefError(1621, lefData->outMsg);
             lefFree(lefData->outMsg);
             CHKERR();
          }
       } else {
          if (lefCallbacks->NonDefaultCbk)
             lefData->lefrNonDefault.addUseVia((yyvsp[-1].string));
       }
    }
#line 8853 "lef.tab.c"
    break;

  case 478: /* $@67: %empty  */
#line 3968 "lef.y"
    {
       lefData->lefDumbMode = 1;
    }
#line 8861 "lef.tab.c"
    break;

  case 479: /* useviarule: K_USEVIARULE $@67 T_STRING ';'  */
#line 3972 "lef.y"
    {
       if (lefData->versionNum < 5.6) {
          if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
             if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
                lefData->outMsg = (char*)lefMalloc(10000);
                sprintf (lefData->outMsg,
                  "USEVIARULE statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                lefError(1622, lefData->outMsg);
                lefFree(lefData->outMsg);
                CHKERR();
             }
          }
       } else {
          if (lefCallbacks->NonDefaultCbk)
             lefData->lefrNonDefault.addUseViaRule((yyvsp[-1].string));
       }
    }
#line 8883 "lef.tab.c"
    break;

  case 480: /* $@68: %empty  */
#line 3992 "lef.y"
    {
        lefData->lefDumbMode = 1;
    }
#line 8891 "lef.tab.c"
    break;

  case 481: /* mincuts: K_MINCUTS $@68 T_STRING int_number ';'  */
#line 3996 "lef.y"
    {
       if (lefData->versionNum < 5.6) {
          if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
             if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
                lefData->outMsg = (char*)lefMalloc(10000);
                sprintf (lefData->outMsg,
                  "MINCUTS statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                lefError(1623, lefData->outMsg);
                lefFree(lefData->outMsg);
                CHKERR();
             }
          }
       } else {
          if (lefCallbacks->NonDefaultCbk)
             lefData->lefrNonDefault.addMinCuts((yyvsp[-2].string), (int)(yyvsp[-1].dval));
       }
    }
#line 8913 "lef.tab.c"
    break;

  case 482: /* $@69: %empty  */
#line 4014 "lef.y"
                    { lefData->lefDumbMode = 10000000;}
#line 8919 "lef.tab.c"
    break;

  case 483: /* nd_prop: K_PROPERTY $@69 nd_prop_list ';'  */
#line 4015 "lef.y"
    { lefData->lefDumbMode = 0;
    }
#line 8926 "lef.tab.c"
    break;

  case 486: /* nd_prop: T_STRING T_STRING  */
#line 4025 "lef.y"
    {
      if (lefCallbacks->NonDefaultCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrNondefProp.propType((yyvsp[-1].string));
         lefData->lefrNonDefault.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 8938 "lef.tab.c"
    break;

  case 487: /* nd_prop: T_STRING QSTRING  */
#line 4033 "lef.y"
    {
      if (lefCallbacks->NonDefaultCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrNondefProp.propType((yyvsp[-1].string));
         lefData->lefrNonDefault.addProp((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 8950 "lef.tab.c"
    break;

  case 488: /* nd_prop: T_STRING NUMBER  */
#line 4041 "lef.y"
    {
      if (lefCallbacks->NonDefaultCbk) {
         char temp[32];
         char propTp;
         sprintf(temp, "%.11g", (yyvsp[0].dval));
         propTp = lefSettings->lefProps.lefrNondefProp.propType((yyvsp[-1].string));
         lefData->lefrNonDefault.addNumProp((yyvsp[-1].string), (yyvsp[0].dval), temp, propTp);
      }
    }
#line 8964 "lef.tab.c"
    break;

  case 489: /* $@70: %empty  */
#line 4051 "lef.y"
                  {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8970 "lef.tab.c"
    break;

  case 490: /* $@71: %empty  */
#line 4052 "lef.y"
  {
    if (lefCallbacks->NonDefaultCbk) lefData->lefrNonDefault.addLayer((yyvsp[0].string));
    lefData->ndLayer++;
    //strcpy(lefData->layerName, $3);
    lefData->layerName = strdup((yyvsp[0].string));
    lefData->ndLayerWidth = 0;
    lefData->ndLayerSpace = 0;
  }
#line 8983 "lef.tab.c"
    break;

  case 491: /* $@72: %empty  */
#line 4061 "lef.y"
  { 
    lefData->ndLayerWidth = 1;
    if (lefCallbacks->NonDefaultCbk) lefData->lefrNonDefault.addWidth((yyvsp[-1].dval));
  }
#line 8992 "lef.tab.c"
    break;

  case 492: /* $@73: %empty  */
#line 4065 "lef.y"
                       {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 8998 "lef.tab.c"
    break;

  case 493: /* nd_layer: K_LAYER $@70 T_STRING $@71 K_WIDTH int_number ';' $@72 nd_layer_stmts K_END $@73 T_STRING  */
#line 4066 "lef.y"
  {
    if (strcmp(lefData->layerName, (yyvsp[0].string)) != 0) {
      if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
         if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
            lefData->outMsg = (char*)lefMalloc(10000);
            sprintf (lefData->outMsg,
               "END LAYER name %s is different from the LAYER name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[-9].string), lefData->layerName);
            lefError(1624, lefData->outMsg);
            lefFree(lefData->outMsg);
            lefFree(lefData->layerName);
            CHKERR();
         } else
            lefFree(lefData->layerName);
      } else
         lefFree(lefData->layerName);
    } else
      lefFree(lefData->layerName);
    if (!lefData->ndLayerWidth) {
      if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
         if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
            lefError(1625, "A WIDTH statement is required in the LAYER statement in NONDEFULTRULE.");
            CHKERR();
         }
      }
    }
    if (!lefData->ndLayerSpace && lefData->versionNum < 5.6) {   // 5.6, SPACING is optional
      if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
         if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
            lefData->outMsg = (char*)lefMalloc(10000);
            sprintf (lefData->outMsg,
               "A SPACING statement is required in the LAYER statement in NONDEFAULTRULE for lef file with version 5.5 and earlier.\nYour lef file is defined with version %.2f. Update your lef to add a LAYER statement and try again.",
                lefData->versionNum);
            lefError(1626, lefData->outMsg);
            lefFree(lefData->outMsg);
            CHKERR();
         }
      }
    }
  }
#line 9042 "lef.tab.c"
    break;

  case 496: /* nd_layer_stmt: K_SPACING int_number ';'  */
#line 4114 "lef.y"
    {
      lefData->ndLayerSpace = 1;
      if (lefCallbacks->NonDefaultCbk) lefData->lefrNonDefault.addSpacing((yyvsp[-1].dval));
    }
#line 9051 "lef.tab.c"
    break;

  case 497: /* nd_layer_stmt: K_WIREEXTENSION int_number ';'  */
#line 4119 "lef.y"
    { if (lefCallbacks->NonDefaultCbk)
         lefData->lefrNonDefault.addWireExtension((yyvsp[-1].dval)); }
#line 9058 "lef.tab.c"
    break;

  case 498: /* nd_layer_stmt: K_RESISTANCE K_RPERSQ int_number ';'  */
#line 4122 "lef.y"
    {
      if (lefData->ignoreVersion) {
         if (lefCallbacks->NonDefaultCbk)
            lefData->lefrNonDefault.addResistance((yyvsp[-1].dval));
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "RESISTANCE RPERSQ statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1627, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->versionNum > 5.5) {  // obsolete in 5.6
         if (lefCallbacks->NonDefaultCbk) // write warning only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings)
              lefWarning(2029, "RESISTANCE RPERSQ statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      } else if (lefCallbacks->NonDefaultCbk)
         lefData->lefrNonDefault.addResistance((yyvsp[-1].dval));
    }
#line 9085 "lef.tab.c"
    break;

  case 499: /* nd_layer_stmt: K_CAPACITANCE K_CPERSQDIST int_number ';'  */
#line 4146 "lef.y"
    {
      if (lefData->ignoreVersion) {
         if (lefCallbacks->NonDefaultCbk)
            lefData->lefrNonDefault.addCapacitance((yyvsp[-1].dval));
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "CAPACITANCE CPERSQDIST statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1628, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
            }
         }
      } else if (lefData->versionNum > 5.5) { // obsolete in 5.6
         if (lefCallbacks->NonDefaultCbk) // write warning only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings)
              lefWarning(2030, "CAPACITANCE CPERSQDIST statement is obsolete in version 5.6. and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      } else if (lefCallbacks->NonDefaultCbk)
         lefData->lefrNonDefault.addCapacitance((yyvsp[-1].dval));
    }
#line 9112 "lef.tab.c"
    break;

  case 500: /* nd_layer_stmt: K_EDGECAPACITANCE int_number ';'  */
#line 4169 "lef.y"
    {
      if (lefData->ignoreVersion) {
         if (lefCallbacks->NonDefaultCbk)
            lefData->lefrNonDefault.addEdgeCap((yyvsp[-1].dval));
      } else if (lefData->versionNum < 5.4) {
         if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "EDGECAPACITANCE statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1629, lefData->outMsg);
               lefFree(lefData->outMsg);
              CHKERR();
            }
         }
      } else if (lefData->versionNum > 5.5) {  // obsolete in 5.6
         if (lefCallbacks->NonDefaultCbk) // write warning only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings)
              lefWarning(2031, "EDGECAPACITANCE statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
      } else if (lefCallbacks->NonDefaultCbk)
         lefData->lefrNonDefault.addEdgeCap((yyvsp[-1].dval));
    }
#line 9139 "lef.tab.c"
    break;

  case 501: /* nd_layer_stmt: K_DIAGWIDTH int_number ';'  */
#line 4192 "lef.y"
    {
      if (lefData->versionNum < 5.6) {  // 5.6 syntax
         if (lefCallbacks->NonDefaultCbk) { // write error only if cbk is set 
            if (lefData->nonDefaultWarnings++ < lefSettings->NonDefaultWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                 "DIAGWIDTH statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
               lefError(1630, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR(); 
            }
         }
      } else {
         if (lefCallbacks->NonDefaultCbk)
            lefData->lefrNonDefault.addDiagWidth((yyvsp[-1].dval));
      }
    }
#line 9161 "lef.tab.c"
    break;

  case 502: /* site: start_site site_options end_site  */
#line 4211 "lef.y"
    { 
      if (lefCallbacks->SiteCbk)
        CALLBACK(lefCallbacks->SiteCbk, lefrSiteCbkType, &lefData->lefrSite);
    }
#line 9170 "lef.tab.c"
    break;

  case 503: /* $@74: %empty  */
#line 4216 "lef.y"
                   {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9176 "lef.tab.c"
    break;

  case 504: /* start_site: K_SITE $@74 T_STRING  */
#line 4217 "lef.y"
    { 
      if (lefCallbacks->SiteCbk) lefData->lefrSite.setName((yyvsp[0].string));
      //strcpy(lefData->siteName, $3);
      lefData->siteName = strdup((yyvsp[0].string));
      lefData->hasSiteClass = 0;
      lefData->hasSiteSize = 0;
      lefData->hasSite = 1;
    }
#line 9189 "lef.tab.c"
    break;

  case 505: /* $@75: %empty  */
#line 4226 "lef.y"
                {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9195 "lef.tab.c"
    break;

  case 506: /* end_site: K_END $@75 T_STRING  */
#line 4227 "lef.y"
    {
      if (strcmp(lefData->siteName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->SiteCbk) { // write error only if cbk is set 
           if (lefData->siteWarnings++ < lefSettings->SiteWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "END SITE name %s is different from the SITE name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->siteName);
              lefError(1631, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->siteName);
              CHKERR();
           } else
              lefFree(lefData->siteName);
        } else
           lefFree(lefData->siteName);
      } else {
        lefFree(lefData->siteName);
        if (lefCallbacks->SiteCbk) { // write error only if cbk is set 
          if (lefData->hasSiteClass == 0) {
             lefError(1632, "A CLASS statement is required in the SITE statement.");
             CHKERR();
          }
          if (lefData->hasSiteSize == 0) {
             lefError(1633, "A SIZE  statement is required in the SITE statement.");
             CHKERR();
          }
        }
      }
    }
#line 9229 "lef.tab.c"
    break;

  case 509: /* site_option: K_SIZE int_number K_BY int_number ';'  */
#line 4264 "lef.y"
    {

      if (lefCallbacks->SiteCbk) lefData->lefrSite.setSize((yyvsp[-3].dval),(yyvsp[-1].dval));
      lefData->hasSiteSize = 1;
    }
#line 9239 "lef.tab.c"
    break;

  case 510: /* site_option: site_symmetry_statement  */
#line 4270 "lef.y"
    { }
#line 9245 "lef.tab.c"
    break;

  case 511: /* site_option: site_class  */
#line 4272 "lef.y"
    { 
      if (lefCallbacks->SiteCbk) lefData->lefrSite.setClass((yyvsp[0].string));
      lefData->hasSiteClass = 1;
    }
#line 9254 "lef.tab.c"
    break;

  case 512: /* site_option: site_rowpattern_statement  */
#line 4277 "lef.y"
    { }
#line 9260 "lef.tab.c"
    break;

  case 513: /* site_option: site_prop  */
#line 4279 "lef.y"
    { }
#line 9266 "lef.tab.c"
    break;

  case 514: /* site_prop: K_PROPERTY prop_name_value ';'  */
#line 4282 "lef.y"
  {
    if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("SITE ... PROPERTY propName propValue ';'")) {
            CHKERR();
        }
    } else if (lefCallbacks->SiteCbk) {
        lefData->setPropDataType((yyvsp[-1].prop), lefSettings->lefProps.lefrSiteProp);
        lefData->lefrSite.addProp((yyvsp[-1].prop));
        (yyvsp[-1].prop) = NULL;
    }

    delete (yyvsp[-1].prop);
  }
#line 9284 "lef.tab.c"
    break;

  case 515: /* site_class: K_CLASS K_PAD ';'  */
#line 4297 "lef.y"
                    {(yyval.string) = (char*)"PAD"; }
#line 9290 "lef.tab.c"
    break;

  case 516: /* site_class: K_CLASS K_CORE ';'  */
#line 4298 "lef.y"
                        {(yyval.string) = (char*)"CORE"; }
#line 9296 "lef.tab.c"
    break;

  case 517: /* site_class: K_CLASS K_VIRTUAL ';'  */
#line 4299 "lef.y"
                           {(yyval.string) = (char*)"VIRTUAL"; }
#line 9302 "lef.tab.c"
    break;

  case 518: /* site_symmetry_statement: K_SYMMETRY site_symmetries ';'  */
#line 4302 "lef.y"
    { }
#line 9308 "lef.tab.c"
    break;

  case 521: /* site_symmetry: K_X  */
#line 4311 "lef.y"
    { if (lefCallbacks->SiteCbk) lefData->lefrSite.setXSymmetry(); }
#line 9314 "lef.tab.c"
    break;

  case 522: /* site_symmetry: K_Y  */
#line 4313 "lef.y"
    { if (lefCallbacks->SiteCbk) lefData->lefrSite.setYSymmetry(); }
#line 9320 "lef.tab.c"
    break;

  case 523: /* site_symmetry: K_R90  */
#line 4315 "lef.y"
    { if (lefCallbacks->SiteCbk) lefData->lefrSite.set90Symmetry(); }
#line 9326 "lef.tab.c"
    break;

  case 524: /* $@76: %empty  */
#line 4317 "lef.y"
                                        {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9332 "lef.tab.c"
    break;

  case 525: /* site_rowpattern_statement: K_ROWPATTERN $@76 site_rowpatterns ';'  */
#line 4319 "lef.y"
    { }
#line 9338 "lef.tab.c"
    break;

  case 528: /* $@77: %empty  */
#line 4326 "lef.y"
                                      {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9344 "lef.tab.c"
    break;

  case 529: /* site_rowpattern: T_STRING orientation $@77  */
#line 4327 "lef.y"
    { if (lefCallbacks->SiteCbk) lefData->lefrSite.addRowPattern((yyvsp[-2].string), (yyvsp[-1].integer)); }
#line 9350 "lef.tab.c"
    break;

  case 530: /* pt: int_number int_number  */
#line 4331 "lef.y"
    { (yyval.pt).x = (yyvsp[-1].dval); (yyval.pt).y = (yyvsp[0].dval); }
#line 9356 "lef.tab.c"
    break;

  case 531: /* pt: '(' int_number int_number ')'  */
#line 4333 "lef.y"
    { (yyval.pt).x = (yyvsp[-2].dval); (yyval.pt).y = (yyvsp[-1].dval); }
#line 9362 "lef.tab.c"
    break;

  case 532: /* $@78: %empty  */
#line 4336 "lef.y"
    { 
      if (lefCallbacks->MacroCbk)
        CALLBACK(lefCallbacks->MacroCbk, lefrMacroCbkType, &lefData->lefrMacro);
      lefData->lefrDoSite = 0;
    }
#line 9372 "lef.tab.c"
    break;

  case 534: /* $@79: %empty  */
#line 4343 "lef.y"
                     {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9378 "lef.tab.c"
    break;

  case 535: /* start_macro: K_MACRO $@79 T_STRING  */
#line 4344 "lef.y"
    {
      lefData->siteDef = 0;
      lefData->symDef = 0;
      lefData->sizeDef = 0; 
      lefData->pinDef = 0; 
      lefData->obsDef = 0; 
      lefData->origDef = 0;
      lefData->lefrMacro.clear();      
      if (lefCallbacks->MacroBeginCbk || lefCallbacks->MacroCbk) {
        // some reader may not have MacroBeginCB, but has MacroCB set
        lefData->lefrMacro.setName((yyvsp[0].string));
        CALLBACK(lefCallbacks->MacroBeginCbk, lefrMacroBeginCbkType, (yyvsp[0].string));
      }
      //strcpy(lefData->macroName, $3);
      lefData->macroName = strdup((yyvsp[0].string));
    }
#line 9399 "lef.tab.c"
    break;

  case 536: /* $@80: %empty  */
#line 4361 "lef.y"
                 {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 9405 "lef.tab.c"
    break;

  case 537: /* end_macro: K_END $@80 T_STRING  */
#line 4362 "lef.y"
    {
      if (strcmp(lefData->macroName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->MacroEndCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "END MACRO name %s is different from the MACRO name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->macroName);
              lefError(1634, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->macroName);
              CHKERR();
           } else
              lefFree(lefData->macroName);
        } else
           lefFree(lefData->macroName);
      } else
        lefFree(lefData->macroName);
      if (lefCallbacks->MacroEndCbk)
        CALLBACK(lefCallbacks->MacroEndCbk, lefrMacroEndCbkType, (yyvsp[0].string));
    }
#line 9430 "lef.tab.c"
    break;

  case 546: /* macro_option: macro_fixedMask  */
#line 4396 "lef.y"
      { }
#line 9436 "lef.tab.c"
    break;

  case 547: /* macro_option: macro_origin  */
#line 4398 "lef.y"
      { }
#line 9442 "lef.tab.c"
    break;

  case 548: /* macro_option: macro_power  */
#line 4400 "lef.y"
      { }
#line 9448 "lef.tab.c"
    break;

  case 549: /* macro_option: macro_foreign  */
#line 4402 "lef.y"
      { }
#line 9454 "lef.tab.c"
    break;

  case 552: /* macro_option: macro_size  */
#line 4406 "lef.y"
      { }
#line 9460 "lef.tab.c"
    break;

  case 553: /* macro_option: macro_site  */
#line 4408 "lef.y"
      { }
#line 9466 "lef.tab.c"
    break;

  case 554: /* macro_option: macro_pin  */
#line 4410 "lef.y"
      { }
#line 9472 "lef.tab.c"
    break;

  case 555: /* macro_option: K_FUNCTION K_BUFFER ';'  */
#line 4412 "lef.y"
      { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setBuffer(); }
#line 9478 "lef.tab.c"
    break;

  case 556: /* macro_option: K_FUNCTION K_INVERTER ';'  */
#line 4414 "lef.y"
      { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setInverter(); }
#line 9484 "lef.tab.c"
    break;

  case 557: /* macro_option: macro_obs  */
#line 4416 "lef.y"
      { }
#line 9490 "lef.tab.c"
    break;

  case 558: /* macro_option: macro_density  */
#line 4418 "lef.y"
      { }
#line 9496 "lef.tab.c"
    break;

  case 559: /* macro_option: macro_clocktype  */
#line 4420 "lef.y"
      { }
#line 9502 "lef.tab.c"
    break;

  case 560: /* macro_option: timing  */
#line 4422 "lef.y"
      { }
#line 9508 "lef.tab.c"
    break;

  case 561: /* $@81: %empty  */
#line 4423 "lef.y"
               {lefData->lefDumbMode = 1000000; }
#line 9514 "lef.tab.c"
    break;

  case 562: /* macro_option: K_PROPERTY $@81 macro_prop_list ';'  */
#line 4424 "lef.y"
      { lefData->lefDumbMode = 0;
      }
#line 9521 "lef.tab.c"
    break;

  case 565: /* macro_symmetry_statement: K_SYMMETRY macro_symmetries ';'  */
#line 4433 "lef.y"
    {
      if (lefData->siteDef) { // SITE is defined before SYMMETRY 
          // pcr 283846 suppress warning 
          if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
               lefWarning(2032, "A SITE statement is defined before SYMMETRY statement.\nTo avoid this warning in the future, define SITE after SYMMETRY");
      }
      lefData->symDef = 1;
    }
#line 9535 "lef.tab.c"
    break;

  case 568: /* macro_symmetry: K_X  */
#line 4450 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setXSymmetry(); }
#line 9541 "lef.tab.c"
    break;

  case 569: /* macro_symmetry: K_Y  */
#line 4452 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setYSymmetry(); }
#line 9547 "lef.tab.c"
    break;

  case 570: /* macro_symmetry: K_R90  */
#line 4454 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.set90Symmetry(); }
#line 9553 "lef.tab.c"
    break;

  case 571: /* macro_name_value_pair: T_STRING NUMBER  */
#line 4458 "lef.y"
    {
      char temp[32];
      sprintf(temp, "%.11g", (yyvsp[0].dval));
      if (lefCallbacks->MacroCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrMacroProp.propType((yyvsp[-1].string));
         lefData->lefrMacro.setNumProperty((yyvsp[-1].string), (yyvsp[0].dval), temp,  propTp);
      }
    }
#line 9567 "lef.tab.c"
    break;

  case 572: /* macro_name_value_pair: T_STRING QSTRING  */
#line 4468 "lef.y"
    {
      if (lefCallbacks->MacroCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrMacroProp.propType((yyvsp[-1].string));
         lefData->lefrMacro.setProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 9579 "lef.tab.c"
    break;

  case 573: /* macro_name_value_pair: T_STRING T_STRING  */
#line 4476 "lef.y"
    {
      if (lefCallbacks->MacroCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrMacroProp.propType((yyvsp[-1].string));
         lefData->lefrMacro.setProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 9591 "lef.tab.c"
    break;

  case 574: /* macro_class: K_CLASS class_type ';'  */
#line 4485 "lef.y"
    {
       if (lefCallbacks->MacroCbk) lefData->lefrMacro.setClass((yyvsp[-1].string));
       if (lefCallbacks->MacroClassTypeCbk)
          CALLBACK(lefCallbacks->MacroClassTypeCbk, lefrMacroClassTypeCbkType, (yyvsp[-1].string));
    }
#line 9601 "lef.tab.c"
    break;

  case 575: /* class_type: K_COVER  */
#line 4492 "lef.y"
          {(yyval.string) = (char*)"COVER"; }
#line 9607 "lef.tab.c"
    break;

  case 576: /* class_type: K_COVER K_BUMP  */
#line 4494 "lef.y"
    { (yyval.string) = (char*)"COVER BUMP";
      if (lefData->versionNum < 5.5) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              if (lefSettings->RelaxMode)
                 lefWarning(2033, "The statement COVER BUMP is a LEF verion 5.5 syntax.\nYour LEF file is version 5.4 or earlier which is incorrect but will be allowed\nbecause this application does not enforce strict version checking.\nOther tools that enforce strict checking will have a syntax error when reading this file.\nYou can change the VERSION statement in this LEF file to 5.5 or higher to stop this warning.");
              else {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "COVER BUMP statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1635, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
      }
    }
#line 9630 "lef.tab.c"
    break;

  case 577: /* class_type: K_RING  */
#line 4512 "lef.y"
              {(yyval.string) = (char*)"RING"; }
#line 9636 "lef.tab.c"
    break;

  case 578: /* class_type: K_BLOCK  */
#line 4513 "lef.y"
              {(yyval.string) = (char*)"BLOCK"; }
#line 9642 "lef.tab.c"
    break;

  case 579: /* class_type: K_BLOCK K_BLACKBOX  */
#line 4515 "lef.y"
    { (yyval.string) = (char*)"BLOCK BLACKBOX";
      if (lefData->versionNum < 5.5) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
             if (lefSettings->RelaxMode)
                lefWarning(2034, "The statement BLOCK BLACKBOX is a LEF verion 5.5 syntax.\nYour LEF file is version 5.4 or earlier which is incorrect but will be allowed\nbecause this application does not enforce strict version checking.\nOther tools that enforce strict checking will have a syntax error when reading this file.\nYou can change the VERSION statement in this LEF file to 5.5 or higher to stop this warning.");
              else {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "BLOCK BLACKBOX statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1636, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
      }
    }
#line 9665 "lef.tab.c"
    break;

  case 580: /* class_type: K_BLOCK K_SOFT  */
#line 4534 "lef.y"
    {
      if (lefData->ignoreVersion) {
        (yyval.string) = (char*)"BLOCK SOFT";
      } else if (lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "BLOCK SOFT statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1637, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      else
        (yyval.string) = (char*)"BLOCK SOFT";
    }
#line 9688 "lef.tab.c"
    break;

  case 581: /* class_type: K_NONE  */
#line 4552 "lef.y"
              {(yyval.string) = (char*)"NONE"; }
#line 9694 "lef.tab.c"
    break;

  case 582: /* class_type: K_BUMP  */
#line 4554 "lef.y"
      {
        if (lefData->versionNum < 5.7) {
          lefData->outMsg = (char*)lefMalloc(10000);
          sprintf(lefData->outMsg,
            "BUMP is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
          lefError(1698, lefData->outMsg);
          lefFree(lefData->outMsg);
          CHKERR();
        }
       
        (yyval.string) = (char*)"BUMP";
     }
#line 9711 "lef.tab.c"
    break;

  case 583: /* class_type: K_PAD  */
#line 4566 "lef.y"
              {(yyval.string) = (char*)"PAD"; }
#line 9717 "lef.tab.c"
    break;

  case 584: /* class_type: K_VIRTUAL  */
#line 4567 "lef.y"
              {(yyval.string) = (char*)"VIRTUAL"; }
#line 9723 "lef.tab.c"
    break;

  case 585: /* class_type: K_PAD pad_type  */
#line 4569 "lef.y"
      {  sprintf(lefData->temp_name, "PAD %s", (yyvsp[0].string));
        (yyval.string) = lefData->temp_name; 
        if (lefData->versionNum < 5.5) {
           if (strcmp("AREAIO", (yyvsp[0].string)) != 0) {
             sprintf(lefData->temp_name, "PAD %s", (yyvsp[0].string));
             (yyval.string) = lefData->temp_name; 
           } else if (lefCallbacks->MacroCbk) { 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
               if (lefSettings->RelaxMode)
                  lefWarning(2035, "The statement PAD AREAIO is a LEF verion 5.5 syntax.\nYour LEF file is version 5.4 or earlier which is incorrect but will be allowed\nbecause this application does not enforce strict version checking.\nOther tools that enforce strict checking will have a syntax error when reading this file.\nYou can change the VERSION statement in this LEF file to 5.5 or higher to stop this warning.");
               else {
                  lefData->outMsg = (char*)lefMalloc(10000);
                  sprintf (lefData->outMsg,
                     "PAD AREAIO statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                  lefError(1638, lefData->outMsg);
                  lefFree(lefData->outMsg);
                  CHKERR();
               }
            }
          }
        }
      }
#line 9750 "lef.tab.c"
    break;

  case 586: /* class_type: K_CORE  */
#line 4591 "lef.y"
              {(yyval.string) = (char*)"CORE"; }
#line 9756 "lef.tab.c"
    break;

  case 587: /* class_type: K_CORNER  */
#line 4593 "lef.y"
      {(yyval.string) = (char*)"CORNER";
      // This token is NOT in the spec but has shown up in 
      // some lef files.  This exception came from LEFOUT
      // in 'frameworks'
      }
#line 9766 "lef.tab.c"
    break;

  case 588: /* class_type: K_CORE core_type  */
#line 4599 "lef.y"
      {sprintf(lefData->temp_name, "CORE %s", (yyvsp[0].string));
      (yyval.string) = lefData->temp_name;}
#line 9773 "lef.tab.c"
    break;

  case 589: /* class_type: K_ENDCAP endcap_type  */
#line 4602 "lef.y"
      {sprintf(lefData->temp_name, "ENDCAP %s", (yyvsp[0].string));
      (yyval.string) = lefData->temp_name;}
#line 9780 "lef.tab.c"
    break;

  case 590: /* pad_type: K_INPUT  */
#line 4606 "lef.y"
                  {(yyval.string) = (char*)"INPUT";}
#line 9786 "lef.tab.c"
    break;

  case 591: /* pad_type: K_OUTPUT  */
#line 4607 "lef.y"
                    {(yyval.string) = (char*)"OUTPUT";}
#line 9792 "lef.tab.c"
    break;

  case 592: /* pad_type: K_INOUT  */
#line 4608 "lef.y"
                    {(yyval.string) = (char*)"INOUT";}
#line 9798 "lef.tab.c"
    break;

  case 593: /* pad_type: K_POWER  */
#line 4609 "lef.y"
                    {(yyval.string) = (char*)"POWER";}
#line 9804 "lef.tab.c"
    break;

  case 594: /* pad_type: K_SPACER  */
#line 4610 "lef.y"
                    {(yyval.string) = (char*)"SPACER";}
#line 9810 "lef.tab.c"
    break;

  case 595: /* pad_type: K_AREAIO  */
#line 4611 "lef.y"
                {(yyval.string) = (char*)"AREAIO";}
#line 9816 "lef.tab.c"
    break;

  case 596: /* core_type: K_FEEDTHRU  */
#line 4614 "lef.y"
                    {(yyval.string) = (char*)"FEEDTHRU";}
#line 9822 "lef.tab.c"
    break;

  case 597: /* core_type: K_TIEHIGH  */
#line 4615 "lef.y"
                    {(yyval.string) = (char*)"TIEHIGH";}
#line 9828 "lef.tab.c"
    break;

  case 598: /* core_type: K_TIELOW  */
#line 4616 "lef.y"
                    {(yyval.string) = (char*)"TIELOW";}
#line 9834 "lef.tab.c"
    break;

  case 599: /* core_type: K_SPACER  */
#line 4618 "lef.y"
    { 
      (yyval.string) = (char*)"SPACER";

      if (!lefData->ignoreVersion && lefData->versionNum < 5.4) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "SPACER statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1639, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
    }
#line 9855 "lef.tab.c"
    break;

  case 600: /* core_type: K_ANTENNACELL  */
#line 4635 "lef.y"
    { 
      (yyval.string) = (char*)"ANTENNACELL";

      if (!lefData->ignoreVersion && lefData->versionNum < 5.4) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNACELL statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1640, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
    }
#line 9876 "lef.tab.c"
    break;

  case 601: /* core_type: K_WELLTAP  */
#line 4652 "lef.y"
    { 
      (yyval.string) = (char*)"WELLTAP";

      if (!lefData->ignoreVersion && lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "WELLTAP statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1641, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
    }
#line 9897 "lef.tab.c"
    break;

  case 602: /* endcap_type: K_PRE  */
#line 4670 "lef.y"
                {(yyval.string) = (char*)"PRE";}
#line 9903 "lef.tab.c"
    break;

  case 603: /* endcap_type: K_POST  */
#line 4671 "lef.y"
                   {(yyval.string) = (char*)"POST";}
#line 9909 "lef.tab.c"
    break;

  case 604: /* endcap_type: K_TOPLEFT  */
#line 4672 "lef.y"
                      {(yyval.string) = (char*)"TOPLEFT";}
#line 9915 "lef.tab.c"
    break;

  case 605: /* endcap_type: K_TOPRIGHT  */
#line 4673 "lef.y"
                       {(yyval.string) = (char*)"TOPRIGHT";}
#line 9921 "lef.tab.c"
    break;

  case 606: /* endcap_type: K_BOTTOMLEFT  */
#line 4674 "lef.y"
                         {(yyval.string) = (char*)"BOTTOMLEFT";}
#line 9927 "lef.tab.c"
    break;

  case 607: /* endcap_type: K_BOTTOMRIGHT  */
#line 4675 "lef.y"
                         {(yyval.string) = (char*)"BOTTOMRIGHT";}
#line 9933 "lef.tab.c"
    break;

  case 608: /* macro_obsspacing: K_OBSSPACING obsspacing_opt obsspaicing_layers ';'  */
#line 4679 "lef.y"
  {
      if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("MACRO ... OBSSPACING{FULLDRC|MIN|spacing}[LAYER layer]...;")) {
            CHKERR();
        }
      }
  }
#line 9945 "lef.tab.c"
    break;

  case 609: /* obsspacing_opt: K_FULLDRC  */
#line 4689 "lef.y"
    {
        if (lefCallbacks->MacroCbk) { 
            lefData->lefrMacro.addOBSSpacing("FULLDRC", 0); 
        }
    }
#line 9955 "lef.tab.c"
    break;

  case 610: /* obsspacing_opt: K_MIN  */
#line 4695 "lef.y"
    {
        if (lefCallbacks->MacroCbk) { 
            lefData->lefrMacro.addOBSSpacing("MIN", 0); 
        }
    }
#line 9965 "lef.tab.c"
    break;

  case 611: /* obsspacing_opt: NUMBER  */
#line 4701 "lef.y"
    {
        if (lefCallbacks->MacroCbk) { 
            lefData->lefrMacro.addOBSSpacing("", (yyvsp[0].dval)); 
        }
    }
#line 9975 "lef.tab.c"
    break;

  case 614: /* $@82: %empty  */
#line 4711 "lef.y"
                           {lefData->lefDumbMode = 1;}
#line 9981 "lef.tab.c"
    break;

  case 615: /* obsspaicing_layer: K_LAYER $@82 T_STRING  */
#line 4712 "lef.y"
    {
        lefData->lefrMacro.addOBSSpacingLayer((yyvsp[0].string));
    }
#line 9989 "lef.tab.c"
    break;

  case 616: /* macro_generator: K_GENERATOR T_STRING ';'  */
#line 4717 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setGenerator((yyvsp[-1].string)); }
#line 9995 "lef.tab.c"
    break;

  case 617: /* macro_generate: K_GENERATE T_STRING T_STRING ';'  */
#line 4720 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setGenerate((yyvsp[-2].string), (yyvsp[-1].string)); }
#line 10001 "lef.tab.c"
    break;

  case 618: /* macro_source: K_SOURCE K_USER ';'  */
#line 4724 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) lefData->lefrMacro.setSource("USER");
      } else
        if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
             lefWarning(2036, "SOURCE statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10014 "lef.tab.c"
    break;

  case 619: /* macro_source: K_SOURCE K_GENERATE ';'  */
#line 4733 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) lefData->lefrMacro.setSource("GENERATE");
      } else
        if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
             lefWarning(2037, "SOURCE statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10027 "lef.tab.c"
    break;

  case 620: /* macro_source: K_SOURCE K_BLOCK ';'  */
#line 4742 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) lefData->lefrMacro.setSource("BLOCK");
      } else
        if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
             lefWarning(2037, "SOURCE statement is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10040 "lef.tab.c"
    break;

  case 621: /* macro_power: K_POWER int_number ';'  */
#line 4752 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->MacroCbk) lefData->lefrMacro.setPower((yyvsp[-1].dval));
      } else
        if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
             lefWarning(2038, "MACRO POWER statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10053 "lef.tab.c"
    break;

  case 622: /* macro_origin: K_ORIGIN pt ';'  */
#line 4762 "lef.y"
    { 
       if (lefData->origDef) { // Has multiple ORIGIN defined in a macro, stop parsing
          if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                lefError(1642, "ORIGIN statement has defined more than once in a MACRO statement.\nOnly one ORIGIN statement can be defined in a Macro.\nParser will stop processing.");
               CHKERR();
             }
          }
       }
       lefData->origDef = 1;
       if (lefData->siteDef) { // SITE is defined before ORIGIN 
          // pcr 283846 suppress warning 
          if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
               lefWarning(2039, "A SITE statement is defined before ORIGIN statement.\nTo avoid this warning in the future, define SITE after ORIGIN");
       }
       if (lefData->pinDef) { // PIN is defined before ORIGIN 
          // pcr 283846 suppress warning 
          if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
               lefWarning(2040, "A PIN statement is defined before ORIGIN statement.\nTo avoid this warning in the future, define PIN after ORIGIN");
       }
       if (lefData->obsDef) { // OBS is defined before ORIGIN 
          // pcr 283846 suppress warning 
          if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
             if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
               lefWarning(2041, "A OBS statement is defined before ORIGIN statement.\nTo avoid this warning in the future, define OBS after ORIGIN");
       }
      
       // Workaround for pcr 640902 
       if (lefCallbacks->MacroCbk) lefData->lefrMacro.setOrigin((yyvsp[-1].pt).x, (yyvsp[-1].pt).y);
       if (lefCallbacks->MacroOriginCbk) {
          lefData->macroNum.x = (yyvsp[-1].pt).x; 
          lefData->macroNum.y = (yyvsp[-1].pt).y; 
          CALLBACK(lefCallbacks->MacroOriginCbk, lefrMacroOriginCbkType, lefData->macroNum);
       }
    }
#line 10095 "lef.tab.c"
    break;

  case 623: /* macro_foreign: start_foreign ';'  */
#line 4802 "lef.y"
    { 
      if (lefCallbacks->MacroCbk) {
        lefData->lefrMacro.addForeign((yyvsp[-1].string), 0, 0.0, 0.0, -1);
      }
      
      if (lefCallbacks->MacroForeignCbk) {
        lefiMacroForeign foreign((yyvsp[-1].string), 0, 0.0, 0.0, 0, 0);
        CALLBACK(lefCallbacks->MacroForeignCbk, lefrMacroForeignCbkType, &foreign);
      }  
    }
#line 10110 "lef.tab.c"
    break;

  case 624: /* macro_foreign: start_foreign pt ';'  */
#line 4813 "lef.y"
    { 
      if (lefCallbacks->MacroCbk) {
        lefData->lefrMacro.addForeign((yyvsp[-2].string), 1, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, -1);
      }
      
      if (lefCallbacks->MacroForeignCbk) {
        lefiMacroForeign foreign((yyvsp[-2].string), 1, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, 0, 0);
        CALLBACK(lefCallbacks->MacroForeignCbk, lefrMacroForeignCbkType, &foreign);
      }  
    }
#line 10125 "lef.tab.c"
    break;

  case 625: /* macro_foreign: start_foreign pt orientation ';'  */
#line 4824 "lef.y"
    { 
      if (lefCallbacks->MacroCbk) {
        lefData->lefrMacro.addForeign((yyvsp[-3].string), 1, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].integer));
      }
      
      if (lefCallbacks->MacroForeignCbk) {
        lefiMacroForeign foreign((yyvsp[-3].string), 1, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, 1, (yyvsp[-1].integer));
        CALLBACK(lefCallbacks->MacroForeignCbk, lefrMacroForeignCbkType, &foreign);
      } 
    }
#line 10140 "lef.tab.c"
    break;

  case 626: /* macro_foreign: start_foreign orientation ';'  */
#line 4835 "lef.y"
    { 
      if (lefCallbacks->MacroCbk) {
        lefData->lefrMacro.addForeign((yyvsp[-2].string), 0, 0.0, 0.0, (yyvsp[-1].integer));
      }

      if (lefCallbacks->MacroForeignCbk) {
        lefiMacroForeign foreign((yyvsp[-2].string), 0, 0.0, 0.0, 1, (yyvsp[-1].integer));
        CALLBACK(lefCallbacks->MacroForeignCbk, lefrMacroForeignCbkType, &foreign);
      } 
    }
#line 10155 "lef.tab.c"
    break;

  case 627: /* macro_fixedMask: K_FIXEDMASK ';'  */
#line 4848 "lef.y"
   {   
       if (lefCallbacks->MacroCbk && lefData->versionNum >= 5.8) {
          lefData->lefrMacro.setFixedMask(1);
       }
       if (lefCallbacks->MacroFixedMaskCbk) {
          CALLBACK(lefCallbacks->MacroFixedMaskCbk, lefrMacroFixedMaskCbkType, 1);
       }        
    }
#line 10168 "lef.tab.c"
    break;

  case 628: /* $@83: %empty  */
#line 4857 "lef.y"
                 { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 10174 "lef.tab.c"
    break;

  case 629: /* macro_eeq: K_EEQ $@83 T_STRING ';'  */
#line 4858 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setEEQ((yyvsp[-1].string)); }
#line 10180 "lef.tab.c"
    break;

  case 630: /* $@84: %empty  */
#line 4860 "lef.y"
                 { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 10186 "lef.tab.c"
    break;

  case 631: /* macro_leq: K_LEQ $@84 T_STRING ';'  */
#line 4861 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->MacroCbk) lefData->lefrMacro.setLEQ((yyvsp[-1].string));
      } else
        if (lefCallbacks->MacroCbk) // write warning only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings)
             lefWarning(2042, "LEQ statement in MACRO is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10199 "lef.tab.c"
    break;

  case 632: /* macro_site: macro_site_word T_STRING ';'  */
#line 4872 "lef.y"
    {
      if (lefCallbacks->MacroCbk) {
        lefData->lefrMacro.setSiteName((yyvsp[-1].string));
      }

      if (lefCallbacks->MacroSiteCbk) {
        lefiMacroSite site((yyvsp[-1].string), 0);
        CALLBACK(lefCallbacks->MacroSiteCbk, lefrMacroSiteCbkType, &site);
      }
    }
#line 10214 "lef.tab.c"
    break;

  case 633: /* macro_site: macro_site_word sitePattern ';'  */
#line 4883 "lef.y"
    {
      if (lefCallbacks->MacroCbk) {
        // also set site name in the variable siteName_ in lefiMacro 
        // this, if user wants to use method lefData->siteName will get the name also 
        lefData->lefrMacro.setSitePattern(lefData->lefrSitePatternPtr);
      }

      if (lefCallbacks->MacroSiteCbk) {
        lefiMacroSite site(0, lefData->lefrSitePatternPtr);
        CALLBACK(lefCallbacks->MacroSiteCbk, lefrMacroSiteCbkType, &site);
      }
        
      lefData->lefrSitePatternPtr = 0;
    }
#line 10233 "lef.tab.c"
    break;

  case 634: /* macro_site_word: K_SITE  */
#line 4899 "lef.y"
    { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; lefData->siteDef = 1;
        if (lefCallbacks->MacroCbk) lefData->lefrDoSite = 1; }
#line 10240 "lef.tab.c"
    break;

  case 635: /* site_word: K_SITE  */
#line 4903 "lef.y"
    { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 10246 "lef.tab.c"
    break;

  case 636: /* macro_size: K_SIZE int_number K_BY int_number ';'  */
#line 4906 "lef.y"
    { 
      if (lefData->siteDef) { // SITE is defined before SIZE 
      }
      lefData->sizeDef = 1;
      if (lefCallbacks->MacroCbk) lefData->lefrMacro.setSize((yyvsp[-3].dval), (yyvsp[-1].dval));
      if (lefCallbacks->MacroSizeCbk) {
         lefData->macroNum.x = (yyvsp[-3].dval); 
         lefData->macroNum.y = (yyvsp[-1].dval); 
         CALLBACK(lefCallbacks->MacroSizeCbk, lefrMacroSizeCbkType, lefData->macroNum);
      }
    }
#line 10262 "lef.tab.c"
    break;

  case 637: /* macro_pin: start_macro_pin macro_pin_options end_macro_pin  */
#line 4922 "lef.y"
    { 
      if (lefCallbacks->PinCbk)
        CALLBACK(lefCallbacks->PinCbk, lefrPinCbkType, &lefData->lefrPin);
      lefData->lefrPin.clear();
    }
#line 10272 "lef.tab.c"
    break;

  case 638: /* $@85: %empty  */
#line 4928 "lef.y"
                       {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; lefData->pinDef = 1;}
#line 10278 "lef.tab.c"
    break;

  case 639: /* start_macro_pin: K_PIN $@85 T_STRING  */
#line 4929 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setName((yyvsp[0].string));
      //strcpy(lefData->pinName, $3);
      lefData->pinName = strdup((yyvsp[0].string));
    }
#line 10287 "lef.tab.c"
    break;

  case 640: /* $@86: %empty  */
#line 4934 "lef.y"
                     {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 10293 "lef.tab.c"
    break;

  case 641: /* end_macro_pin: K_END $@86 T_STRING  */
#line 4935 "lef.y"
    {
      if (strcmp(lefData->pinName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->MacroCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "END PIN name %s is different from the PIN name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->pinName);
              lefError(1643, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->pinName);
              CHKERR();
           } else
              lefFree(lefData->pinName);
        } else
           lefFree(lefData->pinName);
      } else
        lefFree(lefData->pinName);
    }
#line 10316 "lef.tab.c"
    break;

  case 642: /* macro_pin_options: %empty  */
#line 4956 "lef.y"
    { }
#line 10322 "lef.tab.c"
    break;

  case 643: /* macro_pin_options: macro_pin_options macro_pin_option  */
#line 4958 "lef.y"
    { }
#line 10328 "lef.tab.c"
    break;

  case 644: /* macro_pin_option: start_foreign ';'  */
#line 4962 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-1].string), 0, 0.0, 0.0, -1);
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10341 "lef.tab.c"
    break;

  case 645: /* macro_pin_option: start_foreign pt ';'  */
#line 4971 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-2].string), 1, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, -1);
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10354 "lef.tab.c"
    break;

  case 646: /* macro_pin_option: start_foreign pt orientation ';'  */
#line 4980 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-3].string), 1, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].integer));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10367 "lef.tab.c"
    break;

  case 647: /* macro_pin_option: start_foreign K_STRUCTURE ';'  */
#line 4989 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-2].string), 0, 0.0, 0.0, -1);
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10380 "lef.tab.c"
    break;

  case 648: /* macro_pin_option: start_foreign K_STRUCTURE pt ';'  */
#line 4998 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-3].string), 1, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, -1);
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10393 "lef.tab.c"
    break;

  case 649: /* macro_pin_option: start_foreign K_STRUCTURE pt orientation ';'  */
#line 5007 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.addForeign((yyvsp[-4].string), 1, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].integer));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2043, "FOREIGN statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
    }
#line 10406 "lef.tab.c"
    break;

  case 650: /* $@87: %empty  */
#line 5015 "lef.y"
          { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 10412 "lef.tab.c"
    break;

  case 651: /* macro_pin_option: K_LEQ $@87 T_STRING ';'  */
#line 5016 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setLEQ((yyvsp[-1].string));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2044, "LEQ statement in MACRO PIN is obsolete in version 5.6 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.6 or later.");
   }
#line 10425 "lef.tab.c"
    break;

  case 652: /* macro_pin_option: K_POWER int_number ';'  */
#line 5025 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setPower((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2045, "MACRO POWER statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10438 "lef.tab.c"
    break;

  case 653: /* macro_pin_option: electrical_direction  */
#line 5034 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setDirection((yyvsp[0].string)); }
#line 10444 "lef.tab.c"
    break;

  case 654: /* macro_pin_option: K_USE macro_pin_use ';'  */
#line 5036 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setUse((yyvsp[-1].string)); }
#line 10450 "lef.tab.c"
    break;

  case 655: /* macro_pin_option: K_SCANUSE macro_scan_use ';'  */
#line 5038 "lef.y"
    { }
#line 10456 "lef.tab.c"
    break;

  case 656: /* macro_pin_option: K_LEAKAGE int_number ';'  */
#line 5040 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setLeakage((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2046, "MACRO LEAKAGE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, r emove this statement from the LEF file with version 5.4 or later.");
    }
#line 10469 "lef.tab.c"
    break;

  case 657: /* macro_pin_option: K_RISETHRESH int_number ';'  */
#line 5049 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setRiseThresh((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2047, "MACRO RISETHRESH statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10482 "lef.tab.c"
    break;

  case 658: /* macro_pin_option: K_FALLTHRESH int_number ';'  */
#line 5058 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setFallThresh((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2048, "MACRO FALLTHRESH statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10495 "lef.tab.c"
    break;

  case 659: /* macro_pin_option: K_RISESATCUR int_number ';'  */
#line 5067 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setRiseSatcur((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2049, "MACRO RISESATCUR statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10508 "lef.tab.c"
    break;

  case 660: /* macro_pin_option: K_FALLSATCUR int_number ';'  */
#line 5076 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setFallSatcur((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2050, "MACRO FALLSATCUR statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10521 "lef.tab.c"
    break;

  case 661: /* macro_pin_option: K_VLO int_number ';'  */
#line 5085 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setVLO((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2051, "MACRO VLO statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10534 "lef.tab.c"
    break;

  case 662: /* macro_pin_option: K_VHI int_number ';'  */
#line 5094 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setVHI((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2052, "MACRO VHI statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10547 "lef.tab.c"
    break;

  case 663: /* macro_pin_option: K_TIEOFFR int_number ';'  */
#line 5103 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setTieoffr((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2053, "MACRO TIEOFFR statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10560 "lef.tab.c"
    break;

  case 664: /* macro_pin_option: K_SHAPE pin_shape ';'  */
#line 5112 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setShape((yyvsp[-1].string)); }
#line 10566 "lef.tab.c"
    break;

  case 665: /* $@88: %empty  */
#line 5113 "lef.y"
               {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 10572 "lef.tab.c"
    break;

  case 666: /* macro_pin_option: K_MUSTJOIN $@88 T_STRING ';'  */
#line 5114 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setMustjoin((yyvsp[-1].string)); }
#line 10578 "lef.tab.c"
    break;

  case 667: /* $@89: %empty  */
#line 5115 "lef.y"
                        {lefData->lefDumbMode = 1;}
#line 10584 "lef.tab.c"
    break;

  case 668: /* macro_pin_option: K_OUTPUTNOISEMARGIN $@89 int_number int_number ';'  */
#line 5116 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setOutMargin((yyvsp[-2].dval), (yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2054, "MACRO OUTPUTNOISEMARGIN statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10597 "lef.tab.c"
    break;

  case 669: /* $@90: %empty  */
#line 5124 "lef.y"
                       {lefData->lefDumbMode = 1;}
#line 10603 "lef.tab.c"
    break;

  case 670: /* macro_pin_option: K_OUTPUTRESISTANCE $@90 int_number int_number ';'  */
#line 5125 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setOutResistance((yyvsp[-2].dval), (yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2055, "MACRO OUTPUTRESISTANCE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10616 "lef.tab.c"
    break;

  case 671: /* $@91: %empty  */
#line 5133 "lef.y"
                       {lefData->lefDumbMode = 1;}
#line 10622 "lef.tab.c"
    break;

  case 672: /* macro_pin_option: K_INPUTNOISEMARGIN $@91 int_number int_number ';'  */
#line 5134 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setInMargin((yyvsp[-2].dval), (yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2056, "MACRO INPUTNOISEMARGIN statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10635 "lef.tab.c"
    break;

  case 673: /* macro_pin_option: K_CAPACITANCE int_number ';'  */
#line 5143 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setCapacitance((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2057, "MACRO CAPACITANCE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10648 "lef.tab.c"
    break;

  case 674: /* macro_pin_option: K_MAXDELAY int_number ';'  */
#line 5152 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setMaxdelay((yyvsp[-1].dval)); }
#line 10654 "lef.tab.c"
    break;

  case 675: /* macro_pin_option: K_MAXLOAD int_number ';'  */
#line 5154 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setMaxload((yyvsp[-1].dval)); }
#line 10660 "lef.tab.c"
    break;

  case 676: /* macro_pin_option: K_RESISTANCE int_number ';'  */
#line 5156 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setResistance((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2058, "MACRO RESISTANCE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10673 "lef.tab.c"
    break;

  case 677: /* macro_pin_option: K_PULLDOWNRES int_number ';'  */
#line 5165 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setPulldownres((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2059, "MACRO PULLDOWNRES statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10686 "lef.tab.c"
    break;

  case 678: /* macro_pin_option: K_CURRENTSOURCE K_ACTIVE ';'  */
#line 5174 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setCurrentSource("ACTIVE");
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2060, "MACRO CURRENTSOURCE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10699 "lef.tab.c"
    break;

  case 679: /* macro_pin_option: K_CURRENTSOURCE K_RESISTIVE ';'  */
#line 5183 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setCurrentSource("RESISTIVE");
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2061, "MACRO CURRENTSOURCE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10712 "lef.tab.c"
    break;

  case 680: /* macro_pin_option: K_RISEVOLTAGETHRESHOLD int_number ';'  */
#line 5192 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setRiseVoltage((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2062, "MACRO RISEVOLTAGETHRESHOLD statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10725 "lef.tab.c"
    break;

  case 681: /* macro_pin_option: K_FALLVOLTAGETHRESHOLD int_number ';'  */
#line 5201 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setFallVoltage((yyvsp[-1].dval));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2063, "MACRO FALLVOLTAGETHRESHOLD statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10738 "lef.tab.c"
    break;

  case 682: /* macro_pin_option: K_IV_TABLES T_STRING T_STRING ';'  */
#line 5210 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) lefData->lefrPin.setTables((yyvsp[-2].string), (yyvsp[-1].string));
      } else
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2064, "MACRO IV_TABLES statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 10751 "lef.tab.c"
    break;

  case 683: /* $@92: %empty  */
#line 5218 "lef.y"
                { lefData->lefDumbMode = 1;}
#line 10757 "lef.tab.c"
    break;

  case 684: /* macro_pin_option: K_TAPERRULE $@92 T_STRING ';'  */
#line 5219 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setTaperRule((yyvsp[-1].string)); }
#line 10763 "lef.tab.c"
    break;

  case 685: /* $@93: %empty  */
#line 5220 "lef.y"
               {lefData->lefDumbMode = 1000000; }
#line 10769 "lef.tab.c"
    break;

  case 686: /* macro_pin_option: K_PROPERTY $@93 pin_prop_list ';'  */
#line 5221 "lef.y"
    { lefData->lefDumbMode = 0;
    }
#line 10776 "lef.tab.c"
    break;

  case 687: /* macro_pin_option: start_macro_port macro_port_class_option geometries K_END  */
#line 5224 "lef.y"
    {
      lefData->lefDumbMode = 0;
      lefData->hasGeoLayer = 0;
      if (lefCallbacks->PinCbk) {
        lefData->lefrPin.addPort(lefData->lefrGeometriesPtr);
        lefData->lefrGeometriesPtr = 0;
        lefData->lefrDoGeometries = 0;
      }
      if ((lefData->needGeometry) && (lefData->needGeometry != 2))  // if the lefData->last LAYER in PORT
        if (lefCallbacks->PinCbk) // write warning only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings)
             lefWarning(2065, "Either PATH, RECT or POLYGON statement is a required in MACRO/PIN/PORT.");
    }
#line 10794 "lef.tab.c"
    break;

  case 688: /* macro_pin_option: start_macro_port K_END  */
#line 5238 "lef.y"
    {
      // Since in start_macro_port it has call the Init method, here
      // we need to call the Destroy method.
      // Still add a null pointer to set the number of port
      if (lefCallbacks->PinCbk) {
        lefData->lefrPin.addPort(lefData->lefrGeometriesPtr);
        lefData->lefrGeometriesPtr = 0;
        lefData->lefrDoGeometries = 0;
      }
      lefData->hasGeoLayer = 0;
    }
#line 10810 "lef.tab.c"
    break;

  case 689: /* macro_pin_option: K_ANTENNASIZE int_number opt_layer_name ';'  */
#line 5250 "lef.y"
    {  // a pre 5.4 syntax 
      lefData->use5_3 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum >= 5.4) {
        if (lefData->use5_4) {
           if (lefCallbacks->PinCbk) { // write error only if cbk is set 
             if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
                lefData->outMsg = (char*)lefMalloc(10000);
                sprintf (lefData->outMsg,
                   "ANTENNASIZE statement is a version 5.3 and earlier syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                lefError(1644, lefData->outMsg);
                lefFree(lefData->outMsg);
                CHKERR();
             }
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaSize((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10835 "lef.tab.c"
    break;

  case 690: /* macro_pin_option: K_ANTENNAMETALAREA NUMBER opt_layer_name ';'  */
#line 5271 "lef.y"
    {  // a pre 5.4 syntax 
      lefData->use5_3 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum >= 5.4) {
        if (lefData->use5_4) {
           if (lefCallbacks->PinCbk) { // write error only if cbk is set 
              if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAMETALAREA statement is a version 5.3 and earlier syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1645, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaMetalArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10860 "lef.tab.c"
    break;

  case 691: /* macro_pin_option: K_ANTENNAMETALLENGTH int_number opt_layer_name ';'  */
#line 5292 "lef.y"
    { // a pre 5.4 syntax  
      lefData->use5_3 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum >= 5.4) {
        if (lefData->use5_4) {
           if (lefCallbacks->PinCbk) { // write error only if cbk is set 
              if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAMETALLENGTH statement is a version 5.3 and earlier syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
                 lefError(1646, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaMetalLength((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10885 "lef.tab.c"
    break;

  case 692: /* macro_pin_option: K_RISESLEWLIMIT int_number ';'  */
#line 5313 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setRiseSlewLimit((yyvsp[-1].dval)); }
#line 10891 "lef.tab.c"
    break;

  case 693: /* macro_pin_option: K_FALLSLEWLIMIT int_number ';'  */
#line 5315 "lef.y"
    { if (lefCallbacks->PinCbk) lefData->lefrPin.setFallSlewLimit((yyvsp[-1].dval)); }
#line 10897 "lef.tab.c"
    break;

  case 694: /* macro_pin_option: K_ANTENNAPARTIALMETALAREA NUMBER opt_layer_name ';'  */
#line 5317 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALMETALAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1647, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALMETALAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1647, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaPartialMetalArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10931 "lef.tab.c"
    break;

  case 695: /* macro_pin_option: K_ANTENNAPARTIALMETALSIDEAREA NUMBER opt_layer_name ';'  */
#line 5347 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALMETALSIDEAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1648, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALMETALSIDEAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1648, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaPartialMetalSideArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10965 "lef.tab.c"
    break;

  case 696: /* macro_pin_option: K_ANTENNAPARTIALCUTAREA NUMBER opt_layer_name ';'  */
#line 5377 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALCUTAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1649, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAPARTIALCUTAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1649, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaPartialCutArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 10999 "lef.tab.c"
    break;

  case 697: /* macro_pin_option: K_ANTENNADIFFAREA NUMBER opt_layer_name ';'  */
#line 5407 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNADIFFAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1650, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNADIFFAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1650, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaDiffArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 11033 "lef.tab.c"
    break;

  case 698: /* macro_pin_option: K_ANTENNAGATEAREA NUMBER opt_layer_name ';'  */
#line 5437 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAGATEAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1651, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAGATEAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1651, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaGateArea((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 11067 "lef.tab.c"
    break;

  case 699: /* macro_pin_option: K_ANTENNAMAXAREACAR NUMBER req_layer_name ';'  */
#line 5467 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXAREACAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1652, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXAREACAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1652, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaMaxAreaCar((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 11101 "lef.tab.c"
    break;

  case 700: /* macro_pin_option: K_ANTENNAMAXSIDEAREACAR NUMBER req_layer_name ';'  */
#line 5497 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXSIDEAREACAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1653, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXSIDEAREACAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1653, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaMaxSideAreaCar((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 11135 "lef.tab.c"
    break;

  case 701: /* macro_pin_option: K_ANTENNAMAXCUTCAR NUMBER req_layer_name ';'  */
#line 5527 "lef.y"
    { // 5.4 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.4) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXCUTCAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1654, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMAXCUTCAR statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1654, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
      if (lefCallbacks->PinCbk) lefData->lefrPin.addAntennaMaxCutCar((yyvsp[-2].dval), (yyvsp[-1].string));
    }
#line 11169 "lef.tab.c"
    break;

  case 702: /* $@94: %empty  */
#line 5557 "lef.y"
    { // 5.5 syntax 
      lefData->use5_4 = 1;
      if (lefData->ignoreVersion) {
        // do nothing 
      } else if (lefData->versionNum < 5.5) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMODEL statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1655, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->use5_3) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ANTENNAMODEL statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1655, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      }
    }
#line 11202 "lef.tab.c"
    break;

  case 704: /* $@95: %empty  */
#line 5586 "lef.y"
              {lefData->lefDumbMode = 2; lefData->lefNoNum = 2; }
#line 11208 "lef.tab.c"
    break;

  case 705: /* macro_pin_option: K_NETEXPR $@95 QSTRING ';'  */
#line 5587 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "NETEXPR statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1656, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else if (lefData->versionNum >= 6.0 - 0.00001) {
        if (lefData->lef60ObsoltedError("MACRO ... PIN ... NETEXPR \"netExprPropName defaultNetName\" ;")) {
            CHKERR();
        }
      } else if (lefCallbacks->PinCbk) {
        lefData->lefrPin.setNetExpr((yyvsp[-1].string));
      }
    }
#line 11233 "lef.tab.c"
    break;

  case 706: /* $@96: %empty  */
#line 5607 "lef.y"
                        {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 11239 "lef.tab.c"
    break;

  case 707: /* macro_pin_option: K_SUPPLYSENSITIVITY $@96 T_STRING ';'  */
#line 5608 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "SUPPLYSENSITIVITY statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1657, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else
        if (lefCallbacks->PinCbk) lefData->lefrPin.setSupplySensitivity((yyvsp[-1].string));
    }
#line 11259 "lef.tab.c"
    break;

  case 708: /* $@97: %empty  */
#line 5623 "lef.y"
                        {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 11265 "lef.tab.c"
    break;

  case 709: /* macro_pin_option: K_GROUNDSENSITIVITY $@97 T_STRING ';'  */
#line 5624 "lef.y"
    {
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->PinCbk) { // write error only if cbk is set 
           if (lefData->pinWarnings++ < lefSettings->PinWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "GROUNDSENSITIVITY statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1658, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } else
        if (lefCallbacks->PinCbk) lefData->lefrPin.setGroundSensitivity((yyvsp[-1].string));
    }
#line 11285 "lef.tab.c"
    break;

  case 710: /* pin_layer_oxide: K_OXIDE1  */
#line 5642 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(1);
    }
#line 11294 "lef.tab.c"
    break;

  case 711: /* pin_layer_oxide: K_OXIDE2  */
#line 5647 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(2);
    }
#line 11303 "lef.tab.c"
    break;

  case 712: /* pin_layer_oxide: K_OXIDE3  */
#line 5652 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(3);
    }
#line 11312 "lef.tab.c"
    break;

  case 713: /* pin_layer_oxide: K_OXIDE4  */
#line 5657 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(4);
    }
#line 11321 "lef.tab.c"
    break;

  case 714: /* pin_layer_oxide: K_OXIDE5  */
#line 5662 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(5);
    }
#line 11330 "lef.tab.c"
    break;

  case 715: /* pin_layer_oxide: K_OXIDE6  */
#line 5667 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(6);
    }
#line 11339 "lef.tab.c"
    break;

  case 716: /* pin_layer_oxide: K_OXIDE7  */
#line 5672 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(7);
    }
#line 11348 "lef.tab.c"
    break;

  case 717: /* pin_layer_oxide: K_OXIDE8  */
#line 5677 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(8);
    }
#line 11357 "lef.tab.c"
    break;

  case 718: /* pin_layer_oxide: K_OXIDE9  */
#line 5682 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(9);
    }
#line 11366 "lef.tab.c"
    break;

  case 719: /* pin_layer_oxide: K_OXIDE10  */
#line 5687 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(10);
    }
#line 11375 "lef.tab.c"
    break;

  case 720: /* pin_layer_oxide: K_OXIDE11  */
#line 5692 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(11);
    }
#line 11384 "lef.tab.c"
    break;

  case 721: /* pin_layer_oxide: K_OXIDE12  */
#line 5697 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(12);
    }
#line 11393 "lef.tab.c"
    break;

  case 722: /* pin_layer_oxide: K_OXIDE13  */
#line 5702 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(13);
    }
#line 11402 "lef.tab.c"
    break;

  case 723: /* pin_layer_oxide: K_OXIDE14  */
#line 5707 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(14);
    }
#line 11411 "lef.tab.c"
    break;

  case 724: /* pin_layer_oxide: K_OXIDE15  */
#line 5712 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(15);
    }
#line 11420 "lef.tab.c"
    break;

  case 725: /* pin_layer_oxide: K_OXIDE16  */
#line 5717 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(16);
    }
#line 11429 "lef.tab.c"
    break;

  case 726: /* pin_layer_oxide: K_OXIDE17  */
#line 5722 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(17);
    }
#line 11438 "lef.tab.c"
    break;

  case 727: /* pin_layer_oxide: K_OXIDE18  */
#line 5727 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(18);
    }
#line 11447 "lef.tab.c"
    break;

  case 728: /* pin_layer_oxide: K_OXIDE19  */
#line 5732 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(19);
    }
#line 11456 "lef.tab.c"
    break;

  case 729: /* pin_layer_oxide: K_OXIDE20  */
#line 5737 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(20);
    }
#line 11465 "lef.tab.c"
    break;

  case 730: /* pin_layer_oxide: K_OXIDE21  */
#line 5742 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(21);
    }
#line 11474 "lef.tab.c"
    break;

  case 731: /* pin_layer_oxide: K_OXIDE22  */
#line 5747 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(22);
    }
#line 11483 "lef.tab.c"
    break;

  case 732: /* pin_layer_oxide: K_OXIDE23  */
#line 5752 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(23);
    }
#line 11492 "lef.tab.c"
    break;

  case 733: /* pin_layer_oxide: K_OXIDE24  */
#line 5757 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(24);
    }
#line 11501 "lef.tab.c"
    break;

  case 734: /* pin_layer_oxide: K_OXIDE25  */
#line 5762 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(25);
    }
#line 11510 "lef.tab.c"
    break;

  case 735: /* pin_layer_oxide: K_OXIDE26  */
#line 5767 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(26);
    }
#line 11519 "lef.tab.c"
    break;

  case 736: /* pin_layer_oxide: K_OXIDE27  */
#line 5772 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(27);
    }
#line 11528 "lef.tab.c"
    break;

  case 737: /* pin_layer_oxide: K_OXIDE28  */
#line 5777 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(28);
    }
#line 11537 "lef.tab.c"
    break;

  case 738: /* pin_layer_oxide: K_OXIDE29  */
#line 5782 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(29);
    }
#line 11546 "lef.tab.c"
    break;

  case 739: /* pin_layer_oxide: K_OXIDE30  */
#line 5787 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(30);
    }
#line 11555 "lef.tab.c"
    break;

  case 740: /* pin_layer_oxide: K_OXIDE31  */
#line 5792 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(31);
    }
#line 11564 "lef.tab.c"
    break;

  case 741: /* pin_layer_oxide: K_OXIDE32  */
#line 5797 "lef.y"
    {
    if (lefCallbacks->PinCbk)
       lefData->lefrPin.addAntennaModel(32);
    }
#line 11573 "lef.tab.c"
    break;

  case 744: /* pin_name_value_pair: T_STRING NUMBER  */
#line 5809 "lef.y"
    { 
      char temp[32];
      sprintf(temp, "%.11g", (yyvsp[0].dval));
      if (lefCallbacks->PinCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrPinProp.propType((yyvsp[-1].string));
         lefData->lefrPin.setNumProperty((yyvsp[-1].string), (yyvsp[0].dval), temp, propTp);
      }
    }
#line 11587 "lef.tab.c"
    break;

  case 745: /* pin_name_value_pair: T_STRING QSTRING  */
#line 5819 "lef.y"
    {
      if (lefCallbacks->PinCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrPinProp.propType((yyvsp[-1].string));
         lefData->lefrPin.setProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 11599 "lef.tab.c"
    break;

  case 746: /* pin_name_value_pair: T_STRING T_STRING  */
#line 5827 "lef.y"
    {
      if (lefCallbacks->PinCbk) {
         char propTp;
         propTp = lefSettings->lefProps.lefrPinProp.propType((yyvsp[-1].string));
         lefData->lefrPin.setProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
      }
    }
#line 11611 "lef.tab.c"
    break;

  case 747: /* electrical_direction: K_DIRECTION K_INPUT ';'  */
#line 5836 "lef.y"
                                       {(yyval.string) = (char*)"INPUT";}
#line 11617 "lef.tab.c"
    break;

  case 748: /* electrical_direction: K_DIRECTION K_OUTPUT ';'  */
#line 5837 "lef.y"
                                        {(yyval.string) = (char*)"OUTPUT";}
#line 11623 "lef.tab.c"
    break;

  case 749: /* electrical_direction: K_DIRECTION K_OUTPUT K_TRISTATE ';'  */
#line 5838 "lef.y"
                                        {(yyval.string) = (char*)"OUTPUT TRISTATE";}
#line 11629 "lef.tab.c"
    break;

  case 750: /* electrical_direction: K_DIRECTION K_INOUT ';'  */
#line 5839 "lef.y"
                                        {(yyval.string) = (char*)"INOUT";}
#line 11635 "lef.tab.c"
    break;

  case 751: /* electrical_direction: K_DIRECTION K_FEEDTHRU ';'  */
#line 5840 "lef.y"
                                        {(yyval.string) = (char*)"FEEDTHRU";}
#line 11641 "lef.tab.c"
    break;

  case 752: /* start_macro_port: K_PORT  */
#line 5843 "lef.y"
    {
      if (lefCallbacks->PinCbk) {
        lefData->lefrDoGeometries = 1;
        lefData->hasPRP = 0;
        lefData->lefrGeometriesPtr = (lefiGeometries*)lefMalloc( sizeof(lefiGeometries));
        lefData->lefrGeometriesPtr->Init();
      }
      lefData->needGeometry = 0;  // don't need rect/path/poly define yet
      lefData->hasGeoLayer = 0;   // make sure LAYER is set before geometry
    }
#line 11656 "lef.tab.c"
    break;

  case 754: /* macro_port_class_option: K_CLASS class_type ';'  */
#line 5856 "lef.y"
    { if (lefData->lefrDoGeometries)
        lefData->lefrGeometriesPtr->addClass((yyvsp[-1].string)); }
#line 11663 "lef.tab.c"
    break;

  case 755: /* macro_pin_use: K_SIGNAL  */
#line 5860 "lef.y"
                {(yyval.string) = (char*)"SIGNAL";}
#line 11669 "lef.tab.c"
    break;

  case 756: /* macro_pin_use: K_ANALOG  */
#line 5861 "lef.y"
                {(yyval.string) = (char*)"ANALOG";}
#line 11675 "lef.tab.c"
    break;

  case 757: /* macro_pin_use: K_POWER  */
#line 5862 "lef.y"
                {(yyval.string) = (char*)"POWER";}
#line 11681 "lef.tab.c"
    break;

  case 758: /* macro_pin_use: K_GROUND  */
#line 5863 "lef.y"
                {(yyval.string) = (char*)"GROUND";}
#line 11687 "lef.tab.c"
    break;

  case 759: /* macro_pin_use: K_CLOCK  */
#line 5864 "lef.y"
                {(yyval.string) = (char*)"CLOCK";}
#line 11693 "lef.tab.c"
    break;

  case 760: /* macro_pin_use: K_DATA  */
#line 5865 "lef.y"
                {(yyval.string) = (char*)"DATA";}
#line 11699 "lef.tab.c"
    break;

  case 761: /* macro_scan_use: K_INPUT  */
#line 5868 "lef.y"
          {(yyval.string) = (char*)"INPUT";}
#line 11705 "lef.tab.c"
    break;

  case 762: /* macro_scan_use: K_OUTPUT  */
#line 5869 "lef.y"
                {(yyval.string) = (char*)"OUTPUT";}
#line 11711 "lef.tab.c"
    break;

  case 763: /* macro_scan_use: K_START  */
#line 5870 "lef.y"
                {(yyval.string) = (char*)"START";}
#line 11717 "lef.tab.c"
    break;

  case 764: /* macro_scan_use: K_STOP  */
#line 5871 "lef.y"
                {(yyval.string) = (char*)"STOP";}
#line 11723 "lef.tab.c"
    break;

  case 765: /* pin_shape: %empty  */
#line 5874 "lef.y"
  {(yyval.string) = (char*)""; }
#line 11729 "lef.tab.c"
    break;

  case 766: /* pin_shape: K_ABUTMENT  */
#line 5875 "lef.y"
                {(yyval.string) = (char*)"ABUTMENT";}
#line 11735 "lef.tab.c"
    break;

  case 767: /* pin_shape: K_RING  */
#line 5876 "lef.y"
                {(yyval.string) = (char*)"RING";}
#line 11741 "lef.tab.c"
    break;

  case 768: /* pin_shape: K_FEEDTHRU  */
#line 5877 "lef.y"
                {(yyval.string) = (char*)"FEEDTHRU";}
#line 11747 "lef.tab.c"
    break;

  case 770: /* $@98: %empty  */
#line 5882 "lef.y"
          {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 11753 "lef.tab.c"
    break;

  case 771: /* $@99: %empty  */
#line 5883 "lef.y"
    {
      if ((lefData->needGeometry) && (lefData->needGeometry != 2)) // 1 LAYER follow after another
        if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
          // geometries is called by MACRO/OBS & MACRO/PIN/PORT 
          if (lefData->obsDef)
             lefWarning(2076, "Either PATH, RECT or POLYGON statement is a required in MACRO/OBS.");
          else
             lefWarning(2065, "Either PATH, RECT or POLYGON statement is a required in MACRO/PIN/PORT.");
        }
      if (lefData->lefrDoGeometries)
        lefData->lefrGeometriesPtr->addLayer((yyvsp[0].string));
      lefData->needGeometry = 1;    // within LAYER it requires either path, rect, poly
      lefData->hasGeoLayer = 1;
    }
#line 11772 "lef.tab.c"
    break;

  case 773: /* geometry: K_WIDTH int_number ';'  */
#line 5902 "lef.y"
    { 
      if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else
           lefData->lefrGeometriesPtr->addWidth((yyvsp[-1].dval)); 
      } 
    }
#line 11788 "lef.tab.c"
    break;

  case 774: /* geometry: K_PATH maskColor firstPt otherPts ';'  */
#line 5914 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-3].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
                lefData->lefrGeometriesPtr->addPath((int)(yyvsp[-3].integer));
           }
        }
      }
      lefData->hasPRP = 1;
      lefData->needGeometry = 2;
    }
#line 11813 "lef.tab.c"
    break;

  case 775: /* geometry: K_PATH maskColor K_ITERATE firstPt otherPts stepPattern ';'  */
#line 5935 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-5].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
              lefData->lefrGeometriesPtr->addPathIter((int)(yyvsp[-5].integer));
            }
         }
      } 
      lefData->hasPRP = 1;
      lefData->needGeometry = 2;
    }
#line 11838 "lef.tab.c"
    break;

  case 776: /* geometry: K_RECT maskColor pt pt ';'  */
#line 5956 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-3].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
              lefData->lefrGeometriesPtr->addRect((int)(yyvsp[-3].integer), (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].pt).x, (yyvsp[-1].pt).y);
           }
        }
      }
      lefData->needGeometry = 2;
    }
#line 11862 "lef.tab.c"
    break;

  case 777: /* geometry: K_RECT maskColor K_ITERATE pt pt stepPattern ';'  */
#line 5976 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-5].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
              lefData->lefrGeometriesPtr->addRectIter((int)(yyvsp[-5].integer), (yyvsp[-3].pt).x, (yyvsp[-3].pt).y, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y);
           }
        }
      }
      lefData->needGeometry = 2;
    }
#line 11886 "lef.tab.c"
    break;

  case 778: /* geometry: K_POLYGON maskColor firstPt nextPt nextPt otherPts ';'  */
#line 5996 "lef.y"
    {
      if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-5].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
              lefData->lefrGeometriesPtr->addPolygon((int)(yyvsp[-5].integer));
            }
           }
      }
      lefData->hasPRP = 1;
      lefData->needGeometry = 2;
    }
#line 11912 "lef.tab.c"
    break;

  case 779: /* geometry: K_POLYGON maskColor K_ITERATE firstPt nextPt nextPt otherPts stepPattern ';'  */
#line 6018 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (lefData->hasGeoLayer == 0) {   // LAYER statement is missing 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefError(1701, "A LAYER statement is missing in Geometry.\nLAYER is a required statement before any geometry can be defined.");
              CHKERR();
           }
        } else {
           if (lefData->versionNum < 5.8 && (int)(yyvsp[-7].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
           } else {
              lefData->lefrGeometriesPtr->addPolygonIter((int)(yyvsp[-7].integer));
           }
         }
      }
      lefData->hasPRP = 1;
      lefData->needGeometry = 2;
    }
#line 11937 "lef.tab.c"
    break;

  case 780: /* geometry: via_placement  */
#line 6039 "lef.y"
    { }
#line 11943 "lef.tab.c"
    break;

  case 784: /* layer_exceptpgnet: K_EXCEPTPGNET  */
#line 6046 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "EXCEPTPGNET is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1699, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
       if (lefData->lefrDoGeometries)
        lefData->lefrGeometriesPtr->addLayerExceptPgNet();
      }
    }
#line 11961 "lef.tab.c"
    break;

  case 786: /* layer_real_abstract_noroute_opt: real_abstract_noroute  */
#line 6062 "lef.y"
    {
        if (!lefData->obsDef) {
           lefData->outMsg = (char*)lefMalloc(10000);
           sprintf (lefData->outMsg,
                    "%s keyword are not allowed in MACRO PIN PORT statement.",
                    (yyvsp[0].string));
           lefError(2086, lefData->outMsg);
           lefFree(lefData->outMsg);
           CHKERR();
        } else if (lefData->versionNum < 6.0 - 0.00001) {
            if (lefData->lef60NewSyntaxError("MACRO ... OBS LAYER layerName ... REAL|ABSTRACT|NOROUTE")) {
                CHKERR();
            }
        } else {       
            if (lefData->lefrDoGeometries) {
                lefData->lefrGeometriesPtr->addObsType((yyvsp[0].string));
            }
        }
    }
#line 11985 "lef.tab.c"
    break;

  case 787: /* real_abstract_noroute: K_REAL  */
#line 6084 "lef.y"
    {
        (yyval.string) = (char*)"REAL";
    }
#line 11993 "lef.tab.c"
    break;

  case 788: /* real_abstract_noroute: K_ABSTRACT  */
#line 6088 "lef.y"
    {
        (yyval.string) = (char*)"ABSTRACT";
    }
#line 12001 "lef.tab.c"
    break;

  case 789: /* real_abstract_noroute: K_NOROUTE  */
#line 6092 "lef.y"
    {
        (yyval.string) = (char*)"NOROUTE";
    }
#line 12009 "lef.tab.c"
    break;

  case 791: /* opt_geometry_props: geometry_props  */
#line 6098 "lef.y"
    {}
#line 12015 "lef.tab.c"
    break;

  case 793: /* geometry_props: geometry_props geometry_prop  */
#line 6102 "lef.y"
    {
        if (lefData->versionNum < 6.0 - 0.00001) {
            if (lefData->lef60NewSyntaxError("MACRO ... LAYER layerName PROPERTY propName propType")) {
                CHKERR();
            }
        }
    }
#line 12027 "lef.tab.c"
    break;

  case 794: /* geometry_prop: K_PROPERTY prop_name_value  */
#line 6111 "lef.y"
    {
      if (lefData->lefrDoGeometries) {
        lefData->setPropDataType((yyvsp[0].prop), lefSettings->lefProps.lefrPortobsProp);
        lefData->lefrGeometriesPtr->addProp((yyvsp[0].prop));
        (yyvsp[0].prop) = NULL;
      }

      delete (yyvsp[0].prop);
    }
#line 12041 "lef.tab.c"
    break;

  case 796: /* opt_geometry_via_props: geometry_via_props  */
#line 6123 "lef.y"
  {}
#line 12047 "lef.tab.c"
    break;

  case 798: /* geometry_via_props: geometry_via_props geometry_via_prop  */
#line 6127 "lef.y"
  {}
#line 12053 "lef.tab.c"
    break;

  case 799: /* geometry_via_prop: K_PROPERTY prop_name_value  */
#line 6130 "lef.y"
  {
    if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("MACRO ... VIA PROPERTY propName propType")) {
            CHKERR();
        }
    } else {
        if (lefData->lefrDoGeometries) {
            lefData->setPropDataType((yyvsp[0].prop), lefSettings->lefProps.lefrPortobsProp);
            lefData->lefrGeometriesPtr->addViaProp((yyvsp[0].prop));  
            (yyvsp[0].prop) = NULL;
        }
    }

    delete (yyvsp[0].prop);
  }
#line 12073 "lef.tab.c"
    break;

  case 800: /* $@100: %empty  */
#line 6147 "lef.y"
        {
            lefData->lefDumbMode = 2; 
        }
#line 12081 "lef.tab.c"
    break;

  case 801: /* prop_name_value: $@100 prop_name_value_pair  */
#line 6151 "lef.y"
        {
            (yyval.prop) = (yyvsp[0].prop);
            lefData->lefDumbMode = 0; 
        }
#line 12090 "lef.tab.c"
    break;

  case 802: /* prop_name_value_pair: T_STRING prop_string_value  */
#line 6158 "lef.y"
    {
        lefiProp *prop = new lefiProp();
        prop->setPropType("", (yyvsp[-1].string));
        prop->setPropQString((yyvsp[0].string));
        (yyval.prop) = prop;
    }
#line 12101 "lef.tab.c"
    break;

  case 803: /* prop_name_value_pair: T_STRING NUMBER  */
#line 6165 "lef.y"
    {
        lefiProp *prop = new lefiProp();
        prop->setPropType("", (yyvsp[-1].string));
        char temp[32];
        sprintf(temp, "%.11g", (yyvsp[0].dval));
        prop->setPropQString(temp);
        prop->setNumber((yyvsp[0].dval));
        (yyval.prop) = prop;
    }
#line 12115 "lef.tab.c"
    break;

  case 804: /* prop_string_value: T_STRING  */
#line 6176 "lef.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 12123 "lef.tab.c"
    break;

  case 805: /* prop_string_value: QSTRING  */
#line 6180 "lef.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 12131 "lef.tab.c"
    break;

  case 807: /* layer_spacing: K_SPACING int_number  */
#line 6186 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (zeroOrGt((yyvsp[0].dval)))
           lefData->lefrGeometriesPtr->addLayerMinSpacing((yyvsp[0].dval));
        else {
           lefData->outMsg = (char*)lefMalloc(10000);
           sprintf (lefData->outMsg,
              "THE SPACING statement has the value %g in MACRO OBS.\nValue has to be 0 or greater.", (yyvsp[0].dval));
           lefError(1659, lefData->outMsg);
           lefFree(lefData->outMsg);
           CHKERR();
        }
      }
    }
#line 12149 "lef.tab.c"
    break;

  case 808: /* layer_spacing: K_DESIGNRULEWIDTH int_number  */
#line 6200 "lef.y"
    { if (lefData->lefrDoGeometries) {
        if (zeroOrGt((yyvsp[0].dval)))
           lefData->lefrGeometriesPtr->addLayerRuleWidth((yyvsp[0].dval));
        else {
           lefData->outMsg = (char*)lefMalloc(10000);
           sprintf (lefData->outMsg,
              "THE DESIGNRULEWIDTH statement has the value %g in MACRO OBS.\nValue has to be 0 or greater.", (yyvsp[0].dval));
           lefError(1660, lefData->outMsg);
           lefFree(lefData->outMsg);
           CHKERR();
        }
      }
    }
#line 12167 "lef.tab.c"
    break;

  case 809: /* firstPt: pt  */
#line 6215 "lef.y"
    { if (lefData->lefrDoGeometries)
        lefData->lefrGeometriesPtr->startList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 12174 "lef.tab.c"
    break;

  case 810: /* nextPt: pt  */
#line 6219 "lef.y"
    { if (lefData->lefrDoGeometries)
        lefData->lefrGeometriesPtr->addToList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 12181 "lef.tab.c"
    break;

  case 813: /* $@101: %empty  */
#line 6228 "lef.y"
                                            {lefData->lefDumbMode = 1;}
#line 12187 "lef.tab.c"
    break;

  case 814: /* via_placement: K_VIA opt_geometry_via_props maskColor pt $@101 T_STRING ';'  */
#line 6229 "lef.y"
    { 
        if (lefData->lefrDoGeometries){
            if (lefData->versionNum < 5.8 && (int)(yyvsp[-4].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
            } else {
                lefData->lefrGeometriesPtr->addVia((int)(yyvsp[-4].integer), (yyvsp[-3].pt).x, (yyvsp[-3].pt).y, (yyvsp[-1].string));
            }
        }
    }
#line 12204 "lef.tab.c"
    break;

  case 815: /* $@102: %empty  */
#line 6241 "lef.y"
                                 {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 12210 "lef.tab.c"
    break;

  case 816: /* via_placement: K_VIA K_ITERATE maskColor pt $@102 T_STRING stepPattern ';'  */
#line 6243 "lef.y"
    { 
        if (lefData->lefrDoGeometries) {
            if (lefData->versionNum < 5.8 && (int)(yyvsp[-5].integer) > 0) {
              if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
                 lefError(2083, "Color mask information can only be defined with version 5.8.");
                 CHKERR(); 
              }           
            } else {
              lefData->lefrGeometriesPtr->addViaIter((int)(yyvsp[-5].integer), (yyvsp[-4].pt).x, (yyvsp[-4].pt).y, (yyvsp[-2].string)); 
            }
        }
    }
#line 12227 "lef.tab.c"
    break;

  case 817: /* stepPattern: K_DO int_number K_BY int_number K_STEP int_number int_number  */
#line 6257 "lef.y"
     { if (lefData->lefrDoGeometries)
         lefData->lefrGeometriesPtr->addStepPattern((yyvsp[-5].dval), (yyvsp[-3].dval), (yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 12234 "lef.tab.c"
    break;

  case 818: /* sitePattern: T_STRING int_number int_number orientation K_DO int_number K_BY int_number K_STEP int_number int_number  */
#line 6262 "lef.y"
    {
      if (lefData->lefrDoSite) {
        lefData->lefrSitePatternPtr = (lefiSitePattern*)lefMalloc(
                                   sizeof(lefiSitePattern));
        lefData->lefrSitePatternPtr->Init();
        lefData->lefrSitePatternPtr->set((yyvsp[-10].string), (yyvsp[-9].dval), (yyvsp[-8].dval), (yyvsp[-7].integer), (yyvsp[-5].dval), (yyvsp[-3].dval),
          (yyvsp[-1].dval), (yyvsp[0].dval));
        }
    }
#line 12248 "lef.tab.c"
    break;

  case 819: /* sitePattern: T_STRING int_number int_number orientation  */
#line 6272 "lef.y"
    {
      if (lefData->lefrDoSite) {
        lefData->lefrSitePatternPtr = (lefiSitePattern*)lefMalloc(
                                   sizeof(lefiSitePattern));
        lefData->lefrSitePatternPtr->Init();
        lefData->lefrSitePatternPtr->set((yyvsp[-3].string), (yyvsp[-2].dval), (yyvsp[-1].dval), (yyvsp[0].integer), -1, -1,
          -1, -1);
        }
    }
#line 12262 "lef.tab.c"
    break;

  case 820: /* $@103: %empty  */
#line 6284 "lef.y"
    { 
      if (lefData->lefrDoTrack) {
        lefData->lefrTrackPatternPtr = (lefiTrackPattern*)lefMalloc(
                                sizeof(lefiTrackPattern));
        lefData->lefrTrackPatternPtr->Init();
        lefData->lefrTrackPatternPtr->set("X", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12275 "lef.tab.c"
    break;

  case 821: /* $@104: %empty  */
#line 6292 "lef.y"
            {lefData->lefDumbMode = 1000000000;}
#line 12281 "lef.tab.c"
    break;

  case 822: /* trackPattern: K_X int_number K_DO int_number K_STEP int_number $@103 K_LAYER $@104 trackLayers  */
#line 6293 "lef.y"
    { lefData->lefDumbMode = 0;}
#line 12287 "lef.tab.c"
    break;

  case 823: /* $@105: %empty  */
#line 6295 "lef.y"
    { 
      if (lefData->lefrDoTrack) {
        lefData->lefrTrackPatternPtr = (lefiTrackPattern*)lefMalloc(
                                    sizeof(lefiTrackPattern));
        lefData->lefrTrackPatternPtr->Init();
        lefData->lefrTrackPatternPtr->set("Y", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12300 "lef.tab.c"
    break;

  case 824: /* $@106: %empty  */
#line 6303 "lef.y"
            {lefData->lefDumbMode = 1000000000;}
#line 12306 "lef.tab.c"
    break;

  case 825: /* trackPattern: K_Y int_number K_DO int_number K_STEP int_number $@105 K_LAYER $@106 trackLayers  */
#line 6304 "lef.y"
    { lefData->lefDumbMode = 0;}
#line 12312 "lef.tab.c"
    break;

  case 826: /* trackPattern: K_X int_number K_DO int_number K_STEP int_number  */
#line 6306 "lef.y"
    { 
      if (lefData->lefrDoTrack) {
        lefData->lefrTrackPatternPtr = (lefiTrackPattern*)lefMalloc(
                                    sizeof(lefiTrackPattern));
        lefData->lefrTrackPatternPtr->Init();
        lefData->lefrTrackPatternPtr->set("X", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12325 "lef.tab.c"
    break;

  case 827: /* trackPattern: K_Y int_number K_DO int_number K_STEP int_number  */
#line 6315 "lef.y"
    { 
      if (lefData->lefrDoTrack) {
        lefData->lefrTrackPatternPtr = (lefiTrackPattern*)lefMalloc(
                                    sizeof(lefiTrackPattern));
        lefData->lefrTrackPatternPtr->Init();
        lefData->lefrTrackPatternPtr->set("Y", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12338 "lef.tab.c"
    break;

  case 830: /* layer_name: T_STRING  */
#line 6330 "lef.y"
    { if (lefData->lefrDoTrack) lefData->lefrTrackPatternPtr->addLayer((yyvsp[0].string)); }
#line 12344 "lef.tab.c"
    break;

  case 831: /* gcellPattern: K_X int_number K_DO int_number K_STEP int_number  */
#line 6333 "lef.y"
    {
      if (lefData->lefrDoGcell) {
        lefData->lefrGcellPatternPtr = (lefiGcellPattern*)lefMalloc(
                                    sizeof(lefiGcellPattern));
        lefData->lefrGcellPatternPtr->Init();
        lefData->lefrGcellPatternPtr->set("X", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12357 "lef.tab.c"
    break;

  case 832: /* gcellPattern: K_Y int_number K_DO int_number K_STEP int_number  */
#line 6342 "lef.y"
    {
      if (lefData->lefrDoGcell) {
        lefData->lefrGcellPatternPtr = (lefiGcellPattern*)lefMalloc(
                                    sizeof(lefiGcellPattern));
        lefData->lefrGcellPatternPtr->Init();
        lefData->lefrGcellPatternPtr->set("Y", (yyvsp[-4].dval), (int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }    
    }
#line 12370 "lef.tab.c"
    break;

  case 833: /* macro_obs: start_macro_obs geometries K_END  */
#line 6352 "lef.y"
    { 
      if (lefCallbacks->ObstructionCbk) {
        lefData->lefrObstruction.setGeometries(lefData->lefrGeometriesPtr);
        lefData->lefrGeometriesPtr = 0;
        lefData->lefrDoGeometries = 0;
        CALLBACK(lefCallbacks->ObstructionCbk, lefrObstructionCbkType, &lefData->lefrObstruction);
      }
      lefData->lefDumbMode = 0;
      lefData->hasGeoLayer = 0;       // reset 
    }
#line 12385 "lef.tab.c"
    break;

  case 834: /* macro_obs: start_macro_obs K_END  */
#line 6363 "lef.y"
    {
       // The pointer has malloced in start, need to free manually 
       if (lefData->lefrGeometriesPtr) {
          lefData->lefrGeometriesPtr->Destroy();
          lefFree(lefData->lefrGeometriesPtr);
          lefData->lefrGeometriesPtr = 0;
          lefData->lefrDoGeometries = 0;
       }
       lefData->hasGeoLayer = 0;
    }
#line 12400 "lef.tab.c"
    break;

  case 835: /* start_macro_obs: K_OBS  */
#line 6375 "lef.y"
    {
      lefData->obsDef = 1;
      if (lefCallbacks->ObstructionCbk) {
        lefData->lefrDoGeometries = 1;
        lefData->lefrGeometriesPtr = (lefiGeometries*)lefMalloc(
            sizeof(lefiGeometries));
        lefData->lefrGeometriesPtr->Init();
        }
      lefData->hasGeoLayer = 0;
    }
#line 12415 "lef.tab.c"
    break;

  case 836: /* macro_density: K_DENSITY density_layer density_layers K_END  */
#line 6387 "lef.y"
    { 
      if (lefData->versionNum < 5.6) {
        if (lefCallbacks->DensityCbk) { // write error only if cbk is set 
           if (lefData->macroWarnings++ < lefSettings->MacroWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "DENSITY statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1661, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
      } 
      if (lefCallbacks->DensityCbk) {
        CALLBACK(lefCallbacks->DensityCbk, lefrDensityCbkType, &lefData->lefrDensity);
        lefData->lefrDensity.clear();
      }
      lefData->lefDumbMode = 0;
    }
#line 12439 "lef.tab.c"
    break;

  case 839: /* $@107: %empty  */
#line 6411 "lef.y"
                       { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 12445 "lef.tab.c"
    break;

  case 840: /* $@108: %empty  */
#line 6412 "lef.y"
    {
      if (lefCallbacks->DensityCbk)
        lefData->lefrDensity.addLayer((yyvsp[-1].string));
    }
#line 12454 "lef.tab.c"
    break;

  case 844: /* density_layer_rect: K_RECT pt pt int_number ';'  */
#line 6423 "lef.y"
    {
      if (lefCallbacks->DensityCbk)
        lefData->lefrDensity.addRect((yyvsp[-3].pt).x, (yyvsp[-3].pt).y, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y, (yyvsp[-1].dval)); 
    }
#line 12463 "lef.tab.c"
    break;

  case 845: /* $@109: %empty  */
#line 6428 "lef.y"
                             { lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 12469 "lef.tab.c"
    break;

  case 846: /* macro_clocktype: K_CLOCKTYPE $@109 T_STRING ';'  */
#line 6429 "lef.y"
    { if (lefCallbacks->MacroCbk) lefData->lefrMacro.setClockType((yyvsp[-1].string)); }
#line 12475 "lef.tab.c"
    break;

  case 847: /* timing: start_timing timing_options end_timing  */
#line 6432 "lef.y"
    { }
#line 12481 "lef.tab.c"
    break;

  case 848: /* start_timing: K_TIMING  */
#line 6435 "lef.y"
    { }
#line 12487 "lef.tab.c"
    break;

  case 849: /* end_timing: K_END K_TIMING  */
#line 6438 "lef.y"
  {
    if (lefData->versionNum < 5.4) {
      if (lefCallbacks->TimingCbk && lefData->lefrTiming.hasData())
        CALLBACK(lefCallbacks->TimingCbk, lefrTimingCbkType, &lefData->lefrTiming);
      lefData->lefrTiming.clear();
    } else {
      if (lefCallbacks->TimingCbk) // write warning only if cbk is set 
        if (lefData->timingWarnings++ < lefSettings->TimingWarnings)
          lefWarning(2066, "MACRO TIMING statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
      lefData->lefrTiming.clear();
    }
  }
#line 12504 "lef.tab.c"
    break;

  case 852: /* $@110: %empty  */
#line 6458 "lef.y"
    {
    if (lefData->versionNum < 5.4) {
      if (lefCallbacks->TimingCbk && lefData->lefrTiming.hasData())
        CALLBACK(lefCallbacks->TimingCbk, lefrTimingCbkType, &lefData->lefrTiming);
    }
    lefData->lefDumbMode = 1000000000;
    lefData->lefrTiming.clear();
    }
#line 12517 "lef.tab.c"
    break;

  case 853: /* timing_option: K_FROMPIN $@110 list_of_from_strings ';'  */
#line 6467 "lef.y"
    { lefData->lefDumbMode = 0;}
#line 12523 "lef.tab.c"
    break;

  case 854: /* $@111: %empty  */
#line 6468 "lef.y"
            {lefData->lefDumbMode = 1000000000;}
#line 12529 "lef.tab.c"
    break;

  case 855: /* timing_option: K_TOPIN $@111 list_of_to_strings ';'  */
#line 6469 "lef.y"
    { lefData->lefDumbMode = 0;}
#line 12535 "lef.tab.c"
    break;

  case 856: /* $@112: %empty  */
#line 6471 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addRiseFall((yyvsp[-3].string),(yyvsp[-1].dval),(yyvsp[0].dval)); }
#line 12541 "lef.tab.c"
    break;

  case 857: /* timing_option: risefall K_INTRINSIC int_number int_number $@112 slew_spec K_VARIABLE int_number int_number ';'  */
#line 6473 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addRiseFallVariable((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12547 "lef.tab.c"
    break;

  case 858: /* timing_option: risefall delay_or_transition K_UNATENESS unateness K_TABLEDIMENSION int_number int_number int_number ';'  */
#line 6476 "lef.y"
    { if (lefCallbacks->TimingCbk) {
        if ((yyvsp[-7].string)[0] == 'D' || (yyvsp[-7].string)[0] == 'd') // delay 
          lefData->lefrTiming.addDelay((yyvsp[-8].string), (yyvsp[-5].string), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval));
        else
          lefData->lefrTiming.addTransition((yyvsp[-8].string), (yyvsp[-5].string), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval));
      }
    }
#line 12559 "lef.tab.c"
    break;

  case 859: /* timing_option: K_TABLEAXIS list_of_table_axis_dnumbers ';'  */
#line 6484 "lef.y"
    { }
#line 12565 "lef.tab.c"
    break;

  case 860: /* timing_option: K_TABLEENTRIES list_of_table_entries ';'  */
#line 6486 "lef.y"
    { }
#line 12571 "lef.tab.c"
    break;

  case 861: /* timing_option: K_RISERS int_number int_number ';'  */
#line 6488 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setRiseRS((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12577 "lef.tab.c"
    break;

  case 862: /* timing_option: K_FALLRS int_number int_number ';'  */
#line 6490 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setFallRS((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12583 "lef.tab.c"
    break;

  case 863: /* timing_option: K_RISECS int_number int_number ';'  */
#line 6492 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setRiseCS((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12589 "lef.tab.c"
    break;

  case 864: /* timing_option: K_FALLCS int_number int_number ';'  */
#line 6494 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setFallCS((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12595 "lef.tab.c"
    break;

  case 865: /* timing_option: K_RISESATT1 int_number int_number ';'  */
#line 6496 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setRiseAtt1((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12601 "lef.tab.c"
    break;

  case 866: /* timing_option: K_FALLSATT1 int_number int_number ';'  */
#line 6498 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setFallAtt1((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12607 "lef.tab.c"
    break;

  case 867: /* timing_option: K_RISET0 int_number int_number ';'  */
#line 6500 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setRiseTo((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12613 "lef.tab.c"
    break;

  case 868: /* timing_option: K_FALLT0 int_number int_number ';'  */
#line 6502 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setFallTo((yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12619 "lef.tab.c"
    break;

  case 869: /* timing_option: K_UNATENESS unateness ';'  */
#line 6504 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addUnateness((yyvsp[-1].string)); }
#line 12625 "lef.tab.c"
    break;

  case 870: /* timing_option: K_STABLE K_SETUP int_number K_HOLD int_number risefall ';'  */
#line 6506 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setStable((yyvsp[-4].dval),(yyvsp[-2].dval),(yyvsp[-1].string)); }
#line 12631 "lef.tab.c"
    break;

  case 871: /* timing_option: two_pin_trigger from_pin_trigger to_pin_trigger K_TABLEDIMENSION int_number int_number int_number ';'  */
#line 6508 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addSDF2Pins((yyvsp[-7].string),(yyvsp[-6].string),(yyvsp[-5].string),(yyvsp[-3].dval),(yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12637 "lef.tab.c"
    break;

  case 872: /* timing_option: one_pin_trigger K_TABLEDIMENSION int_number int_number int_number ';'  */
#line 6510 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addSDF1Pin((yyvsp[-5].string),(yyvsp[-3].dval),(yyvsp[-2].dval),(yyvsp[-2].dval)); }
#line 12643 "lef.tab.c"
    break;

  case 873: /* timing_option: K_SDFCONDSTART QSTRING ';'  */
#line 6512 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setSDFcondStart((yyvsp[-1].string)); }
#line 12649 "lef.tab.c"
    break;

  case 874: /* timing_option: K_SDFCONDEND QSTRING ';'  */
#line 6514 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setSDFcondEnd((yyvsp[-1].string)); }
#line 12655 "lef.tab.c"
    break;

  case 875: /* timing_option: K_SDFCOND QSTRING ';'  */
#line 6516 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.setSDFcond((yyvsp[-1].string)); }
#line 12661 "lef.tab.c"
    break;

  case 876: /* timing_option: K_EXTENSION ';'  */
#line 6518 "lef.y"
    { }
#line 12667 "lef.tab.c"
    break;

  case 877: /* one_pin_trigger: K_MPWH  */
#line 6522 "lef.y"
    { (yyval.string) = (char*)"MPWH";}
#line 12673 "lef.tab.c"
    break;

  case 878: /* one_pin_trigger: K_MPWL  */
#line 6524 "lef.y"
    { (yyval.string) = (char*)"MPWL";}
#line 12679 "lef.tab.c"
    break;

  case 879: /* one_pin_trigger: K_PERIOD  */
#line 6526 "lef.y"
    { (yyval.string) = (char*)"PERIOD";}
#line 12685 "lef.tab.c"
    break;

  case 880: /* two_pin_trigger: K_SETUP  */
#line 6530 "lef.y"
    { (yyval.string) = (char*)"SETUP";}
#line 12691 "lef.tab.c"
    break;

  case 881: /* two_pin_trigger: K_HOLD  */
#line 6532 "lef.y"
    { (yyval.string) = (char*)"HOLD";}
#line 12697 "lef.tab.c"
    break;

  case 882: /* two_pin_trigger: K_RECOVERY  */
#line 6534 "lef.y"
    { (yyval.string) = (char*)"RECOVERY";}
#line 12703 "lef.tab.c"
    break;

  case 883: /* two_pin_trigger: K_SKEW  */
#line 6536 "lef.y"
    { (yyval.string) = (char*)"SKEW";}
#line 12709 "lef.tab.c"
    break;

  case 884: /* from_pin_trigger: K_ANYEDGE  */
#line 6540 "lef.y"
    { (yyval.string) = (char*)"ANYEDGE";}
#line 12715 "lef.tab.c"
    break;

  case 885: /* from_pin_trigger: K_POSEDGE  */
#line 6542 "lef.y"
    { (yyval.string) = (char*)"POSEDGE";}
#line 12721 "lef.tab.c"
    break;

  case 886: /* from_pin_trigger: K_NEGEDGE  */
#line 6544 "lef.y"
    { (yyval.string) = (char*)"NEGEDGE";}
#line 12727 "lef.tab.c"
    break;

  case 887: /* to_pin_trigger: K_ANYEDGE  */
#line 6548 "lef.y"
    { (yyval.string) = (char*)"ANYEDGE";}
#line 12733 "lef.tab.c"
    break;

  case 888: /* to_pin_trigger: K_POSEDGE  */
#line 6550 "lef.y"
    { (yyval.string) = (char*)"POSEDGE";}
#line 12739 "lef.tab.c"
    break;

  case 889: /* to_pin_trigger: K_NEGEDGE  */
#line 6552 "lef.y"
    { (yyval.string) = (char*)"NEGEDGE";}
#line 12745 "lef.tab.c"
    break;

  case 890: /* delay_or_transition: K_DELAY  */
#line 6556 "lef.y"
    { (yyval.string) = (char*)"DELAY"; }
#line 12751 "lef.tab.c"
    break;

  case 891: /* delay_or_transition: K_TRANSITIONTIME  */
#line 6558 "lef.y"
    { (yyval.string) = (char*)"TRANSITION"; }
#line 12757 "lef.tab.c"
    break;

  case 892: /* list_of_table_entries: table_entry  */
#line 6562 "lef.y"
    { }
#line 12763 "lef.tab.c"
    break;

  case 893: /* list_of_table_entries: list_of_table_entries table_entry  */
#line 6564 "lef.y"
    { }
#line 12769 "lef.tab.c"
    break;

  case 894: /* table_entry: '(' int_number int_number int_number ')'  */
#line 6567 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addTableEntry((yyvsp[-3].dval),(yyvsp[-2].dval),(yyvsp[-1].dval)); }
#line 12775 "lef.tab.c"
    break;

  case 895: /* list_of_table_axis_dnumbers: int_number  */
#line 6571 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addTableAxisNumber((yyvsp[0].dval)); }
#line 12781 "lef.tab.c"
    break;

  case 896: /* list_of_table_axis_dnumbers: list_of_table_axis_dnumbers int_number  */
#line 6573 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addTableAxisNumber((yyvsp[0].dval)); }
#line 12787 "lef.tab.c"
    break;

  case 897: /* slew_spec: %empty  */
#line 6577 "lef.y"
    { }
#line 12793 "lef.tab.c"
    break;

  case 898: /* slew_spec: int_number int_number int_number int_number  */
#line 6579 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addRiseFallSlew((yyvsp[-3].dval),(yyvsp[-2].dval),(yyvsp[-1].dval),(yyvsp[0].dval)); }
#line 12799 "lef.tab.c"
    break;

  case 899: /* slew_spec: int_number int_number int_number int_number int_number int_number int_number  */
#line 6581 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addRiseFallSlew((yyvsp[-6].dval),(yyvsp[-5].dval),(yyvsp[-4].dval),(yyvsp[-3].dval));
      if (lefCallbacks->TimingCbk) lefData->lefrTiming.addRiseFallSlew2((yyvsp[-2].dval),(yyvsp[-1].dval),(yyvsp[0].dval)); }
#line 12806 "lef.tab.c"
    break;

  case 900: /* risefall: K_RISE  */
#line 6586 "lef.y"
    { (yyval.string) = (char*)"RISE"; }
#line 12812 "lef.tab.c"
    break;

  case 901: /* risefall: K_FALL  */
#line 6588 "lef.y"
    { (yyval.string) = (char*)"FALL"; }
#line 12818 "lef.tab.c"
    break;

  case 902: /* unateness: K_INVERT  */
#line 6592 "lef.y"
    { (yyval.string) = (char*)"INVERT"; }
#line 12824 "lef.tab.c"
    break;

  case 903: /* unateness: K_NONINVERT  */
#line 6594 "lef.y"
    { (yyval.string) = (char*)"NONINVERT"; }
#line 12830 "lef.tab.c"
    break;

  case 904: /* unateness: K_NONUNATE  */
#line 6596 "lef.y"
    { (yyval.string) = (char*)"NONUNATE"; }
#line 12836 "lef.tab.c"
    break;

  case 905: /* list_of_from_strings: T_STRING  */
#line 6600 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addFromPin((yyvsp[0].string)); }
#line 12842 "lef.tab.c"
    break;

  case 906: /* list_of_from_strings: list_of_from_strings T_STRING  */
#line 6602 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addFromPin((yyvsp[0].string)); }
#line 12848 "lef.tab.c"
    break;

  case 907: /* list_of_to_strings: T_STRING  */
#line 6606 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addToPin((yyvsp[0].string)); }
#line 12854 "lef.tab.c"
    break;

  case 908: /* list_of_to_strings: list_of_to_strings T_STRING  */
#line 6608 "lef.y"
    { if (lefCallbacks->TimingCbk) lefData->lefrTiming.addToPin((yyvsp[0].string)); }
#line 12860 "lef.tab.c"
    break;

  case 909: /* $@113: %empty  */
#line 6611 "lef.y"
    {
      if (lefCallbacks->ArrayCbk)
        CALLBACK(lefCallbacks->ArrayCbk, lefrArrayCbkType, &lefData->lefrArray);
      lefData->lefrArray.clear();
      lefData->lefrSitePatternPtr = 0;
      lefData->lefrDoSite = 0;
   }
#line 12872 "lef.tab.c"
    break;

  case 911: /* $@114: %empty  */
#line 6620 "lef.y"
                     {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 12878 "lef.tab.c"
    break;

  case 912: /* start_array: K_ARRAY $@114 T_STRING  */
#line 6621 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.setName((yyvsp[0].string));
        CALLBACK(lefCallbacks->ArrayBeginCbk, lefrArrayBeginCbkType, (yyvsp[0].string));
      }
      //strcpy(lefData->arrayName, $3);
      lefData->arrayName = strdup((yyvsp[0].string));
    }
#line 12891 "lef.tab.c"
    break;

  case 913: /* $@115: %empty  */
#line 6630 "lef.y"
                 {lefData->lefDumbMode = 1; lefData->lefNoNum = 1;}
#line 12897 "lef.tab.c"
    break;

  case 914: /* end_array: K_END $@115 T_STRING  */
#line 6631 "lef.y"
    {
      if (lefCallbacks->ArrayCbk && lefCallbacks->ArrayEndCbk)
        CALLBACK(lefCallbacks->ArrayEndCbk, lefrArrayEndCbkType, (yyvsp[0].string));
      if (strcmp(lefData->arrayName, (yyvsp[0].string)) != 0) {
        if (lefCallbacks->ArrayCbk) { // write error only if cbk is set 
           if (lefData->arrayWarnings++ < lefSettings->ArrayWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "END ARRAY name %s is different from the ARRAY name %s.\nCorrect the LEF file before rerunning it through the LEF parser.", (yyvsp[0].string), lefData->arrayName);
              lefError(1662, lefData->outMsg);
              lefFree(lefData->outMsg);
              lefFree(lefData->arrayName);
              CHKERR();
           } else
              lefFree(lefData->arrayName);
        } else
           lefFree(lefData->arrayName);
      } else
        lefFree(lefData->arrayName);
    }
#line 12922 "lef.tab.c"
    break;

  case 915: /* array_rules: %empty  */
#line 6654 "lef.y"
    { }
#line 12928 "lef.tab.c"
    break;

  case 916: /* array_rules: array_rules array_rule  */
#line 6656 "lef.y"
    { }
#line 12934 "lef.tab.c"
    break;

  case 917: /* $@116: %empty  */
#line 6659 "lef.y"
            { if (lefCallbacks->ArrayCbk) lefData->lefrDoSite = 1; lefData->lefDumbMode = 1; }
#line 12940 "lef.tab.c"
    break;

  case 918: /* array_rule: site_word $@116 sitePattern ';'  */
#line 6661 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.addSitePattern(lefData->lefrSitePatternPtr);
      }
    }
#line 12950 "lef.tab.c"
    break;

  case 919: /* $@117: %empty  */
#line 6666 "lef.y"
               {lefData->lefDumbMode = 1; if (lefCallbacks->ArrayCbk) lefData->lefrDoSite = 1; }
#line 12956 "lef.tab.c"
    break;

  case 920: /* array_rule: K_CANPLACE $@117 sitePattern ';'  */
#line 6668 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.addCanPlace(lefData->lefrSitePatternPtr);
      }
    }
#line 12966 "lef.tab.c"
    break;

  case 921: /* $@118: %empty  */
#line 6673 "lef.y"
                   {lefData->lefDumbMode = 1; if (lefCallbacks->ArrayCbk) lefData->lefrDoSite = 1; }
#line 12972 "lef.tab.c"
    break;

  case 922: /* array_rule: K_CANNOTOCCUPY $@118 sitePattern ';'  */
#line 6675 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.addCannotOccupy(lefData->lefrSitePatternPtr);
      }
    }
#line 12982 "lef.tab.c"
    break;

  case 923: /* $@119: %empty  */
#line 6680 "lef.y"
             { if (lefCallbacks->ArrayCbk) lefData->lefrDoTrack = 1; }
#line 12988 "lef.tab.c"
    break;

  case 924: /* array_rule: K_TRACKS $@119 trackPattern ';'  */
#line 6681 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.addTrack(lefData->lefrTrackPatternPtr);
      }
    }
#line 12998 "lef.tab.c"
    break;

  case 925: /* array_rule: floorplan_start floorplan_list K_END T_STRING  */
#line 6687 "lef.y"
    {
    }
#line 13005 "lef.tab.c"
    break;

  case 926: /* $@120: %empty  */
#line 6689 "lef.y"
                { if (lefCallbacks->ArrayCbk) lefData->lefrDoGcell = 1; }
#line 13011 "lef.tab.c"
    break;

  case 927: /* array_rule: K_GCELLGRID $@120 gcellPattern ';'  */
#line 6690 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.addGcell(lefData->lefrGcellPatternPtr);
      }
    }
#line 13021 "lef.tab.c"
    break;

  case 928: /* array_rule: K_DEFAULTCAP int_number cap_list K_END K_DEFAULTCAP  */
#line 6696 "lef.y"
    {
      if (lefCallbacks->ArrayCbk) {
        lefData->lefrArray.setTableSize((int)(yyvsp[-3].dval));
      }
    }
#line 13031 "lef.tab.c"
    break;

  case 929: /* floorplan_start: K_FLOORPLAN T_STRING  */
#line 6703 "lef.y"
    { if (lefCallbacks->ArrayCbk) lefData->lefrArray.addFloorPlan((yyvsp[0].string)); }
#line 13037 "lef.tab.c"
    break;

  case 930: /* floorplan_list: %empty  */
#line 6707 "lef.y"
    { }
#line 13043 "lef.tab.c"
    break;

  case 931: /* floorplan_list: floorplan_list floorplan_element  */
#line 6709 "lef.y"
    { }
#line 13049 "lef.tab.c"
    break;

  case 932: /* $@121: %empty  */
#line 6712 "lef.y"
             { lefData->lefDumbMode = 1; if (lefCallbacks->ArrayCbk) lefData->lefrDoSite = 1; }
#line 13055 "lef.tab.c"
    break;

  case 933: /* floorplan_element: K_CANPLACE $@121 sitePattern ';'  */
#line 6714 "lef.y"
    {
      if (lefCallbacks->ArrayCbk)
        lefData->lefrArray.addSiteToFloorPlan("CANPLACE",
        lefData->lefrSitePatternPtr);
    }
#line 13065 "lef.tab.c"
    break;

  case 934: /* $@122: %empty  */
#line 6719 "lef.y"
                   { if (lefCallbacks->ArrayCbk) lefData->lefrDoSite = 1; lefData->lefDumbMode = 1; }
#line 13071 "lef.tab.c"
    break;

  case 935: /* floorplan_element: K_CANNOTOCCUPY $@122 sitePattern ';'  */
#line 6721 "lef.y"
    {
      if (lefCallbacks->ArrayCbk)
        lefData->lefrArray.addSiteToFloorPlan("CANNOTOCCUPY",
        lefData->lefrSitePatternPtr);
     }
#line 13081 "lef.tab.c"
    break;

  case 936: /* cap_list: %empty  */
#line 6729 "lef.y"
    { }
#line 13087 "lef.tab.c"
    break;

  case 937: /* cap_list: cap_list one_cap  */
#line 6731 "lef.y"
    { }
#line 13093 "lef.tab.c"
    break;

  case 938: /* one_cap: K_MINPINS int_number K_WIRECAP int_number ';'  */
#line 6734 "lef.y"
    { if (lefCallbacks->ArrayCbk) lefData->lefrArray.addDefaultCap((int)(yyvsp[-3].dval), (yyvsp[-1].dval)); }
#line 13099 "lef.tab.c"
    break;

  case 939: /* $@123: %empty  */
#line 6737 "lef.y"
            {lefData->lefDumbMode=1;lefData->lefNlToken=TRUE;}
#line 13105 "lef.tab.c"
    break;

  case 940: /* msg_statement: K_MESSAGE $@123 T_STRING '=' s_expr dtrm  */
#line 6738 "lef.y"
    {  }
#line 13111 "lef.tab.c"
    break;

  case 941: /* $@124: %empty  */
#line 6741 "lef.y"
               {lefData->lefDumbMode=1;lefData->lefNlToken=TRUE;}
#line 13117 "lef.tab.c"
    break;

  case 942: /* create_file_statement: K_CREATEFILE $@124 T_STRING '=' s_expr dtrm  */
#line 6742 "lef.y"
    { }
#line 13123 "lef.tab.c"
    break;

  case 944: /* dtrm: ';'  */
#line 6746 "lef.y"
         {lefData->lefNlToken = FALSE;}
#line 13129 "lef.tab.c"
    break;

  case 945: /* dtrm: '\n'  */
#line 6747 "lef.y"
                 {lefData->lefNlToken = FALSE;}
#line 13135 "lef.tab.c"
    break;

  case 950: /* expression: expression '+' expression  */
#line 6760 "lef.y"
                                {(yyval.dval) = (yyvsp[-2].dval) + (yyvsp[0].dval); }
#line 13141 "lef.tab.c"
    break;

  case 951: /* expression: expression '-' expression  */
#line 6761 "lef.y"
                                {(yyval.dval) = (yyvsp[-2].dval) - (yyvsp[0].dval); }
#line 13147 "lef.tab.c"
    break;

  case 952: /* expression: expression '*' expression  */
#line 6762 "lef.y"
                                {(yyval.dval) = (yyvsp[-2].dval) * (yyvsp[0].dval); }
#line 13153 "lef.tab.c"
    break;

  case 953: /* expression: expression '/' expression  */
#line 6763 "lef.y"
                                {(yyval.dval) = (yyvsp[-2].dval) / (yyvsp[0].dval); }
#line 13159 "lef.tab.c"
    break;

  case 954: /* expression: '-' expression  */
#line 6764 "lef.y"
                                {(yyval.dval) = -(yyvsp[0].dval);}
#line 13165 "lef.tab.c"
    break;

  case 955: /* expression: '(' expression ')'  */
#line 6765 "lef.y"
                                {(yyval.dval) = (yyvsp[-1].dval);}
#line 13171 "lef.tab.c"
    break;

  case 956: /* expression: K_IF b_expr then expression else expression  */
#line 6767 "lef.y"
                {(yyval.dval) = ((yyvsp[-4].integer) != 0) ? (yyvsp[-2].dval) : (yyvsp[0].dval);}
#line 13177 "lef.tab.c"
    break;

  case 957: /* expression: int_number  */
#line 6768 "lef.y"
                                     {(yyval.dval) = (yyvsp[0].dval);}
#line 13183 "lef.tab.c"
    break;

  case 958: /* b_expr: expression relop expression  */
#line 6771 "lef.y"
                              {(yyval.integer) = comp_num((yyvsp[-2].dval),(yyvsp[-1].integer),(yyvsp[0].dval));}
#line 13189 "lef.tab.c"
    break;

  case 959: /* b_expr: expression K_AND expression  */
#line 6772 "lef.y"
                                {(yyval.integer) = (yyvsp[-2].dval) != 0 && (yyvsp[0].dval) != 0;}
#line 13195 "lef.tab.c"
    break;

  case 960: /* b_expr: expression K_OR expression  */
#line 6773 "lef.y"
                                {(yyval.integer) = (yyvsp[-2].dval) != 0 || (yyvsp[0].dval) != 0;}
#line 13201 "lef.tab.c"
    break;

  case 961: /* b_expr: s_expr relop s_expr  */
#line 6774 "lef.y"
                              {(yyval.integer) = comp_str((yyvsp[-2].string),(yyvsp[-1].integer),(yyvsp[0].string));}
#line 13207 "lef.tab.c"
    break;

  case 962: /* b_expr: s_expr K_AND s_expr  */
#line 6775 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].string)[0] != 0 && (yyvsp[0].string)[0] != 0;}
#line 13213 "lef.tab.c"
    break;

  case 963: /* b_expr: s_expr K_OR s_expr  */
#line 6776 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].string)[0] != 0 || (yyvsp[0].string)[0] != 0;}
#line 13219 "lef.tab.c"
    break;

  case 964: /* b_expr: b_expr K_EQ b_expr  */
#line 6777 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].integer) == (yyvsp[0].integer);}
#line 13225 "lef.tab.c"
    break;

  case 965: /* b_expr: b_expr K_NE b_expr  */
#line 6778 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].integer) != (yyvsp[0].integer);}
#line 13231 "lef.tab.c"
    break;

  case 966: /* b_expr: b_expr K_AND b_expr  */
#line 6779 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].integer) && (yyvsp[0].integer);}
#line 13237 "lef.tab.c"
    break;

  case 967: /* b_expr: b_expr K_OR b_expr  */
#line 6780 "lef.y"
                              {(yyval.integer) = (yyvsp[-2].integer) || (yyvsp[0].integer);}
#line 13243 "lef.tab.c"
    break;

  case 968: /* b_expr: K_NOT b_expr  */
#line 6781 "lef.y"
                                               {(yyval.integer) = !(yyval.integer);}
#line 13249 "lef.tab.c"
    break;

  case 969: /* b_expr: '(' b_expr ')'  */
#line 6782 "lef.y"
                              {(yyval.integer) = (yyvsp[-1].integer);}
#line 13255 "lef.tab.c"
    break;

  case 970: /* b_expr: K_IF b_expr then b_expr else b_expr  */
#line 6784 "lef.y"
        {(yyval.integer) = ((yyvsp[-4].integer) != 0) ? (yyvsp[-2].integer) : (yyvsp[0].integer);}
#line 13261 "lef.tab.c"
    break;

  case 971: /* b_expr: K_TRUE  */
#line 6785 "lef.y"
                              {(yyval.integer) = 1;}
#line 13267 "lef.tab.c"
    break;

  case 972: /* b_expr: K_FALSE  */
#line 6786 "lef.y"
                              {(yyval.integer) = 0;}
#line 13273 "lef.tab.c"
    break;

  case 973: /* s_expr: s_expr '+' s_expr  */
#line 6790 "lef.y"
    {
      (yyval.string) = (char*)lefMalloc(strlen((yyvsp[-2].string))+strlen((yyvsp[0].string))+1);
      strcpy((yyval.string),(yyvsp[-2].string));
      strcat((yyval.string),(yyvsp[0].string));
    }
#line 13283 "lef.tab.c"
    break;

  case 974: /* s_expr: '(' s_expr ')'  */
#line 6796 "lef.y"
    { (yyval.string) = (yyvsp[-1].string); }
#line 13289 "lef.tab.c"
    break;

  case 975: /* s_expr: K_IF b_expr then s_expr else s_expr  */
#line 6798 "lef.y"
    {
      lefData->lefDefIf = TRUE;
      if ((yyvsp[-4].integer) != 0) {
        (yyval.string) = (yyvsp[-2].string);        
      } else {
        (yyval.string) = (yyvsp[0].string);
      }
    }
#line 13302 "lef.tab.c"
    break;

  case 976: /* s_expr: QSTRING  */
#line 6807 "lef.y"
    { (yyval.string) = (yyvsp[0].string); }
#line 13308 "lef.tab.c"
    break;

  case 977: /* relop: K_LE  */
#line 6810 "lef.y"
       {(yyval.integer) = C_LE;}
#line 13314 "lef.tab.c"
    break;

  case 978: /* relop: K_LT  */
#line 6811 "lef.y"
         {(yyval.integer) = C_LT;}
#line 13320 "lef.tab.c"
    break;

  case 979: /* relop: K_GE  */
#line 6812 "lef.y"
         {(yyval.integer) = C_GE;}
#line 13326 "lef.tab.c"
    break;

  case 980: /* relop: K_GT  */
#line 6813 "lef.y"
         {(yyval.integer) = C_GT;}
#line 13332 "lef.tab.c"
    break;

  case 981: /* relop: K_EQ  */
#line 6814 "lef.y"
         {(yyval.integer) = C_EQ;}
#line 13338 "lef.tab.c"
    break;

  case 982: /* relop: K_NE  */
#line 6815 "lef.y"
         {(yyval.integer) = C_NE;}
#line 13344 "lef.tab.c"
    break;

  case 983: /* relop: '='  */
#line 6816 "lef.y"
         {(yyval.integer) = C_EQ;}
#line 13350 "lef.tab.c"
    break;

  case 984: /* relop: '<'  */
#line 6817 "lef.y"
         {(yyval.integer) = C_LT;}
#line 13356 "lef.tab.c"
    break;

  case 985: /* relop: '>'  */
#line 6818 "lef.y"
         {(yyval.integer) = C_GT;}
#line 13362 "lef.tab.c"
    break;

  case 986: /* $@125: %empty  */
#line 6822 "lef.y"
    { 
      if (lefCallbacks->PropBeginCbk)
        CALLBACK(lefCallbacks->PropBeginCbk, lefrPropBeginCbkType, 0);
    }
#line 13371 "lef.tab.c"
    break;

  case 987: /* prop_def_section: K_PROPDEF $@125 prop_stmts K_END K_PROPDEF  */
#line 6827 "lef.y"
    { 
      if (lefCallbacks->PropEndCbk)
        CALLBACK(lefCallbacks->PropEndCbk, lefrPropEndCbkType, 0);
    }
#line 13380 "lef.tab.c"
    break;

  case 988: /* prop_stmts: %empty  */
#line 6834 "lef.y"
    { }
#line 13386 "lef.tab.c"
    break;

  case 989: /* prop_stmts: prop_stmts prop_stmt  */
#line 6836 "lef.y"
    { }
#line 13392 "lef.tab.c"
    break;

  case 990: /* $@126: %empty  */
#line 6839 "lef.y"
            {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13398 "lef.tab.c"
    break;

  case 991: /* prop_stmt: K_LIBRARY $@126 T_STRING prop_define ';'  */
#line 6841 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("library", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrLibProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13410 "lef.tab.c"
    break;

  case 992: /* $@127: %empty  */
#line 6848 "lef.y"
                   {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13416 "lef.tab.c"
    break;

  case 993: /* prop_stmt: K_COMPONENTPIN $@127 T_STRING prop_define ';'  */
#line 6850 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("componentpin", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrCompProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13428 "lef.tab.c"
    break;

  case 994: /* $@128: %empty  */
#line 6857 "lef.y"
          {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13434 "lef.tab.c"
    break;

  case 995: /* prop_stmt: K_PIN $@128 T_STRING prop_define ';'  */
#line 6859 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("pin", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrPinProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
      
    }
#line 13447 "lef.tab.c"
    break;

  case 996: /* $@129: %empty  */
#line 6867 "lef.y"
            {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13453 "lef.tab.c"
    break;

  case 997: /* prop_stmt: K_MACRO $@129 T_STRING prop_define ';'  */
#line 6869 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("macro", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrMacroProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13465 "lef.tab.c"
    break;

  case 998: /* $@130: %empty  */
#line 6876 "lef.y"
          {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13471 "lef.tab.c"
    break;

  case 999: /* prop_stmt: K_VIA $@130 T_STRING prop_define ';'  */
#line 6878 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("via", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrViaProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13483 "lef.tab.c"
    break;

  case 1000: /* $@131: %empty  */
#line 6885 "lef.y"
              {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13489 "lef.tab.c"
    break;

  case 1001: /* prop_stmt: K_VIARULE $@131 T_STRING prop_define ';'  */
#line 6887 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("viarule", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrViaRuleProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13501 "lef.tab.c"
    break;

  case 1002: /* $@132: %empty  */
#line 6894 "lef.y"
            {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13507 "lef.tab.c"
    break;

  case 1003: /* prop_stmt: K_LAYER $@132 T_STRING prop_define ';'  */
#line 6896 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("layer", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrLayerProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13519 "lef.tab.c"
    break;

  case 1004: /* $@133: %empty  */
#line 6903 "lef.y"
                     {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13525 "lef.tab.c"
    break;

  case 1005: /* prop_stmt: K_NONDEFAULTRULE $@133 T_STRING prop_define ';'  */
#line 6905 "lef.y"
    { 
      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("nondefaultrule", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrNondefProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13537 "lef.tab.c"
    break;

  case 1006: /* $@134: %empty  */
#line 6912 "lef.y"
              {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13543 "lef.tab.c"
    break;

  case 1007: /* prop_stmt: K_PORTOBS $@134 T_STRING prop_define ';'  */
#line 6914 "lef.y"
    { 
      if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("PROPERTYDEFINITIONS ... [PORTOBS propName propType ...]")) {
            CHKERR();
        }
      }

      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("portobs", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrPortobsProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13561 "lef.tab.c"
    break;

  case 1008: /* $@135: %empty  */
#line 6927 "lef.y"
           {lefData->lefDumbMode = 1; lefData->lefrProp.clear(); }
#line 13567 "lef.tab.c"
    break;

  case 1009: /* prop_stmt: K_SITE $@135 T_STRING prop_define ';'  */
#line 6929 "lef.y"
    { 
      if (lefData->versionNum < 6.0 - 0.00001) {
        if (lefData->lef60NewSyntaxError("PROPERTYDEFINITIONS ... [SITE propName propType ...]")) {
            CHKERR();
        }
      }

      if (lefCallbacks->PropCbk) {
        lefData->lefrProp.setPropType("site", (yyvsp[-2].string));
        CALLBACK(lefCallbacks->PropCbk, lefrPropCbkType, &lefData->lefrProp);
      }
      lefSettings->lefProps.lefrSiteProp.setPropType((yyvsp[-2].string), lefData->lefPropDefType);
    }
#line 13585 "lef.tab.c"
    break;

  case 1010: /* prop_define: K_INTEGER opt_def_range opt_def_dvalue  */
#line 6944 "lef.y"
    { 
      if (lefCallbacks->PropCbk) lefData->lefrProp.setPropInteger();
      lefData->lefPropDefType = 'I';
    }
#line 13594 "lef.tab.c"
    break;

  case 1011: /* prop_define: K_REAL opt_def_range opt_def_value  */
#line 6949 "lef.y"
    { 
      if (lefCallbacks->PropCbk) lefData->lefrProp.setPropReal();
      lefData->lefPropDefType = 'R';
    }
#line 13603 "lef.tab.c"
    break;

  case 1012: /* prop_define: K_STRING  */
#line 6954 "lef.y"
    {
      if (lefCallbacks->PropCbk) lefData->lefrProp.setPropString();
      lefData->lefPropDefType = 'S';
    }
#line 13612 "lef.tab.c"
    break;

  case 1013: /* prop_define: K_STRING QSTRING  */
#line 6959 "lef.y"
    {
      if (lefCallbacks->PropCbk) lefData->lefrProp.setPropQString((yyvsp[0].string));
      lefData->lefPropDefType = 'Q';
    }
#line 13621 "lef.tab.c"
    break;

  case 1014: /* prop_define: K_NAMEMAPSTRING T_STRING  */
#line 6964 "lef.y"
    {
      if (lefCallbacks->PropCbk) lefData->lefrProp.setPropNameMapString((yyvsp[0].string));
      lefData->lefPropDefType = 'S';
    }
#line 13630 "lef.tab.c"
    break;

  case 1015: /* opt_range_second: %empty  */
#line 6971 "lef.y"
    { }
#line 13636 "lef.tab.c"
    break;

  case 1016: /* opt_range_second: K_USELENGTHTHRESHOLD  */
#line 6973 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingRangeUseLength();
    }
#line 13645 "lef.tab.c"
    break;

  case 1017: /* opt_range_second: K_INFLUENCE int_number  */
#line 6978 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        lefData->lefrLayer.setSpacingRangeInfluence((yyvsp[0].dval));
        lefData->lefrLayer.setSpacingRangeInfluenceRange(-1, -1);
      }
    }
#line 13656 "lef.tab.c"
    break;

  case 1018: /* opt_range_second: K_INFLUENCE int_number K_RANGE int_number int_number  */
#line 6985 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        lefData->lefrLayer.setSpacingRangeInfluence((yyvsp[-3].dval));
        lefData->lefrLayer.setSpacingRangeInfluenceRange((yyvsp[-1].dval), (yyvsp[0].dval));
      }
    }
#line 13667 "lef.tab.c"
    break;

  case 1019: /* opt_range_second: K_RANGE int_number int_number  */
#line 6992 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingRangeRange((yyvsp[-1].dval), (yyvsp[0].dval));
    }
#line 13676 "lef.tab.c"
    break;

  case 1020: /* opt_endofline: %empty  */
#line 6999 "lef.y"
    { }
#line 13682 "lef.tab.c"
    break;

  case 1021: /* $@136: %empty  */
#line 7001 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingParSW((yyvsp[-2].dval), (yyvsp[0].dval));
    }
#line 13691 "lef.tab.c"
    break;

  case 1023: /* opt_endofline_twoedges: %empty  */
#line 7009 "lef.y"
    { }
#line 13697 "lef.tab.c"
    break;

  case 1024: /* opt_endofline_twoedges: K_TWOEDGES  */
#line 7011 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingParTwoEdges();
    }
#line 13706 "lef.tab.c"
    break;

  case 1025: /* opt_samenetPGonly: %empty  */
#line 7018 "lef.y"
    { }
#line 13712 "lef.tab.c"
    break;

  case 1026: /* opt_samenetPGonly: K_PGONLY  */
#line 7020 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingSamenetPGonly();
    }
#line 13721 "lef.tab.c"
    break;

  case 1027: /* opt_def_range: %empty  */
#line 7027 "lef.y"
    { }
#line 13727 "lef.tab.c"
    break;

  case 1028: /* opt_def_range: K_RANGE int_number int_number  */
#line 7029 "lef.y"
    {  if (lefCallbacks->PropCbk) lefData->lefrProp.setRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 13733 "lef.tab.c"
    break;

  case 1029: /* opt_def_value: %empty  */
#line 7033 "lef.y"
    { }
#line 13739 "lef.tab.c"
    break;

  case 1030: /* opt_def_value: NUMBER  */
#line 7035 "lef.y"
    { if (lefCallbacks->PropCbk) lefData->lefrProp.setNumber((yyvsp[0].dval)); }
#line 13745 "lef.tab.c"
    break;

  case 1031: /* opt_def_dvalue: %empty  */
#line 7039 "lef.y"
    { }
#line 13751 "lef.tab.c"
    break;

  case 1032: /* opt_def_dvalue: int_number  */
#line 7041 "lef.y"
    { if (lefCallbacks->PropCbk) lefData->lefrProp.setNumber((yyvsp[0].dval)); }
#line 13757 "lef.tab.c"
    break;

  case 1035: /* layer_spacing_opt: K_CENTERTOCENTER  */
#line 7048 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
         if (lefData->hasSpCenter) {
           if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1663, "A CENTERTOCENTER statement was already defined in SPACING\nCENTERTOCENTER can only be defined once per LAYER CUT SPACING.");
              CHKERR();
           }
        }
        lefData->hasSpCenter = 1;
        if (lefData->versionNum < 5.6) {
           if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "CENTERTOCENTER statement is a version 5.6 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1664, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setSpacingCenterToCenter();
      }
    }
#line 13785 "lef.tab.c"
    break;

  case 1036: /* $@137: %empty  */
#line 7072 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        if (lefData->hasSpSamenet) {
           if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefError(1665, "A SAMENET statement was already defined in SPACING\nSAMENET can only be defined once per LAYER CUT SPACING.");
              CHKERR();
           }
        }
        lefData->hasSpSamenet = 1;
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setSpacingSamenet();
       }
    }
#line 13803 "lef.tab.c"
    break;

  case 1037: /* layer_spacing_opt: K_SAMENET $@137 opt_samenetPGonly  */
#line 7086 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "SAMENET is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1684, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      }
    }
#line 13818 "lef.tab.c"
    break;

  case 1038: /* layer_spacing_opt: K_PARALLELOVERLAP  */
#line 7097 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "PARALLELOVERLAP is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1680, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR(); 
      } else {
        if (lefCallbacks->LayerCbk) {
          if (lefData->hasSpParallel) {
             if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                lefError(1666, "A PARALLELOVERLAP statement was already defined in SPACING\nPARALLELOVERLAP can only be defined once per LAYER CUT SPACING.");
                CHKERR();
             }
          }
          lefData->hasSpParallel = 1;
          if (lefCallbacks->LayerCbk)
            lefData->lefrLayer.setSpacingParallelOverlap();
        }
      }
    }
#line 13845 "lef.tab.c"
    break;

  case 1040: /* $@138: %empty  */
#line 7122 "lef.y"
            {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 13851 "lef.tab.c"
    break;

  case 1041: /* $@139: %empty  */
#line 7123 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
{
        if (lefData->versionNum < 5.7) {
           if (lefData->hasSpSamenet) {    // 5.6 and earlier does not allow 
              if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefError(1667, "A SAMENET statement was already defined in SPACING\nEither SAMENET or LAYER can be defined, but not both.");
                 CHKERR();
              }
           }
        }
        lefData->lefrLayer.setSpacingName((yyvsp[0].string));
      }
    }
#line 13870 "lef.tab.c"
    break;

  case 1043: /* $@140: %empty  */
#line 7139 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        if (lefData->versionNum < 5.5) {
           if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
              lefData->outMsg = (char*)lefMalloc(10000);
              sprintf (lefData->outMsg,
                 "ADJACENTCUTS statement is a version 5.5 and later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
              lefError(1668, lefData->outMsg);
              lefFree(lefData->outMsg);
              CHKERR();
           }
        }
        if (lefData->versionNum < 5.7) {
           if (lefData->hasSpSamenet) {    // 5.6 and earlier does not allow 
              if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                 lefError(1669, "A SAMENET statement was already defined in SPACING\nEither SAMENET or ADJACENTCUTS can be defined, but not both.");
                 CHKERR();
              }
           }
        }
        lefData->lefrLayer.setSpacingAdjacent((int)(yyvsp[-2].dval), (yyvsp[0].dval));
      }
    }
#line 13898 "lef.tab.c"
    break;

  case 1045: /* layer_spacing_cut_routing: K_AREA NUMBER  */
#line 7164 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "AREA is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1693, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
        if (lefCallbacks->LayerCbk) {
          if (lefData->versionNum < 5.7) {
             if (lefData->hasSpSamenet) {    // 5.6 and earlier does not allow 
                if (lefData->layerWarnings++ < lefSettings->LayerWarnings) {
                   lefError(1670, "A SAMENET statement was already defined in SPACING\nEither SAMENET or AREA can be defined, but not both.");
                   CHKERR();
                }
             }
          }
          lefData->lefrLayer.setSpacingArea((yyvsp[0].dval));
        }
      }
    }
#line 13925 "lef.tab.c"
    break;

  case 1046: /* $@141: %empty  */
#line 7187 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingRange((yyvsp[-1].dval), (yyvsp[0].dval));
    }
#line 13934 "lef.tab.c"
    break;

  case 1048: /* layer_spacing_cut_routing: K_LENGTHTHRESHOLD int_number  */
#line 7193 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        lefData->lefrLayer.setSpacingLength((yyvsp[0].dval));
      }
    }
#line 13944 "lef.tab.c"
    break;

  case 1049: /* layer_spacing_cut_routing: K_LENGTHTHRESHOLD int_number K_RANGE int_number int_number  */
#line 7199 "lef.y"
    {
      if (lefCallbacks->LayerCbk) {
        lefData->lefrLayer.setSpacingLength((yyvsp[-3].dval));
        lefData->lefrLayer.setSpacingLengthRange((yyvsp[-1].dval), (yyvsp[0].dval));
      }
    }
#line 13955 "lef.tab.c"
    break;

  case 1050: /* $@142: %empty  */
#line 7206 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingEol((yyvsp[-2].dval), (yyvsp[0].dval));
    }
#line 13964 "lef.tab.c"
    break;

  case 1051: /* layer_spacing_cut_routing: K_ENDOFLINE int_number K_WITHIN int_number $@142 opt_endofline  */
#line 7211 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "ENDOFLINE is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1681, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      }
    }
#line 13979 "lef.tab.c"
    break;

  case 1052: /* layer_spacing_cut_routing: K_NOTCHLENGTH int_number  */
#line 7222 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "NOTCHLENGTH is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1682, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setSpacingNotchLength((yyvsp[0].dval));
      }
    }
#line 13997 "lef.tab.c"
    break;

  case 1053: /* layer_spacing_cut_routing: K_ENDOFNOTCHWIDTH int_number K_NOTCHSPACING int_number K_NOTCHLENGTH int_number  */
#line 7236 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "ENDOFNOTCHWIDTH is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1696, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setSpacingEndOfNotchWidth((yyvsp[-4].dval), (yyvsp[-2].dval), (yyvsp[0].dval));
      }
    }
#line 14015 "lef.tab.c"
    break;

  case 1054: /* spacing_cut_layer_opt: %empty  */
#line 7252 "lef.y"
    {}
#line 14021 "lef.tab.c"
    break;

  case 1055: /* spacing_cut_layer_opt: K_STACK  */
#line 7254 "lef.y"
    {
      if (lefCallbacks->LayerCbk)
        lefData->lefrLayer.setSpacingLayerStack();
    }
#line 14030 "lef.tab.c"
    break;

  case 1056: /* opt_adjacentcuts_exceptsame: %empty  */
#line 7261 "lef.y"
    {}
#line 14036 "lef.tab.c"
    break;

  case 1057: /* opt_adjacentcuts_exceptsame: K_EXCEPTSAMEPGNET  */
#line 7263 "lef.y"
    {
      if (lefData->versionNum < 5.7) {
        lefData->outMsg = (char*)lefMalloc(10000);
        sprintf(lefData->outMsg,
          "EXCEPTSAMEPGNET is a version 5.7 or later syntax.\nYour lef file is defined with version %.2f.", lefData->versionNum);
        lefError(1683, lefData->outMsg);
        lefFree(lefData->outMsg);
        CHKERR();
      } else {
        if (lefCallbacks->LayerCbk)
          lefData->lefrLayer.setSpacingAdjacentExcept();
      }
    }
#line 14054 "lef.tab.c"
    break;

  case 1058: /* opt_layer_name: %empty  */
#line 7279 "lef.y"
    { (yyval.string) = 0; }
#line 14060 "lef.tab.c"
    break;

  case 1059: /* $@143: %empty  */
#line 7280 "lef.y"
            {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 14066 "lef.tab.c"
    break;

  case 1060: /* opt_layer_name: K_LAYER $@143 T_STRING  */
#line 7281 "lef.y"
    { (yyval.string) = (yyvsp[0].string); }
#line 14072 "lef.tab.c"
    break;

  case 1061: /* $@144: %empty  */
#line 7285 "lef.y"
           {lefData->lefDumbMode = 1; lefData->lefNoNum = 1; }
#line 14078 "lef.tab.c"
    break;

  case 1062: /* req_layer_name: K_LAYER $@144 T_STRING  */
#line 7286 "lef.y"
    { (yyval.string) = (yyvsp[0].string); }
#line 14084 "lef.tab.c"
    break;

  case 1063: /* universalnoisemargin: K_UNIVERSALNOISEMARGIN int_number int_number ';'  */
#line 7290 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->NoiseMarginCbk) {
          lefData->lefrNoiseMargin.low = (yyvsp[-2].dval);
          lefData->lefrNoiseMargin.high = (yyvsp[-1].dval);
          CALLBACK(lefCallbacks->NoiseMarginCbk, lefrNoiseMarginCbkType, &lefData->lefrNoiseMargin);
        }
      } else
        if (lefCallbacks->NoiseMarginCbk) // write warning only if cbk is set 
          if (lefData->noiseMarginWarnings++ < lefSettings->NoiseMarginWarnings)
            lefWarning(2070, "UNIVERSALNOISEMARGIN statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 14101 "lef.tab.c"
    break;

  case 1064: /* edgeratethreshold1: K_EDGERATETHRESHOLD1 int_number ';'  */
#line 7304 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->EdgeRateThreshold1Cbk) {
          CALLBACK(lefCallbacks->EdgeRateThreshold1Cbk,
          lefrEdgeRateThreshold1CbkType, (yyvsp[-1].dval));
        }
      } else
        if (lefCallbacks->EdgeRateThreshold1Cbk) // write warning only if cbk is set 
          if (lefData->edgeRateThreshold1Warnings++ < lefSettings->EdgeRateThreshold1Warnings)
            lefWarning(2071, "EDGERATETHRESHOLD1 statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 14117 "lef.tab.c"
    break;

  case 1065: /* edgeratethreshold2: K_EDGERATETHRESHOLD2 int_number ';'  */
#line 7317 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->EdgeRateThreshold2Cbk) {
          CALLBACK(lefCallbacks->EdgeRateThreshold2Cbk,
          lefrEdgeRateThreshold2CbkType, (yyvsp[-1].dval));
        }
      } else
        if (lefCallbacks->EdgeRateThreshold2Cbk) // write warning only if cbk is set 
          if (lefData->edgeRateThreshold2Warnings++ < lefSettings->EdgeRateThreshold2Warnings)
            lefWarning(2072, "EDGERATETHRESHOLD2 statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 14133 "lef.tab.c"
    break;

  case 1066: /* edgeratescalefactor: K_EDGERATESCALEFACTOR int_number ';'  */
#line 7330 "lef.y"
    {
      if (lefData->versionNum < 5.4) {
        if (lefCallbacks->EdgeRateScaleFactorCbk) {
          CALLBACK(lefCallbacks->EdgeRateScaleFactorCbk,
          lefrEdgeRateScaleFactorCbkType, (yyvsp[-1].dval));
        }
      } else
        if (lefCallbacks->EdgeRateScaleFactorCbk) // write warning only if cbk is set 
          if (lefData->edgeRateScaleFactorWarnings++ < lefSettings->EdgeRateScaleFactorWarnings)
            lefWarning(2073, "EDGERATESCALEFACTOR statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
    }
#line 14149 "lef.tab.c"
    break;

  case 1067: /* $@145: %empty  */
#line 7343 "lef.y"
    { if (lefCallbacks->NoiseTableCbk) lefData->lefrNoiseTable.setup((int)(yyvsp[0].dval)); }
#line 14155 "lef.tab.c"
    break;

  case 1068: /* noisetable: K_NOISETABLE int_number $@145 ';' noise_table_list end_noisetable dtrm  */
#line 7345 "lef.y"
    { }
#line 14161 "lef.tab.c"
    break;

  case 1069: /* end_noisetable: K_END K_NOISETABLE  */
#line 7349 "lef.y"
  {
    if (lefData->versionNum < 5.4) {
      if (lefCallbacks->NoiseTableCbk)
        CALLBACK(lefCallbacks->NoiseTableCbk, lefrNoiseTableCbkType, &lefData->lefrNoiseTable);
    } else
      if (lefCallbacks->NoiseTableCbk) // write warning only if cbk is set 
        if (lefData->noiseTableWarnings++ < lefSettings->NoiseTableWarnings)
          lefWarning(2074, "NOISETABLE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
  }
#line 14175 "lef.tab.c"
    break;

  case 1072: /* noise_table_entry: K_EDGERATE int_number ';'  */
#line 7367 "lef.y"
    { if (lefCallbacks->NoiseTableCbk)
         {
            lefData->lefrNoiseTable.newEdge();
            lefData->lefrNoiseTable.addEdge((yyvsp[-1].dval));
         }
    }
#line 14186 "lef.tab.c"
    break;

  case 1073: /* noise_table_entry: output_resistance_entry  */
#line 7374 "lef.y"
    { }
#line 14192 "lef.tab.c"
    break;

  case 1074: /* $@146: %empty  */
#line 7377 "lef.y"
    { if (lefCallbacks->NoiseTableCbk) lefData->lefrNoiseTable.addResistance(); }
#line 14198 "lef.tab.c"
    break;

  case 1076: /* num_list: int_number  */
#line 7383 "lef.y"
    { if (lefCallbacks->NoiseTableCbk)
    lefData->lefrNoiseTable.addResistanceNumber((yyvsp[0].dval)); }
#line 14205 "lef.tab.c"
    break;

  case 1077: /* num_list: num_list int_number  */
#line 7386 "lef.y"
    { if (lefCallbacks->NoiseTableCbk)
    lefData->lefrNoiseTable.addResistanceNumber((yyvsp[0].dval)); }
#line 14212 "lef.tab.c"
    break;

  case 1080: /* $@147: %empty  */
#line 7395 "lef.y"
        { if (lefCallbacks->NoiseTableCbk)
        lefData->lefrNoiseTable.addVictimLength((yyvsp[-1].dval)); }
#line 14219 "lef.tab.c"
    break;

  case 1081: /* victim: K_VICTIMLENGTH int_number ';' $@147 K_VICTIMNOISE vnoiselist ';'  */
#line 7398 "lef.y"
        { }
#line 14225 "lef.tab.c"
    break;

  case 1082: /* vnoiselist: int_number  */
#line 7402 "lef.y"
    { if (lefCallbacks->NoiseTableCbk)
    lefData->lefrNoiseTable.addVictimNoise((yyvsp[0].dval)); }
#line 14232 "lef.tab.c"
    break;

  case 1083: /* vnoiselist: vnoiselist int_number  */
#line 7405 "lef.y"
    { if (lefCallbacks->NoiseTableCbk)
    lefData->lefrNoiseTable.addVictimNoise((yyvsp[0].dval)); }
#line 14239 "lef.tab.c"
    break;

  case 1084: /* $@148: %empty  */
#line 7409 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
    lefData->lefrCorrectionTable.setup((int)(yyvsp[-1].dval)); }
#line 14246 "lef.tab.c"
    break;

  case 1085: /* correctiontable: K_CORRECTIONTABLE int_number ';' $@148 correction_table_list end_correctiontable dtrm  */
#line 7412 "lef.y"
    { }
#line 14252 "lef.tab.c"
    break;

  case 1086: /* end_correctiontable: K_END K_CORRECTIONTABLE  */
#line 7416 "lef.y"
  {
    if (lefData->versionNum < 5.4) {
      if (lefCallbacks->CorrectionTableCbk)
        CALLBACK(lefCallbacks->CorrectionTableCbk, lefrCorrectionTableCbkType,
               &lefData->lefrCorrectionTable);
    } else
      if (lefCallbacks->CorrectionTableCbk) // write warning only if cbk is set 
        if (lefData->correctionTableWarnings++ < lefSettings->CorrectionTableWarnings)
          lefWarning(2075, "CORRECTIONTABLE statement is obsolete in version 5.4 and later.\nThe LEF parser will ignore this statement.\nTo avoid this warning in the future, remove this statement from the LEF file with version 5.4 or later.");
  }
#line 14267 "lef.tab.c"
    break;

  case 1089: /* correction_table_item: K_EDGERATE int_number ';'  */
#line 7434 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
         {
            lefData->lefrCorrectionTable.newEdge();
            lefData->lefrCorrectionTable.addEdge((yyvsp[-1].dval));
         }
    }
#line 14278 "lef.tab.c"
    break;

  case 1090: /* correction_table_item: output_list  */
#line 7441 "lef.y"
    { }
#line 14284 "lef.tab.c"
    break;

  case 1091: /* $@149: %empty  */
#line 7444 "lef.y"
  { if (lefCallbacks->CorrectionTableCbk)
  lefData->lefrCorrectionTable.addResistance(); }
#line 14291 "lef.tab.c"
    break;

  case 1092: /* output_list: K_OUTPUTRESISTANCE $@149 numo_list ';' corr_victim_list  */
#line 7447 "lef.y"
  { }
#line 14297 "lef.tab.c"
    break;

  case 1093: /* numo_list: int_number  */
#line 7451 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
    lefData->lefrCorrectionTable.addResistanceNumber((yyvsp[0].dval)); }
#line 14304 "lef.tab.c"
    break;

  case 1094: /* numo_list: numo_list int_number  */
#line 7454 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
    lefData->lefrCorrectionTable.addResistanceNumber((yyvsp[0].dval)); }
#line 14311 "lef.tab.c"
    break;

  case 1097: /* $@150: %empty  */
#line 7464 "lef.y"
     { if (lefCallbacks->CorrectionTableCbk)
     lefData->lefrCorrectionTable.addVictimLength((yyvsp[-1].dval)); }
#line 14318 "lef.tab.c"
    break;

  case 1098: /* corr_victim: K_VICTIMLENGTH int_number ';' $@150 K_CORRECTIONFACTOR corr_list ';'  */
#line 7467 "lef.y"
     { }
#line 14324 "lef.tab.c"
    break;

  case 1099: /* corr_list: int_number  */
#line 7471 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
        lefData->lefrCorrectionTable.addVictimCorrection((yyvsp[0].dval)); }
#line 14331 "lef.tab.c"
    break;

  case 1100: /* corr_list: corr_list int_number  */
#line 7474 "lef.y"
    { if (lefCallbacks->CorrectionTableCbk)
        lefData->lefrCorrectionTable.addVictimCorrection((yyvsp[0].dval)); }
#line 14338 "lef.tab.c"
    break;

  case 1101: /* input_antenna: K_INPUTPINANTENNASIZE int_number ';'  */
#line 7480 "lef.y"
    { // 5.3 syntax 
        lefData->use5_3 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum > 5.3) {
           // A 5.3 syntax in 5.4 
           if (lefData->use5_4) {
              if (lefCallbacks->InputAntennaCbk) { // write warning only if cbk is set 
                if (lefData->inputAntennaWarnings++ < lefSettings->InputAntennaWarnings) {
                   lefData->outMsg = (char*)lefMalloc(10000);
                   sprintf (lefData->outMsg,
                      "INPUTPINANTENNASIZE statement is a version 5.3 or earlier syntax.\nYour lef file with version %.2f, has both old and new INPUTPINANTENNASIZE syntax, which is incorrect.", lefData->versionNum);
                   lefError(1671, lefData->outMsg);
                   lefFree(lefData->outMsg);
                   CHKERR();
                }
              }
           }
        }
        if (lefCallbacks->InputAntennaCbk)
          CALLBACK(lefCallbacks->InputAntennaCbk, lefrInputAntennaCbkType, (yyvsp[-1].dval));
    }
#line 14365 "lef.tab.c"
    break;

  case 1102: /* output_antenna: K_OUTPUTPINANTENNASIZE int_number ';'  */
#line 7504 "lef.y"
    { // 5.3 syntax 
        lefData->use5_3 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum > 5.3) {
           // A 5.3 syntax in 5.4 
           if (lefData->use5_4) {
              if (lefCallbacks->OutputAntennaCbk) { // write warning only if cbk is set 
                if (lefData->outputAntennaWarnings++ < lefSettings->OutputAntennaWarnings) {
                   lefData->outMsg = (char*)lefMalloc(10000);
                   sprintf (lefData->outMsg,
                      "OUTPUTPINANTENNASIZE statement is a version 5.3 or earlier syntax.\nYour lef file with version %.2f, has both old and new OUTPUTPINANTENNASIZE syntax, which is incorrect.", lefData->versionNum);
                   lefError(1672, lefData->outMsg);
                   lefFree(lefData->outMsg);
                   CHKERR();
                }
              }
           }
        }
        if (lefCallbacks->OutputAntennaCbk)
          CALLBACK(lefCallbacks->OutputAntennaCbk, lefrOutputAntennaCbkType, (yyvsp[-1].dval));
    }
#line 14392 "lef.tab.c"
    break;

  case 1103: /* inout_antenna: K_INOUTPINANTENNASIZE int_number ';'  */
#line 7528 "lef.y"
    { // 5.3 syntax 
        lefData->use5_3 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum > 5.3) {
           // A 5.3 syntax in 5.4 
           if (lefData->use5_4) {
              if (lefCallbacks->InoutAntennaCbk) { // write warning only if cbk is set 
                if (lefData->inoutAntennaWarnings++ < lefSettings->InoutAntennaWarnings) {
                   lefData->outMsg = (char*)lefMalloc(10000);
                   sprintf (lefData->outMsg,
                      "INOUTPINANTENNASIZE statement is a version 5.3 or earlier syntax.\nYour lef file with version %.2f, has both old and new INOUTPINANTENNASIZE syntax, which is incorrect.", lefData->versionNum);
                   lefError(1673, lefData->outMsg);
                   lefFree(lefData->outMsg);
                   CHKERR();
                }
              }
           }
        }
        if (lefCallbacks->InoutAntennaCbk)
          CALLBACK(lefCallbacks->InoutAntennaCbk, lefrInoutAntennaCbkType, (yyvsp[-1].dval));
    }
#line 14419 "lef.tab.c"
    break;

  case 1104: /* antenna_input: K_ANTENNAINPUTGATEAREA NUMBER ';'  */
#line 7552 "lef.y"
    { // 5.4 syntax 
        // 11/12/2002 - this is obsolete in 5.5, suppose should be ingored 
        // 12/16/2002 - talked to Dave Noice, leave them in here for debugging
        lefData->use5_4 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum < 5.4) {
           if (lefCallbacks->AntennaInputCbk) { // write warning only if cbk is set 
             if (lefData->antennaInputWarnings++ < lefSettings->AntennaInputWarnings) {
               lefData->outMsg = (char*)lefMalloc(10000);
               sprintf (lefData->outMsg,
                  "ANTENNAINPUTGATEAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.\nEither update your VERSION number or use the 5.3 syntax.", lefData->versionNum);
               lefError(1674, lefData->outMsg);
               lefFree(lefData->outMsg);
               CHKERR();
             }
           }
        } else if (lefData->use5_3) {
           if (lefCallbacks->AntennaInputCbk) { // write warning only if cbk is set 
             if (lefData->antennaInputWarnings++ < lefSettings->AntennaInputWarnings) {
                lefData->outMsg = (char*)lefMalloc(10000);
                sprintf (lefData->outMsg,
                   "ANTENNAINPUTGATEAREA statement is a version 5.4 or later syntax.\nYour lef file with version %.2f, has both old and new ANTENNAINPUTGATEAREA syntax, which is incorrect.", lefData->versionNum);
                lefError(1675, lefData->outMsg);
                lefFree(lefData->outMsg);
               CHKERR();
             }
           }
        }
        if (lefCallbacks->AntennaInputCbk)
          CALLBACK(lefCallbacks->AntennaInputCbk, lefrAntennaInputCbkType, (yyvsp[-1].dval));
    }
#line 14456 "lef.tab.c"
    break;

  case 1105: /* antenna_inout: K_ANTENNAINOUTDIFFAREA NUMBER ';'  */
#line 7586 "lef.y"
    { // 5.4 syntax 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        // 12/16/2002 - talked to Dave Noice, leave them in here for debugging
        lefData->use5_4 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum < 5.4) {
           if (lefCallbacks->AntennaInoutCbk) { // write warning only if cbk is set 
              if (lefData->antennaInoutWarnings++ < lefSettings->AntennaInoutWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAINOUTDIFFAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.\nEither update your VERSION number or use the 5.3 syntax.", lefData->versionNum);
                 lefError(1676, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        } else if (lefData->use5_3) {
           if (lefCallbacks->AntennaInoutCbk) { // write warning only if cbk is set 
              if (lefData->antennaInoutWarnings++ < lefSettings->AntennaInoutWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAINOUTDIFFAREA statement is a version 5.4 or later syntax.\nYour lef file with version %.2f, has both old and new ANTENNAINOUTDIFFAREA syntax, which is incorrect.", lefData->versionNum);
                 lefError(1677, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
        if (lefCallbacks->AntennaInoutCbk)
          CALLBACK(lefCallbacks->AntennaInoutCbk, lefrAntennaInoutCbkType, (yyvsp[-1].dval));
    }
#line 14493 "lef.tab.c"
    break;

  case 1106: /* antenna_output: K_ANTENNAOUTPUTDIFFAREA NUMBER ';'  */
#line 7620 "lef.y"
    { // 5.4 syntax 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        // 12/16/2002 - talked to Dave Noice, leave them in here for debugging
        lefData->use5_4 = 1;
        if (lefData->ignoreVersion) {
           // do nothing 
        } else if (lefData->versionNum < 5.4) {
           if (lefCallbacks->AntennaOutputCbk) { // write warning only if cbk is set 
              if (lefData->antennaOutputWarnings++ < lefSettings->AntennaOutputWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAOUTPUTDIFFAREA statement is a version 5.4 and later syntax.\nYour lef file is defined with version %.2f.\nEither update your VERSION number or use the 5.3 syntax.", lefData->versionNum);
                 lefError(1678, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        } else if (lefData->use5_3) {
           if (lefCallbacks->AntennaOutputCbk) { // write warning only if cbk is set 
              if (lefData->antennaOutputWarnings++ < lefSettings->AntennaOutputWarnings) {
                 lefData->outMsg = (char*)lefMalloc(10000);
                 sprintf (lefData->outMsg,
                    "ANTENNAOUTPUTDIFFAREA statement is a version 5.4 or later syntax.\nYour lef file with version %.2f, has both old and new ANTENNAOUTPUTDIFFAREA syntax, which is incorrect.", lefData->versionNum);
                 lefError(1679, lefData->outMsg);
                 lefFree(lefData->outMsg);
                 CHKERR();
              }
           }
        }
        if (lefCallbacks->AntennaOutputCbk)
          CALLBACK(lefCallbacks->AntennaOutputCbk, lefrAntennaOutputCbkType, (yyvsp[-1].dval));
    }
#line 14530 "lef.tab.c"
    break;

  case 1109: /* extension: K_BEGINEXT  */
#line 7657 "lef.y"
    { 
        if (lefCallbacks->ExtensionCbk)
          CALLBACK(lefCallbacks->ExtensionCbk, lefrExtensionCbkType, &lefData->Hist_text[0]);
        if (lefData->versionNum >= 5.6)
           lefData->ge56almostDone = 1;
    }
#line 14541 "lef.tab.c"
    break;


#line 14545 "lef.tab.c"

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
      yyerror (YY_("syntax error"));
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
                      yytoken, &yylval);
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
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
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
  yyerror (YY_("memory exhausted"));
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
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 7664 "lef.y"


END_LEFDEF_PARSER_NAMESPACE
