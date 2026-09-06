#!/bin/bash
set -e

SRC_DIR="/mnt/c/Users/CrazyWorld/Documents/form_files/LEF-DEF_6.0_62-p004"
BUILD_DIR="${SRC_DIR}/build"
INSTALL_DIR="${SRC_DIR}/lefdef-6.0"

CXX=g++
CC=gcc
CXXFLAGS="-O3 -fPIC -DNDEBUG"
CFLAGS="-O3 -fPIC -DNDEBUG"

echo "=========================================="
echo "  LEF/DEF 6.0 Build Script (-O3)"
echo "=========================================="

rm -rf "${BUILD_DIR}" "${INSTALL_DIR}"
mkdir -p "${BUILD_DIR}"/{lef,clef,def,cdef}
mkdir -p "${INSTALL_DIR}"/{lib,include}

echo ""
echo "[1/4] Building LEF library..."

cd "${SRC_DIR}/lef/lef"
bison -v -plefyy -d lef.y 2>/dev/null
mv lef.tab.c lef.tab.cpp 2>/dev/null || true

LEF_SRCS="crypt.cpp lef.tab.cpp lef_keywords.cpp lefiArray.cpp lefiCrossTalk.cpp \
lefiDebug.cpp lefiEncryptInt.cpp lefiLayer.cpp lefiMacro.cpp lefiMisc.cpp \
lefiNonDefault.cpp lefiProp.cpp lefiPropType.cpp lefiTBExt.cpp lefiUnits.cpp \
lefiVia.cpp lefiViaRule.cpp lefrCallbacks.cpp lefrData.cpp lefrReader.cpp \
lefrSettings.cpp lefwWriter.cpp lefwWriterCalls.cpp"

LEF_OBJS=""
for src in $LEF_SRCS; do
    obj="${BUILD_DIR}/lef/${src%.cpp}.o"
    ${CXX} ${CXXFLAGS} -I. -I.. -c -o "${obj}" "${src}" 2>/dev/null
    LEF_OBJS="${LEF_OBJS} ${obj}"
done

ar rcs "${INSTALL_DIR}/lib/liblef.a" ${LEF_OBJS}
${CXX} -shared -o "${INSTALL_DIR}/lib/liblef.so" ${LEF_OBJS}
echo "    Created: liblef.a, liblef.so"

echo ""
echo "[2/4] Building CLEF library (C interface)..."

cd "${SRC_DIR}/lef/clef"

CLEF_SRCS="xlefiArray.cpp xlefiCrossTalk.cpp xlefiDebug.cpp xlefiEncryptInt.cpp \
xlefiLayer.cpp xlefiMacro.cpp xlefiMisc.cpp xlefiNonDefault.cpp xlefiProp.cpp \
xlefiPropType.cpp xlefiUnits.cpp xlefiUtil.cpp xlefiVia.cpp xlefiViaRule.cpp \
xlefrReader.cpp xlefwWriter.cpp xlefwWriterCalls.cpp"

CLEF_OBJS=""
for src in $CLEF_SRCS; do
    obj="${BUILD_DIR}/clef/${src%.cpp}.o"
    ${CXX} ${CXXFLAGS} -I. -I.. -I../lef -c -o "${obj}" "${src}" 2>/dev/null
    CLEF_OBJS="${CLEF_OBJS} ${obj}"
done

ar rcs "${INSTALL_DIR}/lib/libclef.a" ${CLEF_OBJS}
${CXX} -shared -o "${INSTALL_DIR}/lib/libclef.so" ${CLEF_OBJS}
echo "    Created: libclef.a, libclef.so"

echo ""
echo "[3/4] Building DEF library..."

cd "${SRC_DIR}/def/def"
bison -v -pdefyy -d def.y 2>/dev/null
mv def.tab.c def.tab.cpp 2>/dev/null || true

DEF_SRCS="def.tab.cpp def_keywords.cpp defiAlias.cpp defiBlock.cpp defiComponent.cpp \
defiDebug.cpp defiFPC.cpp defiFill.cpp defiGroup.cpp defiIOTiming.cpp \
defiMisc.cpp defiNet.cpp defiNonDefault.cpp defiPartition.cpp defiPath.cpp \
defiPin.cpp defiPinCap.cpp defiProp.cpp defiPropType.cpp defiRegion.cpp \
defiRowTrack.cpp defiScanchain.cpp defiSite.cpp defiSlot.cpp defiTimingDisable.cpp \
defiUser.cpp defiVia.cpp defrCallbacks.cpp defrData.cpp defrReader.cpp \
defrSettings.cpp defwWriter.cpp defwWriterCalls.cpp"

DEF_OBJS=""
for src in $DEF_SRCS; do
    obj="${BUILD_DIR}/def/${src%.cpp}.o"
    ${CXX} ${CXXFLAGS} -I. -I.. -c -o "${obj}" "${src}" 2>/dev/null
    DEF_OBJS="${DEF_OBJS} ${obj}"
done

ar rcs "${INSTALL_DIR}/lib/libdef.a" ${DEF_OBJS}
${CXX} -shared -o "${INSTALL_DIR}/lib/libdef.so" ${DEF_OBJS}
echo "    Created: libdef.a, libdef.so"

echo ""
echo "[4/4] Building CDEF library (C interface)..."

cd "${SRC_DIR}/def/cdef"

CDEF_SRCS="xdefiAlias.cpp xdefiBlock.cpp xdefiComponent.cpp xdefiDebug.cpp \
xdefiFPC.cpp xdefiFill.cpp xdefiGroup.cpp xdefiIOTiming.cpp xdefiMisc.cpp \
xdefiNet.cpp xdefiNonDefault.cpp xdefiPartition.cpp xdefiPath.cpp xdefiPin.cpp \
xdefiPinCap.cpp xdefiProp.cpp xdefiPropType.cpp xdefiRegion.cpp xdefiRowTrack.cpp \
xdefiScanchain.cpp xdefiSite.cpp xdefiSlot.cpp xdefiTimingDisable.cpp xdefiUser.cpp \
xdefiVia.cpp xdefrReader.cpp xdefwWriter.cpp xdefwWriterCalls.cpp"

CDEF_OBJS=""
for src in $CDEF_SRCS; do
    obj="${BUILD_DIR}/cdef/${src%.cpp}.o"
    ${CXX} ${CXXFLAGS} -I. -I.. -I../def -c -o "${obj}" "${src}" 2>/dev/null
    CDEF_OBJS="${CDEF_OBJS} ${obj}"
done

ar rcs "${INSTALL_DIR}/lib/libcdef.a" ${CDEF_OBJS}
${CXX} -shared -o "${INSTALL_DIR}/lib/libcdef.so" ${CDEF_OBJS}
echo "    Created: libcdef.a, libcdef.so"

echo ""
echo "Installing headers..."
cp "${SRC_DIR}"/lef/lef/*.hpp "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/lef/lef/*.h "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/lef/lef/lef.tab.h "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/lef/clef/*.h "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/def/def/*.hpp "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/def/def/*.h "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/def/def/def.tab.h "${INSTALL_DIR}/include/" 2>/dev/null || true
cp "${SRC_DIR}"/def/cdef/*.h "${INSTALL_DIR}/include/" 2>/dev/null || true

echo ""
echo "=========================================="
echo "  Build Complete!"
echo "=========================================="
echo ""
ls -lh "${INSTALL_DIR}/lib/"
echo ""
echo "Headers: $(ls ${INSTALL_DIR}/include/ | wc -l) files"