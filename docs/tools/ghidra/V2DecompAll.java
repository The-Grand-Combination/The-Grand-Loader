// V2DecompAll: decompiles every function of the program into <outDir>. args: <outDir> [timeoutSeconds]
//   <outDir>/<block>.c   functions whose entry falls in the 64 KB block 0x<block>0000, in address order
//   <outDir>/index.tsv   one row per function: va, name, bytes, callers, callees, day, frame, file, status
// `day` / `frame` = call depth from DayTick (0x006859C0) / CInGameIdler_Update (0x00654D80) over direct
// calls (-1 = not reached; virtual calls are not followed), so `awk '$6 >= 0'` lists what runs every day.
// Read-only: run with -readOnly. Regenerate after importing new names so the output uses them.
//@category V2
import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;
import java.io.*;
import java.util.*;

public class V2DecompAll extends GhidraScript {
    static final long DAY_TICK = 0x006859C0L;
    static final long FRAME = 0x00654D80L;

    Map<Function, Integer> depths(Function root) throws Exception {
        Map<Function, Integer> d = new HashMap<>();
        Deque<Function> q = new ArrayDeque<>();
        d.put(root, 0);
        q.add(root);
        while (!q.isEmpty()) {
            Function f = q.poll();
            for (Function c : f.getCalledFunctions(monitor)) {
                if (c.isExternal() || d.containsKey(c)) continue;
                d.put(c, d.get(f) + 1);
                q.add(c);
            }
        }
        return d;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]);
        out.mkdirs();
        int timeout = a.length > 1 ? Integer.parseInt(a[1]) : 60;
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        Map<Function, Integer> day = depths(fm.getFunctionAt(toAddr(DAY_TICK)));
        Map<Function, Integer> frame = depths(fm.getFunctionAt(toAddr(FRAME)));

        // Group functions by 64 KB block of their entry point.
        TreeMap<Long, List<Function>> blocks = new TreeMap<>();
        int total = 0;
        for (Function f : fm.getFunctions(true)) {
            if (f.isExternal() || f.isThunk()) continue;
            blocks.computeIfAbsent(f.getEntryPoint().getOffset() >>> 16, k -> new ArrayList<>()).add(f);
            total++;
        }

        DecompileConfigurer cfg = di -> {
            DecompileOptions o = new DecompileOptions();
            o.grabFromProgram(currentProgram);
            di.setOptions(o);
            di.toggleCCode(true);
            di.toggleSyntaxTree(false);
            di.setSimplificationStyle("decompile");
        };
        DecompilerCallback<String[]> cb = new DecompilerCallback<>(currentProgram, cfg) {
            @Override
            public String[] process(DecompileResults r, TaskMonitor m) {
                Function f = r.getFunction();
                DecompiledFunction df = r.getDecompiledFunction();
                if (df == null) return new String[] { f.getEntryPoint().toString(), "// decompile failed: " + r.getErrorMessage() + "\n", "failed" };
                return new String[] { f.getEntryPoint().toString(), df.getC(), "ok" };
            }
        };
        cb.setTimeout(timeout);

        PrintWriter idx = new PrintWriter(new BufferedWriter(new FileWriter(new File(out, "index.tsv"))));
        idx.println("va\tname\tbytes\tcallers\tcallees\tday\tframe\tfile\tstatus");
        int done = 0, failed = 0;
        try {
            for (Map.Entry<Long, List<Function>> e : blocks.entrySet()) {
                monitor.checkCancelled();
                String file = String.format("%04X.c", e.getKey());
                Map<String, String[]> res = new HashMap<>();
                for (String[] r : ParallelDecompiler.decompileFunctions(cb, e.getValue(), monitor)) res.put(r[0], r);
                try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(new File(out, file))))) {
                    for (Function f : e.getValue()) {
                        String[] r = res.get(f.getEntryPoint().toString());
                        List<String> callers = new ArrayList<>();
                        for (Reference ref : rm.getReferencesTo(f.getEntryPoint())) {
                            if (!ref.getReferenceType().isCall()) continue;
                            Function c = fm.getFunctionContaining(ref.getFromAddress());
                            callers.add((c == null ? "?" : c.getName()) + "@" + ref.getFromAddress());
                        }
                        int callees = f.getCalledFunctions(monitor).size();
                        int dd = day.getOrDefault(f, -1), fd = frame.getOrDefault(f, -1);
                        String status = r == null ? "missing" : r[2];
                        if (!"ok".equals(status)) failed++;
                        w.println("// ==== 0x" + f.getEntryPoint() + " " + f.getName() + "  bytes=" + f.getBody().getNumAddresses()
                            + " callers=" + callers.size() + " callees=" + callees + " day=" + dd + " frame=" + fd);
                        if (!callers.isEmpty())
                            w.println("// callers: " + String.join(", ", callers.subList(0, Math.min(12, callers.size()))) + (callers.size() > 12 ? ", ..." : ""));
                        w.println(r == null ? "// not decompiled\n" : r[1]);
                        idx.println("0x" + f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + callers.size()
                            + "\t" + callees + "\t" + dd + "\t" + fd + "\t" + file + "\t" + status);
                        done++;
                    }
                }
                println(String.format("V2DecompAll: %s done, %d/%d functions, %d failed", file, done, total, failed));
            }
        } finally {
            cb.dispose();
            idx.close();
        }
        println("V2DecompAll: finished " + done + " functions, " + failed + " failed");
    }
}
