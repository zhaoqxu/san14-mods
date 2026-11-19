param(
    [ValidateSet('all','manager','worker','mods')][string]$Target='all',
    [string[]]$Mod=@(),
    [switch]$IncludeOptional,
    [switch]$Tests,
    [string]$CompilerRoot=$env:SAN14_MSVC_ROOT,
    [string]$Python='',
    [string]$ArtifactRoot='',
    [string]$ScenarioFile='',
    [switch]$Offline,
    [string]$AdapterArchive='',
    [string]$AdapterPackage='',
    [switch]$SkipGameAdapter
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
if(-not $Python){$Python=Join-Path $root '.venv/Scripts/python.exe'}
if(-not $ArtifactRoot){$ArtifactRoot=Join-Path $root 'build/current'}
if(-not [IO.Path]::IsPathRooted($ArtifactRoot)){$ArtifactRoot=Join-Path $root $ArtifactRoot}
$ArtifactRoot=[IO.Path]::GetFullPath($ArtifactRoot)
$Python=(Resolve-Path -LiteralPath $Python).Path
if(-not $ArtifactRoot.StartsWith((Join-Path $root 'build')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'ArtifactRoot must be under this checkout build/'
}
$sdk=Join-Path $root 'sdk'
$worker=Join-Path $ArtifactRoot 'SAN14Base/worker'
$oldPath=$env:PATH
$oldCache=$env:PYINSTALLER_CONFIG_DIR
try{
    $env:PATH=(Split-Path -Parent $Python)+';'+$env:PATH
    $env:PYINSTALLER_CONFIG_DIR=Join-Path $ArtifactRoot 'pyinstaller-cache'
    if($Target -in @('all','manager') -and -not $SkipGameAdapter){
        if($AdapterPackage){
            if($AdapterArchive){throw 'Select AdapterPackage or AdapterArchive'}
            & $Python -B (Join-Path $root 'tools/stage_adapter.py') --source $AdapterPackage --output (Join-Path $ArtifactRoot 'adapter-package')
            if($LASTEXITCODE){throw 'Local binary GameAdapter validation failed'}
        }else{
        if($Tests){
            & $Python -B (Join-Path $root 'tests/test_fetch_adapter.py')
            if($LASTEXITCODE){throw 'GameAdapter download tests failed'}
        }
        $fetchArgs=@('--root',$root,'--output',(Join-Path $ArtifactRoot 'adapter-package'))
        if($Offline){$fetchArgs+='--offline'}
        if($AdapterArchive){$fetchArgs+=@('--archive',$AdapterArchive)}
        & $Python -B (Join-Path $root 'tools/fetch_adapter.py') @fetchArgs
        if($LASTEXITCODE){throw 'Pinned GameAdapter download or verification failed'}
        }
    }
    if($Target -in @('all','worker')){
        & $Python -B (Join-Path $root 'manager/worker/build_worker.py') --output $worker
        if($LASTEXITCODE){throw 'Worker build failed'}
    }
    if($Target -in @('all','manager')){
        $manifest=Join-Path $worker 'worker.integrity.json'
        if(-not (Test-Path -LiteralPath $manifest)){throw 'Build the Worker in this ArtifactRoot before building its pinned Host'}
        & (Join-Path $root 'manager/build.ps1') -SdkRoot $sdk -CompilerRoot $CompilerRoot -WorkerManifest $manifest -ArtifactRoot $ArtifactRoot -Tests:$Tests
        if($LASTEXITCODE){throw 'Manager build failed'}
        if($Tests){
            & $Python -B (Join-Path $root 'manager/worker/tests/smoke_worker.py') (Join-Path $ArtifactRoot 'native/worker-probe.exe') (Join-Path $ArtifactRoot 'SAN14Base')
            if($LASTEXITCODE){throw 'Host and Worker integration failed'}
        }
        if(-not $SkipGameAdapter){
            if($Tests){
                & $Python -B (Join-Path $root 'tests/test_runtime_package.py')
                if($LASTEXITCODE){throw 'Runtime package tests failed'}
                & (Join-Path $ArtifactRoot 'native/adapter-probe.exe') (Join-Path $ArtifactRoot 'adapter-package')
                if($LASTEXITCODE){throw 'Downloaded GameAdapter binary contract failed'}
            }
            & $Python -B (Join-Path $root 'manager/tools/package_runtime.py') --core (Join-Path $ArtifactRoot 'native/dinput8.dll') --adapter (Join-Path $ArtifactRoot 'adapter-package') --runtime $worker --output (Join-Path $ArtifactRoot 'SAN14-runtime.zip')
            if($LASTEXITCODE){throw 'Runtime package verification failed'}
        }
    }
    if($Target -in @('all','mods')){
        if($Tests){
            & $Python -B (Join-Path $root 'tests/test_packaging.py')
            if($LASTEXITCODE){throw 'Mod packaging tests failed'}
        }
        $modArgs=@('--root',$root,'--output',$ArtifactRoot)
        if($IncludeOptional){$modArgs+='--include-optional'}
        foreach($name in $Mod){$modArgs+=@('--mod',$name)}
        $selected=& $Python -B (Join-Path $root 'tools/package.py') @modArgs --list
        if($LASTEXITCODE){throw 'Cannot resolve requested Mods'}
        $selected=($selected -join '') | ConvertFrom-Json
        foreach($item in $selected){
            $project=Join-Path $root $item.path
            if($item.type -eq 'native'){
                & (Join-Path $project 'build.ps1') -SdkRoot $sdk -CompilerRoot $CompilerRoot -Tests:$Tests
                if($LASTEXITCODE){throw "Build failed: $($item.id)"}
            } elseif($item.id -eq 'two-city-all-stars-mod'){
                if(-not $ScenarioFile){throw 'The optional two-city package requires -ScenarioFile with your original CE scenario 01'}
                & $Python -B (Join-Path $project 'tools/build_duel_scenario.py') $ScenarioFile (Join-Path $project 'content/scenario.json') -o (Join-Path $ArtifactRoot 'authored/two-city-all-stars-mod/content/duel.s14')
                if($LASTEXITCODE){throw 'Two-city scenario authoring failed'}
            }
        }
        & $Python -B (Join-Path $root 'tools/package.py') @modArgs
        if($LASTEXITCODE){throw 'Mod packaging failed'}
    }
    if($Target -eq 'all' -and -not $Mod.Count -and -not $SkipGameAdapter){
        & (Join-Path $root 'manager/build.ps1') -Installer -SdkRoot $sdk -CompilerRoot $CompilerRoot -ArtifactRoot $ArtifactRoot -Tests:$Tests
        if($LASTEXITCODE){throw 'Installer stub build failed'}
        if($Tests){
            & $Python -B (Join-Path $root 'tests/test_setup_package.py')
            if($LASTEXITCODE){throw 'Installer package tests failed'}
        }
        & $Python -B (Join-Path $root 'tools/package_setup.py') --root $root --artifact-root $ArtifactRoot
        if($LASTEXITCODE){throw 'Installer packaging failed'}
    }
}finally{$env:PATH=$oldPath;$env:PYINSTALLER_CONFIG_DIR=$oldCache}
