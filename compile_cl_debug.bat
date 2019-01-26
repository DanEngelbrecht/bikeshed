@echo off

if NOT DEFINED VCINSTALLDIR (
    echo "No compatible visual studio found! run vcvarsall.bat first!"
)

IF NOT EXIST build (
    mkdir build
)

pushd build

cl.exe /nologo /Zi /D_CRT_SECURE_NO_WARNINGS /D_HAS_EXCEPTIONS=0 /EHsc /W4 ..\src\bikeshed.cpp ..\test/main.cpp /link  /out:test_debug.exe /pdb:test_debug.pdb

popd

exit /B %ERRORLEVEL%

