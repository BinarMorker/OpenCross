// Create functions at listed addresses (if missing) and decompile hook handlers + new functions.
//@category RDE
import java.io.*; import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.listing.*; import ghidra.program.model.address.*;
public class RdeDecompTargets extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    List<Address> targets = new ArrayList<>();
    for (String l : java.nio.file.Files.readAllLines(new File(a[0]).toPath())) if (!l.isBlank()) targets.add(toAddr(Long.parseLong(l.trim(),16)));
    FunctionManager fm = currentProgram.getFunctionManager();
    Set<Function> todo = new LinkedHashSet<>();
    int created=0;
    for (Address ad : targets) {
      Function f = fm.getFunctionAt(ad);
      if (f == null) {
        if (getInstructionAt(ad)==null) new DisassembleCommand(ad,null,true).applyTo(currentProgram, monitor);
        Function c = fm.getFunctionContaining(ad);
        if (c != null && !c.getEntryPoint().equals(ad)) { todo.add(c); continue; }
        new CreateFunctionCmd(ad).applyTo(currentProgram, monitor);
        f = fm.getFunctionAt(ad); if (f!=null) created++;
      }
      if (f != null) todo.add(f);
    }
    println("created "+created+" todo "+todo.size());
    DecompInterface ifc = new DecompInterface(); ifc.openProgram(currentProgram);
    try (PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(a[1]),"UTF-8"))) {
      for (Function f : todo) {
        DecompileResults r = ifc.decompileFunction(f, 20, monitor);
        w.println("//==== FUNC "+f.getEntryPoint()+" "+f.getName());
        if (r!=null && r.getDecompiledFunction()!=null) w.print(r.getDecompiledFunction().getC()); else w.println("// failed");
      }
    }
  }
}
