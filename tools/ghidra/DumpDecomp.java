// Ghidra headless post-script: apply names from analysis/<prog>_names.txt,
// add function starts from analysis/<prog>_funcs.tsv that auto-analysis
// missed, then write every function's decompilation to the output file.
//
//   args: <names.txt> <funcs.tsv> <out.c> [base]
// base (hex) is added to every address in the two lists: 0x10000 for a DOS
// MZ, whose image Ghidra loads at segment 0x1000. A missing list is skipped.
// Run through tools/ghidra/decomp.sh.
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class DumpDecomp extends GhidraScript {
    private long base = 0;

    private Address addr(String hex) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(base + Long.parseLong(hex.replace("0x", ""), 16));
    }

    private List<String> lines(String path) throws IOException {
        Path p = Paths.get(path);
        return Files.exists(p) ? Files.readAllLines(p) : new ArrayList<>();
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length > 3) base = Long.parseLong(a[3].replace("0x", ""), 16);
        Listing listing = currentProgram.getListing();
        for (String line : lines(a[1])) {
            if (line.startsWith("#") || line.trim().isEmpty()) continue;
            Address at = addr(line.split("\t")[0]);
            if (listing.getFunctionAt(at) == null && listing.getFunctionContaining(at) == null) {
                disassemble(at);
                createFunction(at, null);
            }
        }
        for (String line : lines(a[0])) {
            line = line.split("#")[0].trim();
            if (line.isEmpty()) continue;
            String[] f = line.split("\\s+");
            if (f.length < 2 || !f[1].matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
            Address at = addr(f[0]);
            Function fn = listing.getFunctionAt(at);
            if (fn == null) {
                disassemble(at);
                fn = createFunction(at, f[1]);
            }
            if (fn != null) fn.setName(f[1], SourceType.USER_DEFINED);
        }
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new FileWriter(a[2]))) {
            for (Function fn : listing.getFunctions(true)) {
                DecompileResults r = d.decompileFunction(fn, 60, monitor);
                out.println("// ==== " + fn.getEntryPoint() + " " + fn.getName());
                if (r != null && r.decompileCompleted())
                    out.println(r.getDecompiledFunction().getC());
                else
                    out.println("// decompile failed");
            }
        }
    }
}
