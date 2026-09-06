/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_DEFYY_DEF_TAB_HPP_INCLUDED
# define YY_DEFYY_DEF_TAB_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int defyydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    QSTRING = 258,                 /* QSTRING  */
    T_STRING = 259,                /* T_STRING  */
    SITE_PATTERN = 260,            /* SITE_PATTERN  */
    NUMBER = 261,                  /* NUMBER  */
    K_HISTORY = 262,               /* K_HISTORY  */
    K_NAME = 263,                  /* K_NAME  */
    K_NAMESCASESENSITIVE = 264,    /* K_NAMESCASESENSITIVE  */
    K_DESIGN = 265,                /* K_DESIGN  */
    K_VIAS = 266,                  /* K_VIAS  */
    K_TECH = 267,                  /* K_TECH  */
    K_UNITS = 268,                 /* K_UNITS  */
    K_ARRAY = 269,                 /* K_ARRAY  */
    K_FLOORPLAN = 270,             /* K_FLOORPLAN  */
    K_SITE = 271,                  /* K_SITE  */
    K_CANPLACE = 272,              /* K_CANPLACE  */
    K_CANNOTOCCUPY = 273,          /* K_CANNOTOCCUPY  */
    K_DIEAREA = 274,               /* K_DIEAREA  */
    K_PINS = 275,                  /* K_PINS  */
    K_PINSHAPE = 276,              /* K_PINSHAPE  */
    K_DEFAULTCAP = 277,            /* K_DEFAULTCAP  */
    K_MINPINS = 278,               /* K_MINPINS  */
    K_WIRECAP = 279,               /* K_WIRECAP  */
    K_TRACKS = 280,                /* K_TRACKS  */
    K_GCELLGRID = 281,             /* K_GCELLGRID  */
    K_DO = 282,                    /* K_DO  */
    K_BY = 283,                    /* K_BY  */
    K_STEP = 284,                  /* K_STEP  */
    K_LAYER = 285,                 /* K_LAYER  */
    K_ROW = 286,                   /* K_ROW  */
    K_RECT = 287,                  /* K_RECT  */
    K_COMPS = 288,                 /* K_COMPS  */
    K_COMP_GEN = 289,              /* K_COMP_GEN  */
    K_SOURCE = 290,                /* K_SOURCE  */
    K_WEIGHT = 291,                /* K_WEIGHT  */
    K_EEQMASTER = 292,             /* K_EEQMASTER  */
    K_FIXED = 293,                 /* K_FIXED  */
    K_COVER = 294,                 /* K_COVER  */
    K_UNPLACED = 295,              /* K_UNPLACED  */
    K_PLACED = 296,                /* K_PLACED  */
    K_FOREIGN = 297,               /* K_FOREIGN  */
    K_REGION = 298,                /* K_REGION  */
    K_REGIONS = 299,               /* K_REGIONS  */
    K_NETS = 300,                  /* K_NETS  */
    K_START_NET = 301,             /* K_START_NET  */
    K_MUSTJOIN = 302,              /* K_MUSTJOIN  */
    K_ORIGINAL = 303,              /* K_ORIGINAL  */
    K_USE = 304,                   /* K_USE  */
    K_STYLE = 305,                 /* K_STYLE  */
    K_PATTERN = 306,               /* K_PATTERN  */
    K_PATTERNNAME = 307,           /* K_PATTERNNAME  */
    K_ESTCAP = 308,                /* K_ESTCAP  */
    K_ROUTED = 309,                /* K_ROUTED  */
    K_NEW = 310,                   /* K_NEW  */
    K_SNETS = 311,                 /* K_SNETS  */
    K_SHAPE = 312,                 /* K_SHAPE  */
    K_WIDTH = 313,                 /* K_WIDTH  */
    K_VOLTAGE = 314,               /* K_VOLTAGE  */
    K_SPACING = 315,               /* K_SPACING  */
    K_NONDEFAULTRULE = 316,        /* K_NONDEFAULTRULE  */
    K_NONDEFAULTRULES = 317,       /* K_NONDEFAULTRULES  */
    K_NOFLOPS = 318,               /* K_NOFLOPS  */
    K_N = 319,                     /* K_N  */
    K_S = 320,                     /* K_S  */
    K_E = 321,                     /* K_E  */
    K_W = 322,                     /* K_W  */
    K_FN = 323,                    /* K_FN  */
    K_FE = 324,                    /* K_FE  */
    K_FS = 325,                    /* K_FS  */
    K_FW = 326,                    /* K_FW  */
    K_GROUPS = 327,                /* K_GROUPS  */
    K_GROUP = 328,                 /* K_GROUP  */
    K_SOFT = 329,                  /* K_SOFT  */
    K_MAXX = 330,                  /* K_MAXX  */
    K_MAXY = 331,                  /* K_MAXY  */
    K_MAXHALFPERIMETER = 332,      /* K_MAXHALFPERIMETER  */
    K_CONSTRAINTS = 333,           /* K_CONSTRAINTS  */
    K_NET = 334,                   /* K_NET  */
    K_PATH = 335,                  /* K_PATH  */
    K_SUM = 336,                   /* K_SUM  */
    K_DIFF = 337,                  /* K_DIFF  */
    K_SCANCHAINS = 338,            /* K_SCANCHAINS  */
    K_START = 339,                 /* K_START  */
    K_FLOATING = 340,              /* K_FLOATING  */
    K_ORDERED = 341,               /* K_ORDERED  */
    K_STOP = 342,                  /* K_STOP  */
    K_IN = 343,                    /* K_IN  */
    K_OUT = 344,                   /* K_OUT  */
    K_RISEMIN = 345,               /* K_RISEMIN  */
    K_RISEMAX = 346,               /* K_RISEMAX  */
    K_FALLMIN = 347,               /* K_FALLMIN  */
    K_FALLMAX = 348,               /* K_FALLMAX  */
    K_WIREDLOGIC = 349,            /* K_WIREDLOGIC  */
    K_MAXDIST = 350,               /* K_MAXDIST  */
    K_ASSERTIONS = 351,            /* K_ASSERTIONS  */
    K_DISTANCE = 352,              /* K_DISTANCE  */
    K_MICRONS = 353,               /* K_MICRONS  */
    K_NDR = 354,                   /* K_NDR  */
    K_END = 355,                   /* K_END  */
    K_POWERDOMAIN = 356,           /* K_POWERDOMAIN  */
    K_HINSTS = 357,                /* K_HINSTS  */
    K_IOTIMINGS = 358,             /* K_IOTIMINGS  */
    K_RISE = 359,                  /* K_RISE  */
    K_FALL = 360,                  /* K_FALL  */
    K_VARIABLE = 361,              /* K_VARIABLE  */
    K_SLEWRATE = 362,              /* K_SLEWRATE  */
    K_CAPACITANCE = 363,           /* K_CAPACITANCE  */
    K_DRIVECELL = 364,             /* K_DRIVECELL  */
    K_FROMPIN = 365,               /* K_FROMPIN  */
    K_TOPIN = 366,                 /* K_TOPIN  */
    K_PARALLEL = 367,              /* K_PARALLEL  */
    K_TIMINGDISABLES = 368,        /* K_TIMINGDISABLES  */
    K_THRUPIN = 369,               /* K_THRUPIN  */
    K_MACRO = 370,                 /* K_MACRO  */
    K_PARTITIONS = 371,            /* K_PARTITIONS  */
    K_TURNOFF = 372,               /* K_TURNOFF  */
    K_COMPONENTS = 373,            /* K_COMPONENTS  */
    K_FROMCLOCKPIN = 374,          /* K_FROMCLOCKPIN  */
    K_FROMCOMPPIN = 375,           /* K_FROMCOMPPIN  */
    K_FROMIOPIN = 376,             /* K_FROMIOPIN  */
    K_TOCLOCKPIN = 377,            /* K_TOCLOCKPIN  */
    K_TOCOMPPIN = 378,             /* K_TOCOMPPIN  */
    K_TOIOPIN = 379,               /* K_TOIOPIN  */
    K_SETUPRISE = 380,             /* K_SETUPRISE  */
    K_SETUPFALL = 381,             /* K_SETUPFALL  */
    K_HOLDRISE = 382,              /* K_HOLDRISE  */
    K_HOLDFALL = 383,              /* K_HOLDFALL  */
    K_VPIN = 384,                  /* K_VPIN  */
    K_SUBNET = 385,                /* K_SUBNET  */
    K_XTALK = 386,                 /* K_XTALK  */
    K_PIN = 387,                   /* K_PIN  */
    K_SYNTHESIZED = 388,           /* K_SYNTHESIZED  */
    K_IF = 389,                    /* K_IF  */
    K_THEN = 390,                  /* K_THEN  */
    K_ELSE = 391,                  /* K_ELSE  */
    K_FALSE = 392,                 /* K_FALSE  */
    K_TRUE = 393,                  /* K_TRUE  */
    K_EQ = 394,                    /* K_EQ  */
    K_NE = 395,                    /* K_NE  */
    K_LE = 396,                    /* K_LE  */
    K_LT = 397,                    /* K_LT  */
    K_GE = 398,                    /* K_GE  */
    K_GT = 399,                    /* K_GT  */
    K_OR = 400,                    /* K_OR  */
    K_AND = 401,                   /* K_AND  */
    K_NOT = 402,                   /* K_NOT  */
    K_SPECIAL = 403,               /* K_SPECIAL  */
    K_DIRECTION = 404,             /* K_DIRECTION  */
    K_RANGE = 405,                 /* K_RANGE  */
    K_WIRE = 406,                  /* K_WIRE  */
    K_FPC = 407,                   /* K_FPC  */
    K_HORIZONTAL = 408,            /* K_HORIZONTAL  */
    K_VERTICAL = 409,              /* K_VERTICAL  */
    K_ALIGN = 410,                 /* K_ALIGN  */
    K_MIN = 411,                   /* K_MIN  */
    K_MAX = 412,                   /* K_MAX  */
    K_EQUAL = 413,                 /* K_EQUAL  */
    K_BOTTOMLEFT = 414,            /* K_BOTTOMLEFT  */
    K_TOPRIGHT = 415,              /* K_TOPRIGHT  */
    K_ROWS = 416,                  /* K_ROWS  */
    K_TAPER = 417,                 /* K_TAPER  */
    K_TAPERRULE = 418,             /* K_TAPERRULE  */
    K_VERSION = 419,               /* K_VERSION  */
    K_DIVIDERCHAR = 420,           /* K_DIVIDERCHAR  */
    K_BUSBITCHARS = 421,           /* K_BUSBITCHARS  */
    K_PROPERTYDEFINITIONS = 422,   /* K_PROPERTYDEFINITIONS  */
    K_STRING = 423,                /* K_STRING  */
    K_REAL = 424,                  /* K_REAL  */
    K_INTEGER = 425,               /* K_INTEGER  */
    K_PROPERTY = 426,              /* K_PROPERTY  */
    K_BEGINEXT = 427,              /* K_BEGINEXT  */
    K_ENDEXT = 428,                /* K_ENDEXT  */
    K_NAMEMAPSTRING = 429,         /* K_NAMEMAPSTRING  */
    K_ON = 430,                    /* K_ON  */
    K_OFF = 431,                   /* K_OFF  */
    K_X = 432,                     /* K_X  */
    K_Y = 433,                     /* K_Y  */
    K_COMPONENT = 434,             /* K_COMPONENT  */
    K_MASK = 435,                  /* K_MASK  */
    K_MASKSHIFT = 436,             /* K_MASKSHIFT  */
    K_COMPSMASKSHIFT = 437,        /* K_COMPSMASKSHIFT  */
    K_SAMEMASK = 438,              /* K_SAMEMASK  */
    K_PINPROPERTIES = 439,         /* K_PINPROPERTIES  */
    K_TEST = 440,                  /* K_TEST  */
    K_ONLYBLOCKS = 441,            /* K_ONLYBLOCKS  */
    K_COMMONSCANPINS = 442,        /* K_COMMONSCANPINS  */
    K_SNET = 443,                  /* K_SNET  */
    K_COMPONENTPIN = 444,          /* K_COMPONENTPIN  */
    K_REENTRANTPATHS = 445,        /* K_REENTRANTPATHS  */
    K_SHIELD = 446,                /* K_SHIELD  */
    K_SHIELDNET = 447,             /* K_SHIELDNET  */
    K_NOSHIELD = 448,              /* K_NOSHIELD  */
    K_VIRTUAL = 449,               /* K_VIRTUAL  */
    K_ANTENNAPINPARTIALMETALAREA = 450, /* K_ANTENNAPINPARTIALMETALAREA  */
    K_ANTENNAPINPARTIALMETALSIDEAREA = 451, /* K_ANTENNAPINPARTIALMETALSIDEAREA  */
    K_ANTENNAPINGATEAREA = 452,    /* K_ANTENNAPINGATEAREA  */
    K_ANTENNAPINDIFFAREA = 453,    /* K_ANTENNAPINDIFFAREA  */
    K_ANTENNAPINMAXAREACAR = 454,  /* K_ANTENNAPINMAXAREACAR  */
    K_ANTENNAPINMAXSIDEAREACAR = 455, /* K_ANTENNAPINMAXSIDEAREACAR  */
    K_ANTENNAPINPARTIALCUTAREA = 456, /* K_ANTENNAPINPARTIALCUTAREA  */
    K_ANTENNAPINMAXCUTCAR = 457,   /* K_ANTENNAPINMAXCUTCAR  */
    K_SIGNAL = 458,                /* K_SIGNAL  */
    K_POWER = 459,                 /* K_POWER  */
    K_GROUND = 460,                /* K_GROUND  */
    K_CLOCK = 461,                 /* K_CLOCK  */
    K_TIEOFF = 462,                /* K_TIEOFF  */
    K_ANALOG = 463,                /* K_ANALOG  */
    K_SCAN = 464,                  /* K_SCAN  */
    K_RESET = 465,                 /* K_RESET  */
    K_RING = 466,                  /* K_RING  */
    K_STRIPE = 467,                /* K_STRIPE  */
    K_FOLLOWPIN = 468,             /* K_FOLLOWPIN  */
    K_IOWIRE = 469,                /* K_IOWIRE  */
    K_COREWIRE = 470,              /* K_COREWIRE  */
    K_BLOCKWIRE = 471,             /* K_BLOCKWIRE  */
    K_FILLWIRE = 472,              /* K_FILLWIRE  */
    K_BLOCKAGEWIRE = 473,          /* K_BLOCKAGEWIRE  */
    K_PADRING = 474,               /* K_PADRING  */
    K_BLOCKRING = 475,             /* K_BLOCKRING  */
    K_BLOCKAGES = 476,             /* K_BLOCKAGES  */
    K_PLACEMENT = 477,             /* K_PLACEMENT  */
    K_SLOTS = 478,                 /* K_SLOTS  */
    K_FILLS = 479,                 /* K_FILLS  */
    K_PUSHDOWN = 480,              /* K_PUSHDOWN  */
    K_NETLIST = 481,               /* K_NETLIST  */
    K_DIST = 482,                  /* K_DIST  */
    K_USER = 483,                  /* K_USER  */
    K_TIMING = 484,                /* K_TIMING  */
    K_BALANCED = 485,              /* K_BALANCED  */
    K_STEINER = 486,               /* K_STEINER  */
    K_TRUNK = 487,                 /* K_TRUNK  */
    K_FIXEDBUMP = 488,             /* K_FIXEDBUMP  */
    K_FENCE = 489,                 /* K_FENCE  */
    K_FREQUENCY = 490,             /* K_FREQUENCY  */
    K_GUIDE = 491,                 /* K_GUIDE  */
    K_MAXBITS = 492,               /* K_MAXBITS  */
    K_PARTITION = 493,             /* K_PARTITION  */
    K_TYPE = 494,                  /* K_TYPE  */
    K_ANTENNAMODEL = 495,          /* K_ANTENNAMODEL  */
    K_DRCFILL = 496,               /* K_DRCFILL  */
    K_OXIDE1 = 497,                /* K_OXIDE1  */
    K_OXIDE2 = 498,                /* K_OXIDE2  */
    K_OXIDE3 = 499,                /* K_OXIDE3  */
    K_OXIDE4 = 500,                /* K_OXIDE4  */
    K_OXIDE5 = 501,                /* K_OXIDE5  */
    K_OXIDE6 = 502,                /* K_OXIDE6  */
    K_OXIDE7 = 503,                /* K_OXIDE7  */
    K_OXIDE8 = 504,                /* K_OXIDE8  */
    K_OXIDE9 = 505,                /* K_OXIDE9  */
    K_OXIDE10 = 506,               /* K_OXIDE10  */
    K_OXIDE11 = 507,               /* K_OXIDE11  */
    K_OXIDE12 = 508,               /* K_OXIDE12  */
    K_OXIDE13 = 509,               /* K_OXIDE13  */
    K_OXIDE14 = 510,               /* K_OXIDE14  */
    K_OXIDE15 = 511,               /* K_OXIDE15  */
    K_OXIDE16 = 512,               /* K_OXIDE16  */
    K_OXIDE17 = 513,               /* K_OXIDE17  */
    K_OXIDE18 = 514,               /* K_OXIDE18  */
    K_OXIDE19 = 515,               /* K_OXIDE19  */
    K_OXIDE20 = 516,               /* K_OXIDE20  */
    K_OXIDE21 = 517,               /* K_OXIDE21  */
    K_OXIDE22 = 518,               /* K_OXIDE22  */
    K_OXIDE23 = 519,               /* K_OXIDE23  */
    K_OXIDE24 = 520,               /* K_OXIDE24  */
    K_OXIDE25 = 521,               /* K_OXIDE25  */
    K_OXIDE26 = 522,               /* K_OXIDE26  */
    K_OXIDE27 = 523,               /* K_OXIDE27  */
    K_OXIDE28 = 524,               /* K_OXIDE28  */
    K_OXIDE29 = 525,               /* K_OXIDE29  */
    K_OXIDE30 = 526,               /* K_OXIDE30  */
    K_OXIDE31 = 527,               /* K_OXIDE31  */
    K_OXIDE32 = 528,               /* K_OXIDE32  */
    K_CUTSIZE = 529,               /* K_CUTSIZE  */
    K_CUTSPACING = 530,            /* K_CUTSPACING  */
    K_DESIGNRULEWIDTH = 531,       /* K_DESIGNRULEWIDTH  */
    K_DIAGWIDTH = 532,             /* K_DIAGWIDTH  */
    K_ENCLOSURE = 533,             /* K_ENCLOSURE  */
    K_HALO = 534,                  /* K_HALO  */
    K_GROUNDSENSITIVITY = 535,     /* K_GROUNDSENSITIVITY  */
    K_PHYSICAL = 536,              /* K_PHYSICAL  */
    K_HARDSPACING = 537,           /* K_HARDSPACING  */
    K_LAYERS = 538,                /* K_LAYERS  */
    K_MINCUTS = 539,               /* K_MINCUTS  */
    K_NETEXPR = 540,               /* K_NETEXPR  */
    K_PINPROPERTY = 541,           /* K_PINPROPERTY  */
    K_OFFSET = 542,                /* K_OFFSET  */
    K_ORIGIN = 543,                /* K_ORIGIN  */
    K_ROWCOL = 544,                /* K_ROWCOL  */
    K_STYLES = 545,                /* K_STYLES  */
    K_SOFTFIXED = 546,             /* K_SOFTFIXED  */
    K_POLYGON = 547,               /* K_POLYGON  */
    K_PORT = 548,                  /* K_PORT  */
    K_SUPPLYSENSITIVITY = 549,     /* K_SUPPLYSENSITIVITY  */
    K_VIA = 550,                   /* K_VIA  */
    K_VIARULE = 551,               /* K_VIARULE  */
    K_WIREEXT = 552,               /* K_WIREEXT  */
    K_EXCEPTPGNET = 553,           /* K_EXCEPTPGNET  */
    K_ONLYPGNET = 554,             /* K_ONLYPGNET  */
    K_FILLWIREOPC = 555,           /* K_FILLWIREOPC  */
    K_OPC = 556,                   /* K_OPC  */
    K_PARTIAL = 557,               /* K_PARTIAL  */
    K_ROUTEHALO = 558,             /* K_ROUTEHALO  */
    K_BLOCKAGE = 559,              /* K_BLOCKAGE  */
    K_ROUTE = 560,                 /* K_ROUTE  */
    K_SCANCHAIN = 561,             /* K_SCANCHAIN  */
    K_SPECIALROUTE = 562,          /* K_SPECIALROUTE  */
    K_TRACK = 563                  /* K_TRACK  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */




int defyyparse (defrData *defData);


#endif /* !YY_DEFYY_DEF_TAB_HPP_INCLUDED  */
