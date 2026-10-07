# Sable, a native port of NightPDF
# Copyright (C) 2021  Advaith Madhukar
# Copyright (C) 2026  Abhilash Kar
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Restore + build Sable.sln, printing only error/warning lines (first 30)
# and a one-line summary. Exit code is MSBuild's.
param(
	[ValidateSet("Debug", "Release")][string]$Config = "Debug",
	[ValidateSet("x64", "ARM64")][string]$Platform = "x64"
)
$root = Split-Path -Parent $PSScriptRoot
$sln = Join-Path $root "Sable.sln"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
	# the projects use the VS 2022 toolset (v143). A newer VS can sit next to
	# it (the GitHub runner has VS 2026 too) but can't build these projects
	# without the 2022 tools, so prefer a 2022 install and fall back to latest
	foreach ($range in @("[17.0,18.0)", $null)) {
		$vsArgs = @("-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-find", "MSBuild\**\Bin\MSBuild.exe")
		if ($range) { $vsArgs = @("-version", $range) + $vsArgs }
		$msbuild = & $vswhere @vsArgs | Select-Object -First 1
		if ($msbuild) { break }
	}
}
if (-not $msbuild) {
	Write-Output "build: couldn't find Visual Studio/MSBuild (see Building in README.md)"
	exit 1
}
if (-not (Test-Path $sln)) {
	Write-Output "build: $sln not found"
	exit 1
}

$out = & $msbuild $sln -restore "-p:RestorePackagesConfig=true" -m -nodeReuse:false -nologo -verbosity:minimal "-p:Configuration=$Config" "-p:Platform=$Platform" 2>&1
$code = $LASTEXITCODE
$diag = $out | ForEach-Object { "$_" } | Where-Object { $_ -match ":\s*(fatal )?(error|warning)( [A-Z]+\d+)?\s*:" } | Select-Object -Unique
$errors = @($diag | Where-Object { $_ -match ":\s*(fatal )?error( [A-Z]+\d+)?\s*:" }).Count
$warnings = @($diag).Count - $errors
# on GitHub Actions, print them as annotations so they show up on the run
# page (the raw logs need a signed-in account even on a public repo)
$gh = $env:GITHUB_ACTIONS -eq "true"
$diag | Select-Object -First 30 | ForEach-Object {
	if ($gh) {
		$level = if ($_ -match ":\s*(fatal )?error") { "error" } else { "warning" }
		Write-Output "::${level}::$_"
	} else {
		Write-Output $_
	}
}
if ($code -ne 0 -and @($diag).Count -eq 0) {
	# no coded diagnostics (e.g. "error : ..."): show the tail instead
	$out | Select-Object -Last 10 | ForEach-Object {
		if ($gh) { Write-Output "::error::$_" } else { Write-Output "$_" }
	}
}
$state = if ($code -eq 0) { "OK" } else { "FAILED" }
Write-Output "build $Config|$Platform ${state}: $errors error(s), $warnings warning(s)"
exit $code
