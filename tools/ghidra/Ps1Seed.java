// Seed functions in a raw PS1 main-exe import: the entry point plus every "jal" target in the image.
// Usage: Ps1Seed.java <entryPc, e.g. 0x80013E4C>
//@category RDE
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class Ps1Seed extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    MemoryBlock blk = currentProgram.getMemory().getBlocks()[0];
    long lo = blk.getStart().getOffset(), hi = blk.getEnd().getOffset();
    java.util.TreeSet<Long> targets = new java.util.TreeSet<>();
    if (a.length > 0) targets.add(Long.decode(a[0]));
    byte[] buf = new byte[4];
    for (long ad = lo; ad + 3 <= hi; ad += 4) {
      blk.getBytes(toAddr(ad), buf);
      long w = (buf[0] & 0xFFL) | ((buf[1] & 0xFFL) << 8) | ((buf[2] & 0xFFL) << 16) | ((buf[3] & 0xFFL) << 24);
      if ((w >>> 26) != 3) continue;
      long t = 0x80000000L | ((w & 0x03FFFFFFL) << 2);
      if (t >= lo && t <= hi) targets.add(t);
    }
    int made = 0;
    for (long t : targets) {
      Address ad = toAddr(t);
      if (getInstructionAt(ad) == null) new DisassembleCommand(ad, null, true).applyTo(currentProgram, monitor);
      if (getFunctionAt(ad) == null && new CreateFunctionCmd(ad).applyTo(currentProgram, monitor)) made++;
    }
    println("seeded " + made + " functions from " + targets.size() + " targets");
  }
}
