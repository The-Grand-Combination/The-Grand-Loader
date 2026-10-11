// V2WriteScan: what a set of functions can write outside their own stack.
// args: <outdir> <roots file: one hex address per line, '#' comments, optional label after the address> <max depth>
// From every root, follows direct calls (not virtual ones) up to <max depth> and reports, per reached function:
//   - writes to absolute addresses (globals, statics, lazy-init guards),
//   - calls to the functions listed under "watch" below (random number generators and the like).
// Output: <outdir>/V2WriteScan.txt
//@category V2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class V2WriteScan extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(new File(a[0], "V2WriteScan.txt"), false));
        int maxDepth = Integer.parseInt(a[2]);
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing ls = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();

        Map<Long, String> watch = new LinkedHashMap<>();
        watch.put(0x00AB047BL, "CRT rand");
        watch.put(0x00AB0469L, "CRT srand");
        watch.put(0x009B7610L, "Rng_Next (MT)");
        watch.put(0x009B7700L, "Rng_RegenerateMT");

        Map<Address, Integer> depth = new LinkedHashMap<>();
        Map<Address, String> root = new HashMap<>();
        Map<Address, Address> parent = new HashMap<>();
        Deque<Address> q = new ArrayDeque<>();
        for (String line : Files.readAllLines(Path.of(a[1]))) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\\s+", 2);
            Function f = fm.getFunctionAt(toAddr(Long.parseLong(p[0].replace("0x", ""), 16)));
            if (f == null || depth.containsKey(f.getEntryPoint())) continue;
            depth.put(f.getEntryPoint(), 0);
            root.put(f.getEntryPoint(), p.length > 1 ? p[1] : f.getName());
            q.add(f.getEntryPoint());
        }
        int roots = depth.size();
        while (!q.isEmpty()) {
            Address cur = q.poll();
            int d = depth.get(cur);
            if (d >= maxDepth) continue;
            Function f = fm.getFunctionAt(cur);
            for (Function c : f.getCalledFunctions(monitor)) {
                Address ce = c.getEntryPoint();
                if (depth.containsKey(ce)) continue;
                depth.put(ce, d + 1);
                root.put(ce, root.get(cur));
                parent.put(ce, cur);
                q.add(ce);
            }
        }
        out.println("# roots " + roots + ", functions reached " + depth.size() + ", max depth " + maxDepth);

        Map<Address, List<String>> globalWriters = new TreeMap<>();
        List<String> watched = new ArrayList<>();
        for (Address fa : depth.keySet()) {
            Function f = fm.getFunctionAt(fa);
            for (Instruction ins : ls.getInstructions(f.getBody(), true)) {
                for (Reference r : ins.getReferencesFrom()) {
                    RefType t = r.getReferenceType();
                    Address to = r.getToAddress();
                    if (t.isCall() && watch.containsKey(to.getOffset())) {
                        watched.add(watch.get(to.getOffset()) + " at " + ins.getAddress() + " in " + f.getName() + " d" + depth.get(fa) + " path " + path(fm, parent, fa));
                    }
                    if (!t.isWrite() || !to.isMemoryAddress()) continue;
                    MemoryBlock b = mem.getBlock(to);
                    if (b == null || b.isExecute()) continue;
                    globalWriters.computeIfAbsent(to, k -> new ArrayList<>())
                        .add(ins.getAddress() + " " + ins + "  in " + f.getName() + " d" + depth.get(fa) + "  path " + path(fm, parent, fa));
                }
            }
        }
        out.println("\n## calls to watched functions: " + watched.size());
        for (String s : watched) out.println("  " + s);
        out.println("\n## absolute addresses written: " + globalWriters.size());
        for (Map.Entry<Address, List<String>> e : globalWriters.entrySet()) {
            Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(e.getKey());
            out.println("  " + e.getKey() + (s != null ? " " + s.getName() : "") + "  (" + e.getValue().size() + " writes)");
            int n = 0;
            for (String w : e.getValue()) { if (n++ < 4) out.println("      " + w); }
        }
        out.println("\n## reached functions");
        for (Map.Entry<Address, Integer> e : depth.entrySet()) {
            out.println("  d" + e.getValue() + " " + e.getKey() + " " + fm.getFunctionAt(e.getKey()).getName() + "  [" + root.get(e.getKey()) + "]");
        }
        out.close();
    }

    private String path(FunctionManager fm, Map<Address, Address> parent, Address a) {
        StringBuilder sb = new StringBuilder(fm.getFunctionAt(a).getName());
        for (Address p = parent.get(a); p != null; p = parent.get(p)) sb.insert(0, fm.getFunctionAt(p).getName() + " > ");
        return sb.toString();
    }
}
