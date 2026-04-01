/* A Bison parser, made by GNU Bison 3.5.1.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.5.1"

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

#line 160 "def.tab.c"

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

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
#ifndef YY_DEFYY_DEF_TAB_H_INCLUDED
# define YY_DEFYY_DEF_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int defyydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    QSTRING = 258,
    T_STRING = 259,
    SITE_PATTERN = 260,
    NUMBER = 261,
    K_HISTORY = 262,
    K_NAME = 263,
    K_NAMESCASESENSITIVE = 264,
    K_DESIGN = 265,
    K_VIAS = 266,
    K_TECH = 267,
    K_UNITS = 268,
    K_ARRAY = 269,
    K_FLOORPLAN = 270,
    K_SITE = 271,
    K_CANPLACE = 272,
    K_CANNOTOCCUPY = 273,
    K_DIEAREA = 274,
    K_PINS = 275,
    K_PINSHAPE = 276,
    K_DEFAULTCAP = 277,
    K_MINPINS = 278,
    K_WIRECAP = 279,
    K_TRACKS = 280,
    K_GCELLGRID = 281,
    K_DO = 282,
    K_BY = 283,
    K_STEP = 284,
    K_LAYER = 285,
    K_ROW = 286,
    K_RECT = 287,
    K_COMPS = 288,
    K_COMP_GEN = 289,
    K_SOURCE = 290,
    K_WEIGHT = 291,
    K_EEQMASTER = 292,
    K_FIXED = 293,
    K_COVER = 294,
    K_UNPLACED = 295,
    K_PLACED = 296,
    K_FOREIGN = 297,
    K_REGION = 298,
    K_REGIONS = 299,
    K_NETS = 300,
    K_START_NET = 301,
    K_MUSTJOIN = 302,
    K_ORIGINAL = 303,
    K_USE = 304,
    K_STYLE = 305,
    K_PATTERN = 306,
    K_PATTERNNAME = 307,
    K_ESTCAP = 308,
    K_ROUTED = 309,
    K_NEW = 310,
    K_SNETS = 311,
    K_SHAPE = 312,
    K_WIDTH = 313,
    K_VOLTAGE = 314,
    K_SPACING = 315,
    K_NONDEFAULTRULE = 316,
    K_NONDEFAULTRULES = 317,
    K_NOFLOPS = 318,
    K_N = 319,
    K_S = 320,
    K_E = 321,
    K_W = 322,
    K_FN = 323,
    K_FE = 324,
    K_FS = 325,
    K_FW = 326,
    K_GROUPS = 327,
    K_GROUP = 328,
    K_SOFT = 329,
    K_MAXX = 330,
    K_MAXY = 331,
    K_MAXHALFPERIMETER = 332,
    K_CONSTRAINTS = 333,
    K_NET = 334,
    K_PATH = 335,
    K_SUM = 336,
    K_DIFF = 337,
    K_SCANCHAINS = 338,
    K_START = 339,
    K_FLOATING = 340,
    K_ORDERED = 341,
    K_STOP = 342,
    K_IN = 343,
    K_OUT = 344,
    K_RISEMIN = 345,
    K_RISEMAX = 346,
    K_FALLMIN = 347,
    K_FALLMAX = 348,
    K_WIREDLOGIC = 349,
    K_MAXDIST = 350,
    K_ASSERTIONS = 351,
    K_DISTANCE = 352,
    K_MICRONS = 353,
    K_NDR = 354,
    K_END = 355,
    K_POWERDOMAIN = 356,
    K_HINSTS = 357,
    K_IOTIMINGS = 358,
    K_RISE = 359,
    K_FALL = 360,
    K_VARIABLE = 361,
    K_SLEWRATE = 362,
    K_CAPACITANCE = 363,
    K_DRIVECELL = 364,
    K_FROMPIN = 365,
    K_TOPIN = 366,
    K_PARALLEL = 367,
    K_TIMINGDISABLES = 368,
    K_THRUPIN = 369,
    K_MACRO = 370,
    K_PARTITIONS = 371,
    K_TURNOFF = 372,
    K_COMPONENTS = 373,
    K_FROMCLOCKPIN = 374,
    K_FROMCOMPPIN = 375,
    K_FROMIOPIN = 376,
    K_TOCLOCKPIN = 377,
    K_TOCOMPPIN = 378,
    K_TOIOPIN = 379,
    K_SETUPRISE = 380,
    K_SETUPFALL = 381,
    K_HOLDRISE = 382,
    K_HOLDFALL = 383,
    K_VPIN = 384,
    K_SUBNET = 385,
    K_XTALK = 386,
    K_PIN = 387,
    K_SYNTHESIZED = 388,
    K_IF = 389,
    K_THEN = 390,
    K_ELSE = 391,
    K_FALSE = 392,
    K_TRUE = 393,
    K_EQ = 394,
    K_NE = 395,
    K_LE = 396,
    K_LT = 397,
    K_GE = 398,
    K_GT = 399,
    K_OR = 400,
    K_AND = 401,
    K_NOT = 402,
    K_SPECIAL = 403,
    K_DIRECTION = 404,
    K_RANGE = 405,
    K_WIRE = 406,
    K_FPC = 407,
    K_HORIZONTAL = 408,
    K_VERTICAL = 409,
    K_ALIGN = 410,
    K_MIN = 411,
    K_MAX = 412,
    K_EQUAL = 413,
    K_BOTTOMLEFT = 414,
    K_TOPRIGHT = 415,
    K_ROWS = 416,
    K_TAPER = 417,
    K_TAPERRULE = 418,
    K_VERSION = 419,
    K_DIVIDERCHAR = 420,
    K_BUSBITCHARS = 421,
    K_PROPERTYDEFINITIONS = 422,
    K_STRING = 423,
    K_REAL = 424,
    K_INTEGER = 425,
    K_PROPERTY = 426,
    K_BEGINEXT = 427,
    K_ENDEXT = 428,
    K_NAMEMAPSTRING = 429,
    K_ON = 430,
    K_OFF = 431,
    K_X = 432,
    K_Y = 433,
    K_COMPONENT = 434,
    K_MASK = 435,
    K_MASKSHIFT = 436,
    K_COMPSMASKSHIFT = 437,
    K_SAMEMASK = 438,
    K_PINPROPERTIES = 439,
    K_TEST = 440,
    K_ONLYBLOCKS = 441,
    K_COMMONSCANPINS = 442,
    K_SNET = 443,
    K_COMPONENTPIN = 444,
    K_REENTRANTPATHS = 445,
    K_SHIELD = 446,
    K_SHIELDNET = 447,
    K_NOSHIELD = 448,
    K_VIRTUAL = 449,
    K_ANTENNAPINPARTIALMETALAREA = 450,
    K_ANTENNAPINPARTIALMETALSIDEAREA = 451,
    K_ANTENNAPINGATEAREA = 452,
    K_ANTENNAPINDIFFAREA = 453,
    K_ANTENNAPINMAXAREACAR = 454,
    K_ANTENNAPINMAXSIDEAREACAR = 455,
    K_ANTENNAPINPARTIALCUTAREA = 456,
    K_ANTENNAPINMAXCUTCAR = 457,
    K_SIGNAL = 458,
    K_POWER = 459,
    K_GROUND = 460,
    K_CLOCK = 461,
    K_TIEOFF = 462,
    K_ANALOG = 463,
    K_SCAN = 464,
    K_RESET = 465,
    K_RING = 466,
    K_STRIPE = 467,
    K_FOLLOWPIN = 468,
    K_IOWIRE = 469,
    K_COREWIRE = 470,
    K_BLOCKWIRE = 471,
    K_FILLWIRE = 472,
    K_BLOCKAGEWIRE = 473,
    K_PADRING = 474,
    K_BLOCKRING = 475,
    K_BLOCKAGES = 476,
    K_PLACEMENT = 477,
    K_SLOTS = 478,
    K_FILLS = 479,
    K_PUSHDOWN = 480,
    K_NETLIST = 481,
    K_DIST = 482,
    K_USER = 483,
    K_TIMING = 484,
    K_BALANCED = 485,
    K_STEINER = 486,
    K_TRUNK = 487,
    K_FIXEDBUMP = 488,
    K_FENCE = 489,
    K_FREQUENCY = 490,
    K_GUIDE = 491,
    K_MAXBITS = 492,
    K_PARTITION = 493,
    K_TYPE = 494,
    K_ANTENNAMODEL = 495,
    K_DRCFILL = 496,
    K_OXIDE1 = 497,
    K_OXIDE2 = 498,
    K_OXIDE3 = 499,
    K_OXIDE4 = 500,
    K_OXIDE5 = 501,
    K_OXIDE6 = 502,
    K_OXIDE7 = 503,
    K_OXIDE8 = 504,
    K_OXIDE9 = 505,
    K_OXIDE10 = 506,
    K_OXIDE11 = 507,
    K_OXIDE12 = 508,
    K_OXIDE13 = 509,
    K_OXIDE14 = 510,
    K_OXIDE15 = 511,
    K_OXIDE16 = 512,
    K_OXIDE17 = 513,
    K_OXIDE18 = 514,
    K_OXIDE19 = 515,
    K_OXIDE20 = 516,
    K_OXIDE21 = 517,
    K_OXIDE22 = 518,
    K_OXIDE23 = 519,
    K_OXIDE24 = 520,
    K_OXIDE25 = 521,
    K_OXIDE26 = 522,
    K_OXIDE27 = 523,
    K_OXIDE28 = 524,
    K_OXIDE29 = 525,
    K_OXIDE30 = 526,
    K_OXIDE31 = 527,
    K_OXIDE32 = 528,
    K_CUTSIZE = 529,
    K_CUTSPACING = 530,
    K_DESIGNRULEWIDTH = 531,
    K_DIAGWIDTH = 532,
    K_ENCLOSURE = 533,
    K_HALO = 534,
    K_GROUNDSENSITIVITY = 535,
    K_PHYSICAL = 536,
    K_HARDSPACING = 537,
    K_LAYERS = 538,
    K_MINCUTS = 539,
    K_NETEXPR = 540,
    K_PINPROPERTY = 541,
    K_OFFSET = 542,
    K_ORIGIN = 543,
    K_ROWCOL = 544,
    K_STYLES = 545,
    K_SOFTFIXED = 546,
    K_POLYGON = 547,
    K_PORT = 548,
    K_SUPPLYSENSITIVITY = 549,
    K_VIA = 550,
    K_VIARULE = 551,
    K_WIREEXT = 552,
    K_EXCEPTPGNET = 553,
    K_ONLYPGNET = 554,
    K_FILLWIREOPC = 555,
    K_OPC = 556,
    K_PARTIAL = 557,
    K_ROUTEHALO = 558,
    K_BLOCKAGE = 559,
    K_ROUTE = 560,
    K_SCANCHAIN = 561,
    K_SPECIALROUTE = 562,
    K_TRACK = 563
  };
#endif

/* Value type.  */



int defyyparse (defrData *defData);

#endif /* !YY_DEFYY_DEF_TAB_H_INCLUDED  */



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
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
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

#if ! defined yyoverflow || YYERROR_VERBOSE

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
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


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

