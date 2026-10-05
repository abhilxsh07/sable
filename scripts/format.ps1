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

# Run clang-format over tracked C++ files. -Check is for CI and the
# pre-commit hook (fails on any formatting difference).
param([switch]$Check)
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
	$files = @(git ls-files -- "*.cpp" "*.h" | Where-Object { $_ -notmatch "Generated Files" })
	if ($files.Count -eq 0) {
		Write-Output "format: 0 files"
		exit 0
	}

	$clang = $null
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) {
		$vs = & $vswhere -latest -products * -property installationPath
		foreach ($sub in "VC\Tools\Llvm\x64\bin", "VC\Tools\Llvm\bin") {
			$c = Join-Path "$vs" "$sub\clang-format.exe"
			if ($vs -and (Test-Path $c)) { $clang = $c; break }
		}
	}
	if (-not $clang) {
		$cmd = Get-Command clang-format -ErrorAction SilentlyContinue
		if ($cmd) { $clang = $cmd.Source }
	}
	if (-not $clang) {
		Write-Output "format: clang-format not found (VS C++ Clang tools or LLVM)"
		exit 1
	}

	if ($Check) {
		& $clang --dry-run --Werror $files
	} else {
		& $clang -i $files
	}
	$code = $LASTEXITCODE
	$state = if ($code -eq 0) { "OK" } else { "needs formatting" }
	Write-Output "format: $($files.Count) files, $state"
	exit $code
} finally {
	Pop-Location
}
