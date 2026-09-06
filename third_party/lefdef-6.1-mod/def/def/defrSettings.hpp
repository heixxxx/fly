// *****************************************************************************
// *****************************************************************************
// Copyright 2013-2019, Cadence Design Systems
// 
// This  file  is  part  of  the  Cadence  LEF/DEF  Open   Source
// Distribution,  Product Version 5.8. 
// 
// Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
// 
//        http://www.apache.org/licenses/LICENSE-2.0
// 
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//    implied. See the License for the specific language governing
//    permissions and limitations under the License.
// 
// For updates, support, or to become part of the LEF/DEF Community,
// check www.openeda.org for details.
// 
//  $Author: arakhman $
//  $Revision: #6 $
//  $Date: 2013/08/09 $
//  $State:  $
// *****************************************************************************
// *****************************************************************************

#ifndef defrSettings_h
#define defrSettings_h

#include "defrReader.hpp"

#include <cstring>
#include <string>
#include <map>

#if defined(DEF_KEYWORD_UNORDERED_MAP)
#include <unordered_map>
#include <string_view>
#endif

#define  DEF_DEBUG_IDS 100
#define  defMaxOxides 32

BEGIN_LEFDEF_PARSER_NAMESPACE

struct defCompareCStrings 
{
    bool operator()(const char* lhs, const char* rhs) const {
        return std::strcmp(lhs, rhs) < 0;
    }
};

#if defined(DEF_KEYWORD_UNORDERED_MAP)
typedef std::unordered_map<std::string_view, int> defKeywordMap;
#elif defined(DEF_KEYWORD_STD_MAP)
typedef std::map<const char*, int, defCompareCStrings>  defKeywordMap;
#else
typedef std::map<const char*, int, defCompareCStrings>  defKeywordMap;
#endif

class defrSettings {
public:
    defrSettings();

    void init_symbol_table();

    defKeywordMap Keyword_set; 

    int defiDeltaNumberLines;

    ////////////////////////////////////
    //
    //       Flags to control number of warnings to print out, max will be 999
    //
    ////////////////////////////////////

    int AssertionWarnings;
    int BlockageWarnings;
    int CaseSensitiveWarnings;
    int ComponentWarnings;
    int ConstraintWarnings;
    int DefaultCapWarnings;
    int FillWarnings;
    int GcellGridWarnings;
    int IOTimingWarnings;
    int LogFileAppend; 
    int NetWarnings;
    int NonDefaultWarnings;
    int PinExtWarnings;
    int PinWarnings;
    int RegionWarnings;
    int RowWarnings;
    int TrackWarnings;
    int ScanchainWarnings;
    int SNetWarnings;
    int StylesWarnings;
    int UnitsWarnings;
    int VersionWarnings;
    int ViaWarnings;

    int  nDDMsgs; 
    int* disableDMsgs;
    int  totalDefMsgLimit; // to save the user set total msg limit to output
    int  totalNumErrorsParsed; // to stop parsing after numbe of the errors
    int AllowComponentNets;
    char CommentChar;
    int DisPropStrProcess; 

    int reader_case_sensitive_set;
    int AllowVer60Plus;

    // Section skip flags - for performance optimization
    // When set, the parser will skip the corresponding section
    int SkipComponentsSection;      // Skip COMPONENTS section
    int SkipNetsSection;            // Skip NETS section
    int SkipSpecialNetsSection;     // Skip SPECIALNETS section

    // Net detail skip flags - for fast net name extraction
    // When set, only net name is captured via NetNameCbk, net body is skipped
    int SkipNetDetails;             // Skip net body (pins, wires, routes, etc.)
    int SkipSNetDetails;            // Skip special net body

    // Escape processing flags
    // When set, escape sequences like \/ are processed in T_STRING tokens
    // Default is 0 (disabled) for backward compatibility
    int RemoveBackslash;             // Remove backslash in T_STRING


    DEFI_READ_FUNCTION ReadFunction;
    DEFI_LOG_FUNCTION ErrorLogFunction;
    DEFI_WARNING_LOG_FUNCTION WarningLogFunction;
    DEFI_CONTEXT_LOG_FUNCTION ContextErrorLogFunction;
    DEFI_CONTEXT_WARNING_LOG_FUNCTION ContextWarningLogFunction;
    DEFI_MAGIC_COMMENT_FOUND_FUNCTION MagicCommentFoundFunction;
    DEFI_MALLOC_FUNCTION MallocFunction;
    DEFI_REALLOC_FUNCTION ReallocFunction;
    DEFI_FREE_FUNCTION FreeFunction;
    DEFI_LINE_NUMBER_FUNCTION LineNumberFunction;
    DEFI_LONG_LINE_NUMBER_FUNCTION LongLineNumberFunction;
    DEFI_CONTEXT_LINE_NUMBER_FUNCTION ContextLineNumberFunction;
    DEFI_CONTEXT_LONG_LINE_NUMBER_FUNCTION ContextLongLineNumberFunction;

    int UnusedCallbacks[CBMAX];
    int MsgLimit[DEF_MSGS];

    static const char*  defOxides[defMaxOxides];
};


class defrSession {
public:
    defrSession();

    char*           FileName;
    int             reader_case_sensitive;
    defiUserData    UserData;

    defiPropType    CompProp;
    defiPropType    CompPinProp;
    defiPropType    DesignProp;
    defiPropType    GroupProp;
    defiPropType    NDefProp;
    defiPropType    NetProp;
    defiPropType    RegionProp;
    defiPropType    RowProp;
    defiPropType    SNetProp;
    defiPropType    BlockageProp;
    defiPropType    PinProp;
    defiPropType    PinPropShape;
    defiPropType    RouteProp;
    defiPropType    ScanChainProp;
    defiPropType    SpecialRouteProp;
    defiPropType    ViaProp;
    defiPropType    TrackProp;
};

END_LEFDEF_PARSER_NAMESPACE

#endif
