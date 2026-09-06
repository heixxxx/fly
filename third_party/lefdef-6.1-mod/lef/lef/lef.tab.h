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

#ifndef YY_LEFYY_LEF_TAB_H_INCLUDED
# define YY_LEFYY_LEF_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int lefyydebug;
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
    K_HISTORY = 258,               /* K_HISTORY  */
    K_ABUT = 259,                  /* K_ABUT  */
    K_ABUTMENT = 260,              /* K_ABUTMENT  */
    K_ACTIVE = 261,                /* K_ACTIVE  */
    K_ANALOG = 262,                /* K_ANALOG  */
    K_ARRAY = 263,                 /* K_ARRAY  */
    K_AREA = 264,                  /* K_AREA  */
    K_BLOCK = 265,                 /* K_BLOCK  */
    K_BOTTOMLEFT = 266,            /* K_BOTTOMLEFT  */
    K_BOTTOMRIGHT = 267,           /* K_BOTTOMRIGHT  */
    K_BY = 268,                    /* K_BY  */
    K_CAPACITANCE = 269,           /* K_CAPACITANCE  */
    K_CAPMULTIPLIER = 270,         /* K_CAPMULTIPLIER  */
    K_CLASS = 271,                 /* K_CLASS  */
    K_CLOCK = 272,                 /* K_CLOCK  */
    K_CLOCKTYPE = 273,             /* K_CLOCKTYPE  */
    K_COLUMNMAJOR = 274,           /* K_COLUMNMAJOR  */
    K_DESIGNRULEWIDTH = 275,       /* K_DESIGNRULEWIDTH  */
    K_INFLUENCE = 276,             /* K_INFLUENCE  */
    K_CORE = 277,                  /* K_CORE  */
    K_CORNER = 278,                /* K_CORNER  */
    K_COVER = 279,                 /* K_COVER  */
    K_CPERSQDIST = 280,            /* K_CPERSQDIST  */
    K_CURRENT = 281,               /* K_CURRENT  */
    K_CURRENTSOURCE = 282,         /* K_CURRENTSOURCE  */
    K_CUT = 283,                   /* K_CUT  */
    K_DEFAULT = 284,               /* K_DEFAULT  */
    K_DATABASE = 285,              /* K_DATABASE  */
    K_DATA = 286,                  /* K_DATA  */
    K_DIELECTRIC = 287,            /* K_DIELECTRIC  */
    K_DIRECTION = 288,             /* K_DIRECTION  */
    K_DO = 289,                    /* K_DO  */
    K_EDGECAPACITANCE = 290,       /* K_EDGECAPACITANCE  */
    K_EEQ = 291,                   /* K_EEQ  */
    K_END = 292,                   /* K_END  */
    K_ENDCAP = 293,                /* K_ENDCAP  */
    K_FALL = 294,                  /* K_FALL  */
    K_FALLCS = 295,                /* K_FALLCS  */
    K_FALLT0 = 296,                /* K_FALLT0  */
    K_FALLSATT1 = 297,             /* K_FALLSATT1  */
    K_FALLRS = 298,                /* K_FALLRS  */
    K_FALLSATCUR = 299,            /* K_FALLSATCUR  */
    K_FALLTHRESH = 300,            /* K_FALLTHRESH  */
    K_FEEDTHRU = 301,              /* K_FEEDTHRU  */
    K_FIXED = 302,                 /* K_FIXED  */
    K_FOREIGN = 303,               /* K_FOREIGN  */
    K_FROMPIN = 304,               /* K_FROMPIN  */
    K_GENERATE = 305,              /* K_GENERATE  */
    K_GENERATOR = 306,             /* K_GENERATOR  */
    K_GROUND = 307,                /* K_GROUND  */
    K_HEIGHT = 308,                /* K_HEIGHT  */
    K_HORIZONTAL = 309,            /* K_HORIZONTAL  */
    K_INOUT = 310,                 /* K_INOUT  */
    K_INPUT = 311,                 /* K_INPUT  */
    K_INPUTNOISEMARGIN = 312,      /* K_INPUTNOISEMARGIN  */
    K_COMPONENTPIN = 313,          /* K_COMPONENTPIN  */
    K_INTRINSIC = 314,             /* K_INTRINSIC  */
    K_INVERT = 315,                /* K_INVERT  */
    K_IRDROP = 316,                /* K_IRDROP  */
    K_ITERATE = 317,               /* K_ITERATE  */
    K_IV_TABLES = 318,             /* K_IV_TABLES  */
    K_LAYER = 319,                 /* K_LAYER  */
    K_LEAKAGE = 320,               /* K_LEAKAGE  */
    K_LEQ = 321,                   /* K_LEQ  */
    K_LIBRARY = 322,               /* K_LIBRARY  */
    K_MACRO = 323,                 /* K_MACRO  */
    K_MATCH = 324,                 /* K_MATCH  */
    K_MAXDELAY = 325,              /* K_MAXDELAY  */
    K_MAXLOAD = 326,               /* K_MAXLOAD  */
    K_METALOVERHANG = 327,         /* K_METALOVERHANG  */
    K_MILLIAMPS = 328,             /* K_MILLIAMPS  */
    K_MILLIWATTS = 329,            /* K_MILLIWATTS  */
    K_MINFEATURE = 330,            /* K_MINFEATURE  */
    K_MUSTJOIN = 331,              /* K_MUSTJOIN  */
    K_NAMESCASESENSITIVE = 332,    /* K_NAMESCASESENSITIVE  */
    K_NANOSECONDS = 333,           /* K_NANOSECONDS  */
    K_NETS = 334,                  /* K_NETS  */
    K_NEW = 335,                   /* K_NEW  */
    K_NONDEFAULTRULE = 336,        /* K_NONDEFAULTRULE  */
    K_NONINVERT = 337,             /* K_NONINVERT  */
    K_NONUNATE = 338,              /* K_NONUNATE  */
    K_OBS = 339,                   /* K_OBS  */
    K_OHMS = 340,                  /* K_OHMS  */
    K_OFFSET = 341,                /* K_OFFSET  */
    K_ORIENTATION = 342,           /* K_ORIENTATION  */
    K_ORIGIN = 343,                /* K_ORIGIN  */
    K_OUTPUT = 344,                /* K_OUTPUT  */
    K_OUTPUTNOISEMARGIN = 345,     /* K_OUTPUTNOISEMARGIN  */
    K_OVERHANG = 346,              /* K_OVERHANG  */
    K_OVERLAP = 347,               /* K_OVERLAP  */
    K_OFF = 348,                   /* K_OFF  */
    K_ON = 349,                    /* K_ON  */
    K_OVERLAPS = 350,              /* K_OVERLAPS  */
    K_PAD = 351,                   /* K_PAD  */
    K_PATH = 352,                  /* K_PATH  */
    K_PATTERN = 353,               /* K_PATTERN  */
    K_PICOFARADS = 354,            /* K_PICOFARADS  */
    K_PIN = 355,                   /* K_PIN  */
    K_PITCH = 356,                 /* K_PITCH  */
    K_PLACED = 357,                /* K_PLACED  */
    K_POLYGON = 358,               /* K_POLYGON  */
    K_PORT = 359,                  /* K_PORT  */
    K_POST = 360,                  /* K_POST  */
    K_POWER = 361,                 /* K_POWER  */
    K_PRE = 362,                   /* K_PRE  */
    K_PULLDOWNRES = 363,           /* K_PULLDOWNRES  */
    K_RECT = 364,                  /* K_RECT  */
    K_RESISTANCE = 365,            /* K_RESISTANCE  */
    K_RESISTIVE = 366,             /* K_RESISTIVE  */
    K_RING = 367,                  /* K_RING  */
    K_RISE = 368,                  /* K_RISE  */
    K_RISECS = 369,                /* K_RISECS  */
    K_RISERS = 370,                /* K_RISERS  */
    K_RISESATCUR = 371,            /* K_RISESATCUR  */
    K_RISETHRESH = 372,            /* K_RISETHRESH  */
    K_RISESATT1 = 373,             /* K_RISESATT1  */
    K_RISET0 = 374,                /* K_RISET0  */
    K_RISEVOLTAGETHRESHOLD = 375,  /* K_RISEVOLTAGETHRESHOLD  */
    K_FALLVOLTAGETHRESHOLD = 376,  /* K_FALLVOLTAGETHRESHOLD  */
    K_ROUTING = 377,               /* K_ROUTING  */
    K_ROWMAJOR = 378,              /* K_ROWMAJOR  */
    K_RPERSQ = 379,                /* K_RPERSQ  */
    K_SAMENET = 380,               /* K_SAMENET  */
    K_SCANUSE = 381,               /* K_SCANUSE  */
    K_SHAPE = 382,                 /* K_SHAPE  */
    K_SHRINKAGE = 383,             /* K_SHRINKAGE  */
    K_SIGNAL = 384,                /* K_SIGNAL  */
    K_SITE = 385,                  /* K_SITE  */
    K_SIZE = 386,                  /* K_SIZE  */
    K_SOURCE = 387,                /* K_SOURCE  */
    K_SPACER = 388,                /* K_SPACER  */
    K_SPACING = 389,               /* K_SPACING  */
    K_SPECIALNETS = 390,           /* K_SPECIALNETS  */
    K_STACK = 391,                 /* K_STACK  */
    K_START = 392,                 /* K_START  */
    K_STEP = 393,                  /* K_STEP  */
    K_STOP = 394,                  /* K_STOP  */
    K_STRUCTURE = 395,             /* K_STRUCTURE  */
    K_SYMMETRY = 396,              /* K_SYMMETRY  */
    K_TABLE = 397,                 /* K_TABLE  */
    K_THICKNESS = 398,             /* K_THICKNESS  */
    K_TIEHIGH = 399,               /* K_TIEHIGH  */
    K_TIELOW = 400,                /* K_TIELOW  */
    K_TIEOFFR = 401,               /* K_TIEOFFR  */
    K_TIME = 402,                  /* K_TIME  */
    K_TIMING = 403,                /* K_TIMING  */
    K_TO = 404,                    /* K_TO  */
    K_TOPIN = 405,                 /* K_TOPIN  */
    K_TOPLEFT = 406,               /* K_TOPLEFT  */
    K_TOPRIGHT = 407,              /* K_TOPRIGHT  */
    K_TOPOFSTACKONLY = 408,        /* K_TOPOFSTACKONLY  */
    K_PORTOBS = 409,               /* K_PORTOBS  */
    K_TRISTATE = 410,              /* K_TRISTATE  */
    K_TYPE = 411,                  /* K_TYPE  */
    K_UNATENESS = 412,             /* K_UNATENESS  */
    K_UNITS = 413,                 /* K_UNITS  */
    K_USE = 414,                   /* K_USE  */
    K_VARIABLE = 415,              /* K_VARIABLE  */
    K_VERTICAL = 416,              /* K_VERTICAL  */
    K_VHI = 417,                   /* K_VHI  */
    K_VIA = 418,                   /* K_VIA  */
    K_VIARULE = 419,               /* K_VIARULE  */
    K_VLO = 420,                   /* K_VLO  */
    K_VOLTAGE = 421,               /* K_VOLTAGE  */
    K_VOLTS = 422,                 /* K_VOLTS  */
    K_WIDTH = 423,                 /* K_WIDTH  */
    K_X = 424,                     /* K_X  */
    K_Y = 425,                     /* K_Y  */
    T_STRING = 426,                /* T_STRING  */
    QSTRING = 427,                 /* QSTRING  */
    NUMBER = 428,                  /* NUMBER  */
    K_N = 429,                     /* K_N  */
    K_S = 430,                     /* K_S  */
    K_E = 431,                     /* K_E  */
    K_W = 432,                     /* K_W  */
    K_FN = 433,                    /* K_FN  */
    K_FS = 434,                    /* K_FS  */
    K_FE = 435,                    /* K_FE  */
    K_FW = 436,                    /* K_FW  */
    K_R0 = 437,                    /* K_R0  */
    K_R90 = 438,                   /* K_R90  */
    K_R180 = 439,                  /* K_R180  */
    K_R270 = 440,                  /* K_R270  */
    K_MX = 441,                    /* K_MX  */
    K_MY = 442,                    /* K_MY  */
    K_MXR90 = 443,                 /* K_MXR90  */
    K_MYR90 = 444,                 /* K_MYR90  */
    K_USER = 445,                  /* K_USER  */
    K_MASTERSLICE = 446,           /* K_MASTERSLICE  */
    K_ENDMACRO = 447,              /* K_ENDMACRO  */
    K_ENDMACROPIN = 448,           /* K_ENDMACROPIN  */
    K_ENDVIARULE = 449,            /* K_ENDVIARULE  */
    K_ENDVIA = 450,                /* K_ENDVIA  */
    K_ENDLAYER = 451,              /* K_ENDLAYER  */
    K_ENDSITE = 452,               /* K_ENDSITE  */
    K_CANPLACE = 453,              /* K_CANPLACE  */
    K_CANNOTOCCUPY = 454,          /* K_CANNOTOCCUPY  */
    K_TRACKS = 455,                /* K_TRACKS  */
    K_FLOORPLAN = 456,             /* K_FLOORPLAN  */
    K_GCELLGRID = 457,             /* K_GCELLGRID  */
    K_DEFAULTCAP = 458,            /* K_DEFAULTCAP  */
    K_MINPINS = 459,               /* K_MINPINS  */
    K_WIRECAP = 460,               /* K_WIRECAP  */
    K_STABLE = 461,                /* K_STABLE  */
    K_SETUP = 462,                 /* K_SETUP  */
    K_HOLD = 463,                  /* K_HOLD  */
    K_DEFINE = 464,                /* K_DEFINE  */
    K_DEFINES = 465,               /* K_DEFINES  */
    K_DEFINEB = 466,               /* K_DEFINEB  */
    K_IF = 467,                    /* K_IF  */
    K_THEN = 468,                  /* K_THEN  */
    K_ELSE = 469,                  /* K_ELSE  */
    K_FALSE = 470,                 /* K_FALSE  */
    K_TRUE = 471,                  /* K_TRUE  */
    K_EQ = 472,                    /* K_EQ  */
    K_NE = 473,                    /* K_NE  */
    K_LE = 474,                    /* K_LE  */
    K_LT = 475,                    /* K_LT  */
    K_GE = 476,                    /* K_GE  */
    K_GT = 477,                    /* K_GT  */
    K_OR = 478,                    /* K_OR  */
    K_AND = 479,                   /* K_AND  */
    K_NOT = 480,                   /* K_NOT  */
    K_DELAY = 481,                 /* K_DELAY  */
    K_TABLEDIMENSION = 482,        /* K_TABLEDIMENSION  */
    K_TABLEAXIS = 483,             /* K_TABLEAXIS  */
    K_TABLEENTRIES = 484,          /* K_TABLEENTRIES  */
    K_TRANSITIONTIME = 485,        /* K_TRANSITIONTIME  */
    K_EXTENSION = 486,             /* K_EXTENSION  */
    K_PROPDEF = 487,               /* K_PROPDEF  */
    K_STRING = 488,                /* K_STRING  */
    K_INTEGER = 489,               /* K_INTEGER  */
    K_REAL = 490,                  /* K_REAL  */
    K_RANGE = 491,                 /* K_RANGE  */
    K_PROPERTY = 492,              /* K_PROPERTY  */
    K_VIRTUAL = 493,               /* K_VIRTUAL  */
    K_BUSBITCHARS = 494,           /* K_BUSBITCHARS  */
    K_VERSION = 495,               /* K_VERSION  */
    K_BEGINEXT = 496,              /* K_BEGINEXT  */
    K_ENDEXT = 497,                /* K_ENDEXT  */
    K_UNIVERSALNOISEMARGIN = 498,  /* K_UNIVERSALNOISEMARGIN  */
    K_EDGERATETHRESHOLD1 = 499,    /* K_EDGERATETHRESHOLD1  */
    K_CORRECTIONTABLE = 500,       /* K_CORRECTIONTABLE  */
    K_EDGERATESCALEFACTOR = 501,   /* K_EDGERATESCALEFACTOR  */
    K_EDGERATETHRESHOLD2 = 502,    /* K_EDGERATETHRESHOLD2  */
    K_VICTIMNOISE = 503,           /* K_VICTIMNOISE  */
    K_NOISETABLE = 504,            /* K_NOISETABLE  */
    K_EDGERATE = 505,              /* K_EDGERATE  */
    K_OUTPUTRESISTANCE = 506,      /* K_OUTPUTRESISTANCE  */
    K_VICTIMLENGTH = 507,          /* K_VICTIMLENGTH  */
    K_CORRECTIONFACTOR = 508,      /* K_CORRECTIONFACTOR  */
    K_OUTPUTPINANTENNASIZE = 509,  /* K_OUTPUTPINANTENNASIZE  */
    K_INPUTPINANTENNASIZE = 510,   /* K_INPUTPINANTENNASIZE  */
    K_INOUTPINANTENNASIZE = 511,   /* K_INOUTPINANTENNASIZE  */
    K_CURRENTDEN = 512,            /* K_CURRENTDEN  */
    K_PWL = 513,                   /* K_PWL  */
    K_ANTENNALENGTHFACTOR = 514,   /* K_ANTENNALENGTHFACTOR  */
    K_TAPERRULE = 515,             /* K_TAPERRULE  */
    K_DIVIDERCHAR = 516,           /* K_DIVIDERCHAR  */
    K_ANTENNASIZE = 517,           /* K_ANTENNASIZE  */
    K_ANTENNAMETALLENGTH = 518,    /* K_ANTENNAMETALLENGTH  */
    K_ANTENNAMETALAREA = 519,      /* K_ANTENNAMETALAREA  */
    K_RISESLEWLIMIT = 520,         /* K_RISESLEWLIMIT  */
    K_FALLSLEWLIMIT = 521,         /* K_FALLSLEWLIMIT  */
    K_FUNCTION = 522,              /* K_FUNCTION  */
    K_BUFFER = 523,                /* K_BUFFER  */
    K_INVERTER = 524,              /* K_INVERTER  */
    K_NAMEMAPSTRING = 525,         /* K_NAMEMAPSTRING  */
    K_NOWIREEXTENSIONATPIN = 526,  /* K_NOWIREEXTENSIONATPIN  */
    K_WIREEXTENSION = 527,         /* K_WIREEXTENSION  */
    K_MESSAGE = 528,               /* K_MESSAGE  */
    K_CREATEFILE = 529,            /* K_CREATEFILE  */
    K_OPENFILE = 530,              /* K_OPENFILE  */
    K_CLOSEFILE = 531,             /* K_CLOSEFILE  */
    K_WARNING = 532,               /* K_WARNING  */
    K_ERROR = 533,                 /* K_ERROR  */
    K_FATALERROR = 534,            /* K_FATALERROR  */
    K_RECOVERY = 535,              /* K_RECOVERY  */
    K_SKEW = 536,                  /* K_SKEW  */
    K_ANYEDGE = 537,               /* K_ANYEDGE  */
    K_POSEDGE = 538,               /* K_POSEDGE  */
    K_NEGEDGE = 539,               /* K_NEGEDGE  */
    K_SDFCONDSTART = 540,          /* K_SDFCONDSTART  */
    K_SDFCONDEND = 541,            /* K_SDFCONDEND  */
    K_SDFCOND = 542,               /* K_SDFCOND  */
    K_MPWH = 543,                  /* K_MPWH  */
    K_MPWL = 544,                  /* K_MPWL  */
    K_PERIOD = 545,                /* K_PERIOD  */
    K_ACCURRENTDENSITY = 546,      /* K_ACCURRENTDENSITY  */
    K_DCCURRENTDENSITY = 547,      /* K_DCCURRENTDENSITY  */
    K_AVERAGE = 548,               /* K_AVERAGE  */
    K_PEAK = 549,                  /* K_PEAK  */
    K_RMS = 550,                   /* K_RMS  */
    K_FREQUENCY = 551,             /* K_FREQUENCY  */
    K_CUTAREA = 552,               /* K_CUTAREA  */
    K_MEGAHERTZ = 553,             /* K_MEGAHERTZ  */
    K_USELENGTHTHRESHOLD = 554,    /* K_USELENGTHTHRESHOLD  */
    K_LENGTHTHRESHOLD = 555,       /* K_LENGTHTHRESHOLD  */
    K_ANTENNAINPUTGATEAREA = 556,  /* K_ANTENNAINPUTGATEAREA  */
    K_ANTENNAINOUTDIFFAREA = 557,  /* K_ANTENNAINOUTDIFFAREA  */
    K_ANTENNAOUTPUTDIFFAREA = 558, /* K_ANTENNAOUTPUTDIFFAREA  */
    K_ANTENNAAREARATIO = 559,      /* K_ANTENNAAREARATIO  */
    K_ANTENNADIFFAREARATIO = 560,  /* K_ANTENNADIFFAREARATIO  */
    K_ANTENNACUMAREARATIO = 561,   /* K_ANTENNACUMAREARATIO  */
    K_ANTENNACUMDIFFAREARATIO = 562, /* K_ANTENNACUMDIFFAREARATIO  */
    K_ANTENNAAREAFACTOR = 563,     /* K_ANTENNAAREAFACTOR  */
    K_ANTENNASIDEAREARATIO = 564,  /* K_ANTENNASIDEAREARATIO  */
    K_ANTENNADIFFSIDEAREARATIO = 565, /* K_ANTENNADIFFSIDEAREARATIO  */
    K_ANTENNACUMSIDEAREARATIO = 566, /* K_ANTENNACUMSIDEAREARATIO  */
    K_ANTENNACUMDIFFSIDEAREARATIO = 567, /* K_ANTENNACUMDIFFSIDEAREARATIO  */
    K_ANTENNASIDEAREAFACTOR = 568, /* K_ANTENNASIDEAREAFACTOR  */
    K_DIFFUSEONLY = 569,           /* K_DIFFUSEONLY  */
    K_MANUFACTURINGGRID = 570,     /* K_MANUFACTURINGGRID  */
    K_FIXEDMASK = 571,             /* K_FIXEDMASK  */
    K_ANTENNACELL = 572,           /* K_ANTENNACELL  */
    K_CLEARANCEMEASURE = 573,      /* K_CLEARANCEMEASURE  */
    K_EUCLIDEAN = 574,             /* K_EUCLIDEAN  */
    K_MAXXY = 575,                 /* K_MAXXY  */
    K_USEMINSPACING = 576,         /* K_USEMINSPACING  */
    K_ROWMINSPACING = 577,         /* K_ROWMINSPACING  */
    K_ROWABUTSPACING = 578,        /* K_ROWABUTSPACING  */
    K_FLIP = 579,                  /* K_FLIP  */
    K_NONE = 580,                  /* K_NONE  */
    K_ANTENNAPARTIALMETALAREA = 581, /* K_ANTENNAPARTIALMETALAREA  */
    K_ANTENNAPARTIALMETALSIDEAREA = 582, /* K_ANTENNAPARTIALMETALSIDEAREA  */
    K_ANTENNAGATEAREA = 583,       /* K_ANTENNAGATEAREA  */
    K_ANTENNADIFFAREA = 584,       /* K_ANTENNADIFFAREA  */
    K_ANTENNAMAXAREACAR = 585,     /* K_ANTENNAMAXAREACAR  */
    K_ANTENNAMAXSIDEAREACAR = 586, /* K_ANTENNAMAXSIDEAREACAR  */
    K_ANTENNAPARTIALCUTAREA = 587, /* K_ANTENNAPARTIALCUTAREA  */
    K_ANTENNAMAXCUTCAR = 588,      /* K_ANTENNAMAXCUTCAR  */
    K_SLOTWIREWIDTH = 589,         /* K_SLOTWIREWIDTH  */
    K_SLOTWIRELENGTH = 590,        /* K_SLOTWIRELENGTH  */
    K_SLOTWIDTH = 591,             /* K_SLOTWIDTH  */
    K_SLOTLENGTH = 592,            /* K_SLOTLENGTH  */
    K_MAXADJACENTSLOTSPACING = 593, /* K_MAXADJACENTSLOTSPACING  */
    K_MAXCOAXIALSLOTSPACING = 594, /* K_MAXCOAXIALSLOTSPACING  */
    K_MAXEDGESLOTSPACING = 595,    /* K_MAXEDGESLOTSPACING  */
    K_SPLITWIREWIDTH = 596,        /* K_SPLITWIREWIDTH  */
    K_MINIMUMDENSITY = 597,        /* K_MINIMUMDENSITY  */
    K_MAXIMUMDENSITY = 598,        /* K_MAXIMUMDENSITY  */
    K_DENSITYCHECKWINDOW = 599,    /* K_DENSITYCHECKWINDOW  */
    K_DENSITYCHECKSTEP = 600,      /* K_DENSITYCHECKSTEP  */
    K_FILLACTIVESPACING = 601,     /* K_FILLACTIVESPACING  */
    K_MINIMUMCUT = 602,            /* K_MINIMUMCUT  */
    K_ADJACENTCUTS = 603,          /* K_ADJACENTCUTS  */
    K_ANTENNAMODEL = 604,          /* K_ANTENNAMODEL  */
    K_BUMP = 605,                  /* K_BUMP  */
    K_ENCLOSURE = 606,             /* K_ENCLOSURE  */
    K_FROMABOVE = 607,             /* K_FROMABOVE  */
    K_FROMBELOW = 608,             /* K_FROMBELOW  */
    K_IMPLANT = 609,               /* K_IMPLANT  */
    K_LENGTH = 610,                /* K_LENGTH  */
    K_MAXVIASTACK = 611,           /* K_MAXVIASTACK  */
    K_AREAIO = 612,                /* K_AREAIO  */
    K_BLACKBOX = 613,              /* K_BLACKBOX  */
    K_MAXWIDTH = 614,              /* K_MAXWIDTH  */
    K_MINENCLOSEDAREA = 615,       /* K_MINENCLOSEDAREA  */
    K_MINSTEP = 616,               /* K_MINSTEP  */
    K_ORIENT = 617,                /* K_ORIENT  */
    K_OXIDE1 = 618,                /* K_OXIDE1  */
    K_OXIDE2 = 619,                /* K_OXIDE2  */
    K_OXIDE3 = 620,                /* K_OXIDE3  */
    K_OXIDE4 = 621,                /* K_OXIDE4  */
    K_OXIDE5 = 622,                /* K_OXIDE5  */
    K_OXIDE6 = 623,                /* K_OXIDE6  */
    K_OXIDE7 = 624,                /* K_OXIDE7  */
    K_OXIDE8 = 625,                /* K_OXIDE8  */
    K_OXIDE9 = 626,                /* K_OXIDE9  */
    K_OXIDE10 = 627,               /* K_OXIDE10  */
    K_OXIDE11 = 628,               /* K_OXIDE11  */
    K_OXIDE12 = 629,               /* K_OXIDE12  */
    K_OXIDE13 = 630,               /* K_OXIDE13  */
    K_OXIDE14 = 631,               /* K_OXIDE14  */
    K_OXIDE15 = 632,               /* K_OXIDE15  */
    K_OXIDE16 = 633,               /* K_OXIDE16  */
    K_OXIDE17 = 634,               /* K_OXIDE17  */
    K_OXIDE18 = 635,               /* K_OXIDE18  */
    K_OXIDE19 = 636,               /* K_OXIDE19  */
    K_OXIDE20 = 637,               /* K_OXIDE20  */
    K_OXIDE21 = 638,               /* K_OXIDE21  */
    K_OXIDE22 = 639,               /* K_OXIDE22  */
    K_OXIDE23 = 640,               /* K_OXIDE23  */
    K_OXIDE24 = 641,               /* K_OXIDE24  */
    K_OXIDE25 = 642,               /* K_OXIDE25  */
    K_OXIDE26 = 643,               /* K_OXIDE26  */
    K_OXIDE27 = 644,               /* K_OXIDE27  */
    K_OXIDE28 = 645,               /* K_OXIDE28  */
    K_OXIDE29 = 646,               /* K_OXIDE29  */
    K_OXIDE30 = 647,               /* K_OXIDE30  */
    K_OXIDE31 = 648,               /* K_OXIDE31  */
    K_OXIDE32 = 649,               /* K_OXIDE32  */
    K_PARALLELRUNLENGTH = 650,     /* K_PARALLELRUNLENGTH  */
    K_MINWIDTH = 651,              /* K_MINWIDTH  */
    K_PROTRUSIONWIDTH = 652,       /* K_PROTRUSIONWIDTH  */
    K_SPACINGTABLE = 653,          /* K_SPACINGTABLE  */
    K_WITHIN = 654,                /* K_WITHIN  */
    K_ABOVE = 655,                 /* K_ABOVE  */
    K_BELOW = 656,                 /* K_BELOW  */
    K_CENTERTOCENTER = 657,        /* K_CENTERTOCENTER  */
    K_CUTSIZE = 658,               /* K_CUTSIZE  */
    K_CUTSPACING = 659,            /* K_CUTSPACING  */
    K_DENSITY = 660,               /* K_DENSITY  */
    K_DIAG45 = 661,                /* K_DIAG45  */
    K_DIAG135 = 662,               /* K_DIAG135  */
    K_MASK = 663,                  /* K_MASK  */
    K_DIAGMINEDGELENGTH = 664,     /* K_DIAGMINEDGELENGTH  */
    K_DIAGSPACING = 665,           /* K_DIAGSPACING  */
    K_DIAGPITCH = 666,             /* K_DIAGPITCH  */
    K_DIAGWIDTH = 667,             /* K_DIAGWIDTH  */
    K_GENERATED = 668,             /* K_GENERATED  */
    K_GROUNDSENSITIVITY = 669,     /* K_GROUNDSENSITIVITY  */
    K_HARDSPACING = 670,           /* K_HARDSPACING  */
    K_INSIDECORNER = 671,          /* K_INSIDECORNER  */
    K_LAYERS = 672,                /* K_LAYERS  */
    K_LENGTHSUM = 673,             /* K_LENGTHSUM  */
    K_MICRONS = 674,               /* K_MICRONS  */
    K_MINCUTS = 675,               /* K_MINCUTS  */
    K_MINSIZE = 676,               /* K_MINSIZE  */
    K_NETEXPR = 677,               /* K_NETEXPR  */
    K_OUTSIDECORNER = 678,         /* K_OUTSIDECORNER  */
    K_PREFERENCLOSURE = 679,       /* K_PREFERENCLOSURE  */
    K_ROWCOL = 680,                /* K_ROWCOL  */
    K_ROWPATTERN = 681,            /* K_ROWPATTERN  */
    K_SOFT = 682,                  /* K_SOFT  */
    K_SUPPLYSENSITIVITY = 683,     /* K_SUPPLYSENSITIVITY  */
    K_USEVIA = 684,                /* K_USEVIA  */
    K_USEVIARULE = 685,            /* K_USEVIARULE  */
    K_WELLTAP = 686,               /* K_WELLTAP  */
    K_ARRAYCUTS = 687,             /* K_ARRAYCUTS  */
    K_ARRAYSPACING = 688,          /* K_ARRAYSPACING  */
    K_ANTENNAAREADIFFREDUCEPWL = 689, /* K_ANTENNAAREADIFFREDUCEPWL  */
    K_ANTENNAAREAMINUSDIFF = 690,  /* K_ANTENNAAREAMINUSDIFF  */
    K_NOROUTE = 691,               /* K_NOROUTE  */
    K_ABSTRACT = 692,              /* K_ABSTRACT  */
    K_ANTENNACUMROUTINGPLUSCUT = 693, /* K_ANTENNACUMROUTINGPLUSCUT  */
    K_ANTENNAGATEPLUSDIFF = 694,   /* K_ANTENNAGATEPLUSDIFF  */
    K_ENDOFLINE = 695,             /* K_ENDOFLINE  */
    K_ENDOFNOTCHWIDTH = 696,       /* K_ENDOFNOTCHWIDTH  */
    K_EXCEPTEXTRACUT = 697,        /* K_EXCEPTEXTRACUT  */
    K_EXCEPTSAMEPGNET = 698,       /* K_EXCEPTSAMEPGNET  */
    K_EXCEPTPGNET = 699,           /* K_EXCEPTPGNET  */
    K_OBSSPACING = 700,            /* K_OBSSPACING  */
    K_FULLDRC = 701,               /* K_FULLDRC  */
    K_MIN = 702,                   /* K_MIN  */
    K_LONGARRAY = 703,             /* K_LONGARRAY  */
    K_MAXEDGES = 704,              /* K_MAXEDGES  */
    K_NOTCHLENGTH = 705,           /* K_NOTCHLENGTH  */
    K_NOTCHSPACING = 706,          /* K_NOTCHSPACING  */
    K_ORTHOGONAL = 707,            /* K_ORTHOGONAL  */
    K_PARALLELEDGE = 708,          /* K_PARALLELEDGE  */
    K_PARALLELOVERLAP = 709,       /* K_PARALLELOVERLAP  */
    K_PGONLY = 710,                /* K_PGONLY  */
    K_PRL = 711,                   /* K_PRL  */
    K_TWOEDGES = 712,              /* K_TWOEDGES  */
    K_TWOWIDTHS = 713,             /* K_TWOWIDTHS  */
    IF = 714,                      /* IF  */
    LNOT = 715,                    /* LNOT  */
    UMINUS = 716                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 194 "lef.y"

        double    dval ;
        int       integer ;
        char *    string ;
        LefDefParser::lefPOINT  pt;
        LefDefParser::lefiProp*  prop;

#line 533 "lef.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE lefyylval;


int lefyyparse (void);


#endif /* !YY_LEFYY_LEF_TAB_H_INCLUDED  */