#define YYUNDEFTOK  2
#define YYMAXUTOK   563


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

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
    2868,  2868,  2876,  2877,  2880,  2880,  2894,  2894,  2901,  2901,
    2910,  2911,  2918,  2931,  2932,  2936,  2935,  2948,  2959,  2976,
    2976,  2994,  2994,  3004,  3007,  3010,  3021,  3032,  3035,  3038,
    3038,  3049,  3051,  3052,  3051,  3082,  3093,  3103,  3081,  3118,
    3117,  3126,  3132,  3134,  3136,  3138,  3140,  3144,  3143,  3154,
    3154,  3167,  3168,  3168,  3171,  3172,  3175,  3177,  3179,  3182,
    3187,  3192,  3197,  3211,  3211,  3231,  3232,  3251,  3251,  3264,
    3268,  3267,  3286,  3307,  3285,  3326,  3343,  3361,  3363,  3368,
    3379,  3394,  3402,  3414,  3438,  3469,  3498,  3522,  3523,  3525,
    3524,  3539,  3540,  3550,  3561,  3562,  3574,  3584,  3593,  3602,
    3610,  3620,  3630,  3640,  3651,  3661,  3670,  3679,  3689,  3698,
    3699,  3702,  3703,  3703,  3703,  3708,  3707,  3729,  3742,  3753,
    3753,  3765,  3789,  3790,  3794,  3802,  3830,  3829,  3852,  3851,
    3869,  3882,  3884,  3886,  3888,  3890,  3892,  3894,  3896,  3912,
    3914,  3924,  3926,  3929,  3932,  3933,  3937,  3956,  3957,  3961,
    3961,  3962,  3962,  3966,  3965,  3978,  3983,  3991,  3990,  3999,
    4000,  3999,  4062,  4062,  4131,  4132,  4131,  4183,  4194,  4197,
    4200,  4200,  4211,  4214,  4217,  4227,  4230,  4243,  4246,  4252,
    4258,  4264,  4264,  4277,  4278,  4282,  4282,  4291,  4291,  4309,
    4310,  4309,  4317,  4318,  4323,  4324,  4328,  4338,  4340,  4342,
    4353,  4363,  4365,  4367,  4378,  4379,  4379,  4442,  4442,  4479,
    4483,  4482,  4526,  4535,  4525,  4555,  4562,  4575,  4587,  4590,
    4596,  4597,  4600,  4606,  4606,  4617,  4618,  4621,  4628,  4629,
    4632,  4634,  4634,  4637,  4637,  4639,  4644,  4658,  4657,  4675,
    4674,  4692,  4691,  4708,  4709,  4712,  4719,  4720,  4723,  4730,
    4731,  4734,  4741,  4751,  4756,  4757,  4760,  4771,  4780,  4790,
    4791,  4794,  4802,  4810,  4819,  4826,  4830,  4833,  4847,  4861,
    4862,  4865,  4866,  4876,  4889,  4889,  4894,  4894,  4899,  4904,
    4910,  4911,  4913,  4915,  4915,  4924,  4925,  4928,  4929,  4932,
    4937,  4942,  4947,  4953,  4964,  4975,  4978,  4984,  4985,  4988,
    4994,  4994,  5003,  5004,  5009,  5010,  5013,  5013,  5021,  5020,
    5035,  5034,  5048,  5048,  5055,  5055,  5064,  5064,  5084,  5091,
    5095,  5090,  5121,  5122,  5121,  5145,  5146,  5155,  5169,  5170,
    5174,  5173,  5183,  5184,  5197,  5218,  5249,  5250,  5254,  5255,
    5259,  5262,  5259,  5277,  5278,  5291,  5312,  5344,  5345,  5348,
    5357,  5360,  5371,  5372,  5375,  5381,  5381,  5387,  5388,  5392,
    5397,  5402,  5407,  5408,  5407,  5416,  5423,  5424,  5422,  5430,
    5431,  5431,  5437,  5438,  5444,  5444,  5446,  5452,  5458,  5464,
    5465,  5468,  5469,  5468,  5473,  5475,  5478,  5480,  5482,  5484,
    5487,  5488,  5492,  5491,  5495,  5494,  5499,  5500,  5502,  5502,
    5504,  5504,  5507,  5511,  5518,  5519,  5522,  5523,  5522,  5531,
    5531,  5539,  5539,  5547,  5553,  5554,  5553,  5559,  5559,  5565,
    5572,  5575,  5582,  5583,  5586,  5592,  5592,  5598,  5599,  5606,
    5607,  5609,  5613,  5614,  5616,  5619,  5620,  5623,  5623,  5629,
    5629,  5635,  5635,  5641,  5641,  5647,  5647,  5653,  5653,  5658,
    5666,  5665,  5669,  5670,  5673,  5678,  5684,  5685,  5688,  5689,
    5691,  5693,  5695,  5697,  5701,  5702,  5705,  5708,  5711,  5714,
    5718,  5722,  5723,  5726,  5726,  5735,  5736,  5741,  5742,  5745,
    5744,  5762,  5762,  5765,  5767,  5769,  5771,  5774,  5776,  5791,
    5792,  5795,  5799,  5800,  5803,  5804,  5803,  5813,  5814,  5816,
    5816,  5820,  5821,  5824,  5835,  5844,  5854,  5856,  5860,  5864,
    5865,  5868,  5877,  5878,  5877,  5897,  5896,  5914,  5915,  5918,
    5944,  5971,  5978,  5978,  5990,  6003,  6002,  6022,  6027,  6027,
    6046,  6070,  6091,  6109,  6140,  6166,  6167,  6172,  6172,  6190,
    6190,  6203,  6202,  6222,  6240,  6258,  6296,  6315,  6356,  6373,
    6374,  6377,  6383,  6382,  6408,  6410,  6421,  6426,  6427,  6430,
    6438,  6439,  6438,  6446,  6447,  6450,  6456,  6455,  6468,  6470,
    6474,  6478,  6479,  6482,  6483,  6482,  6497,  6498,  6497,  6517,
    6518,  6521,  6535,  6534,  6561,  6562,  6564,  6565,  6566,  6571,
    6590,  6596,  6602,  6603,  6607,  6610,  6621,  6622,  6625,  6626,
    6627,  6631,  6630,  6652,  6651,  6674,  6694,  6704,  6714,  6717,
    6735,  6739,  6740,  6743,  6744,  6743,  6754,  6755,  6758,  6763,
    6765,  6763,  6772,  6772,  6778,  6778,  6784,  6784,  6790,  6793,
    6794,  6797,  6803,  6809,  6817,  6817,  6821,  6822,  6825,  6836,
    6845,  6856,  6858,  6879,  6883,  6884,  6888,  6887
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "QSTRING", "T_STRING", "SITE_PATTERN",
  "NUMBER", "K_HISTORY", "K_NAME", "K_NAMESCASESENSITIVE", "K_DESIGN",
  "K_VIAS", "K_TECH", "K_UNITS", "K_ARRAY", "K_FLOORPLAN", "K_SITE",
  "K_CANPLACE", "K_CANNOTOCCUPY", "K_DIEAREA", "K_PINS", "K_PINSHAPE",
  "K_DEFAULTCAP", "K_MINPINS", "K_WIRECAP", "K_TRACKS", "K_GCELLGRID",
  "K_DO", "K_BY", "K_STEP", "K_LAYER", "K_ROW", "K_RECT", "K_COMPS",
  "K_COMP_GEN", "K_SOURCE", "K_WEIGHT", "K_EEQMASTER", "K_FIXED",
  "K_COVER", "K_UNPLACED", "K_PLACED", "K_FOREIGN", "K_REGION",
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
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
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
     455,   456,   457,   458,   459,   460,   461,   462,   463,   464,
     465,   466,   467,   468,   469,   470,   471,   472,   473,   474,
     475,   476,   477,   478,   479,   480,   481,   482,   483,   484,
     485,   486,   487,   488,   489,   490,   491,   492,   493,   494,
     495,   496,   497,   498,   499,   500,   501,   502,   503,   504,
     505,   506,   507,   508,   509,   510,   511,   512,   513,   514,
     515,   516,   517,   518,   519,   520,   521,   522,   523,   524,
     525,   526,   527,   528,   529,   530,   531,   532,   533,   534,
     535,   536,   537,   538,   539,   540,   541,   542,   543,   544,
     545,   546,   547,   548,   549,   550,   551,   552,   553,   554,
     555,   556,   557,   558,   559,   560,   561,   562,   563,    59,
      45,    43,    40,    41,    42,    44
};
# endif

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
      -1,     2,     3,     4,     7,    12,    55,    56,    57,   118,
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

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
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

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
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

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
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


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


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

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



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

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value, defData); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, defrData *defData)
{
  FILE *yyoutput = yyo;
  YYUSE (yyoutput);
  YYUSE (defData);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yytype], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep, defrData *defData)
{
  YYFPRINTF (yyo, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyo, yytype, yyvaluep, defData);
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
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, int yyrule, defrData *defData)
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
                       yystos[+yyssp[yyi + 1 - yynrhs]],
                       &yyvsp[(yyi + 1) - (yynrhs)]
                                              , defData);
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
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
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


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
#  else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                yy_state_t *yyssp, int yytoken)
{
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Actual size of YYARG. */
  int yycount = 0;
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[+*yyssp];
      YYPTRDIFF_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
      yysize = yysize0;
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYPTRDIFF_T yysize1
                    = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
                    yysize = yysize1;
                  else
                    return 2;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    /* Don't count the "%s"s in the final size, but reserve room for
       the terminator.  */
    YYPTRDIFF_T yysize1 = yysize + (yystrlen (yyformat) - 2 * yycount) + 1;
    if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
      yysize = yysize1;
    else
      return 2;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, defrData *defData)
{
  YYUSE (yyvaluep);
  YYUSE (defData);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/*----------.
| yyparse.  |
`----------*/

int
yyparse (defrData *defData)
{
/* The lookahead symbol.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs;

    yy_state_fast_t yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss;
    yy_state_t *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYPTRDIFF_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
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

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
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
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
# undef YYSTACK_RELOCATE
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

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex (&yylval, defData);
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
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
  case 4:
#line 243 "def.y"
                { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3264 "def.tab.c"
    break;

  case 5:
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
#line 3305 "def.tab.c"
    break;

  case 7:
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
#line 3322 "def.tab.c"
    break;

  case 8:
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
#line 3343 "def.tab.c"
    break;

  case 51:
#line 336 "def.y"
                      {defData->dumb_mode = 1; defData->no_num = 1; }
#line 3349 "def.tab.c"
    break;

  case 52:
#line 337 "def.y"
      {
            if (defData->callbacks->DesignCbk)
              CALLBACK(defData->callbacks->DesignCbk, defrDesignStartCbkType, (yyvsp[-1].string));
            defData->hasDes = 1;
          }
#line 3359 "def.tab.c"
    break;

  case 53:
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
#line 3387 "def.tab.c"
    break;

  case 54:
#line 368 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3393 "def.tab.c"
    break;

  case 55:
#line 369 "def.y"
          { 
            if (defData->callbacks->TechnologyCbk)
              CALLBACK(defData->callbacks->TechnologyCbk, defrTechNameCbkType, (yyvsp[-1].string));
          }
#line 3402 "def.tab.c"
    break;

  case 56:
#line 374 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 3408 "def.tab.c"
    break;

  case 57:
#line 375 "def.y"
          { 
            if (defData->callbacks->ArrayNameCbk)
              CALLBACK(defData->callbacks->ArrayNameCbk, defrArrayNameCbkType, (yyvsp[-1].string));
          }
#line 3417 "def.tab.c"
    break;

  case 58:
#line 380 "def.y"
                            { defData->dumb_mode = 1; defData->no_num = 1; }
#line 3423 "def.tab.c"
    break;

  case 59:
#line 381 "def.y"
          { 
            if (defData->callbacks->FloorPlanNameCbk)
              CALLBACK(defData->callbacks->FloorPlanNameCbk, defrFloorPlanNameCbkType, (yyvsp[-1].string));
          }
#line 3432 "def.tab.c"
    break;

  case 60:
#line 387 "def.y"
          { 
            if (defData->callbacks->HistoryCbk)
              CALLBACK(defData->callbacks->HistoryCbk, defrHistoryCbkType, &defData->History_text[0]);
          }
#line 3441 "def.tab.c"
    break;

  case 61:
#line 393 "def.y"
          {
            if (defData->callbacks->PropDefStartCbk)
              CALLBACK(defData->callbacks->PropDefStartCbk, defrPropDefStartCbkType, 0);
          }
#line 3450 "def.tab.c"
    break;

  case 62:
#line 398 "def.y"
          { 
            if (defData->callbacks->PropDefEndCbk)
              CALLBACK(defData->callbacks->PropDefEndCbk, defrPropDefEndCbkType, 0);
            defData->real_num = 0;     // just want to make sure it is reset 
          }
#line 3460 "def.tab.c"
    break;

  case 64:
#line 406 "def.y"
            { }
#line 3466 "def.tab.c"
    break;

  case 65:
#line 408 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1; defData->Prop.clear(); }
#line 3472 "def.tab.c"
    break;

  case 66:
#line 410 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("design", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->DesignProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3484 "def.tab.c"
    break;

  case 67:
#line 417 "def.y"
                { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3490 "def.tab.c"
    break;

  case 68:
#line 419 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("net", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->NetProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3502 "def.tab.c"
    break;

  case 69:
#line 426 "def.y"
                 { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3508 "def.tab.c"
    break;

  case 70:
#line 428 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("specialnet", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->SNetProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3520 "def.tab.c"
    break;

  case 71:
#line 435 "def.y"
                   { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3526 "def.tab.c"
    break;

  case 72:
#line 437 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("region", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->RegionProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3538 "def.tab.c"
    break;

  case 73:
#line 444 "def.y"
                  { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3544 "def.tab.c"
    break;

  case 74:
#line 446 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("group", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->GroupProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3556 "def.tab.c"
    break;

  case 75:
#line 453 "def.y"
                      { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3562 "def.tab.c"
    break;

  case 76:
#line 455 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("component", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->CompProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3574 "def.tab.c"
    break;

  case 77:
#line 462 "def.y"
                { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3580 "def.tab.c"
    break;

  case 78:
#line 464 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("row", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->RowProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3592 "def.tab.c"
    break;

  case 79:
#line 473 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3598 "def.tab.c"
    break;

  case 80:
#line 475 "def.y"
            {
              if (defData->callbacks->PropCbk) {
                defData->Prop.setPropType("componentpin", (yyvsp[-2].string));
                CALLBACK(defData->callbacks->PropCbk, defrPropCbkType, &defData->Prop);
              }
              defData->session->CompPinProp.setPropType(defData->DEFCASE((yyvsp[-2].string)), defData->defPropDefType);
            }
#line 3610 "def.tab.c"
    break;

  case 81:
#line 483 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3616 "def.tab.c"
    break;

  case 82:
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
#line 3639 "def.tab.c"
    break;

  case 83:
#line 504 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3645 "def.tab.c"
    break;

  case 84:
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
#line 3663 "def.tab.c"
    break;

  case 85:
#line 520 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3669 "def.tab.c"
    break;

  case 86:
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
#line 3687 "def.tab.c"
    break;

  case 87:
#line 536 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3693 "def.tab.c"
    break;

  case 88:
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
#line 3711 "def.tab.c"
    break;

  case 89:
#line 552 "def.y"
          { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3717 "def.tab.c"
    break;

  case 90:
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
#line 3735 "def.tab.c"
    break;

  case 91:
#line 568 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3741 "def.tab.c"
    break;

  case 92:
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
#line 3759 "def.tab.c"
    break;

  case 93:
#line 584 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3765 "def.tab.c"
    break;

  case 94:
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
#line 3783 "def.tab.c"
    break;

  case 95:
#line 600 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3789 "def.tab.c"
    break;

  case 96:
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
#line 3807 "def.tab.c"
    break;

  case 97:
#line 616 "def.y"
            { defData->dumb_mode = 1 ; defData->no_num = 1; defData->Prop.clear(); }
#line 3813 "def.tab.c"
    break;

  case 98:
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
#line 3831 "def.tab.c"
    break;

  case 99:
#line 631 "def.y"
                    { yyerrok; yyclearin;}
#line 3837 "def.tab.c"
    break;

  case 100:
#line 633 "def.y"
                                 { defData->real_num = 0; }
#line 3843 "def.tab.c"
    break;

  case 101:
#line 634 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropInteger();
              defData->defPropDefType = 'I';
            }
#line 3852 "def.tab.c"
    break;

  case 102:
#line 638 "def.y"
                 { defData->real_num = 1; }
#line 3858 "def.tab.c"
    break;

  case 103:
#line 639 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropReal();
              defData->defPropDefType = 'R';
              defData->real_num = 0;
            }
#line 3868 "def.tab.c"
    break;

  case 104:
#line 645 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropString();
              defData->defPropDefType = 'S';
            }
#line 3877 "def.tab.c"
    break;

  case 105:
#line 650 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropQString((yyvsp[0].string));
              defData->defPropDefType = 'Q';
            }
#line 3886 "def.tab.c"
    break;

  case 106:
#line 655 "def.y"
            {
              if (defData->callbacks->PropCbk) defData->Prop.setPropNameMapString((yyvsp[0].string));
              defData->defPropDefType = 'S';
            }
#line 3895 "def.tab.c"
    break;

  case 108:
#line 662 "def.y"
            { if (defData->callbacks->PropCbk) defData->Prop.setNumber((yyvsp[0].dval)); }
#line 3901 "def.tab.c"
    break;

  case 109:
#line 665 "def.y"
          {
            if (defData->callbacks->UnitsCbk) {
              if (defData->defValidNum((int)(yyvsp[-1].dval)))
                CALLBACK(defData->callbacks->UnitsCbk,  defrUnitsCbkType, (yyvsp[-1].dval));
            }
          }
#line 3912 "def.tab.c"
    break;

  case 110:
#line 673 "def.y"
          {
            if (defData->callbacks->DividerCbk)
              CALLBACK(defData->callbacks->DividerCbk, defrDividerCbkType, (yyvsp[-1].string));
            defData->hasDivChar = 1;
          }
#line 3922 "def.tab.c"
    break;

  case 111:
#line 680 "def.y"
          { 
            if (defData->callbacks->BusBitCbk)
              CALLBACK(defData->callbacks->BusBitCbk, defrBusBitCbkType, (yyvsp[-1].string));
            defData->hasBusBit = 1;
          }
#line 3932 "def.tab.c"
    break;

  case 112:
#line 686 "def.y"
                     {defData->dumb_mode = 1;defData->no_num = 1; }
#line 3938 "def.tab.c"
    break;

  case 113:
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
#line 3953 "def.tab.c"
    break;

  case 114:
#line 698 "def.y"
                             {defData->dumb_mode = 1;defData->no_num = 1; }
#line 3959 "def.tab.c"
    break;

  case 115:
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
#line 3974 "def.tab.c"
    break;

  case 116:
#line 711 "def.y"
               {(yyval.integer) = 0;}
#line 3980 "def.tab.c"
    break;

  case 117:
#line 712 "def.y"
               {(yyval.integer) = 1;}
#line 3986 "def.tab.c"
    break;

  case 118:
#line 713 "def.y"
               {(yyval.integer) = 2;}
#line 3992 "def.tab.c"
    break;

  case 119:
#line 714 "def.y"
               {(yyval.integer) = 3;}
#line 3998 "def.tab.c"
    break;

  case 120:
#line 715 "def.y"
               {(yyval.integer) = 4;}
#line 4004 "def.tab.c"
    break;

  case 121:
#line 716 "def.y"
               {(yyval.integer) = 5;}
#line 4010 "def.tab.c"
    break;

  case 122:
#line 717 "def.y"
               {(yyval.integer) = 6;}
#line 4016 "def.tab.c"
    break;

  case 123:
#line 718 "def.y"
               {(yyval.integer) = 7;}
#line 4022 "def.tab.c"
    break;

  case 124:
#line 721 "def.y"
          {
            defData->Geometries.Reset();
          }
#line 4030 "def.tab.c"
    break;

  case 125:
#line 725 "def.y"
          {
            if (defData->callbacks->DieAreaCbk) {
               defData->DieArea.addPoint(&defData->Geometries);
               CALLBACK(defData->callbacks->DieAreaCbk, defrDieAreaCbkType, &(defData->DieArea));
            }
          }
#line 4041 "def.tab.c"
    break;

  case 126:
#line 734 "def.y"
            { }
#line 4047 "def.tab.c"
    break;

  case 127:
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
#line 4062 "def.tab.c"
    break;

  case 130:
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
#line 4076 "def.tab.c"
    break;

  case 131:
#line 764 "def.y"
            { }
#line 4082 "def.tab.c"
    break;

  case 132:
#line 767 "def.y"
            { }
#line 4088 "def.tab.c"
    break;

  case 133:
#line 770 "def.y"
          { 
            if (defData->callbacks->StartPinsCbk)
              CALLBACK(defData->callbacks->StartPinsCbk, defrStartPinsCbkType, ROUND((yyvsp[-1].dval)));
          }
#line 4097 "def.tab.c"
    break;

  case 136:
#line 779 "def.y"
         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 4103 "def.tab.c"
    break;

  case 137:
#line 780 "def.y"
         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 4109 "def.tab.c"
    break;

  case 138:
#line 781 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              defData->Pin.Setup((yyvsp[-4].string), (yyvsp[0].string));
            }
            defData->hasPort = 0;
            defData->hadPortOnce = 0;
          }
#line 4121 "def.tab.c"
    break;

  case 139:
#line 789 "def.y"
          { 
            if (defData->callbacks->PinCbk)
              CALLBACK(defData->callbacks->PinCbk, defrPinCbkType, &defData->Pin);
          }
#line 4130 "def.tab.c"
    break;

  case 142:
#line 798 "def.y"
          {
            if (defData->callbacks->PinCbk)
              defData->Pin.setSpecial();
          }
#line 4139 "def.tab.c"
    break;

  case 143:
#line 804 "def.y"
          { 
            if (defData->callbacks->PinExtCbk)
              CALLBACK(defData->callbacks->PinExtCbk, defrPinExtCbkType, &defData->History_text[0]);
          }
#line 4148 "def.tab.c"
    break;

  case 144:
#line 810 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.setDirection((yyvsp[0].string));
          }
#line 4157 "def.tab.c"
    break;

  case 145:
#line 816 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + PROPERTY {propName propVal}...")) {
                    CHKERR();
                }
            } 
            defData->dumb_mode = DEF_MAX_INT; 
          }
#line 4170 "def.tab.c"
    break;

  case 146:
#line 825 "def.y"
          {
            defData->dumb_mode = 0; 
          }
#line 4178 "def.tab.c"
    break;

  case 147:
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
#line 4206 "def.tab.c"
    break;

  case 148:
#line 854 "def.y"
                                  { defData->dumb_mode = 1; }
#line 4212 "def.tab.c"
    break;

  case 149:
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
#line 4235 "def.tab.c"
    break;

  case 150:
#line 874 "def.y"
                                  { defData->dumb_mode = 1; }
#line 4241 "def.tab.c"
    break;

  case 151:
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
#line 4264 "def.tab.c"
    break;

  case 152:
#line 895 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) defData->Pin.setUse((yyvsp[0].string));
          }
#line 4272 "def.tab.c"
    break;

  case 153:
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
#line 4299 "def.tab.c"
    break;

  case 154:
#line 922 "def.y"
                      { defData->dumb_mode = 1; }
#line 4305 "def.tab.c"
    break;

  case 155:
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
#line 4325 "def.tab.c"
    break;

  case 156:
#line 939 "def.y"
          {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
              if (defData->hasPort)
                 defData->Pin.addPortLayerPts((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
              else if (!defData->hadPortOnce)
                 defData->Pin.addLayerPts((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
            }
          }
#line 4338 "def.tab.c"
    break;

  case 157:
#line 948 "def.y"
                        { defData->dumb_mode = 1; }
#line 4344 "def.tab.c"
    break;

  case 158:
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
#line 4380 "def.tab.c"
    break;

  case 159:
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
#line 4395 "def.tab.c"
    break;

  case 160:
#line 991 "def.y"
                    { defData->dumb_mode = 1; }
#line 4401 "def.tab.c"
    break;

  case 161:
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
#line 4443 "def.tab.c"
    break;

  case 162:
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
#line 4471 "def.tab.c"
    break;

  case 163:
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
#line 4499 "def.tab.c"
    break;

  case 164:
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
#line 4521 "def.tab.c"
    break;

  case 165:
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
#line 4543 "def.tab.c"
    break;

  case 166:
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
#line 4565 "def.tab.c"
    break;

  case 167:
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
#line 4587 "def.tab.c"
    break;

  case 168:
#line 1152 "def.y"
                                                    {defData->dumb_mode=1;}
#line 4593 "def.tab.c"
    break;

  case 169:
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
#line 4615 "def.tab.c"
    break;

  case 170:
#line 1170 "def.y"
                                                        {defData->dumb_mode=1;}
#line 4621 "def.tab.c"
    break;

  case 171:
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
#line 4643 "def.tab.c"
    break;

  case 172:
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
#line 4665 "def.tab.c"
    break;

  case 173:
#line 1207 "def.y"
                                                   {defData->dumb_mode=1;}
#line 4671 "def.tab.c"
    break;

  case 174:
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
#line 4693 "def.tab.c"
    break;

  case 175:
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
#line 4713 "def.tab.c"
    break;

  case 177:
#line 1244 "def.y"
        {
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk) {
                defData->setPropDataType((yyvsp[0].prop), "PIN", defData->session->PinProp);
                defData->Pin.addProp((yyvsp[0].prop));
                (yyvsp[0].prop) = 0;
            }

            delete (yyvsp[0].prop);
        }
#line 4727 "def.tab.c"
    break;

  case 178:
#line 1254 "def.y"
        {
        }
#line 4734 "def.tab.c"
    break;

  case 179:
#line 1257 "def.y"
        {
            (yyval.prop) = (yyvsp[0].prop);
        }
#line 4742 "def.tab.c"
    break;

  case 180:
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
#line 4758 "def.tab.c"
    break;

  case 181:
#line 1276 "def.y"
        {
            (yyval.prop) = new defiProp(defData);
            (yyval.prop)->setPropQString((yyvsp[0].string));
            (yyval.prop)->setPropString();
            (yyval.prop)->setPropType("", (yyvsp[-1].string));
        }
#line 4769 "def.tab.c"
    break;

  case 183:
#line 1285 "def.y"
    {
        defData->addProp((yyvsp[0].prop));
    }
#line 4777 "def.tab.c"
    break;

  case 184:
#line 1290 "def.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 4785 "def.tab.c"
    break;

  case 185:
#line 1294 "def.y"
        {
            (yyval.string) = (yyvsp[0].string);
        }
#line 4793 "def.tab.c"
    break;

  case 186:
#line 1298 "def.y"
          { 
            (yyval.integer) = 0;
          }
#line 4801 "def.tab.c"
    break;

  case 187:
#line 1302 "def.y"
          {
            if (defData->VersionNum < 6.0 - 0.00001) {
                if (defData->def60NewSyntaxError("PINS ... + VIA viaName [MASK] orient pt")) {
                    CHKERR();
                }
            } 

            (yyval.integer) = (yyvsp[0].integer);
          }
#line 4815 "def.tab.c"
    break;

  case 189:
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
#line 4830 "def.tab.c"
    break;

  case 190:
#line 1326 "def.y"
        { (yyval.integer) = 0; }
#line 4836 "def.tab.c"
    break;

  case 191:
#line 1328 "def.y"
         { 
           if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->pinWarnings, defData->settings->PinWarnings)) {
             (yyval.integer) = (yyvsp[0].dval);
           }
         }
#line 4846 "def.tab.c"
    break;

  case 193:
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
#line 4861 "def.tab.c"
    break;

  case 195:
#line 1349 "def.y"
          {
          }
#line 4868 "def.tab.c"
    break;

  case 197:
#line 1354 "def.y"
    {}
#line 4874 "def.tab.c"
    break;

  case 199:
#line 1358 "def.y"
    {}
#line 4880 "def.tab.c"
    break;

  case 200:
#line 1361 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 4888 "def.tab.c"
    break;

  case 201:
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
#line 4914 "def.tab.c"
    break;

  case 203:
#line 1389 "def.y"
    {}
#line 4920 "def.tab.c"
    break;

  case 205:
#line 1393 "def.y"
    {}
#line 4926 "def.tab.c"
    break;

  case 206:
#line 1396 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 4934 "def.tab.c"
    break;

  case 207:
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
#line 4960 "def.tab.c"
    break;

  case 209:
#line 1424 "def.y"
    {}
#line 4966 "def.tab.c"
    break;

  case 211:
#line 1428 "def.y"
        {}
#line 4972 "def.tab.c"
    break;

  case 212:
#line 1431 "def.y"
          { 
            defData->dumb_mode = 2; 
          }
#line 4980 "def.tab.c"
    break;

  case 213:
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
#line 5006 "def.tab.c"
    break;

  case 214:
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
#line 5037 "def.tab.c"
    break;

  case 215:
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
#line 5068 "def.tab.c"
    break;

  case 217:
#line 1515 "def.y"
          {
          }
#line 5075 "def.tab.c"
    break;

  case 218:
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
#line 5106 "def.tab.c"
    break;

  case 219:
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
#line 5137 "def.tab.c"
    break;

  case 220:
#line 1575 "def.y"
          { defData->aOxide = 1;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5146 "def.tab.c"
    break;

  case 221:
#line 1580 "def.y"
          { defData->aOxide = 2;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5155 "def.tab.c"
    break;

  case 222:
#line 1585 "def.y"
          { defData->aOxide = 3;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5164 "def.tab.c"
    break;

  case 223:
#line 1590 "def.y"
          { defData->aOxide = 4;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5173 "def.tab.c"
    break;

  case 224:
#line 1595 "def.y"
          { defData->aOxide = 5;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5182 "def.tab.c"
    break;

  case 225:
#line 1600 "def.y"
          { defData->aOxide = 6;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5191 "def.tab.c"
    break;

  case 226:
#line 1605 "def.y"
          { defData->aOxide = 7;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5200 "def.tab.c"
    break;

  case 227:
#line 1610 "def.y"
          { defData->aOxide = 8;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5209 "def.tab.c"
    break;

  case 228:
#line 1615 "def.y"
          { defData->aOxide = 9;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5218 "def.tab.c"
    break;

  case 229:
#line 1620 "def.y"
          { defData->aOxide = 10;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5227 "def.tab.c"
    break;

  case 230:
#line 1625 "def.y"
          { defData->aOxide = 11;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5236 "def.tab.c"
    break;

  case 231:
#line 1630 "def.y"
          { defData->aOxide = 12;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5245 "def.tab.c"
    break;

  case 232:
#line 1635 "def.y"
          { defData->aOxide = 13;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5254 "def.tab.c"
    break;

  case 233:
#line 1640 "def.y"
          { defData->aOxide = 14;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5263 "def.tab.c"
    break;

  case 234:
#line 1645 "def.y"
          { defData->aOxide = 15;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5272 "def.tab.c"
    break;

  case 235:
#line 1650 "def.y"
          { defData->aOxide = 16;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5281 "def.tab.c"
    break;

  case 236:
#line 1655 "def.y"
          { defData->aOxide = 17;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5290 "def.tab.c"
    break;

  case 237:
#line 1660 "def.y"
          { defData->aOxide = 18;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5299 "def.tab.c"
    break;

  case 238:
#line 1665 "def.y"
          { defData->aOxide = 19;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5308 "def.tab.c"
    break;

  case 239:
#line 1670 "def.y"
          { defData->aOxide = 20;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5317 "def.tab.c"
    break;

  case 240:
#line 1675 "def.y"
          { defData->aOxide = 21;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5326 "def.tab.c"
    break;

  case 241:
#line 1680 "def.y"
          { defData->aOxide = 22;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5335 "def.tab.c"
    break;

  case 242:
#line 1685 "def.y"
          { defData->aOxide = 23;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5344 "def.tab.c"
    break;

  case 243:
#line 1690 "def.y"
          { defData->aOxide = 24;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5353 "def.tab.c"
    break;

  case 244:
#line 1695 "def.y"
          { defData->aOxide = 25;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5362 "def.tab.c"
    break;

  case 245:
#line 1700 "def.y"
          { defData->aOxide = 26;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5371 "def.tab.c"
    break;

  case 246:
#line 1705 "def.y"
          { defData->aOxide = 27;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5380 "def.tab.c"
    break;

  case 247:
#line 1710 "def.y"
          { defData->aOxide = 28;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5389 "def.tab.c"
    break;

  case 248:
#line 1715 "def.y"
          { defData->aOxide = 29;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5398 "def.tab.c"
    break;

  case 249:
#line 1720 "def.y"
          { defData->aOxide = 30;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5407 "def.tab.c"
    break;

  case 250:
#line 1725 "def.y"
          { defData->aOxide = 31;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5416 "def.tab.c"
    break;

  case 251:
#line 1730 "def.y"
          { defData->aOxide = 32;
            if (defData->callbacks->PinCbk || defData->callbacks->PinExtCbk)
              defData->Pin.addAntennaModel(defData->aOxide);
          }
#line 5425 "def.tab.c"
    break;

  case 252:
#line 1737 "def.y"
          { (yyval.string) = (char*)"SIGNAL"; }
#line 5431 "def.tab.c"
    break;

  case 253:
#line 1739 "def.y"
          { (yyval.string) = (char*)"POWER"; }
#line 5437 "def.tab.c"
    break;

  case 254:
#line 1741 "def.y"
          { (yyval.string) = (char*)"GROUND"; }
#line 5443 "def.tab.c"
    break;

  case 255:
#line 1743 "def.y"
          { (yyval.string) = (char*)"CLOCK"; }
#line 5449 "def.tab.c"
    break;

  case 256:
#line 1745 "def.y"
          { (yyval.string) = (char*)"TIEOFF"; }
#line 5455 "def.tab.c"
    break;

  case 257:
#line 1747 "def.y"
          { (yyval.string) = (char*)"ANALOG"; }
#line 5461 "def.tab.c"
    break;

  case 258:
#line 1749 "def.y"
          { (yyval.string) = (char*)"SCAN"; }
#line 5467 "def.tab.c"
    break;

  case 259:
#line 1751 "def.y"
          { (yyval.string) = (char*)"RESET"; }
#line 5473 "def.tab.c"
    break;

  case 260:
#line 1755 "def.y"
          { (yyval.string) = (char*)""; }
#line 5479 "def.tab.c"
    break;

  case 261:
#line 1756 "def.y"
                  {defData->dumb_mode=1;}
#line 5485 "def.tab.c"
    break;

  case 262:
#line 1757 "def.y"
          { (yyval.string) = (yyvsp[0].string); }
#line 5491 "def.tab.c"
    break;

  case 263:
#line 1760 "def.y"
        { 
          if (defData->callbacks->PinEndCbk)
            CALLBACK(defData->callbacks->PinEndCbk, defrPinEndCbkType, 0);
        }
#line 5500 "def.tab.c"
    break;

  case 264:
#line 1765 "def.y"
                {defData->dumb_mode = 2; defData->no_num = 2; }
#line 5506 "def.tab.c"
    break;

  case 265:
#line 1767 "def.y"
        {
          if (defData->callbacks->RowCbk) {
            defData->rowName = (yyvsp[-4].string);
            defData->Row.setup((yyvsp[-4].string), (yyvsp[-3].string), (yyvsp[-2].dval), (yyvsp[-1].dval), (yyvsp[0].integer));
          }
        }
#line 5517 "def.tab.c"
    break;

  case 266:
#line 1775 "def.y"
        {
          if (defData->callbacks->RowCbk) 
            CALLBACK(defData->callbacks->RowCbk, defrRowCbkType, &defData->Row);
        }
#line 5526 "def.tab.c"
    break;

  case 267:
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
#line 5541 "def.tab.c"
    break;

  case 268:
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
#line 5581 "def.tab.c"
    break;

  case 269:
#line 1829 "def.y"
        {
          defData->hasDoStep = 0;
        }
#line 5589 "def.tab.c"
    break;

  case 270:
#line 1833 "def.y"
        {
          defData->hasDoStep = 1;
          defData->Row.setHasDoStep();
          defData->xStep = (yyvsp[-1].dval);
          defData->yStep = (yyvsp[0].dval);
        }
#line 5600 "def.tab.c"
    break;

  case 273:
#line 1844 "def.y"
                            {defData->dumb_mode = DEF_MAX_INT; }
#line 5606 "def.tab.c"
    break;

  case 274:
#line 1846 "def.y"
         { defData->dumb_mode = 0; }
#line 5612 "def.tab.c"
    break;

  case 277:
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
#line 5628 "def.tab.c"
    break;

  case 278:
#line 1865 "def.y"
        {
          if (defData->callbacks->RowCbk) {
             char propTp;
             propTp =  defData->session->RowProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "ROW");
             defData->Row.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 5641 "def.tab.c"
    break;

  case 279:
#line 1874 "def.y"
        {
          if (defData->callbacks->RowCbk) {
             char propTp;
             propTp =  defData->session->RowProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "ROW");
             defData->Row.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 5654 "def.tab.c"
    break;

  case 280:
#line 1884 "def.y"
        {
          if (defData->callbacks->TrackCbk) {
            defData->Track.setup((yyvsp[-1].string));
          }
        }
#line 5664 "def.tab.c"
    break;

  case 281:
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
#line 5695 "def.tab.c"
    break;

  case 282:
#line 1918 "def.y"
        {
          (yyval.string) = (yyvsp[0].string);
        }
#line 5703 "def.tab.c"
    break;

  case 283:
#line 1923 "def.y"
            { (yyval.string) = (char*)"X";}
#line 5709 "def.tab.c"
    break;

  case 284:
#line 1925 "def.y"
            { (yyval.string) = (char*)"Y";}
#line 5715 "def.tab.c"
    break;

  case 285:
#line 1933 "def.y"
    {}
#line 5721 "def.tab.c"
    break;

  case 287:
#line 1937 "def.y"
    {}
#line 5727 "def.tab.c"
    break;

  case 289:
#line 1942 "def.y"
    {}
#line 5733 "def.tab.c"
    break;

  case 290:
#line 1945 "def.y"
           { 
                defData->dumb_mode = 2; 
           }
#line 5741 "def.tab.c"
    break;

  case 291:
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
#line 5761 "def.tab.c"
    break;

  case 293:
#line 1966 "def.y"
                {defData->dumb_mode=1;}
#line 5767 "def.tab.c"
    break;

  case 294:
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
#line 5783 "def.tab.c"
    break;

  case 296:
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
#line 5799 "def.tab.c"
    break;

  case 298:
#line 1995 "def.y"
           { 
              if (defData->validateMaskInput((int)(yyvsp[-1].dval), defData->trackWarnings, defData->settings->TrackWarnings)) {
                  if (defData->callbacks->TrackCbk) {
                    defData->Track.addMask((yyvsp[-1].dval), (yyvsp[0].integer));
                  }
               }
            }
#line 5811 "def.tab.c"
    break;

  case 299:
#line 2005 "def.y"
        { (yyval.integer) = 0; }
#line 5817 "def.tab.c"
    break;

  case 300:
#line 2007 "def.y"
        { (yyval.integer) = 1; }
#line 5823 "def.tab.c"
    break;

  case 302:
#line 2010 "def.y"
                  { defData->dumb_mode = 1000; }
#line 5829 "def.tab.c"
    break;

  case 303:
#line 2011 "def.y"
            { defData->dumb_mode = 0; }
#line 5835 "def.tab.c"
    break;

  case 306:
#line 2018 "def.y"
        {
          if (defData->callbacks->TrackCbk)
            defData->Track.addLayer((yyvsp[0].string));
        }
#line 5844 "def.tab.c"
    break;

  case 307:
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
#line 5875 "def.tab.c"
    break;

  case 308:
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
#line 5889 "def.tab.c"
    break;

  case 309:
#line 2064 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) { 
                if (defData->def60ObsoletedError("+ BEGINEXT ... ENDEXT")) {
                    CHKERR();
                }
            }
        }
#line 5901 "def.tab.c"
    break;

  case 311:
#line 2076 "def.y"
        {
          if (defData->callbacks->ViaStartCbk)
            CALLBACK(defData->callbacks->ViaStartCbk, defrViaStartCbkType, ROUND((yyvsp[-1].dval)));
        }
#line 5910 "def.tab.c"
    break;

  case 314:
#line 2085 "def.y"
                     {defData->dumb_mode = 1;defData->no_num = 1; }
#line 5916 "def.tab.c"
    break;

  case 315:
#line 2086 "def.y"
            {
              if (defData->callbacks->ViaCbk) defData->Via.setup((yyvsp[0].string));
              defData->viaRule = 0;
            }
#line 5925 "def.tab.c"
    break;

  case 316:
#line 2091 "def.y"
            {
              if (defData->callbacks->ViaCbk)
                CALLBACK(defData->callbacks->ViaCbk, defrViaCbkType, &defData->Via);
              defData->Via.clear();
            }
#line 5935 "def.tab.c"
    break;

  case 319:
#line 2101 "def.y"
                       {defData->dumb_mode = 1;defData->no_num = 1; }
#line 5941 "def.tab.c"
    break;

  case 320:
#line 2102 "def.y"
        { 
            if (defData->callbacks->ViaCbk)
            if (defData->validateMaskInput((yyvsp[-2].integer), defData->viaWarnings, defData->settings->ViaWarnings)) {
                defData->Via.addLayer((yyvsp[-3].string), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y, (yyvsp[-2].integer));
            }
        }
#line 5952 "def.tab.c"
    break;

  case 321:
#line 2109 "def.y"
        { 
            defData->dumb_mode = 2; 
        }
#line 5960 "def.tab.c"
    break;

  case 322:
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
#line 5980 "def.tab.c"
    break;

  case 323:
#line 2128 "def.y"
                        { defData->dumb_mode = 1; }
#line 5986 "def.tab.c"
    break;

  case 324:
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
#line 6008 "def.tab.c"
    break;

  case 325:
#line 2147 "def.y"
            {
              if (defData->VersionNum >= 5.6) {  // only add if 5.6 or beyond
                if (defData->callbacks->ViaCbk)
                  if (defData->validateMaskInput((yyvsp[-5].integer), defData->viaWarnings, defData->settings->ViaWarnings)) {
                    defData->Via.addPolygon((yyvsp[-6].string), &defData->Geometries, (yyvsp[-5].integer));
                  }
              }
            }
#line 6021 "def.tab.c"
    break;

  case 326:
#line 2155 "def.y"
                            {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6027 "def.tab.c"
    break;

  case 327:
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
#line 6041 "def.tab.c"
    break;

  case 328:
#line 2165 "def.y"
                        {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6047 "def.tab.c"
    break;

  case 329:
#line 2167 "def.y"
                       {defData->dumb_mode = 3;defData->no_num = 1; }
#line 6053 "def.tab.c"
    break;

  case 330:
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
#line 6078 "def.tab.c"
    break;

  case 332:
#line 2192 "def.y"
          { 
            if (defData->callbacks->ViaExtCbk)
              CALLBACK(defData->callbacks->ViaExtCbk, defrViaExtCbkType, &defData->History_text[0]);
          }
#line 6087 "def.tab.c"
    break;

  case 333:
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
#line 6103 "def.tab.c"
    break;

  case 334:
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
#line 6119 "def.tab.c"
    break;

  case 335:
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
#line 6135 "def.tab.c"
    break;

  case 336:
#line 2233 "def.y"
                        {defData->dumb_mode = 1;defData->no_num = 1; }
#line 6141 "def.tab.c"
    break;

  case 337:
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
#line 6157 "def.tab.c"
    break;

  case 338:
#line 2247 "def.y"
          { defData->Geometries.startList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6163 "def.tab.c"
    break;

  case 339:
#line 2250 "def.y"
          { defData->Geometries.addToList((yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6169 "def.tab.c"
    break;

  case 342:
#line 2257 "def.y"
          {
            defData->save_x = (yyvsp[-2].dval);
            defData->save_y = (yyvsp[-1].dval);
            (yyval.pt).x = ROUND((yyvsp[-2].dval));
            (yyval.pt).y = ROUND((yyvsp[-1].dval));
          }
#line 6180 "def.tab.c"
    break;

  case 343:
#line 2264 "def.y"
          {
            defData->save_y = (yyvsp[-1].dval);
            (yyval.pt).x = ROUND(defData->save_x);
            (yyval.pt).y = ROUND((yyvsp[-1].dval));
          }
#line 6190 "def.tab.c"
    break;

  case 344:
#line 2270 "def.y"
          {
            defData->save_x = (yyvsp[-2].dval);
            (yyval.pt).x = ROUND((yyvsp[-2].dval));
            (yyval.pt).y = ROUND(defData->save_y);
          }
#line 6200 "def.tab.c"
    break;

  case 345:
#line 2276 "def.y"
          {
            (yyval.pt).x = ROUND(defData->save_x);
            (yyval.pt).y = ROUND(defData->save_y);
          }
#line 6209 "def.tab.c"
    break;

  case 346:
#line 2282 "def.y"
      { (yyval.integer) = 0; }
#line 6215 "def.tab.c"
    break;

  case 347:
#line 2284 "def.y"
      { (yyval.integer) = (yyvsp[0].dval); }
#line 6221 "def.tab.c"
    break;

  case 348:
#line 2287 "def.y"
        { 
          if (defData->callbacks->ViaEndCbk)
            CALLBACK(defData->callbacks->ViaEndCbk, defrViaEndCbkType, 0);
        }
#line 6230 "def.tab.c"
    break;

  case 349:
#line 2293 "def.y"
        {
          if (defData->callbacks->RegionEndCbk)
            CALLBACK(defData->callbacks->RegionEndCbk, defrRegionEndCbkType, 0);
        }
#line 6239 "def.tab.c"
    break;

  case 350:
#line 2299 "def.y"
        {
          if (defData->callbacks->RegionStartCbk)
            CALLBACK(defData->callbacks->RegionStartCbk, defrRegionStartCbkType, ROUND((yyvsp[-1].dval)));
        }
#line 6248 "def.tab.c"
    break;

  case 352:
#line 2306 "def.y"
            {}
#line 6254 "def.tab.c"
    break;

  case 353:
#line 2308 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 6260 "def.tab.c"
    break;

  case 354:
#line 2309 "def.y"
        {
          if (defData->callbacks->RegionCbk)
             defData->Region.setup((yyvsp[0].string));
          defData->regTypeDef = 0;
        }
#line 6270 "def.tab.c"
    break;

  case 355:
#line 2315 "def.y"
        { CALLBACK(defData->callbacks->RegionCbk, defrRegionCbkType, &defData->Region); }
#line 6276 "def.tab.c"
    break;

  case 356:
#line 2319 "def.y"
        { if (defData->callbacks->RegionCbk)
          defData->Region.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6283 "def.tab.c"
    break;

  case 357:
#line 2322 "def.y"
        { if (defData->callbacks->RegionCbk)
          defData->Region.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y); }
#line 6290 "def.tab.c"
    break;

  case 360:
#line 2330 "def.y"
                               {defData->dumb_mode = DEF_MAX_INT; }
#line 6296 "def.tab.c"
    break;

  case 361:
#line 2332 "def.y"
         { defData->dumb_mode = 0; }
#line 6302 "def.tab.c"
    break;

  case 362:
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
#line 6319 "def.tab.c"
    break;

  case 365:
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
#line 6338 "def.tab.c"
    break;

  case 366:
#line 2368 "def.y"
        {
          if (defData->callbacks->RegionCbk) {
             char propTp;
             propTp = defData->session->RegionProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "REGION");
             defData->Region.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6351 "def.tab.c"
    break;

  case 367:
#line 2377 "def.y"
        {
          if (defData->callbacks->RegionCbk) {
             char propTp;
             propTp = defData->session->RegionProp.propType((yyvsp[-1].string));
             CHKPROPTYPE(propTp, (yyvsp[-1].string), "REGION");
             defData->Region.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6364 "def.tab.c"
    break;

  case 368:
#line 2387 "def.y"
            { (yyval.string) = (char*)"FENCE"; }
#line 6370 "def.tab.c"
    break;

  case 369:
#line 2389 "def.y"
            { (yyval.string) = (char*)"GUIDE"; }
#line 6376 "def.tab.c"
    break;

  case 370:
#line 2392 "def.y"
         {
           defData->dumb_mode = DEF_MAX_INT; 
           defData->no_num = DEF_MAX_INT;
         }
#line 6385 "def.tab.c"
    break;

  case 371:
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
#line 6408 "def.tab.c"
    break;

  case 373:
#line 2420 "def.y"
         {
            defData->Component = new defiComponent(defData);

            if (defData->callbacks->ComponentStartCbk) {
                CALLBACK(defData->callbacks->ComponentStartCbk,
                         defrComponentStartCbkType, ROUND((yyvsp[-1].dval)));
            }
         }
#line 6421 "def.tab.c"
    break;

  case 376:
#line 2434 "def.y"
        {
            if (defData->callbacks->ComponentMaskShiftLayerCbk) {
              defData->ComponentMaskShiftLayer.addMaskShiftLayer((yyvsp[0].string));
            }
        }
#line 6431 "def.tab.c"
    break;

  case 379:
#line 2445 "def.y"
         {
            if (defData->callbacks->ComponentCbk) {
                CALLBACK(defData->callbacks->ComponentCbk,
                         defrComponentCbkType, defData->Component);

                defData->Component->clear();
            }
         }
#line 6444 "def.tab.c"
    break;

  case 380:
#line 2455 "def.y"
         {
            defData->dumb_mode = 0;
            defData->no_num = 0;
         }
#line 6453 "def.tab.c"
    break;

  case 381:
#line 2460 "def.y"
                      {defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT; }
#line 6459 "def.tab.c"
    break;

  case 382:
#line 2462 "def.y"
         {
            if (defData->callbacks->ComponentCbk)
              defData->Component->IdAndName((yyvsp[-1].string), (yyvsp[0].string));
         }
#line 6468 "def.tab.c"
    break;

  case 383:
#line 2468 "def.y"
        { }
#line 6474 "def.tab.c"
    break;

  case 384:
#line 2470 "def.y"
            {
              if (defData->callbacks->ComponentCbk)
                defData->Component->addNet("*");
            }
#line 6483 "def.tab.c"
    break;

  case 385:
#line 2475 "def.y"
            {
              if (defData->callbacks->ComponentCbk)
                defData->Component->addNet((yyvsp[0].string));
            }
#line 6492 "def.tab.c"
    break;

  case 402:
#line 2490 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            CALLBACK(defData->callbacks->ComponentExtCbk, defrComponentExtCbkType,
                     &defData->History_text[0]);
        }
#line 6502 "def.tab.c"
    break;

  case 403:
#line 2496 "def.y"
                          {defData->dumb_mode=1; defData->no_num = 1; }
#line 6508 "def.tab.c"
    break;

  case 404:
#line 2497 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setEEQ((yyvsp[0].string));
        }
#line 6517 "def.tab.c"
    break;

  case 405:
#line 2502 "def.y"
                              { defData->dumb_mode = 2;  defData->no_num = 2; }
#line 6523 "def.tab.c"
    break;

  case 406:
#line 2504 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
             defData->Component->setGenerate((yyvsp[-1].string), (yyvsp[0].string));
        }
#line 6532 "def.tab.c"
    break;

  case 407:
#line 2510 "def.y"
      { (yyval.string) = (char*)""; }
#line 6538 "def.tab.c"
    break;

  case 408:
#line 2512 "def.y"
      { (yyval.string) = (yyvsp[0].string); }
#line 6544 "def.tab.c"
    break;

  case 409:
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
#line 6558 "def.tab.c"
    break;

  case 410:
#line 2526 "def.y"
            { (yyval.string) = (char*)"NETLIST"; }
#line 6564 "def.tab.c"
    break;

  case 411:
#line 2528 "def.y"
            { (yyval.string) = (char*)"DIST"; }
#line 6570 "def.tab.c"
    break;

  case 412:
#line 2530 "def.y"
            { (yyval.string) = (char*)"USER"; }
#line 6576 "def.tab.c"
    break;

  case 413:
#line 2532 "def.y"
            { (yyval.string) = (char*)"TIMING"; }
#line 6582 "def.tab.c"
    break;

  case 414:
#line 2537 "def.y"
        { }
#line 6588 "def.tab.c"
    break;

  case 415:
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
#line 6602 "def.tab.c"
    break;

  case 416:
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
#line 6617 "def.tab.c"
    break;

  case 417:
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
#line 6632 "def.tab.c"
    break;

  case 418:
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
#line 6651 "def.tab.c"
    break;

  case 419:
#line 2588 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setHalo((int)(yyvsp[-3].dval), (int)(yyvsp[-2].dval),
                                                 (int)(yyvsp[-1].dval), (int)(yyvsp[0].dval));
        }
#line 6661 "def.tab.c"
    break;

  case 421:
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
#line 6683 "def.tab.c"
    break;

  case 422:
#line 2615 "def.y"
                                       { defData->dumb_mode = 2; defData->no_num = 2; }
#line 6689 "def.tab.c"
    break;

  case 423:
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
#line 6712 "def.tab.c"
    break;

  case 424:
#line 2635 "def.y"
                              { defData->dumb_mode = DEF_MAX_INT; }
#line 6718 "def.tab.c"
    break;

  case 425:
#line 2637 "def.y"
      { defData->dumb_mode = 0; }
#line 6724 "def.tab.c"
    break;

  case 428:
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
#line 6739 "def.tab.c"
    break;

  case 429:
#line 2655 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            char propTp;
            propTp = defData->session->CompProp.propType((yyvsp[-1].string));
            CHKPROPTYPE(propTp, (yyvsp[-1].string), "COMPONENT");
            defData->Component->addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6752 "def.tab.c"
    break;

  case 430:
#line 2664 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            char propTp;
            propTp = defData->session->CompProp.propType((yyvsp[-1].string));
            CHKPROPTYPE(propTp, (yyvsp[-1].string), "COMPONENT");
            defData->Component->addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
          }
        }
#line 6765 "def.tab.c"
    break;

  case 431:
#line 2674 "def.y"
        { defData->dumb_mode = 1; defData->no_num = 1; }
#line 6771 "def.tab.c"
    break;

  case 432:
#line 2676 "def.y"
                            { defData->dumb_mode = 1; defData->no_num = 1; }
#line 6777 "def.tab.c"
    break;

  case 433:
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
#line 6793 "def.tab.c"
    break;

  case 434:
#line 2692 "def.y"
         { (yyval.pt) = (yyvsp[0].pt); }
#line 6799 "def.tab.c"
    break;

  case 435:
#line 2694 "def.y"
         { (yyval.pt).x = ROUND((yyvsp[-1].dval)); (yyval.pt).y = ROUND((yyvsp[0].dval)); }
#line 6805 "def.tab.c"
    break;

  case 436:
#line 2697 "def.y"
        {
          if (defData->callbacks->ComponentCbk) {
            defData->Component->setPlacementStatus((yyvsp[-2].integer));
            defData->Component->setPlacementLocation((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer));
          }
        }
#line 6816 "def.tab.c"
    break;

  case 437:
#line 2704 "def.y"
        {
          if (defData->callbacks->ComponentCbk)
            defData->Component->setPlacementStatus(
                                         DEFI_COMPONENT_UNPLACED);
            defData->Component->setPlacementLocation(-1, -1, -1);
        }
#line 6827 "def.tab.c"
    break;

  case 438:
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
#line 6844 "def.tab.c"
    break;

  case 439:
#line 2725 "def.y"
                           { defData->dumb_mode = 1; defData->no_num = 1; }
#line 6850 "def.tab.c"
    break;

  case 440:
#line 2726 "def.y"
        {  
          if (defData->callbacks->ComponentCbk) {
            if (defData->validateMaskShiftInput((yyvsp[0].string), defData->componentWarnings, defData->settings->ComponentWarnings)) {
                defData->Component->setMaskShift((yyvsp[0].string));
            }
          }
        }
#line 6862 "def.tab.c"
    break;

  case 441:
#line 2735 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_FIXED; }
#line 6868 "def.tab.c"
    break;

  case 442:
#line 2737 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_COVER; }
#line 6874 "def.tab.c"
    break;

  case 443:
#line 2739 "def.y"
        { (yyval.integer) = DEFI_COMPONENT_PLACED; }
#line 6880 "def.tab.c"
    break;

  case 444:
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
#line 6896 "def.tab.c"
    break;

  case 445:
#line 2753 "def.y"
                                {defData->dumb_mode = 3;}
#line 6902 "def.tab.c"
    break;

  case 446:
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
#line 6922 "def.tab.c"
    break;

  case 447:
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
#line 6938 "def.tab.c"
    break;

  case 448:
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
#line 6952 "def.tab.c"
    break;

  case 449:
#line 2795 "def.y"
        { 
            if (defData->callbacks->ComponentEndCbk) {
                  CALLBACK(defData->callbacks->ComponentEndCbk,
                           defrComponentEndCbkType, 0);
            }

            delete defData->Component;
            defData->Component = NULL;
        }
#line 6966 "def.tab.c"
    break;

  case 451:
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
#line 6983 "def.tab.c"
    break;

  case 454:
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
#line 6999 "def.tab.c"
    break;

  case 455:
#line 2846 "def.y"
        {defData->dumb_mode = 0; defData->no_num = 0; }
#line 7005 "def.tab.c"
    break;

  case 456:
#line 2850 "def.y"
        {
            defData->dumb_mode = DEF_MAX_INT; 
            defData->no_num = DEF_MAX_INT; 
            defData->nondef_is_keyword = TRUE; 
            defData->mustjoin_is_keyword = TRUE;
            defData->routeStatus = (char*)"ROUTED";
            defData->shieldName = NULL;
        }
#line 7018 "def.tab.c"
    break;

  case 458:
#line 2860 "def.y"
        {
          // 9/22/1999 
          // this is shared by both net and special net 
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->setName((yyvsp[0].string));
          if (defData->callbacks->NetNameCbk)
            CALLBACK(defData->callbacks->NetNameCbk, defrNetNameCbkType, (yyvsp[0].string));
        }
#line 7031 "def.tab.c"
    break;

  case 460:
#line 2868 "def.y"
                                  {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7037 "def.tab.c"
    break;

  case 461:
#line 2869 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addMustPin((yyvsp[-3].string), (yyvsp[-1].string), 0);
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7048 "def.tab.c"
    break;

  case 464:
#line 2880 "def.y"
                             {defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT;}
#line 7054 "def.tab.c"
    break;

  case 465:
#line 2882 "def.y"
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
#line 7071 "def.tab.c"
    break;

  case 466:
#line 2894 "def.y"
                  {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7077 "def.tab.c"
    break;

  case 467:
#line 2895 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addPin("*", (yyvsp[-2].string), (yyvsp[-1].integer));
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7088 "def.tab.c"
    break;

  case 468:
#line 2901 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7094 "def.tab.c"
    break;

  case 469:
#line 2902 "def.y"
        {
          if ((defData->callbacks->NetCbk && (defData->netOsnet==1)) || (defData->callbacks->SNetCbk && (defData->netOsnet==2)))
            defData->Net->addPin("PIN", (yyvsp[-2].string), (yyvsp[-1].integer));
          defData->dumb_mode = 3;
          defData->no_num = 3;
        }
#line 7105 "def.tab.c"
    break;

  case 470:
#line 2910 "def.y"
          { (yyval.integer) = 0; }
#line 7111 "def.tab.c"
    break;

  case 471:
#line 2912 "def.y"
        {
          if (defData->callbacks->NetConnectionExtCbk)
            CALLBACK(defData->callbacks->NetConnectionExtCbk, defrNetConnectionExtCbkType,
              &defData->History_text[0]);
          (yyval.integer) = 0;
        }
#line 7122 "def.tab.c"
    break;

  case 472:
#line 2919 "def.y"
        {  
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... {compName pinName | PIN pinName} + SYNTHESIZED")) {
                    CHKERR();
                }
            } 
           
            (yyval.integer) = 1; 
        }
#line 7136 "def.tab.c"
    break;

  case 475:
#line 2936 "def.y"
        {
            if (defData->callbacks->NetCbk) {
                defData->setPropsDataTypes("NET", defData->session->NetProp);
                defData->addNetProps();
             }

            defData->cleanProps();        
            defData->dumb_mode = 1; 
        }
#line 7150 "def.tab.c"
    break;

  case 476:
#line 2946 "def.y"
        {}
#line 7156 "def.tab.c"
    break;

  case 477:
#line 2949 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + SOURCE {DIST|NETLIST|TEST|TIMING|USER}")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setSource((yyvsp[0].string)); 
            }
        }
#line 7170 "def.tab.c"
    break;

  case 478:
#line 2960 "def.y"
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
#line 7190 "def.tab.c"
    break;

  case 479:
#line 2976 "def.y"
                          { defData->real_num = 1; }
#line 7196 "def.tab.c"
    break;

  case 480:
#line 2977 "def.y"
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
#line 7217 "def.tab.c"
    break;

  case 481:
#line 2994 "def.y"
                         {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7223 "def.tab.c"
    break;

  case 482:
#line 2995 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + ORIGINAL netName")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setOriginal((yyvsp[0].string)); 
            }
        }
#line 7237 "def.tab.c"
    break;

  case 483:
#line 3005 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setPattern((yyvsp[0].string)); }
#line 7243 "def.tab.c"
    break;

  case 484:
#line 3008 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setWeight(ROUND((yyvsp[0].dval))); }
#line 7249 "def.tab.c"
    break;

  case 485:
#line 3011 "def.y"
        { 
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + XTALK class")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setXTalk(ROUND((yyvsp[0].dval))); 
            }
        }
#line 7263 "def.tab.c"
    break;

  case 486:
#line 3022 "def.y"
        {  
            if (defData->VersionNum >= 6.0 - 0.00001) {
                if (defData->def60ObsoletedError("NETS ... netName ... + ESTCAP wireCapacitance")) {
                    CHKERR();
                }
            } else if (defData->callbacks->NetCbk) {
                defData->Net->setCap((yyvsp[0].dval)); 
            }
         }
#line 7277 "def.tab.c"
    break;

  case 487:
#line 3033 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setUse((yyvsp[0].string)); }
#line 7283 "def.tab.c"
    break;

  case 488:
#line 3036 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->setStyle((int)(yyvsp[0].dval)); }
#line 7289 "def.tab.c"
    break;

  case 489:
#line 3038 "def.y"
                               { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7295 "def.tab.c"
    break;

  case 490:
#line 3039 "def.y"
        { 
          if (defData->callbacks->NetCbk && defData->callbacks->NetNonDefaultRuleCbk) {
             // User wants a callback on nondefaultrule 
             CALLBACK(defData->callbacks->NetNonDefaultRuleCbk,
                      defrNetNonDefaultRuleCbkType, (yyvsp[0].string));
          }
          // Still save data in the class 
          if (defData->callbacks->NetCbk) defData->Net->setNonDefaultRule((yyvsp[0].string));
        }
#line 7309 "def.tab.c"
    break;

  case 492:
#line 3051 "def.y"
                          { defData->dumb_mode = 1; defData->no_num = 1; }
#line 7315 "def.tab.c"
    break;

  case 493:
#line 3052 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addShieldNet((yyvsp[0].string)); }
#line 7321 "def.tab.c"
    break;

  case 494:
#line 3054 "def.y"
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
#line 7352 "def.tab.c"
    break;

  case 495:
#line 3082 "def.y"
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
#line 7368 "def.tab.c"
    break;

  case 496:
#line 3093 "def.y"
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
#line 7383 "def.tab.c"
    break;

  case 497:
#line 3103 "def.y"
                   {
          defData->routed_is_keyword = TRUE;
          defData->fixed_is_keyword = TRUE;
          defData->cover_is_keyword = TRUE;
        }
#line 7393 "def.tab.c"
    break;

  case 498:
#line 3107 "def.y"
                         {
          if (defData->callbacks->NetCbk) {
            defData->Net->addSubnet(defData->Subnet);
            defData->Subnet = NULL;
            defData->routed_is_keyword = FALSE;
            defData->fixed_is_keyword = FALSE;
            defData->cover_is_keyword = FALSE;
          }
        }
#line 7407 "def.tab.c"
    break;

  case 499:
#line 3118 "def.y"
        {
            defData->dumb_mode = DEF_MAX_INT;
        }
#line 7415 "def.tab.c"
    break;

  case 500:
#line 3122 "def.y"
        {
            defData->dumb_mode = 0;
        }
#line 7423 "def.tab.c"
    break;

  case 501:
#line 3127 "def.y"
        { 
          if (defData->callbacks->NetExtCbk)
            CALLBACK(defData->callbacks->NetExtCbk, defrNetExtCbkType, &defData->History_text[0]);
        }
#line 7432 "def.tab.c"
    break;

  case 502:
#line 3133 "def.y"
        { (yyval.string) = (char*)"NETLIST"; }
#line 7438 "def.tab.c"
    break;

  case 503:
#line 3135 "def.y"
        { (yyval.string) = (char*)"DIST"; }
#line 7444 "def.tab.c"
    break;

  case 504:
#line 3137 "def.y"
        { (yyval.string) = (char*)"USER"; }
#line 7450 "def.tab.c"
    break;

  case 505:
#line 3139 "def.y"
        { (yyval.string) = (char*)"TIMING"; }
#line 7456 "def.tab.c"
    break;

  case 506:
#line 3141 "def.y"
        { (yyval.string) = (char*)"TEST"; }
#line 7462 "def.tab.c"
    break;

  case 507:
#line 3144 "def.y"
        {
          // vpin_options may have to deal with orient 
          defData->orient_is_keyword = TRUE;
        }
#line 7471 "def.tab.c"
    break;

  case 508:
#line 3149 "def.y"
        { if (defData->callbacks->NetCbk)
            defData->Net->addVpinBounds((yyvsp[-3].pt).x, (yyvsp[-3].pt).y, (yyvsp[-2].pt).x, (yyvsp[-2].pt).y);
          defData->orient_is_keyword = FALSE;
        }
#line 7480 "def.tab.c"
    break;

  case 509:
#line 3154 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1;}
#line 7486 "def.tab.c"
    break;

  case 510:
#line 3155 "def.y"
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
#line 7502 "def.tab.c"
    break;

  case 512:
#line 3168 "def.y"
                  {defData->dumb_mode=1;}
#line 7508 "def.tab.c"
    break;

  case 513:
#line 3169 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addVpinLayer((yyvsp[0].string)); }
#line 7514 "def.tab.c"
    break;

  case 515:
#line 3173 "def.y"
        { if (defData->callbacks->NetCbk) defData->Net->addVpinLoc((yyvsp[-2].string), (yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].integer)); }
#line 7520 "def.tab.c"
    break;

  case 516:
#line 3176 "def.y"
        { (yyval.string) = (char*)"PLACED"; }
#line 7526 "def.tab.c"
    break;

  case 517:
#line 3178 "def.y"
        { (yyval.string) = (char*)"FIXED"; }
#line 7532 "def.tab.c"
    break;

  case 518:
#line 3180 "def.y"
        { (yyval.string) = (char*)"COVER"; }
#line 7538 "def.tab.c"
    break;

  case 519:
#line 3183 "def.y"
        { 
            defData->routeStatus = (char*)"FIXED";
            defData->shieldName = NULL;
        }
#line 7547 "def.tab.c"
    break;

  case 520:
#line 3188 "def.y"
        { 
            defData->routeStatus = (char*)"COVER";
            defData->shieldName = NULL;
        }
#line 7556 "def.tab.c"
    break;

  case 521:
#line 3193 "def.y"
        { 
            defData->routeStatus = (char*)"ROUTED"; 
            defData->shieldName = NULL;
        }
#line 7565 "def.tab.c"
    break;

  case 522:
#line 3198 "def.y"
        {
            if (defData->VersionNum >= 6.0 - 0.00001) {            
                if (defData->def60ObsoletedError("NETS ... regularWiring ... + NOSHIELD")) {
                    CHKERR();
                }
            }

            defData->routeStatus = (char*)"NOSHIELD";
            defData->shieldName = NULL;
        }
#line 7580 "def.tab.c"
    break;

  case 523:
#line 3211 "def.y"
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
#line 7599 "def.tab.c"
    break;

  case 524:
#line 3226 "def.y"
    {
        defData->Shield = NULL;
        defData->Wire = NULL;
    }
#line 7608 "def.tab.c"
    break;

  case 526:
#line 3233 "def.y"
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
#line 7629 "def.tab.c"
    break;

  case 527:
#line 3251 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 7640 "def.tab.c"
    break;

  case 528:
#line 3258 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            defData->finishPath(0, &defData->needNPCbk);
            defData->PathObj = NULL;
        }
    }
#line 7651 "def.tab.c"
    break;

  case 529:
#line 3265 "def.y"
    { }
#line 7657 "def.tab.c"
    break;

  case 530:
#line 3268 "def.y"
    {
        defData->dumb_mode = 1;

        if (defData->callbacks->NetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 7670 "def.tab.c"
    break;

  case 531:
#line 3277 "def.y"
    {
        if (defData->callbacks->NetCbk) {
            defData->finishPath(0, &defData->needNPCbk);
            defData->PathObj = NULL;
        }
    }
#line 7681 "def.tab.c"
    break;

  case 532:
#line 3286 "def.y"
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
#line 7706 "def.tab.c"
    break;

  case 533:
#line 3307 "def.y"
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
#line 7723 "def.tab.c"
    break;

  case 534:
#line 3322 "def.y"
      { defData->dumb_mode = 0;   defData->virtual_is_keyword = FALSE; defData->mask_is_keyword = FALSE,
       defData->rect_is_keyword = FALSE; }
#line 7730 "def.tab.c"
    break;

  case 535:
#line 3327 "def.y"
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
#line 7749 "def.tab.c"
    break;

  case 536:
#line 3344 "def.y"
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
#line 7768 "def.tab.c"
    break;

  case 537:
#line 3361 "def.y"
    {
    }
#line 7775 "def.tab.c"
    break;

  case 538:
#line 3364 "def.y"
    {}
#line 7781 "def.tab.c"
    break;

  case 539:
#line 3369 "def.y"
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
#line 7796 "def.tab.c"
    break;

  case 540:
#line 3380 "def.y"
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
#line 7815 "def.tab.c"
    break;

  case 541:
#line 3395 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVia((yyvsp[-1].string));
            defData->PathObj->addViaRotation((yyvsp[0].integer));
        }
    }
#line 7827 "def.tab.c"
    break;

  case 542:
#line 3403 "def.y"
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
#line 7843 "def.tab.c"
    break;

  case 543:
#line 3415 "def.y"
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
#line 7871 "def.tab.c"
    break;

  case 544:
#line 3439 "def.y"
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
#line 7906 "def.tab.c"
    break;

  case 545:
#line 3470 "def.y"
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
#line 7939 "def.tab.c"
    break;

  case 546:
#line 3499 "def.y"
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
#line 7967 "def.tab.c"
    break;

  case 549:
#line 3525 "def.y"
    {
        defData->dumb_mode = 6;
    }
#line 7975 "def.tab.c"
    break;

  case 550:
#line 3529 "def.y"
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
#line 7990 "def.tab.c"
    break;

  case 552:
#line 3541 "def.y"
    {
       // reset defData->dumb_mode to 1 just incase the next token is a via of the path
        // 2/5/2004 - pcr 686781
        defData->dumb_mode = DEF_MAX_INT; defData->by_is_keyword = TRUE; defData->do_is_keyword = TRUE;
        defData->new_is_keyword = TRUE; defData->step_is_keyword = TRUE;
        defData->orient_is_keyword = TRUE;
    }
#line 8002 "def.tab.c"
    break;

  case 553:
#line 3551 "def.y"
    {
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->sNetWarnings,
                                       defData->settings->SNetWarnings)) {
            if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
                || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
                defData->PathObj->addMask((yyvsp[0].dval)); 
            }
        }  
    }
#line 8016 "def.tab.c"
    break;

  case 555:
#line 3563 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.0001) {
            if (defData->def60NewSyntaxError("NETS ... ( x y [extValue] ) [MASK maskNum] [WIDTH width] ( x y [extValue] )")) {
                CHKERR();
            }
        } else if (defData->callbacks->NetCbk && (defData->netOsnet == 1)) {
            defData->PathObj->addWidth((yyvsp[0].dval));
        } 
    }
#line 8030 "def.tab.c"
    break;

  case 556:
#line 3575 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-2].dval);
        defData->save_y = (yyvsp[-1].dval); 
    }
#line 8044 "def.tab.c"
    break;

  case 557:
#line 3585 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND(defData->save_x), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-1].dval);
      }
#line 8057 "def.tab.c"
    break;

  case 558:
#line 3594 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addPoint(ROUND((yyvsp[-2].dval)), ROUND(defData->save_y));
        }

        defData->save_x = (yyvsp[-2].dval);
    }
#line 8070 "def.tab.c"
    break;

  case 559:
#line 3603 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addPoint(ROUND(defData->save_x),
                                       ROUND(defData->save_y));
        }
    }
#line 8082 "def.tab.c"
    break;

  case 560:
#line 3611 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND((yyvsp[-3].dval)), ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-3].dval);
        defData->save_y = (yyvsp[-2].dval);
    }
#line 8096 "def.tab.c"
    break;

  case 561:
#line 3621 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND(defData->save_x),
                                            ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-2].dval);
    }
#line 8110 "def.tab.c"
    break;

  case 562:
#line 3631 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addFlushPoint(ROUND((yyvsp[-3].dval)), ROUND(defData->save_y),
                                           ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-3].dval);
    }
#line 8124 "def.tab.c"
    break;

  case 563:
#line 3641 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet==1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet==2))) {
            defData->PathObj->addFlushPoint(ROUND(defData->save_x),
                                           ROUND(defData->save_y),
                                           ROUND((yyvsp[-1].dval)));
        }
    }
#line 8137 "def.tab.c"
    break;

  case 564:
#line 3652 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND((yyvsp[-2].dval)), ROUND((yyvsp[-1].dval)));
        }

        defData->save_x = (yyvsp[-2].dval);
        defData->save_y = (yyvsp[-1].dval);
    }
#line 8151 "def.tab.c"
    break;

  case 565:
#line 3662 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND(defData->save_x), ROUND((yyvsp[-1].dval)));
        }

        defData->save_y = (yyvsp[-1].dval);
    }
#line 8164 "def.tab.c"
    break;

  case 566:
#line 3671 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND((yyvsp[-2].dval)), ROUND(defData->save_y));
        }

        defData->save_x = (yyvsp[-2].dval);
    }
#line 8177 "def.tab.c"
    break;

  case 567:
#line 3680 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
             || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addVirtualPoint(ROUND(defData->save_x),
                                              ROUND(defData->save_y));
        }
    }
#line 8189 "def.tab.c"
    break;

  case 568:
#line 3690 "def.y"
    {
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addViaRect((yyvsp[-4].dval), (yyvsp[-3].dval), (yyvsp[-2].dval), (yyvsp[-1].dval)); 
        }    
    }
#line 8200 "def.tab.c"
    break;

  case 575:
#line 3708 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 8208 "def.tab.c"
    break;

  case 576:
#line 3712 "def.y"
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
#line 8229 "def.tab.c"
    break;

  case 577:
#line 3730 "def.y"
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
#line 8245 "def.tab.c"
    break;

  case 578:
#line 3743 "def.y"
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
#line 8260 "def.tab.c"
    break;

  case 579:
#line 3753 "def.y"
                { defData->dumb_mode = 1; }
#line 8266 "def.tab.c"
    break;

  case 580:
#line 3754 "def.y"
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
#line 8281 "def.tab.c"
    break;

  case 581:
#line 3766 "def.y"
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
#line 8308 "def.tab.c"
    break;

  case 584:
#line 3795 "def.y"
    {  
        if ((defData->callbacks->NetCbk && (defData->netOsnet == 1))
            || (defData->callbacks->SNetCbk && (defData->netOsnet == 2))) {
            defData->PathObj->addShape((yyvsp[0].string)); 
        }
    }
#line 8319 "def.tab.c"
    break;

  case 585:
#line 3803 "def.y"
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
#line 8349 "def.tab.c"
    break;

  case 586:
#line 3830 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 8357 "def.tab.c"
    break;

  case 587:
#line 3834 "def.y"
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
#line 8378 "def.tab.c"
    break;

  case 588:
#line 3852 "def.y"
    { 
        defData->dumb_mode = 1; 
        defData->no_num = 1; 
    }
#line 8387 "def.tab.c"
    break;

  case 589:
#line 3857 "def.y"
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
#line 8403 "def.tab.c"
    break;

  case 590:
#line 3870 "def.y"
    {
        if (defData->callbacks->NetEndCbk) {
            CALLBACK(defData->callbacks->NetEndCbk, defrNetEndCbkType, 0);
        }

        defData->netOsnet = 0;
        defData->width_is_keyword = FALSE;

        delete defData->Net;
        defData->Net = NULL;
    }
#line 8419 "def.tab.c"
    break;

  case 591:
#line 3883 "def.y"
            { (yyval.string) = (char*)"RING"; }
#line 8425 "def.tab.c"
    break;

  case 592:
#line 3885 "def.y"
            { (yyval.string) = (char*)"STRIPE"; }
#line 8431 "def.tab.c"
    break;

  case 593:
#line 3887 "def.y"
            { (yyval.string) = (char*)"FOLLOWPIN"; }
#line 8437 "def.tab.c"
    break;

  case 594:
#line 3889 "def.y"
            { (yyval.string) = (char*)"IOWIRE"; }
#line 8443 "def.tab.c"
    break;

  case 595:
#line 3891 "def.y"
            { (yyval.string) = (char*)"COREWIRE"; }
#line 8449 "def.tab.c"
    break;

  case 596:
#line 3893 "def.y"
            { (yyval.string) = (char*)"BLOCKWIRE"; }
#line 8455 "def.tab.c"
    break;

  case 597:
#line 3895 "def.y"
            { (yyval.string) = (char*)"FILLWIRE"; }
#line 8461 "def.tab.c"
    break;

  case 598:
#line 3897 "def.y"
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
#line 8481 "def.tab.c"
    break;

  case 599:
#line 3913 "def.y"
            { (yyval.string) = (char*)"DRCFILL"; }
#line 8487 "def.tab.c"
    break;

  case 600:
#line 3915 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... + SHAPE BLOCKAGEWIRE")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"BLOCKAGEWIRE"; 
            }
#line 8501 "def.tab.c"
    break;

  case 601:
#line 3925 "def.y"
            { (yyval.string) = (char*)"PADRING"; }
#line 8507 "def.tab.c"
    break;

  case 602:
#line 3927 "def.y"
            { (yyval.string) = (char*)"BLOCKRING"; }
#line 8513 "def.tab.c"
    break;

  case 606:
#line 3938 "def.y"
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
#line 8535 "def.tab.c"
    break;

  case 608:
#line 3958 "def.y"
        {
        }
#line 8542 "def.tab.c"
    break;

  case 613:
#line 3966 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->setPropsDataTypes("SPECIAL NET", defData->session->SNetProp);
            defData->addNetProps();
         }

        defData->cleanProps();       
    }
#line 8555 "def.tab.c"
    break;

  case 614:
#line 3975 "def.y"
    {
    }
#line 8562 "def.tab.c"
    break;

  case 615:
#line 3979 "def.y"
    {  
      defData->shapeType = (yyvsp[0].string);
    }
#line 8570 "def.tab.c"
    break;

  case 616:
#line 3984 "def.y"
    {
      if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->sNetWarnings, defData->settings->SNetWarnings)) {
          defData->specialWire_mask = (yyvsp[0].dval);
      }     
    }
#line 8580 "def.tab.c"
    break;

  case 617:
#line 3991 "def.y"
    {
      defData->dumb_mode = DEF_MAX_INT;
    }
#line 8588 "def.tab.c"
    break;

  case 618:
#line 3995 "def.y"
    {
      defData->dumb_mode = 0;
    }
#line 8596 "def.tab.c"
    break;

  case 619:
#line 3999 "def.y"
                  { defData->dumb_mode = 1; }
#line 8602 "def.tab.c"
    break;

  case 620:
#line 4000 "def.y"
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
#line 8623 "def.tab.c"
    break;

  case 621:
#line 4018 "def.y"
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
#line 8671 "def.tab.c"
    break;

  case 622:
#line 4062 "def.y"
               { defData->dumb_mode = 1; }
#line 8677 "def.tab.c"
    break;

  case 623:
#line 4063 "def.y"
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
#line 8750 "def.tab.c"
    break;

  case 624:
#line 4131 "def.y"
              { defData->dumb_mode = 1; }
#line 8756 "def.tab.c"
    break;

  case 625:
#line 4132 "def.y"
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
#line 8775 "def.tab.c"
    break;

  case 626:
#line 4147 "def.y"
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
#line 8815 "def.tab.c"
    break;

  case 627:
#line 4184 "def.y"
    { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + SOURCE DIST|NETLIST|TEST|TIMING|USER")) {
                CHKERR();
            }
        } else if (defData->callbacks->SNetCbk) {
            defData->Net->setSource((yyvsp[0].string)); 
        }
    }
#line 8829 "def.tab.c"
    break;

  case 628:
#line 4195 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setFixedbump(); }
#line 8835 "def.tab.c"
    break;

  case 629:
#line 4198 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setFrequency((yyvsp[0].dval)); }
#line 8841 "def.tab.c"
    break;

  case 630:
#line 4200 "def.y"
                   {defData->dumb_mode = 1; defData->no_num = 1;}
#line 8847 "def.tab.c"
    break;

  case 631:
#line 4201 "def.y"
    { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + ORIGINAL netName")) {
                CHKERR();
            }
        } else if (defData->callbacks->SNetCbk) {
            defData->Net->setOriginal((yyvsp[0].string)); 
        }
    }
#line 8861 "def.tab.c"
    break;

  case 632:
#line 4212 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setPattern((yyvsp[0].string)); }
#line 8867 "def.tab.c"
    break;

  case 633:
#line 4215 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setWeight(ROUND((yyvsp[0].dval))); }
#line 8873 "def.tab.c"
    break;

  case 634:
#line 4218 "def.y"
    { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5) {
            if (defData->callbacks->SNetCbk) {
                defData->Net->setCap((yyvsp[0].dval));
            }
        }
    }
#line 8886 "def.tab.c"
    break;

  case 635:
#line 4228 "def.y"
    { if (defData->callbacks->SNetCbk) defData->Net->setUse((yyvsp[0].string)); }
#line 8892 "def.tab.c"
    break;

  case 636:
#line 4231 "def.y"
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
#line 8908 "def.tab.c"
    break;

  case 637:
#line 4244 "def.y"
    { CALLBACK(defData->callbacks->NetExtCbk, defrNetExtCbkType, &defData->History_text[0]); }
#line 8914 "def.tab.c"
    break;

  case 638:
#line 4247 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"FIXED"; 
            defData->dumb_mode = 1; 
        }
#line 8924 "def.tab.c"
    break;

  case 639:
#line 4253 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"COVER"; 
            defData->dumb_mode = 1; 
        }
#line 8934 "def.tab.c"
    break;

  case 640:
#line 4259 "def.y"
        { 
            defData->shieldName = NULL;
            defData->routeStatus = (char*)"ROUTED"; 
            defData->dumb_mode = 1; 
        }
#line 8944 "def.tab.c"
    break;

  case 641:
#line 4264 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 8950 "def.tab.c"
    break;

  case 642:
#line 4265 "def.y"
        {
            if (defData->VersionNum < 6.0 - 0.00001) {
                defData->routeStatus = (char*)"SHIELD";
            } 

            defData->dumb_mode = 1; 
            defData->no_num = 1; 

            defData->shieldName = (yyvsp[0].string);
        }
#line 8965 "def.tab.c"
    break;

  case 643:
#line 4277 "def.y"
        { (yyval.integer) = 0; }
#line 8971 "def.tab.c"
    break;

  case 644:
#line 4279 "def.y"
        { (yyval.integer) = (yyvsp[0].integer); }
#line 8977 "def.tab.c"
    break;

  case 645:
#line 4282 "def.y"
                        { defData->dumb_mode = 1; }
#line 8983 "def.tab.c"
    break;

  case 646:
#line 4283 "def.y"
            {
              // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
              if (defData->VersionNum < 5.5)
                 if (defData->callbacks->SNetCbk) defData->Net->setWidth((yyvsp[-1].string), (yyvsp[0].dval));
              else
                 defData->defWarning(7026, "The WIDTH statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
            }
#line 8995 "def.tab.c"
    break;

  case 647:
#line 4291 "def.y"
                             { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9001 "def.tab.c"
    break;

  case 648:
#line 4292 "def.y"
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
#line 9022 "def.tab.c"
    break;

  case 649:
#line 4309 "def.y"
                            { defData->dumb_mode = 1; }
#line 9028 "def.tab.c"
    break;

  case 650:
#line 4310 "def.y"
            {
              if (defData->callbacks->SNetCbk) defData->Net->setSpacing((yyvsp[-1].string),(yyvsp[0].dval));
            }
#line 9036 "def.tab.c"
    break;

  case 651:
#line 4314 "def.y"
            {
            }
#line 9043 "def.tab.c"
    break;

  case 653:
#line 4319 "def.y"
            {
              if (defData->callbacks->SNetCbk) defData->Net->setRange((yyvsp[-1].dval),(yyvsp[0].dval));
            }
#line 9051 "def.tab.c"
    break;

  case 655:
#line 4325 "def.y"
            { defData->Prop.setRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 9057 "def.tab.c"
    break;

  case 656:
#line 4329 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("NETS ... netName ... + PATTERN BALANCED")) {
                        CHKERR();
                    }
                }

                (yyval.string) = (char*)"BALANCED"; 
              }
#line 9071 "def.tab.c"
    break;

  case 657:
#line 4339 "def.y"
            { (yyval.string) = (char*)"STEINER"; }
#line 9077 "def.tab.c"
    break;

  case 658:
#line 4341 "def.y"
            { (yyval.string) = (char*)"TRUNK"; }
#line 9083 "def.tab.c"
    break;

  case 659:
#line 4343 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("NETS ... netName ... + PATTERN WIREDLOGIC")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"WIREDLOGIC"; 
             }
#line 9097 "def.tab.c"
    break;

  case 660:
#line 4354 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + PATTERN BALANCED")) {
                        CHKERR();
                    }
                }

                (yyval.string) = (char*)"BALANCED"; 
            }
#line 9111 "def.tab.c"
    break;

  case 661:
#line 4364 "def.y"
            { (yyval.string) = (char*)"STEINER"; }
#line 9117 "def.tab.c"
    break;

  case 662:
#line 4366 "def.y"
            { (yyval.string) = (char*)"TRUNK"; }
#line 9123 "def.tab.c"
    break;

  case 663:
#line 4368 "def.y"
            { 
                if (defData->VersionNum >= 6.0 - 0.00001) {
                    if (defData->def60ObsoletedError("SPECIALNETS ... netName ... + PATTERN WIREDLOGIC")) {
                        CHKERR();
                    }
                }
                
                (yyval.string) = (char*)"WIREDLOGIC"; 
             }
#line 9137 "def.tab.c"
    break;

  case 665:
#line 4379 "def.y"
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
#line 9167 "def.tab.c"
    break;

  case 666:
#line 4405 "def.y"
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
#line 9207 "def.tab.c"
    break;

  case 667:
#line 4442 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 9218 "def.tab.c"
    break;

  case 668:
#line 4449 "def.y"
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
#line 9253 "def.tab.c"
    break;

  case 669:
#line 4480 "def.y"
    { }
#line 9259 "def.tab.c"
    break;

  case 670:
#line 4483 "def.y"
    {
        defData->dumb_mode = 1;

        if (defData->callbacks->SNetCbk) {
            defData->PathObj = new defiPath(defData);
            defData->startPath();
        }
    }
#line 9272 "def.tab.c"
    break;

  case 671:
#line 4492 "def.y"
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
#line 9308 "def.tab.c"
    break;

  case 672:
#line 4526 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj->addLayer((yyvsp[0].string));
        }
        
        defData->dumb_mode = 0;
        defData->no_num = 0;
    }
#line 9321 "def.tab.c"
    break;

  case 673:
#line 4535 "def.y"
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
#line 9337 "def.tab.c"
    break;

  case 674:
#line 4547 "def.y"
    {
        defData->dumb_mode = 0;
        defData->rect_is_keyword = FALSE;
        defData->mask_is_keyword = FALSE;
        defData->virtual_is_keyword = FALSE;
    }
#line 9348 "def.tab.c"
    break;

  case 675:
#line 4556 "def.y"
    {
        if (defData->callbacks->SNetCbk) {
            defData->PathObj->addWidth(ROUND((yyvsp[0].dval)));
        }
    }
#line 9358 "def.tab.c"
    break;

  case 676:
#line 4563 "def.y"
    { 
        defData->Net = new defiNet(defData);

        if (defData->callbacks->SNetStartCbk) {
            CALLBACK(defData->callbacks->SNetStartCbk,
                     defrSNetStartCbkType,
                     ROUND((yyvsp[-1].dval)));
        }

        defData->netOsnet = 2;
    }
#line 9374 "def.tab.c"
    break;

  case 677:
#line 4576 "def.y"
    { 
        if (defData->callbacks->SNetEndCbk) {
            CALLBACK(defData->callbacks->SNetEndCbk, defrSNetEndCbkType, 0);
        }

        defData->netOsnet = 0;

        delete defData->Net;
        defData->Net = NULL;
    }
#line 9389 "def.tab.c"
    break;

  case 679:
#line 4591 "def.y"
      {
        if (defData->callbacks->GroupsStartCbk)
           CALLBACK(defData->callbacks->GroupsStartCbk, defrGroupsStartCbkType, ROUND((yyvsp[-1].dval)));
      }
#line 9398 "def.tab.c"
    break;

  case 682:
#line 4601 "def.y"
      {
        if (defData->callbacks->GroupCbk)
           CALLBACK(defData->callbacks->GroupCbk, defrGroupCbkType, &defData->Group);
      }
#line 9407 "def.tab.c"
    break;

  case 683:
#line 4606 "def.y"
                 { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9413 "def.tab.c"
    break;

  case 684:
#line 4607 "def.y"
      {
        defData->dumb_mode = DEF_MAX_INT;
        defData->no_num = DEF_MAX_INT;
        /* dumb_mode is automatically turned off at the first
         * + in the options or at the ; at the end of the group */
        if (defData->callbacks->GroupCbk) defData->Group.setup((yyvsp[0].string));
        if (defData->callbacks->GroupNameCbk)
           CALLBACK(defData->callbacks->GroupNameCbk, defrGroupNameCbkType, (yyvsp[0].string));
      }
#line 9427 "def.tab.c"
    break;

  case 686:
#line 4619 "def.y"
      {  }
#line 9433 "def.tab.c"
    break;

  case 687:
#line 4622 "def.y"
      {
        // if (defData->callbacks->GroupCbk) defData->Group.addMember($1); 
        if (defData->callbacks->GroupMemberCbk)
          CALLBACK(defData->callbacks->GroupMemberCbk, defrGroupMemberCbkType, (yyvsp[0].string));
      }
#line 9443 "def.tab.c"
    break;

  case 690:
#line 4633 "def.y"
      { }
#line 9449 "def.tab.c"
    break;

  case 691:
#line 4634 "def.y"
                           { defData->dumb_mode = DEF_MAX_INT; }
#line 9455 "def.tab.c"
    break;

  case 692:
#line 4636 "def.y"
      { defData->dumb_mode = 0; }
#line 9461 "def.tab.c"
    break;

  case 693:
#line 4637 "def.y"
                         { defData->dumb_mode = 1;  defData->no_num = 1; }
#line 9467 "def.tab.c"
    break;

  case 694:
#line 4638 "def.y"
      { }
#line 9473 "def.tab.c"
    break;

  case 695:
#line 4640 "def.y"
      { 
        if (defData->callbacks->GroupMemberCbk)
          CALLBACK(defData->callbacks->GroupExtCbk, defrGroupExtCbkType, &defData->History_text[0]);
      }
#line 9482 "def.tab.c"
    break;

  case 696:
#line 4645 "def.y"
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
#line 9498 "def.tab.c"
    break;

  case 697:
#line 4658 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9507 "def.tab.c"
    break;

  case 698:
#line 4663 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;

        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + HINSTS hinst1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9522 "def.tab.c"
    break;

  case 699:
#line 4675 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9531 "def.tab.c"
    break;

  case 700:
#line 4680 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;
        
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + COMPONENTS component1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9546 "def.tab.c"
    break;

  case 701:
#line 4692 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 9555 "def.tab.c"
    break;

  case 702:
#line 4697 "def.y"
      { 
        defData->dumb_mode = 0; 
        defData->no_num = 0;

        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("GROUPS ... - groupName ... + GROUPS group1 ...")) {
                CHKERR();
            }
        } 
      }
