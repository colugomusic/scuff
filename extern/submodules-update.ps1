# this garbage was written by chatgpt so dont ask me how it works

param (
    [string[]]$SubmoduleLists  # Accepts multiple files as an array
)

# Check if at least one file is provided
if (-Not $SubmoduleLists) {
    Write-Host "No submodule list files specified. Exiting..." -ForegroundColor Yellow
    exit 0
}

# Iterate over each provided submodule list file
foreach ($SubmoduleList in $SubmoduleLists) {
    # Check if the file exists
    if (-Not (Test-Path $SubmoduleList)) {
        Write-Host "Error: Submodule list file '$SubmoduleList' not found!" -ForegroundColor Red
        continue  # Skip to the next file instead of exiting
    }

    # Read submodule paths from the file, ignoring empty lines
    $submodules = Get-Content $SubmoduleList | Where-Object {$_ -match '\S'}  # Only keep non-empty lines

    # Loop through each submodule and update it
    foreach ($submodule in $submodules) {
        Write-Host "Updating submodule: $submodule" -ForegroundColor Cyan
        git submodule update --remote --merge $submodule
    }
}

Write-Host "Submodule update complete!" -ForegroundColor Green