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

# Launch the built Sable.exe, passing CLI arguments through.
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet("Debug", "Release")][string]$Config = "Debug",
	[ValidateSet("x64", "ARM64")][string]$Platform = "x64",
	[Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs
)
$root = Split-Path -Parent $PSScriptRoot
$exe = Get-ChildItem -Path $root -Recurse -Filter "Sable.exe" -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match "\\$Platform\\$Config\\" } |
	Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) {
	Write-Output "run: Sable.exe not built for $Config|$Platform (run scripts/build.ps1)"
	exit 1
}
if ($AppArgs) {
	# Start-Process doesn't quote items, so paths with spaces would split
	$quoted = $AppArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }
	Start-Process -FilePath $exe.FullName -ArgumentList $quoted -WorkingDirectory (Get-Location)
} else {
	Start-Process -FilePath $exe.FullName -WorkingDirectory (Get-Location)
}