#line 9570 "def.tab.c"
    break;

  case 704:
#line 4710 "def.y"
      {}
#line 9576 "def.tab.c"
    break;

  case 705:
#line 4713 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
                defData->Group.addHinst((yyvsp[0].string));
        }
      }
#line 9586 "def.tab.c"
    break;

  case 707:
#line 4721 "def.y"
      {}
#line 9592 "def.tab.c"
    break;

  case 708:
#line 4724 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
            defData->Group.addComponent((yyvsp[0].string));
        }
      }
#line 9602 "def.tab.c"
    break;

  case 710:
#line 4732 "def.y"
      {}
#line 9608 "def.tab.c"
    break;

  case 711:
#line 4735 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
            defData->Group.addGroup((yyvsp[0].string));
        }
      }
#line 9618 "def.tab.c"
    break;

  case 712:
#line 4742 "def.y"
      {
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5) {
          if (defData->callbacks->GroupCbk)
            defData->Group.addRegionRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
        }
        else
          defData->defWarning(7027, "The GROUP REGION pt pt statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 9632 "def.tab.c"
    break;

  case 713:
#line 4752 "def.y"
      { if (defData->callbacks->GroupCbk)
          defData->Group.setRegionName((yyvsp[0].string));
      }
#line 9640 "def.tab.c"
    break;

  case 716:
#line 4761 "def.y"
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
#line 9655 "def.tab.c"
    break;

  case 717:
#line 4772 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
          char propTp;
          propTp = defData->session->GroupProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "GROUP");
          defData->Group.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 9668 "def.tab.c"
    break;

  case 718:
