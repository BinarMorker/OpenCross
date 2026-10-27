// Headless: write every symbol of the current program to <outdir>/<program name>.csv
// (address,name,kind,source,namespace). kind is function, label or data.
//@category RDE
import java.io.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DumpSymbols extends GhidraScript {
    static String q(String s) {
        return (s.indexOf(',') >= 0 || s.indexOf('"') >= 0) ? "\"" + s.replace("\"", "\"\"") + "\"" : s;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(args.length > 0 ? args[0] : ".");
        dir.mkdirs();
        File out = new File(dir, currentProgram.getName() + ".csv");
        Listing listing = currentProgram.getListing();
        int n = 0;
        try (PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(out), "UTF-8"))) {
            w.println("address,name,kind,source,namespace");
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (s.isExternal()) continue;
                SymbolType t = s.getSymbolType();
                String kind;
                if (t == SymbolType.FUNCTION) kind = "function";
                else if (t == SymbolType.LABEL) {
                    Data d = listing.getDefinedDataAt(s.getAddress());
                    kind = (d != null) ? "data" : "label";
                } else continue;
                w.println(s.getAddress() + "," + q(s.getName()) + "," + kind + "," + s.getSource() + ","
                        + q(s.getParentNamespace().getName()));
                n++;
            }
        }
        println("DumpSymbols " + currentProgram.getName() + ": " + n + " symbols -> " + out);
    }
}
