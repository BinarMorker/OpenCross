// Headless: export decompilation, strings+xrefs, imports+xrefs, function list.
//@category RDE
import java.io.*;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;

public class ExportAll extends GhidraScript {
    String fn(Address a) {
        Function f = getFunctionContaining(a);
        return f == null ? ("nofunc_" + a) : (f.getName() + "@" + f.getEntryPoint());
    }
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs()[0];
        new File(outDir).mkdirs();
        ReferenceManager rm = currentProgram.getReferenceManager();
        // strings
        try (PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(outDir, "strings.txt")), "UTF-8"))) {
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!d.hasStringValue()) continue;
                StringDataInstance s = StringDataInstance.getStringDataInstance(d);
                String v = s.getStringValue();
                if (v == null) continue;
                Set<String> refs = new TreeSet<>();
                for (Reference r : rm.getReferencesTo(d.getAddress())) refs.add(fn(r.getFromAddress()));
                w.println(d.getAddress() + "\t" + v.replace("\n", "\n").replace("\r", "\r") + "\t" + String.join(",", refs));
            }
        }
        // imports
        try (PrintWriter w = new PrintWriter(new File(outDir, "imports.txt"))) {
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
                Set<String> refs = new TreeSet<>();
                for (Reference r : s.getReferences()) refs.add(fn(r.getFromAddress()));
                // also thunks/IAT pointers
                w.println(s.getParentNamespace().getName() + "!" + s.getName() + "\t" + String.join(",", refs));
            }
        }
        // functions + decompilation
        DecompInterface ifc = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        ifc.setOptions(opt);
        ifc.openProgram(currentProgram);
        int n = 0;
        try (PrintWriter fl = new PrintWriter(new File(outDir, "functions.txt"));
             PrintWriter dc = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(outDir, "decomp.c")), "UTF-8"))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                n++;
                int callers = f.getCallingFunctions(monitor).size();
                fl.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + callers);
                if (f.isThunk() || f.isExternal()) continue;
                DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                dc.println("//==== FUNC " + f.getEntryPoint() + " " + f.getName() + " size=" + f.getBody().getNumAddresses());
                if (res != null && res.getDecompiledFunction() != null) dc.print(res.getDecompiledFunction().getC());
                else dc.println("// decompile failed");
                if (n % 500 == 0) println("decomp " + n);
            }
        }
        ifc.dispose();
        println("done " + n);
    }
}