#line 4781 "def.y"
      {
        if (defData->callbacks->GroupCbk) {
          char propTp;
          propTp = defData->session->GroupProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "GROUP");
          defData->Group.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 9681 "def.tab.c"
    break;

  case 720:
#line 4792 "def.y"
      { }
#line 9687 "def.tab.c"
    break;

  case 721:
#line 4795 "def.y"
      {
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setMaxX(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7028, "The GROUP SOFT MAXX statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 9699 "def.tab.c"
    break;

  case 722:
#line 4803 "def.y"
      { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setMaxY(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7029, "The GROUP SOFT MAXY statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 9711 "def.tab.c"
    break;

  case 723:
#line 4811 "def.y"
      { 
        // 11/12/2002 - this is obsolete in 5.5, & will be ignored 
        if (defData->VersionNum < 5.5)
          if (defData->callbacks->GroupCbk) defData->Group.setPerim(ROUND((yyvsp[0].dval)));
        else
          defData->defWarning(7030, "The GROUP SOFT MAXHALFPERIMETER statement is obsolete in version 5.5 and later.\nThe DEF parser will ignore this statement.");
      }
#line 9723 "def.tab.c"
    break;

  case 724:
#line 4820 "def.y"
      { 
        if (defData->callbacks->GroupsEndCbk)
          CALLBACK(defData->callbacks->GroupsEndCbk, defrGroupsEndCbkType, 0);
      }
#line 9732 "def.tab.c"
    break;

  case 727:
#line 4834 "def.y"
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
#line 9749 "def.tab.c"
    break;

  case 728:
#line 4848 "def.y"
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
#line 9766 "def.tab.c"
    break;

  case 732:
#line 4867 "def.y"
      {
        if ((defData->VersionNum < 5.4) && (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)) {
          if (defData->Assertion.isConstraint()) 
            CALLBACK(defData->callbacks->ConstraintCbk, defrConstraintCbkType, &defData->Assertion);
          if (defData->Assertion.isAssertion()) 
            CALLBACK(defData->callbacks->AssertionCbk, defrAssertionCbkType, &defData->Assertion);
        }
      }
#line 9779 "def.tab.c"
    break;

  case 733:
#line 4877 "def.y"
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
#line 9795 "def.tab.c"
    break;

  case 734:
#line 4889 "def.y"
               { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9801 "def.tab.c"
    break;

  case 735:
#line 4890 "def.y"
      {
         if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.addNet((yyvsp[0].string));
      }
#line 9810 "def.tab.c"
    break;

  case 736:
#line 4894 "def.y"
               {defData->dumb_mode = 4; defData->no_num = 4;}
#line 9816 "def.tab.c"
    break;

  case 737:
#line 4895 "def.y"
      {
         if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.addPath((yyvsp[-3].string), (yyvsp[-2].string), (yyvsp[-1].string), (yyvsp[0].string));
      }
#line 9825 "def.tab.c"
    break;

  case 738:
#line 4900 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.setSum();
      }
#line 9834 "def.tab.c"
    break;

  case 739:
#line 4905 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
           defData->Assertion.setDiff();
      }
#line 9843 "def.tab.c"
    break;

  case 741:
#line 4912 "def.y"
      { }
#line 9849 "def.tab.c"
    break;

  case 743:
#line 4915 "def.y"
                                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 9855 "def.tab.c"
    break;

  case 744:
#line 4917 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setWiredlogic((yyvsp[-4].string), (yyvsp[-1].dval));
      }
#line 9864 "def.tab.c"
    break;

  case 745:
#line 4924 "def.y"
      { (yyval.string) = (char*)""; }
#line 9870 "def.tab.c"
    break;

  case 746:
#line 4926 "def.y"
      { (yyval.string) = (char*)"+"; }
#line 9876 "def.tab.c"
    break;

  case 749:
#line 4933 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setRiseMin((yyvsp[0].dval));
      }
#line 9885 "def.tab.c"
    break;

  case 750:
#line 4938 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setRiseMax((yyvsp[0].dval));
      }
#line 9894 "def.tab.c"
    break;

  case 751:
#line 4943 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setFallMin((yyvsp[0].dval));
      }
