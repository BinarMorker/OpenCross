// Headless: apply a "name = 0xADDR;" symbol list to the current program.
// Optional trailing "# function" or "# data" comment gives the kind (the first word, so "# function: what it does" works);
// "#" lines are comments.
// An address that is a function entry gets renamed; kind "function" with no function covering the address
// creates one; anything else (kind data, or an address inside another function) becomes a label.
//@category RDE
import java.io.*;
import java.util.regex.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ApplySymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("usage: ApplySymbols.java <symbols.txt>");
        Pattern line = Pattern.compile("^\\s*([^\\s=#]+)\\s*=\\s*(?:0x)?([0-9a-fA-F]+)\\s*;?\\s*(?:#\\s*(\\w+))?.*$");
        int renamed = 0, created = 0, labels = 0, labelsInFunc = 0, outside = 0, failed = 0, bad = 0;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(args[0]), "UTF-8"))) {
            String s;
            while ((s = r.readLine()) != null) {
                String t = s.trim();
                if (t.isEmpty() || t.startsWith("#")) continue;
                Matcher m = line.matcher(s);
                if (!m.matches()) { bad++; continue; }
                String name = m.group(1);
                boolean wantFunc = "function".equalsIgnoreCase(m.group(3));
                Address a = toAddr(Long.parseLong(m.group(2), 16));
                if (a == null || !currentProgram.getMemory().contains(a)) { outside++; continue; }
                try {
                    Function f = getFunctionAt(a);
                    if (f != null) {
                        if (!f.getName().equals(name)) f.setName(name, SourceType.USER_DEFINED);
                        renamed++;
                    } else if (wantFunc && getFunctionContaining(a) == null) {
                        if (getInstructionAt(a) == null) disassemble(a);
                        Function nf = createFunction(a, name);
                        if (nf != null) { created++; }
                        else {
                            currentProgram.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED);
                            labels++;
                        }
                    } else {
                        currentProgram.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED);
                        if (getFunctionContaining(a) != null) labelsInFunc++; else labels++;
                    }
                } catch (Exception e) {
                    failed++;
                    println("failed " + name + " @ " + a + ": " + e.getMessage());
                }
            }
        }
        println("ApplySymbols: renamed=" + renamed + " functions_created=" + created + " labels=" + labels
                + " labels_inside_functions=" + labelsInFunc + " outside_memory=" + outside + " bad_lines=" + bad
                + " failed=" + failed);
    }
}
