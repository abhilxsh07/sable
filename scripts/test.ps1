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

# Build, then run the GoogleTest binaries with --gtest_brief=1 (failures only).
param(
	[ValidateSet("Debug", "Release")][string]$Config = "Debug",
	[ValidateSet("x64", "ARM64")][string]$Platform = "x64"
)
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot "build.ps1") -Config $Config -Platform $Platform
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$tests = Get-ChildItem -Path (Join-Path $root "tests") -Recurse -Filter "*Tests.exe" -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match "\\$Platform\\$Config\\" }
if (-not $tests) {
	Write-Output "test: no test binaries found for $Config|$Platform"
	exit 1
}
$failed = 0
foreach ($t in $tests) {
	& $t.FullName --gtest_brief=1
	if ($LASTEXITCODE -ne 0) { $failed++ }
}
$state = if ($failed -eq 0) { "OK" } else { "FAILED" }
Write-Output "test ${state}: $(@($tests).Count) binary(ies), $failed failing"
exit $failed