#line 9903 "def.tab.c"
    break;

  case 752:
#line 4948 "def.y"
      {
        if (defData->callbacks->ConstraintCbk || defData->callbacks->AssertionCbk)
          defData->Assertion.setFallMax((yyvsp[0].dval));
      }
#line 9912 "def.tab.c"
    break;

  case 753:
#line 4954 "def.y"
      { if ((defData->VersionNum < 5.4) && defData->callbacks->ConstraintsEndCbk) {
          CALLBACK(defData->callbacks->ConstraintsEndCbk, defrConstraintsEndCbkType, 0);
        } else {
          if (defData->callbacks->ConstraintsEndCbk) {
            if (defData->constraintWarnings++ < defData->settings->ConstraintWarnings)
              defData->defWarning(7032, "The CONSTRAINTS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
      }
#line 9926 "def.tab.c"
    break;

  case 754:
#line 4965 "def.y"
      { if ((defData->VersionNum < 5.4) && defData->callbacks->AssertionsEndCbk) {
          CALLBACK(defData->callbacks->AssertionsEndCbk, defrAssertionsEndCbkType, 0);
        } else {
          if (defData->callbacks->AssertionsEndCbk) {
            if (defData->assertionWarnings++ < defData->settings->AssertionWarnings)
              defData->defWarning(7031, "The ASSERTIONS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
          }
        }
      }
#line 9940 "def.tab.c"
    break;

  case 756:
#line 4979 "def.y"
      { if (defData->callbacks->ScanchainsStartCbk)
          CALLBACK(defData->callbacks->ScanchainsStartCbk, defrScanchainsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 9949 "def.tab.c"
    break;

  case 758:
#line 4986 "def.y"
      {}
#line 9955 "def.tab.c"
    break;

  case 759:
#line 4989 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk)
          CALLBACK(defData->callbacks->ScanchainCbk, defrScanchainCbkType, &defData->Scanchain);
      }
#line 9964 "def.tab.c"
    break;

  case 760:
#line 4994 "def.y"
                {defData->dumb_mode = 1; defData->no_num = 1;}
#line 9970 "def.tab.c"
    break;

  case 761:
#line 4995 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.closeOrderedList();
          defData->Scanchain.setName((yyvsp[0].string));
        }
        defData->bit_is_keyword = TRUE;
      }
#line 9982 "def.tab.c"
    break;

  case 764:
#line 5009 "def.y"
      { (yyval.string) = (char*)""; }
#line 9988 "def.tab.c"
    break;

  case 765:
#line 5011 "def.y"
      { (yyval.string) = (yyvsp[0].string); }
#line 9994 "def.tab.c"
    break;

  case 766:
#line 5013 "def.y"
                         {defData->dumb_mode = 2; defData->no_num = 2;}
#line 10000 "def.tab.c"
    break;

  case 767:
#line 5014 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.closeOrderedList();
          defData->Scanchain.setStart((yyvsp[-1].string), (yyvsp[0].string));
        }
      }
#line 10011 "def.tab.c"
    break;

  case 768:
#line 5021 "def.y"
      {
         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;      
      }
#line 10020 "def.tab.c"
    break;

  case 769:
#line 5026 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
           defData->Scanchain.closeOrderedList();     
        }

        defData->dumb_mode = 0; 
        defData->no_num = 0; 
      }
