// Headless Ghidra exporter for p2boost research.
// @category p2boost

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class ExportP2Analysis extends GhidraScript {
    private static final String[] TERMS = {
        "portal", "present", "reset", "device lost", "focus", "alt-tab",
        "framerate", "fps_max", "frametime", "render target", "visibility",
        "pvs", "split screen", "splitscreen", "co-op", "coop"
    };

    private static String clean(String value) {
        if (value == null) return "";
        return value.replace("\\", "\\\\").replace("\t", "\\t")
            .replace("\r", "\\r").replace("\n", "\\n");
    }

    private boolean interesting(String value) {
        String lower = value.toLowerCase(Locale.ROOT);
        for (String term : TERMS) if (lower.contains(term)) return true;
        return false;
    }

    private Function containingFunction(Address address) {
        Function function = getFunctionContaining(address);
        if (function != null) return function;
        Instruction instruction = getInstructionContaining(address);
        return instruction == null ? null : getFunctionContaining(instruction.getAddress());
    }

    private void addFunctionAndCallers(Set<Function> targets, long address, int depth) {
        Address targetAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(address);
        Function function = getFunctionAt(targetAddress);
        if (function == null || !targets.add(function) || depth <= 0) return;
        for (Reference reference : getReferencesTo(targetAddress)) {
            Function caller = containingFunction(reference.getFromAddress());
            if (caller != null) addFunctionAndCallers(targets, caller.getEntryPoint().getOffset(), depth - 1);
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("output directory argument required");
        File output = new File(args[0]);
        if (!output.exists() && !output.mkdirs()) throw new Exception("cannot create " + output);
        String base = currentProgram.getName().replaceAll("[^A-Za-z0-9_.-]", "_");
        Set<Function> targets = new HashSet<>();
        if (base.equalsIgnoreCase("engine.dll")) addFunctionAndCallers(targets, 0x101f9d40L, 2);
        if (base.equalsIgnoreCase("shaderapidx9.dll")) {
            addFunctionAndCallers(targets, 0x10024450L, 2);
            addFunctionAndCallers(targets, 0x10024690L, 2);
        }
        if (base.equalsIgnoreCase("client.dll")) {
            addFunctionAndCallers(targets, 0x101ee590L, 2);
            addFunctionAndCallers(targets, 0x10272830L, 2);
            addFunctionAndCallers(targets, 0x10284300L, 2);
        }

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(output, base + "-functions.tsv")))) {
            writer.write("address\tname\tentry_type\tcalling_convention\tparameter_count\n");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                monitor.checkCancelled();
                Function f = functions.next();
                writer.write(f.getEntryPoint() + "\t" + clean(f.getName(true)) + "\t" +
                    f.getSignatureSource() + "\t" + clean(f.getCallingConventionName()) + "\t" +
                    f.getParameterCount() + "\n");
                if (interesting(f.getName(true))) targets.add(f);
            }
        }

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(output, base + "-symbols.tsv")))) {
            writer.write("address\tname\ttype\tsource\n");
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            while (symbols.hasNext()) {
                monitor.checkCancelled();
                Symbol symbol = symbols.next();
                writer.write(symbol.getAddress() + "\t" + clean(symbol.getName(true)) + "\t" +
                    symbol.getSymbolType() + "\t" + symbol.getSource() + "\n");
            }
        }

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(output, base + "-interesting-strings.tsv")))) {
            writer.write("address\tvalue\treferencing_function\n");
            for (Data data : currentProgram.getListing().getDefinedData(true)) {
                monitor.checkCancelled();
                if (!data.hasStringValue()) continue;
                String value = String.valueOf(data.getValue());
                if (!interesting(value)) continue;
                Reference[] references = getReferencesTo(data.getAddress());
                boolean emitted = false;
                for (Reference reference : references) {
                    Function f = containingFunction(reference.getFromAddress());
                    if (f != null) targets.add(f);
                    writer.write(data.getAddress() + "\t" + clean(value) + "\t" +
                        (f == null ? "" : clean(f.getName(true))) + "\n");
                    emitted = true;
                }
                if (!emitted) writer.write(data.getAddress() + "\t" + clean(value) + "\t\n");
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(output, base + "-target-decompilation.txt")))) {
            for (Function f : targets) {
                monitor.checkCancelled();
                writer.write("\n===== " + f.getEntryPoint() + " " + f.getName(true) + " =====\n");
                DecompileResults result = decompiler.decompileFunction(f, 60, monitor);
                if (result.decompileCompleted()) writer.write(result.getDecompiledFunction().getC());
                else writer.write("DECOMPILATION FAILED: " + result.getErrorMessage() + "\n");
            }
        } finally {
            decompiler.dispose();
        }
    }
}
