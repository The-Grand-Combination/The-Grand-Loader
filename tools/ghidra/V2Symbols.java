// V2Symbols: keeps the Ghidra project and re/symbols/*.csv in step. args: <symbolsDir> <outDir> <cmd>
//   check   compares functions.csv / globals.csv with the program (read-only)
//   import  renames functions and labels globals from the CSVs (run without -readOnly to save)
//   export  lists user-named functions and labels that the CSVs do not have yet
// Output: <outDir>/V2Symbols_<cmd>.txt
//@category V2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class V2Symbols extends GhidraScript {
    // Minimal CSV: fields separated by commas, a field may be quoted with "" as an escaped quote.
    static List<String[]> readCsv(Path p) throws IOException {
        List<String[]> rows = new ArrayList<>();
        List<String> lines = Files.readAllLines(p);
        for (int n = 1; n < lines.size(); n++) {
            String line = lines.get(n);
            if (line.isBlank()) continue;
            List<String> f = new ArrayList<>();
            StringBuilder cur = new StringBuilder();
            boolean q = false;
            for (int i = 0; i < line.length(); i++) {
                char c = line.charAt(i);
                if (q) {
                    if (c == '"' && i + 1 < line.length() && line.charAt(i + 1) == '"') { cur.append('"'); i++; }
                    else if (c == '"') q = false;
                    else cur.append(c);
                } else if (c == '"') q = true;
                else if (c == ',') { f.add(cur.toString()); cur.setLength(0); }
                else cur.append(c);
            }
            f.add(cur.toString());
            rows.add(f.toArray(new String[0]));
        }
        return rows;
    }

    Address va(String s) { return toAddr(Long.parseLong(s.replace("0x", ""), 16)); }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Path dir = Path.of(a[0]);
        String cmd = a[2];
        PrintWriter out = new PrintWriter(new FileWriter(new File(a[1], "V2Symbols_" + cmd + ".txt"), false));
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        List<String[]> funcs = readCsv(dir.resolve("functions.csv"));
        List<String[]> globals = readCsv(dir.resolve("globals.csv"));
        Set<Address> known = new HashSet<>();
        int problems = 0, changed = 0;

        for (String[] r : funcs) {
            Address addr = va(r[0]);
            known.add(addr);
            Function f = fm.getFunctionAt(addr);
            if (f == null) {
                Function c = fm.getFunctionContaining(addr);
                out.println("NOFUNC   " + r[0] + " " + r[1] + (c == null ? "  (no function here)" : "  (inside " + c.getName() + " @ " + c.getEntryPoint() + ")"));
                problems++;
                continue;
            }
            if (cmd.equals("check")) {
                boolean same = f.getName().equals(r[1]);
                out.println((same ? "OK       " : "NAME     ") + r[0] + " " + r[1] + (same ? "" : "  (ghidra: " + f.getName() + ")"));
            } else if (cmd.equals("import") && !f.getName().equals(r[1])) {
                out.println("RENAME   " + r[0] + " " + f.getName() + " -> " + r[1]);
                f.setName(r[1], SourceType.USER_DEFINED);
                changed++;
            }
        }
        for (String[] r : globals) {
            Address addr = va(r[0]);
            known.add(addr);
            Symbol s = st.getPrimarySymbol(addr);
            String cur = s == null ? "(none)" : s.getName(true);
            // RTTI already names vtables (Class::vftable); those are left alone.
            boolean rtti = cur.endsWith("vftable");
            if (cmd.equals("check")) {
                boolean same = s != null && (s.getName().equals(r[1]) || rtti);
                out.println((same ? "OK       " : "LABEL    ") + r[0] + " " + r[1] + (same ? "" : "  (ghidra: " + cur + ")"));
            } else if (cmd.equals("import") && !rtti && (s == null || !s.getName().equals(r[1]))) {
                out.println("LABEL    " + r[0] + " " + cur + " -> " + r[1]);
                Symbol ns = st.createLabel(addr, r[1], SourceType.USER_DEFINED);
                ns.setPrimary();
                changed++;
            }
        }
        if (cmd.equals("export")) {
            for (Function f : fm.getFunctions(true)) {
                Symbol s = f.getSymbol();
                if (s.getSource() == SourceType.USER_DEFINED && !known.contains(f.getEntryPoint()))
                    out.println("0x" + f.getEntryPoint() + "," + f.getName() + ",?,,ghidra,");
            }
            for (Symbol s : st.getAllSymbols(true)) {
                if (s.getSymbolType() == SymbolType.LABEL && s.getSource() == SourceType.USER_DEFINED && !known.contains(s.getAddress()))
                    out.println("0x" + s.getAddress() + "," + s.getName() + ",?,?,ghidra,  # label");
            }
        }
        out.println("# " + cmd + ": " + funcs.size() + " functions, " + globals.size() + " globals, " + problems + " not at a function start, " + changed + " changed");
        out.close();
    }
}
