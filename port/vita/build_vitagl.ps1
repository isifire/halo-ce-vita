# Rebuild all objects: HAVE_SHARK_LOG changes VitaGL's shader struct layout.
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$dependency = Join-Path $repoRoot 'gpu-source/vitaGL-master'
Push-Location $repoRoot
try {
    foreach ($patch in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'patches') -Filter 'vitagl-*.patch') {
        & git apply --reverse --check --directory=gpu-source/vitaGL-master $patch.FullName 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git apply --check --directory=gpu-source/vitaGL-master $patch.FullName
            if ($LASTEXITCODE -ne 0) { throw "Dependency differs from supported VitaGL source: $($patch.Name)" }
            & git apply --directory=gpu-source/vitaGL-master $patch.FullName
            if ($LASTEXITCODE -ne 0) { throw 'Failed applying VitaGL patch' }
        }
    }
    $vitaBin = Join-Path $repoRoot 'sdk-extracted/vitasdk/bin'
    $gpuInclude = Join-Path $repoRoot 'gpu-hard/arm-vita-eabi/include'
    $sdkInclude = Join-Path $repoRoot 'sdk-extracted/vitasdk/arm-vita-eabi/include'
    $savedPath = $env:Path
    $env:Path = "$vitaBin;C:\msys64\ucrt64\bin;$env:Path"
    Set-Location $dependency
    $buildHeader = Join-Path $repoRoot 'port/vita/include/halo_vitagl_build.h'
    $flags = "-g -Wl,-q -O3 -ffast-math -mtune=cortex-a9 -mfpu=neon -mfp16-format=ieee -Wno-incompatible-pointer-types -Wno-stringop-overflow -include $buildHeader -DHAVE_SHARK_LOG -DSTRICT_UNIFORMS_COMPLIANCE -Isource -I$gpuInclude -I$sdkInclude"
    & mingw32-make -B -j8 "CFLAGS=$flags"
    if ($LASTEXITCODE -ne 0) { throw 'VitaGL rebuild failed' }
} finally {
    if ($savedPath) { $env:Path = $savedPath }
    Pop-Location
}
