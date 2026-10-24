# Dot-source: . (Join-Path $PSScriptRoot "quiet_start.ps1")
# Start-Quiet <exe> <argument string> <working dir> [stdout file] [stderr file] -> a process object (WaitForExit / ExitCode).
# The child's first ShowWindow takes the STARTUPINFO show mode, so a test window (ccx) opens minimised WITHOUT activation
# (SW_SHOWMINNOACTIVE): it never takes the focus from whatever the user is doing. Start-Process -WindowStyle Minimized activates it.
if (-not ("OcQuietStart" -as [type])) {
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class OcQuietStart {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct STARTUPINFO {
    public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
    public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
  }
  [StructLayout(LayoutKind.Sequential)]
  struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
  static extern bool CreateProcessW(string app, System.Text.StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                    IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool SetHandleInformation(IntPtr h, uint mask, uint flags);
  static System.IO.FileStream Inheritable(string path) {
    if (string.IsNullOrEmpty(path)) return null;
    var fs = new System.IO.FileStream(path, System.IO.FileMode.Create, System.IO.FileAccess.Write, System.IO.FileShare.ReadWrite);
    SetHandleInformation(fs.SafeFileHandle.DangerousGetHandle(), 1, 1);  // HANDLE_FLAG_INHERIT
    return fs;
  }
  // stdoutPath / stderrPath: optional files for the child's standard output / error ("" = none).
  public static int Start(string exe, string args, string dir, string stdoutPath, string stderrPath) {
    var si = new STARTUPINFO();
    si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
    si.dwFlags = 0x1;        // STARTF_USESHOWWINDOW
    si.wShowWindow = 7;      // SW_SHOWMINNOACTIVE
    var outFs = Inheritable(stdoutPath);
    var errFs = Inheritable(stderrPath);
    bool redirect = outFs != null || errFs != null;
    if (redirect) {
      si.dwFlags |= 0x100;   // STARTF_USESTDHANDLES
      si.hStdOutput = outFs != null ? outFs.SafeFileHandle.DangerousGetHandle() : IntPtr.Zero;
      si.hStdError = errFs != null ? errFs.SafeFileHandle.DangerousGetHandle() : IntPtr.Zero;
    }
    PROCESS_INFORMATION pi;
    var cmd = new System.Text.StringBuilder("\"" + exe + "\" " + args);
    try {
      // BELOW_NORMAL_PRIORITY_CLASS: never steals CPU from the user's foreground work either.
      if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, redirect, 0x4000, IntPtr.Zero, dir, ref si, out pi))
        throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    } finally {
      if (outFs != null) outFs.Dispose();
      if (errFs != null) errFs.Dispose();
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pi.dwProcessId;
  }
}
"@
}
function Start-Quiet([string]$Exe, [string]$Arguments, [string]$WorkingDirectory, [string]$StdOut = "", [string]$StdErr = "") {
  $id = [OcQuietStart]::Start($Exe, $Arguments, $WorkingDirectory, $StdOut, $StdErr)
  $p = Get-Process -Id $id
  # Keep the handle open so ExitCode is readable after the exit.
  $null = $p.Handle
  return $p
}
