@echo off
setlocal
pushd "%~dp0"
set "PARALLAX_GAME=%~1"
if not defined PARALLAX_GAME set "PARALLAX_GAME=..\..\..\game\mod_hl2mp"
if not exist "%PARALLAX_GAME%\gameinfo.txt" (
	echo Error: pass the mod directory containing gameinfo.txt.
	popd
	exit /b 1
)
if exist "shaders\fxc\lightmappedgeneric_parallax_ps20b.vcs" del /q "shaders\fxc\lightmappedgeneric_parallax_ps20b.vcs"
"..\..\devtools\bin\ShaderCompile2.exe" -ver 20b -shaderpath "%CD%" lightmappedgeneric_parallax_ps2x.fxc
if errorlevel 1 (
	popd
	exit /b 1
)
if not exist "shaders\fxc\lightmappedgeneric_parallax_ps20b.vcs" (
	echo Error: parallax shader compilation did not produce the expected file.
	popd
	exit /b 1
)
if not exist "%PARALLAX_GAME%\shaders\fxc" mkdir "%PARALLAX_GAME%\shaders\fxc"
copy /y "shaders\fxc\lightmappedgeneric_parallax_ps20b.vcs" "%PARALLAX_GAME%\shaders\fxc\lightmappedgeneric_parallax_ps20b.vcs" >nul
set "PARALLAX_RESULT=%ERRORLEVEL%"
popd
exit /b %PARALLAX_RESULT%
