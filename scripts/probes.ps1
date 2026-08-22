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

# Build the Debug surface check (SableSurfaceTest=true), run every probe
# mode with SableProbeAutoClose and print each exit code. The exit code is
# the number of failing modes. Two stay manual: Shortcut waits for keys and
# the plain surface check waits for its buttons.
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet("x64", "ARM64")][string]$Platform = "x64",
	[string[]]$Mode = @(),
	[ValidateRange(10, 3600)][int]$TimeoutSeconds = 300,
	[ValidateRange(0, 1024)][int]$MaxCpu = 0,
	[switch]$NoBuild,
	[switch]$KeepSurfaceBuild,
	[switch]$List
)
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $PSScriptRoot "build.ps1"

# each mode is the environment the probe reads; the tabs probe takes one
# scenario switch on top of SableTabsTest.
$modes = [ordered]@{
	"SurfaceRetry" = @{ SableSurfaceRetryTest = "true" }
	"Error" = @{ SableErrorTest = "true" }
	"Scale" = @{ SableScaleTest = "true" }
	"Tabs" = @{ SableTabsTest = "true" }
}
$scenarios = @(
	"Accessibility", "AnnotationActions", "AttachmentSave", "Attachments", "CheckBox",
	"Choice", "ChoiceList", "ClearList", "Comb", "Combine", "CommentLocations",
	"Comments", "DefaultList", "Edit", "EllipsePlacement", "Export",
	"FillMarks",
	"Filter", "Find", "Finish", "Gestures", "Highlight", "InheritedCheckBox",
	"InheritedText", "Ink", "InkEdit", "Keyboard", "KeyboardInput", "Layout",
	"LineArrow", "Link",
	"LongGestures", "MarkupComment", "MixedForm", "Multiline", "Music",
	"NoSpellCheck", "NoteEdit", "NotePlacement", "Notes", "ObsidianStartup",
	"Organize",
	"Outline", "Pan", "PasswordForm", "Presentation", "Print", "PrintUi",
	"Properties", "Radio", "ReadAloud", "Redraw", "Rotation", "SaveCopy",
	"Selection",
	"Settings", "SortedChoice", "Stamp", "StampEdit", "StrikeOut",
	"TextBoxUnicode", "ToolSurfaces", "Underline", "Windows"
)
foreach ($name in $scenarios) {
	$modes[$name] = @{ SableTabsTest = "true"; "Sable${name}Test" = "true" }
}
# Finish reads the log file for the verbose startup line, which DEBUG turns on
$modes["Finish"]["DEBUG"] = "1"
# a forced failure has to come back as a failing exit code, or a pass
# from this script means nothing.
$expectFail = @{
	"FaultCheck" = @{ SableScaleTest = "true"; SableProbeFault = "check" }
	"FaultException" = @{ SableErrorTest = "true"; SableProbeFault = "exception" }
}
foreach ($name in $expectFail.Keys) {
	$modes[$name] = $expectFail[$name]
}

if ($List) {
	$modes.Keys | ForEach-Object { Write-Output $_ }
	exit 0
}
$selected = @($modes.Keys)
if ($Mode.Count -gt 0) {
	$unknown = @($Mode | Where-Object { -not $modes.Contains($_) })
	if ($unknown.Count -gt 0) {
		Write-Output "probes: unknown mode(s) $($unknown -join ', ') (see -List)"
		exit 1
	}
	$selected = @($Mode)
}

function Invoke-Build([string[]]$Property) {
	$buildArgs = @{ Config = "Debug"; Platform = $Platform; Property = $Property }
	if ($MaxCpu -gt 0) { $buildArgs.MaxCpu = $MaxCpu }
	# host output keeps the build log out of the returned exit code
	& $build @buildArgs | ForEach-Object { Write-Host $_ }
	$code = $LASTEXITCODE
	if ($code -ne 0 -and $MaxCpu -eq 0) {
		# parallel builds can run out of PCH memory here; one CPU doesn't
		Write-Host "probes: retrying the build with -MaxCpu 1"
		$buildArgs.MaxCpu = 1
		& $build @buildArgs | ForEach-Object { Write-Host $_ }
		$code = $LASTEXITCODE
	}
	return $code
}

# every variable a probe reads, so one mode can't leak into the next
$names = @("SableSurfaceTest", "SableProbeAutoClose", "SableProbeFault",
	"SableSurfaceRetryTest", "SableErrorTest", "SableScaleTest",
	"SableTabsTest", "SableShortcutTest") +
	@($scenarios | ForEach-Object { "Sable${_}Test" })
$saved = @{}
foreach ($name in $names) {
	$saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
	[Environment]::SetEnvironmentVariable($name, $null, "Process")
}
$results = @()
try {
	if (-not $NoBuild) {
		$code = Invoke-Build @("SableSurfaceTest=true")
		if ($code -ne 0) {
			Write-Output "probes: surface build failed ($code)"
			exit 1
		}
	}
	$exe = Join-Path $root "$Platform\Debug\Sable\Sable.exe"
	if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
		Write-Output "probes: $exe is missing (build first)"
		exit 1
	}
	foreach ($name in $selected) {
		$vars = $modes[$name]
		foreach ($key in $vars.Keys) {
			[Environment]::SetEnvironmentVariable($key, $vars[$key], "Process")
		}
		[Environment]::SetEnvironmentVariable("SableProbeAutoClose", "true", "Process")
		$watch = [Diagnostics.Stopwatch]::StartNew()
		$errors = Join-Path ([IO.Path]::GetTempPath()) "sable-probe-$name.err"
		$process = Start-Process -FilePath $exe -WorkingDirectory $root `
			-RedirectStandardError $errors -PassThru
		# reading the handle now keeps ExitCode available after exit
		$null = $process.Handle
		$exit = $null
		if ($process.WaitForExit($TimeoutSeconds * 1000)) {
			$process.WaitForExit()
			$exit = $process.ExitCode
		} else {
			Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
		}
		$watch.Stop()
		foreach ($key in $vars.Keys) {
			[Environment]::SetEnvironmentVariable($key, $null, "Process")
		}
		$failing = $expectFail.ContainsKey($name)
		$passed = if ($null -eq $exit) { $false }
			elseif ($failing) { $exit -ne 0 } else { $exit -eq 0 }
		$shown = if ($null -eq $exit) { "timeout" } else { "$exit" }
		$state = if ($passed) { "pass" } else { "FAIL" }
		$line = "{0,-18} {1,-8} {2,-5} {3,5:N0}s" -f $name, $shown, $state,
			$watch.Elapsed.TotalSeconds
		Write-Output $line
		# a failing probe prints its reason on stderr
		if (-not $passed -and (Test-Path -LiteralPath $errors)) {
			Get-Content -LiteralPath $errors -TotalCount 3 |
				ForEach-Object { Write-Output "    $_" }
		}
		Remove-Item -LiteralPath $errors -ErrorAction SilentlyContinue
		$results += [pscustomobject]@{ Mode = $name; Passed = $passed }
	}
} finally {
	foreach ($name in $names) {
		[Environment]::SetEnvironmentVariable($name, $saved[$name], "Process")
	}
	if (-not $NoBuild -and -not $KeepSurfaceBuild) {
		# the surface build replaces the home screen, so put it back
		$null = Invoke-Build @()
	}
}
$failed = @($results | Where-Object { -not $_.Passed }).Count
$state = if ($failed -eq 0) { "OK" } else { "FAILED" }
Write-Output "probes ${state}: $($results.Count - $failed) passed, $failed failing"
exit $failed
