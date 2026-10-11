// V2Dump: headless helper. args: <outdir> <cmd> <addr...>
//   xref | callers | callees | decomp | disasm <addr> <len> ...
//@category V2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class V2Dump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outdir = a[0];
        String cmd = a[1];
        PrintWriter out = new PrintWriter(new FileWriter(new File(outdir, "V2Dump_" + cmd + ".txt"), false));
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing ls = currentProgram.getListing();
        DecompInterface di = null;
        if (cmd.equals("decomp")) { di = new DecompInterface(); di.openProgram(currentProgram); }
        for (int i = 2; i < a.length; i++) {
            Address addr = (cmd.equals("scan") || cmd.equals("syms")) ? null : toAddr(Long.parseLong(a[i].replace("0x", ""), 16));
            Function f = addr == null ? null : fm.getFunctionContaining(addr);
            out.println("=== " + a[i] + " func=" + (f == null ? "none" : f.getName() + " @ " + f.getEntryPoint()));
            if (cmd.equals("xref")) {
                for (Reference r : rm.getReferencesTo(addr)) {
                    Function rf = fm.getFunctionContaining(r.getFromAddress());
                    out.println("  ref from " + r.getFromAddress() + " (" + r.getReferenceType() + ") in " + (rf == null ? "?" : rf.getName() + "@" + rf.getEntryPoint()));
                }
            }
            if (f != null && (cmd.equals("xref") || cmd.equals("callers"))) {
                for (Reference r : rm.getReferencesTo(f.getEntryPoint())) {
                    Function rf = fm.getFunctionContaining(r.getFromAddress());
                    out.println("  caller " + r.getFromAddress() + " (" + r.getReferenceType() + ") in " + (rf == null ? "?" : rf.getName() + "@" + rf.getEntryPoint()));
                }
            }
            if (f != null && cmd.equals("callees")) {
                for (Function c : f.getCalledFunctions(monitor)) out.println("  callee " + c.getName() + "@" + c.getEntryPoint());
            }
            if (cmd.equals("up")) {
                int depth = Integer.parseInt(a[++i]);
                Map<Address,String> seen = new LinkedHashMap<>();
                Deque<Address> q = new ArrayDeque<>();
                seen.put(f.getEntryPoint(), f.getName()); q.add(f.getEntryPoint());
                Map<Address,Integer> d = new HashMap<>(); d.put(f.getEntryPoint(), 0);
                while (!q.isEmpty()) { Address cur = q.poll(); int cd = d.get(cur); if (cd >= depth) continue;
                    for (Reference r : rm.getReferencesTo(cur)) { if (!r.getReferenceType().isCall()) continue; Function cf = fm.getFunctionContaining(r.getFromAddress()); if (cf == null) continue; Address ce = cf.getEntryPoint(); if (seen.containsKey(ce)) continue; seen.put(ce, seen.get(cur) + " <- " + cf.getName()); d.put(ce, cd+1); q.add(ce); } }
                for (Map.Entry<Address,String> e : seen.entrySet()) out.println("  d" + d.get(e.getKey()) + " " + e.getKey() + "  " + e.getValue());
                continue;
            }
            if (cmd.equals("syms")) {
                java.util.regex.Pattern p = java.util.regex.Pattern.compile(a[i]);
                for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(true)) if (p.matcher(s.getName(true)).find()) out.println("  " + s.getAddress() + "  " + s.getName(true));
                continue;
            }
            if (cmd.equals("scan")) {
                java.util.regex.Pattern p = java.util.regex.Pattern.compile(a[i].startsWith("@") ? java.nio.file.Files.readString(java.nio.file.Path.of(a[i].substring(1))).trim() : a[i]);
                InstructionIterator it = ls.getInstructions(true);
                while (it.hasNext()) { Instruction ins = it.next(); String s = ins.toString(); if (p.matcher(s).find()) { Function ff = fm.getFunctionContaining(ins.getAddress()); out.println("  " + ins.getAddress() + "  " + s + "   in " + (ff==null?"?":ff.getName()+"@"+ff.getEntryPoint())); } }
                continue;
            }
            if (cmd.equals("before")) {
                int n = Integer.parseInt(a[++i]);
                List<String> lines = new ArrayList<>();
                Instruction ins = ls.getInstructionContaining(addr);
                for (int k = 0; k < n && ins != null; k++) { lines.add(0, "  " + ins.getAddress() + "  " + ins.toString()); ins = ins.getPrevious(); }
                for (String s : lines) out.println(s);
                continue;
            }
            if (cmd.equals("disasm")) {
                long len = Long.parseLong(a[++i].replace("0x", ""), 16);
                for (Instruction ins = ls.getInstructionAt(addr); ins != null && ins.getAddress().subtract(addr) < len; ins = ins.getNext())
                    out.println("  " + ins.getAddress() + "  " + ins.toString());
            }
            if (f != null && cmd.equals("decomp")) {
                DecompileResults res = di.decompileFunction(f, 120, monitor);
                String c = res.getDecompiledFunction() == null ? "// decompile failed: " + res.getErrorMessage() : res.getDecompiledFunction().getC();
                try (PrintWriter pw = new PrintWriter(new File(outdir, f.getName() + "_" + f.getEntryPoint() + ".c"))) { pw.print(c); }
                out.println("  wrote " + f.getName() + "_" + f.getEntryPoint() + ".c (" + c.length() + " chars)");
            }
        }
        out.close();
        if (di != null) di.dispose();
    }
}
