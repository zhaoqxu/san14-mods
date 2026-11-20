<#
.SYNOPSIS
Validate and package a SAN14 mod. Native plugins need no Python; declarative
content compilation requires Python with Pillow and jsonschema.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$ModDir,
    [Parameter(Mandatory = $false)]
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$ModRoot = [IO.Path]::GetFullPath($ModDir)
$ManifestPath = Join-Path $ModRoot 'mod.json'
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Missing mod.json in $ModRoot"
}

$Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Manifest.schema_version -ne 1) { throw 'mod.json schema_version must be 1' }
$Id = [string]$Manifest.id
$Version = [string]$Manifest.version
if ($Id -notmatch '^[a-z0-9][a-z0-9._-]{1,63}$') { throw "Invalid mod id: $Id" }
if ($Version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw "Invalid mod version: $Version" }
function Assert-MetadataUrl([object]$taskSourceUrl,[string]$taskUrlField) {
    if($taskSourceUrl -isnot [string]){throw "$taskUrlField must be a HTTPS URL"}
    if($taskSourceUrl){
        if($taskSourceUrl.Length -gt 2048 -or -not $taskSourceUrl.StartsWith('https://',[StringComparison]::Ordinal) -or $taskSourceUrl -match '[^\x21-\x7e]|\\'){throw "Invalid $taskUrlField"}
        $taskSourceAuthority=($taskSourceUrl.Substring(8) -split '[/?#]',2)[0]
        if(-not $taskSourceAuthority -or $taskSourceAuthority -notmatch '^[A-Za-z0-9.:-]+$' -or $taskSourceAuthority.StartsWith('.') -or $taskSourceAuthority.EndsWith('.')){throw "Invalid $taskUrlField"}
    }
}
foreach($taskUrlField in @('author_url','source_url')){if($Manifest.PSObject.Properties.Name -contains $taskUrlField){Assert-MetadataUrl $Manifest.$taskUrlField $taskUrlField}}
foreach($taskPeopleField in @('contributors','coauthors')) {
if ($Manifest.PSObject.Properties.Name -contains $taskPeopleField) {
    if($Manifest.$taskPeopleField -isnot [Array] -or $Manifest.$taskPeopleField.Count -gt 16){throw 'contributors must be an array of at most 16 names'}
    $taskContributorNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($taskContributor in $Manifest.$taskPeopleField){
        if($taskContributor -is [string] -and $taskPeopleField -eq 'contributors'){$taskContributorName=$taskContributor}
        elseif($taskContributor -is [pscustomobject] -and $taskContributor.PSObject.Properties.Name -contains 'name'){
            foreach($taskField in $taskContributor.PSObject.Properties.Name){if($taskField -notin @('name','url')){throw 'Unknown contributor field'}}
            $taskContributorName=$taskContributor.name
            if($taskContributor.PSObject.Properties.Name -contains 'url'){Assert-MetadataUrl $taskContributor.url 'contributors.url'}
        }else{throw 'contributors entries require a name'}
        if($taskContributorName -isnot [string] -or -not $taskContributorName -or $taskContributorName.Length -gt 80 -or $taskContributorName.StartsWith(' ') -or $taskContributorName.EndsWith(' ') -or $taskContributorName -match '[\x00-\x1f\x7f]' -or -not $taskContributorNames.Add($taskContributorName)){throw 'Invalid contributor name'}
    }
}
}
if ($Manifest.entry.type -eq 'content') {
    if (-not $Output) { $Output = Join-Path (Split-Path -Parent $ModRoot) "$Id-$Version.s14mod" }
    & python (Join-Path $PSScriptRoot 'content_compiler.py') $ModRoot -o $Output
    if ($LASTEXITCODE) { throw 'Declarative content compilation failed (requires Python, Pillow and jsonschema)' }
    return
}
if (-not $Manifest.entry -or $Manifest.entry.type -ne 'native' -or $Manifest.entry.abi -ne 1) {
    throw 'entry must be {type: "native", abi: 1, path: "..."}'
}
$EntryRel = ([string]$Manifest.entry.path).Replace('/', '\')
if ([IO.Path]::IsPathRooted($EntryRel) -or $EntryRel -match '(^|[\\/])\.\.([\\/]|$)' -or $EntryRel -notmatch '\.dll$') {
    throw "Invalid entry.path: $($Manifest.entry.path)"
}
$EntryPath = [IO.Path]::GetFullPath((Join-Path $ModRoot $EntryRel))
if (-not $EntryPath.StartsWith($ModRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $EntryPath -PathType Leaf)) {
    throw "Missing DLL declared by entry.path: $($Manifest.entry.path)"
}

$AllowedPermissions = @(
    'game.read.officers', 'game.read.forces', 'game.read.cities', 'game.read.units',
    'game.read.areas', 'game.read.saves', 'game.commands', 'ui.panel', 'ui.officer-detail',
    'input.hotkey', 'input.text', 'storage.global', 'storage.playthrough', 'storage.saved-edit',
    'storage.save', 'network', 'game.rules', 'settings', 'game.memory.read', 'game.content'
)
foreach ($perm in @($Manifest.permissions)) {
    if ($AllowedPermissions -notcontains $perm) { throw "Unknown permission: $perm" }
}
if (@($Manifest.permissions) -contains 'game.memory.read' -and (-not $Manifest.game_exe_sha256 -or @($Manifest.game_exe_sha256).Count -eq 0)) {
    throw 'game.memory.read requires game_exe_sha256'
}

if (-not $Output) {
    $Output = Join-Path (Split-Path -Parent $ModRoot) "$Id-$Version.s14mod"
} else {
    $Output = [IO.Path]::GetFullPath($Output)
    if ([IO.Path]::GetExtension($Output).ToLowerInvariant() -ne '.s14mod') {
        throw "Output path must end with .s14mod: $Output"
    }
}

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class San14ModPackager {
    private static readonly uint[] CrcTable = BuildCrcTable();
    private static uint[] BuildCrcTable() {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        return table;
    }
    private static uint Crc32(byte[] data) {
        uint c = 0xFFFFFFFFu;
        foreach (byte b in data) c = CrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
        return ~c;
    }
    private static int RvaToOffset(byte[] pe, int sectionTableOffset, int sectionCount, uint rva) {
        for (int i = 0; i < sectionCount; i++) {
            int s = sectionTableOffset + i * 40;
            uint virtSize = BitConverter.ToUInt32(pe, s + 8);
            uint virtAddr = BitConverter.ToUInt32(pe, s + 12);
            uint rawSize = BitConverter.ToUInt32(pe, s + 16);
            uint rawPtr = BitConverter.ToUInt32(pe, s + 20);
            uint span = Math.Max(virtSize, rawSize);
            if (rva >= virtAddr && rva < virtAddr + span) {
                uint delta = rva - virtAddr;
                if (delta < rawSize && rawPtr + delta < pe.Length) return (int)(rawPtr + delta);
            }
        }
        return -1;
    }
    private static string ReadAsciiZ(byte[] pe, int offset) {
        if (offset < 0 || offset >= pe.Length) return "";
        int end = offset;
        while (end < pe.Length && pe[end] != 0) end++;
        return Encoding.ASCII.GetString(pe, offset, end - offset);
    }
    public static void ValidatePeDll(string dllPath) {
        byte[] pe = File.ReadAllBytes(dllPath);
        if (pe.Length < 0x100 || pe[0] != 'M' || pe[1] != 'Z') throw new Exception("Entry DLL is not a valid PE file");
        int e_lfanew = BitConverter.ToInt32(pe, 0x3C);
        if (e_lfanew < 0 || e_lfanew + 0x108 > pe.Length) throw new Exception("Truncated PE header");
        if (pe[e_lfanew] != 'P' || pe[e_lfanew + 1] != 'E' || pe[e_lfanew + 2] != 0 || pe[e_lfanew + 3] != 0)
            throw new Exception("Invalid PE signature");
        ushort machine = BitConverter.ToUInt16(pe, e_lfanew + 4);
        if (machine != 0x8664) throw new Exception("Entry DLL must be AMD64 (x64)");
        ushort sections = BitConverter.ToUInt16(pe, e_lfanew + 6);
        ushort optSize = BitConverter.ToUInt16(pe, e_lfanew + 20);
        int opt = e_lfanew + 24;
        if (BitConverter.ToUInt16(pe, opt) != 0x20B) throw new Exception("Entry DLL must be PE32+ (64-bit)");
        int secTable = opt + optSize;
        uint expRva = BitConverter.ToUInt32(pe, opt + 112);
        uint expSize = BitConverter.ToUInt32(pe, opt + 116);
        if (expRva == 0 || expSize == 0) throw new Exception("Entry DLL exports no functions (missing SAN14Mod_Initialize)");
        int expOff = RvaToOffset(pe, secTable, sections, expRva);
        if (expOff < 0 || expOff + 40 > pe.Length) throw new Exception("Invalid PE export directory");
        uint nameCount = BitConverter.ToUInt32(pe, expOff + 24);
        uint namesRva = BitConverter.ToUInt32(pe, expOff + 32);
        int namesOff = RvaToOffset(pe, secTable, sections, namesRva);
        bool foundInit = false;
        for (uint i = 0; i < nameCount && namesOff >= 0 && namesOff + (int)(i * 4 + 4) <= pe.Length; i++) {
            uint nameRva = BitConverter.ToUInt32(pe, namesOff + (int)(i * 4));
            if (ReadAsciiZ(pe, RvaToOffset(pe, secTable, sections, nameRva)) == "SAN14Mod_Initialize") {
                foundInit = true;
                break;
            }
        }
        if (!foundInit) throw new Exception("Entry DLL must export SAN14Mod_Initialize");
    }
    public static int WriteStrictZip(string outputPath, string[] sourcePaths, string[] entryNames) {
        var tempPath = outputPath + ".tmp";
        var central = new MemoryStream();
        int count = sourcePaths.Length;
        using (var fs = new FileStream(tempPath, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var bw = new BinaryWriter(fs)) {
            for (int i = 0; i < count; i++) {
                byte[] raw = File.ReadAllBytes(sourcePaths[i]);
                byte[] compressed;
                using (var ms = new MemoryStream()) {
                    using (var ds = new DeflateStream(ms, CompressionLevel.Optimal, true)) ds.Write(raw, 0, raw.Length);
                    compressed = ms.ToArray();
                }
                ushort method = 8;
                byte[] payload = compressed;
                if (compressed.Length >= raw.Length) { method = 0; payload = raw; }
                byte[] nameBytes = Encoding.UTF8.GetBytes(entryNames[i]);
                uint crc = Crc32(raw);
                uint offset = (uint)fs.Position;
                bw.Write(0x04034B50u); bw.Write((ushort)20); bw.Write((ushort)0x0800); bw.Write(method);
                bw.Write((ushort)0); bw.Write((ushort)0); bw.Write(crc);
                bw.Write((uint)payload.Length); bw.Write((uint)raw.Length);
                bw.Write((ushort)nameBytes.Length); bw.Write((ushort)0);
                bw.Write(nameBytes); bw.Write(payload);

                var cbw = new BinaryWriter(central);
                cbw.Write(0x02014B50u); cbw.Write((ushort)20); cbw.Write((ushort)20); cbw.Write((ushort)0x0800); cbw.Write(method);
                cbw.Write((ushort)0); cbw.Write((ushort)0); cbw.Write(crc);
                cbw.Write((uint)payload.Length); cbw.Write((uint)raw.Length);
                cbw.Write((ushort)nameBytes.Length); cbw.Write((ushort)0); cbw.Write((ushort)0);
                cbw.Write((ushort)0); cbw.Write((ushort)0); cbw.Write(0u); cbw.Write(offset);
                cbw.Write(nameBytes);
            }
            uint centralOffset = (uint)fs.Position;
            byte[] centralBytes = central.ToArray();
            bw.Write(centralBytes);
            bw.Write(0x06054B50u); bw.Write((ushort)0); bw.Write((ushort)0);
            bw.Write((ushort)count); bw.Write((ushort)count);
            bw.Write((uint)centralBytes.Length); bw.Write(centralOffset); bw.Write((ushort)0);
        }
        long length = new FileInfo(tempPath).Length;
        if (length > 128L * 1024 * 1024) { File.Delete(tempPath); throw new Exception("Archive exceeds 128 MiB limit"); }
        if (File.Exists(outputPath)) File.Delete(outputPath);
        File.Move(tempPath, outputPath);
        return (int)length;
    }
}
'@

[San14ModPackager]::ValidatePeDll($EntryPath)

$ExcludedDirs = @('build', 'cmakefiles', '.vs', '.vscode', '.idea', '.git', '__pycache__', 'dist', 'src', 'tests', 'tools')
$ExcludedFiles = @('cmakelists.txt', 'cmakepresets.json', 'cmakeuserpresets.json', 'project.json', 'build.ps1', '.gitignore', '.gitmodules')
$ExcludedPatterns = @('*.pdb', '*.obj', '*.ilk', '*.exp', '*.lib', '*.tlog', '*.lastbuildstate', '*.idb', '*.ipdb', '*.iobj', '*.cpp', '*.cc', '*.cxx', '*.c', '*.h', '*.hpp', '*.s14mod', '*.zip', '*.pyc', '*.pyo')

$SourcePaths = [System.Collections.Generic.List[string]]::new()
$EntryNames = [System.Collections.Generic.List[string]]::new()
$Seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[long]$TotalBytes = 0

foreach ($item in (Get-ChildItem -LiteralPath $ModRoot -Recurse -File | Sort-Object FullName)) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Symbolic links are not allowed: $($item.FullName)" }
    $rel = $item.FullName.Substring($ModRoot.Length).TrimStart('\')
    $posix = $rel.Replace('\', '/')
    $isEntry = [String]::Equals($item.FullName, $EntryPath, [StringComparison]::OrdinalIgnoreCase)
    if (-not $isEntry -and $posix -ne 'mod.json') {
        $parts = $posix.Split('/')
        $skip = $false
        for ($i = 0; $i -lt $parts.Length - 1; $i++) {
            if ($ExcludedDirs -contains $parts[$i].ToLowerInvariant()) { $skip = $true; break }
        }
        if ($skip) { continue }
        $lowerName = $item.Name.ToLowerInvariant()
        if ($ExcludedFiles -contains $lowerName) { continue }
        foreach ($pat in $ExcludedPatterns) {
            if ($lowerName -like $pat) { $skip = $true; break }
        }
        if ($skip) { continue }
    }
    if ($item.Name.ToLowerInvariant() -eq 'mod.json' -and $posix -ne 'mod.json') {
        throw "Nested mod.json is not allowed: $posix"
    }
    if (-not $Seen.Add($posix)) { throw "Duplicate path (case-insensitive): $posix" }
    if ($item.Length -gt 64MB) { throw "File exceeds 64 MiB limit: $posix" }
    $TotalBytes += $item.Length
    if ($TotalBytes -gt 256MB) { throw 'Expanded mod package exceeds 256 MiB limit' }
    $SourcePaths.Add($item.FullName)
    $EntryNames.Add($posix)
}

if ($SourcePaths.Count -gt 2048) { throw 'Mod package exceeds 2048 entries limit' }
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $Output))
$ArchiveSize = [San14ModPackager]::WriteStrictZip($Output, $SourcePaths.ToArray(), $EntryNames.ToArray())
Write-Output "Packed $Id v$Version ($($SourcePaths.Count) files, $ArchiveSize bytes) -> $Output"
