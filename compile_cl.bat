@echo off

call .\find_mvsc.bat

if NOT DEFINED VCINSTALLDIR (
    echo "No Visual Studio installation found, aborting, try running run vcvarsall.bat first!"
    exit 1
)

IF NOT EXIST build (
    mkdir build
)

pushd build

cl.exe /nologo /Zi /O2 /D_CRT_SECURE_NO_WARNINGS /D_HAS_EXCEPTIONS=0 /EHsc /W4 ..\src\bikeshed.cpp ..\third-party\nadir\src\nadir_win32.cpp ..\test/main.cpp /link  /out:test.exe /pdb:test.pdb

popd

exit /B %ERRORLEVEL%
