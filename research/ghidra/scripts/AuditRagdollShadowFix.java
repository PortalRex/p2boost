// Targeted exact-build audit for the ragdoll model-instance shadow fix.
// @category p2boost

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class AuditRagdollShadowFix extends GhidraScript {
    private Function containingFunction(Address address) {
        Function function = getFunctionContaining(address);
        if (function != null) return function;
        Instruction instruction = getInstructionContaining(address);
        return instruction == null ? null : getFunctionContaining(instruction.getAddress());
    }

    private void addReferences(Set<Function> functions, long address) {
        Address target = toAddr(address);
        for (Reference reference : getReferencesTo(target)) {
            Function function = containingFunction(reference.getFromAddress());
            if (function != null) functions.add(function);
        }
    }

    private void writeFunction(BufferedWriter output, DecompInterface decompiler,
                               Function function) throws Exception {
        output.write("===== " + function.getEntryPoint() + " " + function.getName() + " =====\n");
        for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
            output.write(instruction.getAddress() + "\t" + instruction + "\n");
        }

        DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
        if (result.decompileCompleted()) {
            output.write("\n" + result.getDecompiledFunction().getC() + "\n");
        } else {
            output.write("\n<decompilation failed: " + result.getErrorMessage() + ">\n");
        }
        output.write("\n");
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("expected output directory");

        File directory = new File(args[0]);
        if (!directory.exists() && !directory.mkdirs()) {
            throw new IllegalStateException("could not create " + directory);
        }

        // Globals recovered by the project's full client.dll export. They are
        // populated from VEngineModel016, VEngineShadowMgr002, and
        // VEngineVGui001 respectively during the client factory connection.
        Set<Function> functions = new LinkedHashSet<>();
        addReferences(functions, 0x109aa230L);
        addReferences(functions, 0x109aa25cL);
        addReferences(functions, 0x109aa24cL);

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        File report = new File(directory, "client.dll-ragdoll-shadow-audit.txt");
        try (BufferedWriter output = new BufferedWriter(new FileWriter(report))) {
            output.write("modelrender=109aa230 shadowmgr=109aa25c enginevgui=109aa24c\n");
            output.write("referencing_functions=" + functions.size() + "\n\n");
            for (Function function : functions) {
                if (monitor.isCancelled()) break;
                writeFunction(output, decompiler, function);
            }
        } finally {
            decompiler.dispose();
        }
        println("Wrote " + report.getAbsolutePath());
    }
}
