@echo off
setlocal
set "PEP_CONFIG=%~1"
if not defined PEP_CONFIG set "PEP_CONFIG=Debug"
msbuild "%~dp0QcPep8845.sln" /m /p:Configuration=%PEP_CONFIG% /p:Platform=ARM64
exit /b %errorlevel%