#line 10033 "def.tab.c"
    break;

  case 770:
#line 5035 "def.y"
      {
         if (defData->callbacks->ScanchainCbk) {
           defData->Scanchain.startOrderedList();
         }

         defData->dumb_mode = DEF_MAX_INT; 
         defData->no_num = DEF_MAX_INT;
      }
#line 10046 "def.tab.c"
    break;

  case 771:
#line 5044 "def.y"
      {         
         defData->dumb_mode = 0; 
         defData->no_num = 0; 
      }
#line 10055 "def.tab.c"
    break;

  case 772:
#line 5048 "def.y"
                   {defData->dumb_mode = 2; defData->no_num = 2; }
#line 10061 "def.tab.c"
    break;

  case 773:
#line 5049 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.setStop((yyvsp[-1].string), (yyvsp[0].string));
          defData->Scanchain.closeOrderedList();
        }
      }
#line 10072 "def.tab.c"
    break;

  case 774:
#line 5055 "def.y"
                             { defData->dumb_mode = 10; defData->no_num = 10; }
#line 10078 "def.tab.c"
    break;

  case 775:
#line 5056 "def.y"
      { 
        if (defData->callbacks->ScanchainCbk) {
            defData->Scanchain.closeOrderedList();
        }
        
        defData->dumb_mode = 0;  
        defData->no_num = 0; 
      }
#line 10091 "def.tab.c"
    break;

  case 776:
#line 5064 "def.y"
                        { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10097 "def.tab.c"
    break;

  case 777:
#line 5066 "def.y"
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
#line 10120 "def.tab.c"
    break;

  case 778:
#line 5085 "def.y"
      {
        if (defData->callbacks->ScanChainExtCbk) {
          CALLBACK(defData->callbacks->ScanChainExtCbk, defrScanChainExtCbkType, &defData->History_text[0]);
        }
      }
#line 10130 "def.tab.c"
    break;

  case 779:
#line 5091 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 10138 "def.tab.c"
    break;

  case 780:
#line 5095 "def.y"
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
#line 10164 "def.tab.c"
    break;

  case 781:
#line 5117 "def.y"
      {
         defData->dumb_mode = 0; 
         defData->no_num = 0;       
      }
#line 10173 "def.tab.c"
    break;

  case 782:
#line 5121 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10179 "def.tab.c"
    break;

  case 783:
#line 5122 "def.y"
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
#line 10200 "def.tab.c"
    break;

  case 784:
#line 5139 "def.y"
      {
         defData->dumb_mode = 0; 
         defData->no_num = 0;       
      }
#line 10209 "def.tab.c"
    break;

  case 785:
#line 5145 "def.y"
      { }
#line 10215 "def.tab.c"
    break;

  case 786:
#line 5147 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          if (strcmp((yyvsp[-2].string), "IN") == 0 || strcmp((yyvsp[-2].string), "in") == 0)
            defData->Scanchain.setCommonIn((yyvsp[-1].string));
          else if (strcmp((yyvsp[-2].string), "OUT") == 0 || strcmp((yyvsp[-2].string), "out") == 0)
            defData->Scanchain.setCommonOut((yyvsp[-1].string));
        }
      }
