@echo off
setlocal
set "PEP_CONFIG=%~1"
if not defined PEP_CONFIG set "PEP_CONFIG=Debug"
set "PEP_MSBUILD=msbuild"
if exist "%VSINSTALLDIR%\MSBuild\Current\Bin\amd64\MSBuild.exe" set "PEP_MSBUILD=%VSINSTALLDIR%\MSBuild\Current\Bin\amd64\MSBuild.exe"
"%PEP_MSBUILD%" "%~dp0QcPep8845.sln" /m /p:Configuration=%PEP_CONFIG% /p:Platform=ARM64
exit /b %errorlevel%
