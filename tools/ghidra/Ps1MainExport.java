// Apply "name = 0xADDR;" symbols, analyze, and export per-function decompilation (PS1 main exe).
//@category RDE
import java.io.*; import java.util.*; import java.util.regex.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.listing.*; import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
public class Ps1MainExport extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs(); File out = new File(a[1]); out.mkdirs();
    Pattern p = Pattern.compile("^\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*0x([0-9A-Fa-f]+)\\s*;");
    int named = 0;
    for (String l : java.nio.file.Files.readAllLines(new File(a[0]).toPath())) {
      Matcher m = p.matcher(l); if (!m.find()) continue;
      long v = Long.parseLong(m.group(2), 16); if (v < 0x80010000L || v >= 0x800AA000L) continue;
      Address ad = toAddr(v);
      if (getInstructionAt(ad) == null) new DisassembleCommand(ad, null, true).applyTo(currentProgram, monitor);
      Function f = getFunctionAt(ad);
      if (f == null) { new CreateFunctionCmd(ad).applyTo(currentProgram, monitor); f = getFunctionAt(ad); }
      if (f != null) { f.setName(m.group(1), SourceType.USER_DEFINED); named++; }
    }
    println("named " + named);
    DecompInterface ifc = new DecompInterface(); ifc.openProgram(currentProgram); int n = 0;
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      DecompileResults r = ifc.decompileFunction(f, 30, monitor);
      if (r == null || r.getDecompiledFunction() == null) continue;
      String nm = f.getName().replaceAll("[^A-Za-z0-9_]", "_");
      String ad = f.getEntryPoint().toString();
      try (PrintWriter w = new PrintWriter(new File(out, nm + "__" + ad + ".c"))) {
        w.println("/* slps_023.64 @ " + ad + "\s*/"); w.print(r.getDecompiledFunction().getC()); }
      n++;
    }
    println("exported " + n);
  }
}