#line 10228 "def.tab.c"
    break;

  case 787:
#line 5156 "def.y"
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
#line 10245 "def.tab.c"
    break;

  case 790:
#line 5174 "def.y"
      {
        if (defData->callbacks->ScanchainCbk) {
          defData->Scanchain.addFloatingInst((yyvsp[0].string));
        }
      }
#line 10255 "def.tab.c"
    break;

  case 791:
#line 5180 "def.y"
      {}
#line 10261 "def.tab.c"
    break;

  case 792:
#line 5183 "def.y"
      {}
#line 10267 "def.tab.c"
    break;

  case 793:
#line 5185 "def.y"
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
#line 10284 "def.tab.c"
    break;

  case 794:
#line 5198 "def.y"
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
#line 10309 "def.tab.c"
    break;

  case 795:
#line 5220 "def.y"
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
#line 10342 "def.tab.c"
    break;

  case 797:
#line 5251 "def.y"
      { 
      }
#line 10349 "def.tab.c"
    break;

  case 799:
#line 5256 "def.y"
      {}
#line 10355 "def.tab.c"
    break;

  case 800:
#line 5259 "def.y"
      { 
      }
#line 10362 "def.tab.c"
    break;

  case 801:
#line 5262 "def.y"
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
#line 10378 "def.tab.c"
    break;

  case 802:
#line 5274 "def.y"
      { 
      }
#line 10385 "def.tab.c"
    break;

  case 804:
#line 5279 "def.y"
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
#line 10402 "def.tab.c"
    break;

  case 805:
#line 5292 "def.y"
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
#line 10427 "def.tab.c"
    break;

  case 806:
#line 5314 "def.y"
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
#line 10460 "def.tab.c"
    break;

  case 807:
#line 5344 "def.y"
      { (yyval.integer) = -1; }
#line 10466 "def.tab.c"
    break;

  case 808:
#line 5346 "def.y"
      { (yyval.integer) = ROUND((yyvsp[0].dval)); }
#line 10472 "def.tab.c"
    break;

  case 809:
#line 5349 "def.y"
      { 
        if (defData->callbacks->ScanchainsEndCbk)
          CALLBACK(defData->callbacks->ScanchainsEndCbk, defrScanchainsEndCbkType, 0);
        defData->bit_is_keyword = FALSE;
        defData->dumb_mode = 0; defData->no_num = 0;
      }
#line 10483 "def.tab.c"
    break;

  case 811:
#line 5361 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingsStartCbk) {
          CALLBACK(defData->callbacks->IOTimingsStartCbk, defrIOTimingsStartCbkType, ROUND((yyvsp[-1].dval)));
        } else {
          if (defData->callbacks->IOTimingsStartCbk)
            if (defData->iOTimingWarnings++ < defData->settings->IOTimingWarnings)
              defData->defWarning(7035, "The IOTIMINGS statement is obsolete in version 5.4 and later.\nThe DEF parser will ignore this statement.");
        }
      }
#line 10497 "def.tab.c"
    break;

  case 813:
#line 5373 "def.y"
      { }
#line 10503 "def.tab.c"
    break;

  case 814:
#line 5376 "def.y"
      { 
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingCbk)
          CALLBACK(defData->callbacks->IOTimingCbk, defrIOTimingCbkType, &defData->IOTiming);
      }
#line 10512 "def.tab.c"
    break;

  case 815:
#line 5381 "def.y"
                        {defData->dumb_mode = 2; defData->no_num = 2; }
#line 10518 "def.tab.c"
    break;

  case 816:
#line 5382 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setName((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 10527 "def.tab.c"
    break;

  case 819:
#line 5393 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setVariable((yyvsp[-3].string), (yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 10536 "def.tab.c"
    break;

  case 820:
#line 5398 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setSlewRate((yyvsp[-3].string), (yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 10545 "def.tab.c"
    break;

  case 821:
#line 5403 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setCapacitance((yyvsp[0].dval));
      }
#line 10554 "def.tab.c"
    break;

  case 822:
#line 5407 "def.y"
                        {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10560 "def.tab.c"
    break;

  case 823:
#line 5408 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setDriveCell((yyvsp[0].string));
      }
#line 10569 "def.tab.c"
    break;

  case 825:
#line 5417 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IoTimingsExtCbk)
          CALLBACK(defData->callbacks->IoTimingsExtCbk, defrIoTimingsExtCbkType, &defData->History_text[0]);
      }
#line 10578 "def.tab.c"
    break;

  case 826:
#line 5423 "def.y"
              {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10584 "def.tab.c"
    break;

  case 827:
#line 5424 "def.y"
      {
        if (defData->callbacks->IOTimingCbk) 
          defData->IOTiming.setTo((yyvsp[0].string));
      }
#line 10593 "def.tab.c"
    break;

  case 830:
#line 5431 "def.y"
                  {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10599 "def.tab.c"
    break;

  case 831:
#line 5432 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setFrom((yyvsp[0].string));
      }
#line 10608 "def.tab.c"
    break;

  case 833:
#line 5439 "def.y"
      {
        if (defData->callbacks->IOTimingCbk)
          defData->IOTiming.setParallel((yyvsp[0].dval));
      }
#line 10617 "def.tab.c"
    break;

  case 834:
#line 5444 "def.y"
                 { (yyval.string) = (char*)"RISE"; }
#line 10623 "def.tab.c"
    break;

  case 835:
#line 5444 "def.y"
                                                  { (yyval.string) = (char*)"FALL"; }
#line 10629 "def.tab.c"
    break;

  case 836:
#line 5447 "def.y"
      {
        if (defData->VersionNum < 5.4 && defData->callbacks->IOTimingsEndCbk)
          CALLBACK(defData->callbacks->IOTimingsEndCbk, defrIOTimingsEndCbkType, 0);
      }
#line 10638 "def.tab.c"
    break;

  case 837:
#line 5453 "def.y"
      { 
        if (defData->callbacks->FPCEndCbk)
          CALLBACK(defData->callbacks->FPCEndCbk, defrFPCEndCbkType, 0);
      }
#line 10647 "def.tab.c"
    break;

  case 838:
#line 5459 "def.y"
      {
        if (defData->callbacks->FPCStartCbk)
          CALLBACK(defData->callbacks->FPCStartCbk, defrFPCStartCbkType, ROUND((yyvsp[-1].dval)));
      }
#line 10656 "def.tab.c"
    break;

  case 840:
#line 5466 "def.y"
      {}
#line 10662 "def.tab.c"
    break;

  case 841:
#line 5468 "def.y"
             { defData->dumb_mode = 1; defData->no_num = 1;  }
#line 10668 "def.tab.c"
    break;

  case 842:
#line 5469 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setName((yyvsp[-1].string), (yyvsp[0].string)); }
#line 10674 "def.tab.c"
    break;

  case 843:
#line 5471 "def.y"
      { if (defData->callbacks->FPCCbk) CALLBACK(defData->callbacks->FPCCbk, defrFPCCbkType, &defData->FPC); }
#line 10680 "def.tab.c"
    break;

  case 844:
#line 5474 "def.y"
      { (yyval.string) = (char*)"HORIZONTAL"; }
#line 10686 "def.tab.c"
    break;

  case 845:
#line 5476 "def.y"
      { (yyval.string) = (char*)"VERTICAL"; }
#line 10692 "def.tab.c"
    break;

  case 846:
#line 5479 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setAlign(); }
#line 10698 "def.tab.c"
    break;

  case 847:
#line 5481 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setMax((yyvsp[0].dval)); }
#line 10704 "def.tab.c"
    break;

  case 848:
#line 5483 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setMin((yyvsp[0].dval)); }
#line 10710 "def.tab.c"
    break;

  case 849:
#line 5485 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setEqual((yyvsp[0].dval)); }
#line 10716 "def.tab.c"
    break;

  case 852:
#line 5492 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setDoingBottomLeft(); }
#line 10722 "def.tab.c"
    break;

  case 854:
#line 5495 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.setDoingTopRight(); }
#line 10728 "def.tab.c"
    break;

  case 858:
#line 5502 "def.y"
                         {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10734 "def.tab.c"
    break;

  case 859:
#line 5503 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.addRow((yyvsp[-1].string)); }
#line 10740 "def.tab.c"
    break;

  case 860:
#line 5504 "def.y"
                       {defData->dumb_mode = 1; defData->no_num = 1; }
#line 10746 "def.tab.c"
    break;

  case 861:
#line 5505 "def.y"
      { if (defData->callbacks->FPCCbk) defData->FPC.addComps((yyvsp[-1].string)); }
#line 10752 "def.tab.c"
    break;

  case 863:
#line 5512 "def.y"
      { 
        if (defData->callbacks->TimingDisablesStartCbk)
          CALLBACK(defData->callbacks->TimingDisablesStartCbk, defrTimingDisablesStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 10762 "def.tab.c"
    break;

  case 865:
#line 5520 "def.y"
      {}
#line 10768 "def.tab.c"
    break;

  case 866:
#line 5522 "def.y"
                                   { defData->dumb_mode = 2; defData->no_num = 2;  }
#line 10774 "def.tab.c"
    break;

  case 867:
#line 5523 "def.y"
                       { defData->dumb_mode = 2; defData->no_num = 2;  }
#line 10780 "def.tab.c"
    break;

  case 868:
#line 5524 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setFromTo((yyvsp[-6].string), (yyvsp[-5].string), (yyvsp[-2].string), (yyvsp[-1].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                &defData->TimingDisable);
        }
      }
#line 10792 "def.tab.c"
    break;

  case 869:
#line 5531 "def.y"
                      {defData->dumb_mode = 2; defData->no_num = 2; }
#line 10798 "def.tab.c"
    break;

  case 870:
#line 5532 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setThru((yyvsp[-2].string), (yyvsp[-1].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                   &defData->TimingDisable);
        }
      }
#line 10810 "def.tab.c"
    break;

  case 871:
#line 5539 "def.y"
                    {defData->dumb_mode = 1; defData->no_num = 1;}
#line 10816 "def.tab.c"
    break;

  case 872:
#line 5540 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk) {
          defData->TimingDisable.setMacro((yyvsp[-2].string));
          CALLBACK(defData->callbacks->TimingDisableCbk, defrTimingDisableCbkType,
                &defData->TimingDisable);
        }
      }
#line 10828 "def.tab.c"
    break;

  case 873:
#line 5548 "def.y"
      { if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setReentrantPathsFlag();
      }
#line 10836 "def.tab.c"
    break;

  case 874:
#line 5553 "def.y"
                           {defData->dumb_mode = 1; defData->no_num = 1;}
#line 10842 "def.tab.c"
    break;

  case 875:
#line 5554 "def.y"
      {defData->dumb_mode=1; defData->no_num = 1;}
#line 10848 "def.tab.c"
    break;

  case 876:
#line 5555 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setMacroFromTo((yyvsp[-3].string),(yyvsp[0].string));
      }
#line 10857 "def.tab.c"
    break;

  case 877:
#line 5559 "def.y"
                         {defData->dumb_mode=1; defData->no_num = 1;}
#line 10863 "def.tab.c"
    break;

  case 878:
#line 5560 "def.y"
      {
        if (defData->callbacks->TimingDisableCbk)
          defData->TimingDisable.setMacroThru((yyvsp[0].string));
      }
#line 10872 "def.tab.c"
    break;

  case 879:
#line 5566 "def.y"
      { 
        if (defData->callbacks->TimingDisablesEndCbk)
          CALLBACK(defData->callbacks->TimingDisablesEndCbk, defrTimingDisablesEndCbkType, 0);
      }
#line 10881 "def.tab.c"
    break;

  case 881:
#line 5576 "def.y"
      {
        if (defData->callbacks->PartitionsStartCbk)
          CALLBACK(defData->callbacks->PartitionsStartCbk, defrPartitionsStartCbkType,
                   ROUND((yyvsp[-1].dval)));
      }
#line 10891 "def.tab.c"
    break;

  case 883:
#line 5584 "def.y"
      { }
#line 10897 "def.tab.c"
    break;

  case 884:
#line 5587 "def.y"
      { 
        if (defData->callbacks->PartitionCbk)
          CALLBACK(defData->callbacks->PartitionCbk, defrPartitionCbkType, &defData->Partition);
      }
#line 10906 "def.tab.c"
    break;

  case 885:
#line 5592 "def.y"
                     { defData->dumb_mode = 1; defData->no_num = 1; }
#line 10912 "def.tab.c"
    break;

  case 886:
#line 5593 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setName((yyvsp[-1].string));
      }
#line 10921 "def.tab.c"
    break;

  case 888:
#line 5600 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.addTurnOff((yyvsp[-1].string), (yyvsp[0].string));
      }
#line 10930 "def.tab.c"
    break;

  case 889:
#line 5606 "def.y"
      { (yyval.string) = (char*)" "; }
#line 10936 "def.tab.c"
    break;

  case 890:
#line 5608 "def.y"
      { (yyval.string) = (char*)"R"; }
#line 10942 "def.tab.c"
    break;

  case 891:
#line 5610 "def.y"
      { (yyval.string) = (char*)"F"; }
#line 10948 "def.tab.c"
    break;

  case 892:
#line 5613 "def.y"
      { (yyval.string) = (char*)" "; }
#line 10954 "def.tab.c"
    break;

  case 893:
#line 5615 "def.y"
      { (yyval.string) = (char*)"R"; }
#line 10960 "def.tab.c"
    break;

  case 894:
#line 5617 "def.y"
      { (yyval.string) = (char*)"F"; }
#line 10966 "def.tab.c"
    break;

  case 897:
#line 5623 "def.y"
                                     {defData->dumb_mode=2; defData->no_num = 2;}
#line 10972 "def.tab.c"
    break;

  case 898:
#line 5625 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromClockPin((yyvsp[-3].string), (yyvsp[-2].string));
      }
#line 10981 "def.tab.c"
    break;

  case 899:
#line 5629 "def.y"
                          {defData->dumb_mode=2; defData->no_num = 2; }
#line 10987 "def.tab.c"
    break;

  case 900:
#line 5631 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromCompPin((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 10996 "def.tab.c"
    break;

  case 901:
#line 5635 "def.y"
                        {defData->dumb_mode=1; defData->no_num = 1; }
#line 11002 "def.tab.c"
    break;

  case 902:
#line 5637 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setFromIOPin((yyvsp[-1].string));
      }
#line 11011 "def.tab.c"
    break;

  case 903:
#line 5641 "def.y"
                         {defData->dumb_mode=2; defData->no_num = 2; }
#line 11017 "def.tab.c"
    break;

  case 904:
#line 5643 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToClockPin((yyvsp[-3].string), (yyvsp[-2].string));
      }
#line 11026 "def.tab.c"
    break;

  case 905:
#line 5647 "def.y"
                        {defData->dumb_mode=2; defData->no_num = 2; }
#line 11032 "def.tab.c"
    break;

  case 906:
#line 5649 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToCompPin((yyvsp[-2].string), (yyvsp[-1].string));
      }
#line 11041 "def.tab.c"
    break;

  case 907:
#line 5653 "def.y"
                      {defData->dumb_mode=1; defData->no_num = 2; }
#line 11047 "def.tab.c"
    break;

  case 908:
#line 5654 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setToIOPin((yyvsp[-1].string));
      }
#line 11056 "def.tab.c"
    break;

  case 909:
#line 5659 "def.y"
      { 
        if (defData->callbacks->PartitionsExtCbk)
          CALLBACK(defData->callbacks->PartitionsExtCbk, defrPartitionsExtCbkType,
                   &defData->History_text[0]);
      }
#line 11066 "def.tab.c"
    break;

  case 910:
#line 5666 "def.y"
      { defData->dumb_mode = DEF_MAX_INT; defData->no_num = DEF_MAX_INT; }
#line 11072 "def.tab.c"
    break;

  case 911:
#line 5667 "def.y"
      { defData->dumb_mode = 0; defData->no_num = 0; }
#line 11078 "def.tab.c"
    break;

  case 913:
#line 5671 "def.y"
      { }
#line 11084 "def.tab.c"
    break;

  case 914:
#line 5674 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setMin((yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 11093 "def.tab.c"
    break;

  case 915:
#line 5679 "def.y"
      {
        if (defData->callbacks->PartitionCbk)
          defData->Partition.setMax((yyvsp[-1].dval), (yyvsp[0].dval));
      }
#line 11102 "def.tab.c"
    break;

  case 917:
#line 5686 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addPin((yyvsp[0].string)); }
#line 11108 "def.tab.c"
    break;

  case 920:
#line 5692 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addRiseMin((yyvsp[0].dval)); }
#line 11114 "def.tab.c"
    break;

  case 921:
#line 5694 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addFallMin((yyvsp[0].dval)); }
#line 11120 "def.tab.c"
    break;

  case 922:
#line 5696 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addRiseMax((yyvsp[0].dval)); }
#line 11126 "def.tab.c"
    break;

  case 923:
#line 5698 "def.y"
      { if (defData->callbacks->PartitionCbk) defData->Partition.addFallMax((yyvsp[0].dval)); }
#line 11132 "def.tab.c"
    break;

  case 926:
#line 5706 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addRiseMinRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11139 "def.tab.c"
    break;

  case 927:
#line 5709 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addFallMinRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11146 "def.tab.c"
    break;

  case 928:
#line 5712 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addRiseMaxRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11153 "def.tab.c"
    break;

  case 929:
#line 5715 "def.y"
      { if (defData->callbacks->PartitionCbk)
          defData->Partition.addFallMaxRange((yyvsp[-1].dval), (yyvsp[0].dval)); }
#line 11160 "def.tab.c"
    break;

  case 930:
#line 5719 "def.y"
      { if (defData->callbacks->PartitionsEndCbk)
          CALLBACK(defData->callbacks->PartitionsEndCbk, defrPartitionsEndCbkType, 0); }
#line 11167 "def.tab.c"
    break;

  case 932:
#line 5724 "def.y"
      { }
#line 11173 "def.tab.c"
    break;

  case 933:
#line 5726 "def.y"
               {defData->dumb_mode=2; defData->no_num = 2; }
#line 11179 "def.tab.c"
    break;

  case 934:
#line 5728 "def.y"
      {
        // note that the defData->first T_STRING could be the keyword VPIN 
        if (defData->callbacks->NetCbk)
          defData->Subnet->addPin((yyvsp[-3].string), (yyvsp[-2].string), (yyvsp[-1].integer));
      }
#line 11189 "def.tab.c"
    break;

  case 935:
#line 5735 "def.y"
      { (yyval.integer) = 0; }
#line 11195 "def.tab.c"
    break;

  case 936:
#line 5737 "def.y"
      { 
        (yyval.integer) = 1; 
      }
#line 11203 "def.tab.c"
    break;

  case 939:
#line 5745 "def.y"
      {
        if (defData->callbacks->NetCbk) {
            defData->Wire = new defiWire(defData);
            defData->Wire->Init((yyvsp[0].string), NULL);
            defData->Subnet->addWire(defData->Wire);
        }
      }
#line 11215 "def.tab.c"
    break;

  case 940:
#line 5753 "def.y"
      {  
        defData->by_is_keyword = FALSE;
        defData->do_is_keyword = FALSE;
        defData->new_is_keyword = FALSE;
        defData->step_is_keyword = FALSE;
        defData->orient_is_keyword = FALSE;
        defData->needNPCbk = 0;
        defData->Wire = NULL;
      }
