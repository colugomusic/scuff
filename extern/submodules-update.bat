:: this garbage was written by ChatGPT so dont ask me how it works

@echo off
setlocal enabledelayedexpansion

:: Check if at least one argument (submodule list file) is passed
if "%~1"=="" (
    echo No submodule list file specified. Exiting...
    pause
    exit /b
)

:: Loop through each file passed as arguments
for %%f in (%*) do (
    if not exist "%%f" (
        echo Error: File "%%f" not found! Skipping...
        continue
    )
    
    echo Updating submodules from: %%f
    
    :: Read each submodule from the file
    for /f "usebackq delims=" %%s in (%%f) do (
        echo Updating submodule: %%s
        git submodule update --remote --merge %%s
    )
)

echo Submodule update complete!
pause