function Get-KcdcQaRuntimeRoot([string]$Override) {
    if ($Override) { return [IO.Path]::GetFullPath($Override).TrimEnd('\') }
    $common = & git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the main KCDC checkout.' }
    $mainKcdc = Split-Path -Parent $common
    $framework = [IO.Path]::GetFullPath((Join-Path $mainKcdc '..\..\..'))
    if ([IO.Path]::GetFullPath((Join-Path $framework 'code\projects\kcdc')) -ne $mainKcdc) {
        throw 'Main KCDC checkout is not under Framework/code/projects/kcdc. Supply the same permanent -RuntimeRoot to both QA helpers.'
    }
    return Join-Path $framework 'builds\qa-runtime'
}