#line 11229 "def.tab.c"
    break;

  case 941:
#line 5762 "def.y"
                         { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11235 "def.tab.c"
    break;

  case 942:
#line 5763 "def.y"
      { if (defData->callbacks->NetCbk) defData->Subnet->setNonDefault((yyvsp[0].string)); }
#line 11241 "def.tab.c"
    break;

  case 943:
#line 5766 "def.y"
      { (yyval.string) = (char*)"FIXED"; defData->dumb_mode = 1; }
#line 11247 "def.tab.c"
    break;

  case 944:
#line 5768 "def.y"
      { (yyval.string) = (char*)"COVER"; defData->dumb_mode = 1; }
#line 11253 "def.tab.c"
    break;

  case 945:
#line 5770 "def.y"
      { (yyval.string) = (char*)"ROUTED"; defData->dumb_mode = 1; }
#line 11259 "def.tab.c"
    break;

  case 946:
#line 5772 "def.y"
      { (yyval.string) = (char*)"NOSHIELD"; defData->dumb_mode = 1; }
#line 11265 "def.tab.c"
    break;

  case 948:
#line 5777 "def.y"
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
#line 11281 "def.tab.c"
    break;

  case 949:
#line 5791 "def.y"
      { }
#line 11287 "def.tab.c"
    break;

  case 950:
#line 5793 "def.y"
      { }
#line 11293 "def.tab.c"
    break;

  case 951:
#line 5796 "def.y"
      { if (defData->callbacks->PinPropEndCbk)
          CALLBACK(defData->callbacks->PinPropEndCbk, defrPinPropEndCbkType, 0); }
#line 11300 "def.tab.c"
    break;

  case 954:
#line 5803 "def.y"
                       { defData->dumb_mode = 2; defData->no_num = 2; }
#line 11306 "def.tab.c"
    break;

  case 955:
#line 5804 "def.y"
      { if (defData->callbacks->PinPropCbk) defData->PinProp.setName((yyvsp[-1].string), (yyvsp[0].string)); }
#line 11312 "def.tab.c"
    break;

  case 956:
#line 5806 "def.y"
      { if (defData->callbacks->PinPropCbk) {
          CALLBACK(defData->callbacks->PinPropCbk, defrPinPropCbkType, &defData->PinProp);
         // reset the property number
         defData->PinProp.clear();
        }
      }
#line 11323 "def.tab.c"
    break;

  case 959:
#line 5816 "def.y"
                         { defData->dumb_mode = DEF_MAX_INT; }
#line 11329 "def.tab.c"
    break;

  case 960:
#line 5818 "def.y"
      { defData->dumb_mode = 0; }
#line 11335 "def.tab.c"
    break;

  case 963:
#line 5825 "def.y"
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
#line 11350 "def.tab.c"
    break;

  case 964:
#line 5836 "def.y"
      {
        if (defData->callbacks->PinPropCbk) {
          char propTp;
          propTp = defData->session->CompPinProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "PINPROPERTIES");
          defData->PinProp.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 11363 "def.tab.c"
    break;

  case 965:
#line 5845 "def.y"
      {
        if (defData->callbacks->PinPropCbk) {
          char propTp;
          propTp = defData->session->CompPinProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "PINPROPERTIES");
          defData->PinProp.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 11376 "def.tab.c"
    break;

  case 967:
#line 5857 "def.y"
      { if (defData->callbacks->BlockageStartCbk)
          CALLBACK(defData->callbacks->BlockageStartCbk, defrBlockageStartCbkType, ROUND((yyvsp[-1].dval))); }
#line 11383 "def.tab.c"
    break;

  case 968:
#line 5861 "def.y"
      { if (defData->callbacks->BlockageEndCbk)
          CALLBACK(defData->callbacks->BlockageEndCbk, defrBlockageEndCbkType, 0); }
#line 11390 "def.tab.c"
    break;

  case 971:
#line 5870 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
          CALLBACK(defData->callbacks->BlockageCbk, defrBlockageCbkType, &defData->Blockage);
          defData->Blockage.clear();
        }
      }
#line 11401 "def.tab.c"
    break;

  case 972:
#line 5877 "def.y"
                           { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11407 "def.tab.c"
    break;

  case 973:
#line 5878 "def.y"
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
#line 11427 "def.tab.c"
    break;

  case 975:
#line 5897 "def.y"
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
#line 11447 "def.tab.c"
    break;

  case 979:
#line 5919 "def.y"
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
#line 11477 "def.tab.c"
    break;

  case 980:
#line 5945 "def.y"
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
#line 11507 "def.tab.c"
    break;

  case 981:
#line 5972 "def.y"
      {      
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->blockageWarnings, defData->settings->BlockageWarnings)) {
          defData->Blockage.setMask((int)(yyvsp[0].dval));
        }
      }
#line 11517 "def.tab.c"
    break;

  case 982:
#line 5978 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11523 "def.tab.c"
    break;

  case 983:
#line 5979 "def.y"
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
#line 11539 "def.tab.c"
    break;

  case 984:
#line 5991 "def.y"
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
#line 11555 "def.tab.c"
    break;

  case 985:
#line 6003 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 11563 "def.tab.c"
    break;

  case 986:
#line 6007 "def.y"
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
#line 11583 "def.tab.c"
    break;

  case 988:
#line 6027 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11589 "def.tab.c"
    break;

  case 989:
#line 6028 "def.y"
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
#line 11611 "def.tab.c"
    break;

  case 990:
#line 6047 "def.y"
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
#line 11639 "def.tab.c"
    break;

  case 991:
#line 6071 "def.y"
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
#line 11664 "def.tab.c"
    break;

  case 992:
#line 6092 "def.y"
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
#line 11686 "def.tab.c"
    break;

  case 993:
#line 6110 "def.y"
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
#line 11721 "def.tab.c"
    break;

  case 994:
#line 6141 "def.y"
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
#line 11750 "def.tab.c"
    break;

  case 997:
#line 6172 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11756 "def.tab.c"
    break;

  case 998:
#line 6173 "def.y"
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
#line 11778 "def.tab.c"
    break;

  case 999:
#line 6190 "def.y"
                   { defData->dumb_mode = 1; defData->no_num = 1; }
#line 11784 "def.tab.c"
    break;

  case 1000:
#line 6191 "def.y"
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
#line 11800 "def.tab.c"
    break;

  case 1001:
#line 6203 "def.y"
      { 
        defData->dumb_mode = 2; 
      }
#line 11808 "def.tab.c"
    break;

  case 1002:
#line 6207 "def.y"
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
#line 11828 "def.tab.c"
    break;

  case 1003:
#line 6223 "def.y"
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
#line 11850 "def.tab.c"
    break;

  case 1004:
#line 6241 "def.y"
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
#line 11872 "def.tab.c"
    break;

  case 1005:
#line 6259 "def.y"
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
#line 11914 "def.tab.c"
    break;

  case 1006:
#line 6297 "def.y"
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
#line 11936 "def.tab.c"
    break;

  case 1007:
#line 6316 "def.y"
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
#line 11980 "def.tab.c"
    break;

  case 1008:
#line 6357 "def.y"
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
#line 12000 "def.tab.c"
    break;

  case 1011:
#line 6378 "def.y"
      {
        if (defData->callbacks->BlockageCbk)
          defData->Blockage.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
      }
#line 12009 "def.tab.c"
    break;

  case 1012:
#line 6383 "def.y"
      {
        if (defData->callbacks->BlockageCbk) {
            defData->Geometries.Reset();
        }
      }
#line 12019 "def.tab.c"
    break;

  case 1013:
#line 6389 "def.y"
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
#line 12041 "def.tab.c"
    break;

  case 1015:
#line 6411 "def.y"
      { 
        if (defData->VersionNum >= 6.0 - 0.00001) {
            if (defData->def60ObsoletedError("SLOTS ... END SLOTS")) {
                CHKERR();
            }
        } else if (defData->callbacks->SlotStartCbk) {
            CALLBACK(defData->callbacks->SlotStartCbk, defrSlotStartCbkType, ROUND((yyvsp[-1].dval))); 
        }
      }
#line 12055 "def.tab.c"
    break;

  case 1016:
#line 6422 "def.y"
      { if (defData->callbacks->SlotEndCbk)
          CALLBACK(defData->callbacks->SlotEndCbk, defrSlotEndCbkType, 0); 
      }
#line 12063 "def.tab.c"
    break;

  case 1019:
#line 6431 "def.y"
      {
        if (defData->callbacks->SlotCbk) {
          CALLBACK(defData->callbacks->SlotCbk, defrSlotCbkType, &defData->Slot);
          defData->Slot.clear();
        }
      }
#line 12074 "def.tab.c"
    break;

  case 1020:
#line 6438 "def.y"
                       { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12080 "def.tab.c"
    break;

  case 1021:
#line 6439 "def.y"
      {
        if (defData->callbacks->SlotCbk) {
          defData->Slot.setLayer((yyvsp[0].string));
          defData->Slot.clearPoly();     // free poly, if any
        }
      }
#line 12091 "def.tab.c"
    break;

  case 1025:
#line 6451 "def.y"
      {
        if (defData->callbacks->SlotCbk)
          defData->Slot.addRect((yyvsp[-1].pt).x, (yyvsp[-1].pt).y, (yyvsp[0].pt).x, (yyvsp[0].pt).y);
      }
#line 12100 "def.tab.c"
    break;

  case 1026:
#line 6456 "def.y"
      {
          defData->Geometries.Reset();
      }
#line 12108 "def.tab.c"
    break;

  case 1027:
#line 6460 "def.y"
      {
        if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond
          if (defData->callbacks->SlotCbk)
            defData->Slot.addPolygon(&defData->Geometries);
        }
      }
#line 12119 "def.tab.c"
    break;

  case 1029:
#line 6471 "def.y"
      { if (defData->callbacks->FillStartCbk)
          CALLBACK(defData->callbacks->FillStartCbk, defrFillStartCbkType, ROUND((yyvsp[-1].dval))); }
#line 12126 "def.tab.c"
    break;

  case 1030:
#line 6475 "def.y"
      { if (defData->callbacks->FillEndCbk)
          CALLBACK(defData->callbacks->FillEndCbk, defrFillEndCbkType, 0); }
#line 12133 "def.tab.c"
    break;

  case 1033:
#line 6482 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12139 "def.tab.c"
    break;

  case 1034:
#line 6483 "def.y"
      {
        if (defData->callbacks->FillCbk) {
            defData->Fill.setLayer((yyvsp[0].string));
            defData->Fill.clearShapes();    // Free shapes, if any.
        }
      }
#line 12150 "def.tab.c"
    break;

  case 1035:
#line 6490 "def.y"
      {
        if (defData->callbacks->FillCbk) {
            CALLBACK(defData->callbacks->FillCbk, defrFillCbkType, &defData->Fill);
            defData->Fill.Destroy();
            defData->Fill.Init();
        }
      }
#line 12162 "def.tab.c"
    break;

  case 1036:
#line 6497 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12168 "def.tab.c"
    break;

  case 1037:
#line 6498 "def.y"
      {
        if (defData->callbacks->FillCbk) {
          defData->Fill.setVia((yyvsp[0].string));
          defData->Fill.clearPts();
          defData->Geometries.Reset();
          defData->Orients.clear();
        }
      }
#line 12181 "def.tab.c"
    break;

  case 1038:
#line 6507 "def.y"
      {
        if (defData->callbacks->FillCbk) {
          defData->Fill.addPts(&defData->Geometries, &defData->Orients);
          CALLBACK(defData->callbacks->FillCbk, defrFillCbkType, &defData->Fill);
        }

        defData->Fill.clear();
        defData->Orients.clear();
      }
#line 12195 "def.tab.c"
    break;

  case 1041:
#line 6522 "def.y"
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
#line 12212 "def.tab.c"
    break;

  case 1042:
#line 6535 "def.y"
      {
        defData->Geometries.Reset();
      }
#line 12220 "def.tab.c"
    break;

  case 1043:
#line 6539 "def.y"
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
#line 12246 "def.tab.c"
    break;

  case 1049:
#line 6572 "def.y"
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
#line 12268 "def.tab.c"
    break;

  case 1050:
#line 6591 "def.y"
          { 
            defData->Geometries.startList((yyvsp[0].pt).x, (yyvsp[0].pt).y); 
            defData->Orients.push_back((yyvsp[-1].integer));
          }
#line 12277 "def.tab.c"
    break;

  case 1051:
#line 6597 "def.y"
          { 
            defData->Geometries.addToList((yyvsp[0].pt).x, (yyvsp[0].pt).y); 
            defData->Orients.push_back((yyvsp[-1].integer));
          }
#line 12286 "def.tab.c"
    break;

  case 1054:
#line 6607 "def.y"
    {
        (yyval.integer) = 0;
    }
#line 12294 "def.tab.c"
    break;

  case 1055:
#line 6611 "def.y"
    {
        if (defData->VersionNum < 6.0 - 0.00001) {
            if (defData->def60NewSyntaxError("FILLS ... - VIA ... orient pt  ...")) {
                CHKERR();
            }
        } 

        (yyval.integer) = (yyvsp[0].integer);
    }
#line 12308 "def.tab.c"
    break;

  case 1061:
#line 6631 "def.y"
    { 
        defData->dumb_mode = 2; 
    }
#line 12316 "def.tab.c"
    break;

  case 1062:
#line 6635 "def.y"
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
#line 12336 "def.tab.c"
    break;

  case 1063:
#line 6652 "def.y"
    {
        defData->dumb_mode = 2; 
    }
#line 12344 "def.tab.c"
    break;

  case 1064:
#line 6656 "def.y"
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
#line 12364 "def.tab.c"
    break;

  case 1065:
#line 6675 "def.y"
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
#line 12386 "def.tab.c"
    break;

  case 1066:
#line 6695 "def.y"
      { 
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->fillWarnings, defData->settings->FillWarnings)) {
             if (defData->callbacks->FillCbk) {
                defData->Fill.setMask((int)(yyvsp[0].dval));
             }
        }
      }
#line 12398 "def.tab.c"
    break;

  case 1067:
#line 6705 "def.y"
      { 
        if (defData->validateMaskInput((int)(yyvsp[0].dval), defData->fillWarnings, defData->settings->FillWarnings)) {
             if (defData->callbacks->FillCbk) {
                defData->Fill.setMask((int)(yyvsp[0].dval));
             }
        }
      }
#line 12410 "def.tab.c"
    break;

  case 1069:
#line 6718 "def.y"
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
#line 12431 "def.tab.c"
    break;

  case 1070:
#line 6736 "def.y"
      { if (defData->callbacks->NonDefaultEndCbk)
          CALLBACK(defData->callbacks->NonDefaultEndCbk, defrNonDefaultEndCbkType, 0); }
#line 12438 "def.tab.c"
    break;

  case 1073:
#line 6743 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12444 "def.tab.c"
    break;

  case 1074:
#line 6744 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.clear(); 
          defData->NonDefault.setName((yyvsp[0].string));
        }
      }
#line 12455 "def.tab.c"
    break;

  case 1075:
#line 6751 "def.y"
      { if (defData->callbacks->NonDefaultCbk)
          CALLBACK(defData->callbacks->NonDefaultCbk, defrNonDefaultCbkType, &defData->NonDefault); }
#line 12462 "def.tab.c"
    break;

  case 1078:
#line 6759 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk)
          defData->NonDefault.setHardspacing();
      }
#line 12471 "def.tab.c"
    break;

  case 1079:
#line 6763 "def.y"
                    { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12477 "def.tab.c"
    break;

  case 1080:
#line 6765 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addLayer((yyvsp[-2].string));
          defData->NonDefault.addWidth((yyvsp[0].dval));
        }
      }
#line 12488 "def.tab.c"
    break;

  case 1082:
#line 6772 "def.y"
                  { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12494 "def.tab.c"
    break;

  case 1083:
#line 6773 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addVia((yyvsp[0].string));
        }
      }
#line 12504 "def.tab.c"
    break;

  case 1084:
#line 6778 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12510 "def.tab.c"
    break;

  case 1085:
#line 6779 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addViaRule((yyvsp[0].string));
        }
      }
#line 12520 "def.tab.c"
    break;

  case 1086:
#line 6784 "def.y"
                      { defData->dumb_mode = 1; defData->no_num = 1; }
#line 12526 "def.tab.c"
    break;

  case 1087:
#line 6785 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addMinCuts((yyvsp[-1].string), (int)(yyvsp[0].dval));
        }
      }
#line 12536 "def.tab.c"
    break;

  case 1091:
#line 6798 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addDiagWidth((yyvsp[0].dval));
        }
      }
#line 12546 "def.tab.c"
    break;

  case 1092:
#line 6804 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addSpacing((yyvsp[0].dval));
        }
      }
#line 12556 "def.tab.c"
    break;

  case 1093:
#line 6810 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          defData->NonDefault.addWireExt((yyvsp[0].dval));
        }
      }
#line 12566 "def.tab.c"
    break;

  case 1094:
#line 6817 "def.y"
                                    { defData->dumb_mode = DEF_MAX_INT;  }
#line 12572 "def.tab.c"
    break;

  case 1095:
#line 6819 "def.y"
      { defData->dumb_mode = 0; }
#line 12578 "def.tab.c"
    break;

  case 1098:
#line 6826 "def.y"
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
#line 12593 "def.tab.c"
    break;

  case 1099:
#line 6837 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          char propTp;
          propTp = defData->session->NDefProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "NONDEFAULTRULE");
          defData->NonDefault.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 12606 "def.tab.c"
    break;

  case 1100:
#line 6846 "def.y"
      {
        if (defData->callbacks->NonDefaultCbk) {
          char propTp;
          propTp = defData->session->NDefProp.propType((yyvsp[-1].string));
          CHKPROPTYPE(propTp, (yyvsp[-1].string), "NONDEFAULTRULE");
          defData->NonDefault.addProperty((yyvsp[-1].string), (yyvsp[0].string), propTp);
        }
      }
#line 12619 "def.tab.c"
    break;

  case 1102:
#line 6859 "def.y"
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
#line 12643 "def.tab.c"
    break;

  case 1103:
#line 6880 "def.y"
      { if (defData->callbacks->StylesEndCbk)
          CALLBACK(defData->callbacks->StylesEndCbk, defrStylesEndCbkType, 0); }
#line 12650 "def.tab.c"
    break;

  case 1106:
#line 6888 "def.y"
      {
        if (defData->callbacks->StylesCbk) defData->Styles.setStyle((int)(yyvsp[0].dval));
        defData->Geometries.Reset();
      }
#line 12659 "def.tab.c"
    break;

  case 1107:
#line 6893 "def.y"
      {
        if (defData->VersionNum >= 5.6) {  // only 5.6 and beyond will call the callback
          if (defData->callbacks->StylesCbk) {
            defData->Styles.setPolygon(&defData->Geometries);
            CALLBACK(defData->callbacks->StylesCbk, defrStylesCbkType, &defData->Styles);
          }
        }
      }
#line 12672 "def.tab.c"
    break;


#line 12676 "def.tab.c"

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
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

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
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (defData, YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *, YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (defData, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
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

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
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
                  yystos[yystate], yyvsp, defData);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (defData, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif


/*-----------------------------------------------------.
| yyreturn -- parsing is finished, return the result.  |
`-----------------------------------------------------*/
yyreturn:
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
                  yystos[+*yyssp], yyvsp, defData);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 6903 "def.y"


END_LEFDEF_PARSER_NAMESPACE
