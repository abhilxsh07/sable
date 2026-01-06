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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

# Checks an extracted x64 package; it cannot replace a clean-machine pass.
param(
	[Parameter(Mandatory = $true)][string]$Directory,
	[Parameter(Mandatory = $true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$directoryPath = [IO.Path]::GetFullPath($Directory)
$workPath = [IO.Path]::GetFullPath($WorkDirectory)
$exe = Join-Path $directoryPath 'Sable.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
	throw 'smoke: extracted Sable.exe is missing'
}
New-Item -ItemType Directory -Path $workPath -Force | Out-Null
$variables = @('DEBUG', 'EASTER_EGG', 'SableTabsTest',
	'SableSurfaceTest', 'SableProbeAutoClose')
$saved = @{}
foreach ($name in $variables) {
	$saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
	[Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$process = $null
try {
	foreach ($option in @('help', 'version')) {
		$out = Join-Path $workPath ($option + '.out')
		$err = Join-Path $workPath ($option + '.err')
		$process = Start-Process -FilePath $exe -ArgumentList ('--' + $option) `
			-WorkingDirectory $workPath -WindowStyle Hidden -PassThru `
			-RedirectStandardOutput $out -RedirectStandardError $err
		if (-not $process.WaitForExit(10000)) { throw "smoke: $option timed out" }
		if ($process.ExitCode -ne 0 -or (Get-Item -LiteralPath $out).Length -eq 0) {
			throw "smoke: $option failed"
		}
		if ($option -eq 'version') {
			$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
			if (-not ([IO.File]::ReadAllText($out).Contains($version))) {
				throw 'smoke: executable version does not match VERSION'
			}
		}
		Write-Output "smoke: --$option passed"
		$process.Dispose()
		$process = $null
	}
	$pdf = Join-Path $root 'tests/fixtures/pages.pdf'
	$process = Start-Process -FilePath $exe -ArgumentList ('"' + $pdf + '"') `
		-WorkingDirectory $workPath -WindowStyle Hidden -PassThru
	$deadline = [DateTime]::UtcNow.AddSeconds(15)
	do {
		$process.Refresh()
		if ($process.HasExited) { throw 'smoke: reader exited before showing' }
		if ($process.MainWindowHandle -ne 0) { break }
		Start-Sleep -Milliseconds 200
	} while ([DateTime]::UtcNow -lt $deadline)
	if ($process.MainWindowHandle -eq 0) { throw 'smoke: no reader window' }
	Start-Sleep -Milliseconds 1000
	if (-not $process.CloseMainWindow()) { throw 'smoke: could not close reader' }
	if (-not $process.WaitForExit(10000) -or $process.ExitCode -ne 0) {
		throw 'smoke: reader did not exit normally'
	}
	Write-Output 'smoke: PDF startup and close passed'
} finally {
	if ($process) {
		if (-not $process.HasExited) {
			$process.Kill()
			[void]$process.WaitForExit(10000)
		}
		$process.Dispose()
	}
	foreach ($name in $variables) {
		[Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
	}
}
