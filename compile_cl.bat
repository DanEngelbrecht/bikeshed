echo off

if NOT DEFINED VCINSTALLDIR (
    echo "No compatible visual studio found! run vcvarsall.bat first!"
)

IF NOT EXIST build (
    mkdir build
)

pushd build

cl.exe /nologo /Zi /O2 /D_CRT_SECURE_NO_WARNINGS /D_HAS_EXCEPTIONS=0 /EHsc /W4 ..\src\sogo.cpp ..\src\sogo_utils.cpp ..\src\sogo_nodes.cpp ..\third-party\xxHash-1\xxhash.c ..\test/main.cpp /link  /out:test.exe /pdb:test.pdb

popd

exit /B %ERRORLEVEL%
