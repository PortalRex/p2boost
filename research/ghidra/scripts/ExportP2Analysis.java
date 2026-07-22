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
import ghidra.program.model.listing.DataIterator;
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
        if (function == null) function = getFunctionContaining(targetAddress);
        if (function == null || !targets.add(function) || depth <= 0) return;
        // Samples normally land inside a function. Once the containing
        // function is known, callers reference its entry point rather than
        // the sampled instruction, so walk references to the entry address.
        for (Reference reference : getReferencesTo(function.getEntryPoint())) {
            Function caller = containingFunction(reference.getFromAddress());
            if (caller != null) addFunctionAndCallers(targets, caller.getEntryPoint().getOffset(), depth - 1);
        }
    }

    private void addFunctionsReferencingString(Set<Function> targets, String value, int depth) {
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext()) {
            Data item = data.next();
            if (!item.hasStringValue() || !value.equals(String.valueOf(item.getValue()))) continue;
            addFunctionsReferencingAddress(targets, item.getAddress(), depth, 2, new HashSet<Address>());
        }
    }

    private void addFunctionsReferencingAddress(Set<Function> targets, Address address, int depth,
                                                 int indirections, Set<Address> visited) {
        if (!visited.add(address)) return;
        for (Reference reference : getReferencesTo(address)) {
            Function function = containingFunction(reference.getFromAddress());
            if (function != null) {
                addFunctionAndCallers(targets, function.getEntryPoint().getOffset(), depth);
            } else if (indirections > 0) {
                addFunctionsReferencingAddress(targets, reference.getFromAddress(), depth,
                                                indirections - 1, visited);
            }
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
        if (base.equalsIgnoreCase("engine.dll")) {
            addFunctionAndCallers(targets, 0x101f9d40L, 2);
            long[] profileTargets = {
                0x100e4240L, 0x10120bd0L, 0x101249f0L, 0x10109e10L,
                0x100c0840L, 0x100e70b0L, 0x101b41f0L, 0x100da870L,
                0x100e6dd0L, 0x100e4120L, 0x1006f4c0L, 0x10122fa0L,
                0x100ea960L, 0x10127b60L, 0x10127380L, 0x100dd330L,
                0x100ea5b0L, 0x100dc590L, 0x100dbd20L, 0x1015bff0L,
                0x100f8b30L, 0x101a9460L, 0x101a9ad0L, 0x101ad1c0L,
                0x101ae7f0L, 0x100dc8a0L, 0x100eab1fL, 0x1019601cL,
                0x101983f9L, 0x10198773L, 0x101a736bL, 0x101a74baL,
                0x101a7554L, 0x101fa00dL, 0x101f8df0L, 0x101f8f0fL,
                0x101f995eL,
                // 2026-07-22 synchronized ETW/PresentMon 8.43 ms lobby frame.
                0x10124adbL, 0x1031224aL, 0x100e1a90L, 0x10124da2L,
                0x10070f38L, 0x10218c8aL, 0x1026febcL
            };
            for (long address : profileTargets) addFunctionAndCallers(targets, address, 1);
        }
        if (base.equalsIgnoreCase("shaderapidx9.dll")) {
            addFunctionAndCallers(targets, 0x100241a0L, 1);
            addFunctionAndCallers(targets, 0x10024450L, 2);
            addFunctionAndCallers(targets, 0x10024690L, 2);
            addFunctionAndCallers(targets, 0x10004b80L, 1);
            addFunctionAndCallers(targets, 0x10004320L, 1);
            // Focus-restore crash 2026-07-22: renderer worker stack leading to
            // d3d9.dll+0xefd7b while dereferencing a stale shader resource.
            long[] focusCrashTargets = {
                0x1000a720L, 0x1001edb0L, 0x1001eaa0L, 0x100082b0L,
                0x10026ed0L, 0x1000b0e0L, 0x1000d420L
            };
            for (long address : focusCrashTargets) addFunctionAndCallers(targets, address, 1);
            long[] steadyRenderTargets = {
                0x10025df5L, 0x1002042fL, 0x10024553L, 0x100247fcL,
                0x10024ae8L, 0x1002637bL, 0x1002649bL, 0x1001a31dL
            };
            for (long address : steadyRenderTargets) addFunctionAndCallers(targets, address, 1);
            long[] dynamicBufferTargets = {
                // Functions which update the dynamic-vertex/index counters. These are
                // explicit because the counter labels are reached through pointer
                // indirection that Ghidra's string-reference pass does not always follow.
                0x10008cf0L, 0x10008f60L, 0x100090b0L, 0x100093d0L,
                // D3D9 allocation plus the vertex/index Lock entry paths.
                0x10005a90L, 0x10005b40L, 0x10009160L, 0x1000ad90L
            };
            for (long address : dynamicBufferTargets) addFunctionAndCallers(targets, address, 2);
            long[] hardwareSyncTargets = {
                // Queued render-context slot 219 reaches +0x14a50; its two query
                // helpers are the deterministic Trust Fling hitch path.
                0x10014a50L, 0x1000ed60L, 0x1000eeb0L
            };
            for (long address : hardwareSyncTargets) addFunctionAndCallers(targets, address, 2);
            long[] driverSerializationTargets = {
                // Correlated co-op paint stack samples enter these shader API paths
                // before blocking in the native D3D9 driver.
                0x1000aa11L, 0x1000b28cL, 0x1001ee55L, 0x1001ee77L
            };
            for (long address : driverSerializationTargets) addFunctionAndCallers(targets, address, 3);
            addFunctionsReferencingString(targets, "CMeshMgr::DiscardVertexBuffers", 2);
            addFunctionsReferencingString(targets, "TexGroup_global_Dynamic_Verts", 2);
            addFunctionsReferencingString(targets, "TexGroup_global_Dynamic_Indices", 2);
        }
        if (base.equalsIgnoreCase("materialsystem.dll")) {
            addFunctionAndCallers(targets, 0x10011230L, 2);
            addFunctionAndCallers(targets, 0x10027610L, 1);
            addFunctionAndCallers(targets, 0x1001bd70L, 1);
            addFunctionAndCallers(targets, 0x10013980L, 1);
            long[] focusCrashTargets = {
                0x1003ac30L, 0x1003b340L, 0x10003ea0L,
                0x10025080L, 0x1000f3f0L, 0x10024ed0L
            };
            for (long address : focusCrashTargets) addFunctionAndCallers(targets, address, 1);
            long[] steadyRenderTargets = {
                0x10032c55L, 0x10033105L, 0x10033ce5L, 0x10033d81L,
                0x1002f92aL, 0x10044e8fL, 0x10088798L, 0x1004c091L
            };
            for (long address : steadyRenderTargets) addFunctionAndCallers(targets, address, 1);
            long[] threadedEndFrameTargets = {
                // CMaterialSystem::EndFrame, its queued render worker, command-buffer
                // execution/copy/reset, and the thread-pool wait exposed by VProf.
                // The final six addresses cover the queued ForceHardwareSync and
                // Present dispatch thunks plus their concrete render-context targets.
                0x10012a70L, 0x1000f3f0L, 0x10025080L, 0x10024030L,
                0x100236a0L, 0x100235f0L, 0x10023ee0L, 0x10024a90L, 0x1000aeb0L,
                0x1000cef0L, 0x1000ce10L, 0x10010a10L, 0x1000eeb0L,
                0x10013c60L, 0x10013d00L, 0x1000cfc0L,
                0x1002e980L, 0x10029fc0L, 0x100394e0L
            };
            for (long address : threadedEndFrameTargets) addFunctionAndCallers(targets, address, 2);
            addFunctionAndCallers(targets, 0x1003ac67L, 3);
            addFunctionAndCallers(targets, 0x1003b514L, 3);
            addFunctionsReferencingString(targets, "mat_bufferprimitives", 2);
            addFunctionsReferencingString(targets,
                "%d calls queued for %d bytes in parameters and overhead, %d bytes verts, %d bytes indices, %d bytes other\n", 2);
        }
        if (base.equalsIgnoreCase("studiorender.dll")) {
            long[] queuedStudioRenderTargets = {
                // Real co-op paint gameplay: the three leaf callbacks reached
                // through the material-system queued command wrapper. +0x4e710
                // owns the 15-20 ms command inside the largest 60-90 ms batch.
                0x1004e710L, 0x1004caa0L, 0x1004e540L
            };
            for (long address : queuedStudioRenderTargets)
                addFunctionAndCallers(targets, address, 3);
        }
        if (base.equalsIgnoreCase("stdshader_dx9.dll")) {
            long[] driverSerializationCallers = {
                // Shader leaves present on every sample of the deterministic
                // co-op paint driver-serialization burst.
                0x1004aeb3L, 0x1000c7f6L, 0x1004a5c5L,
                0x10044509L, 0x100445c7L, 0x1001a5b5L,
                0x1001723cL, 0x1003feeeL, 0x10040059L,
                0x10038ff4L
            };
            for (long address : driverSerializationCallers)
                addFunctionAndCallers(targets, address, 3);
        }
        if (base.equalsIgnoreCase("filesystem_stdio.dll")) {
            long[] steadyReadTargets = {
                0x10001d3eL, 0x10004fdbL, 0x1000b8beL, 0x1000b923L,
                0x1000bbd8L, 0x1000c035L, 0x1000c54fL, 0x1002c3b1L,
                0x10001f9aL, 0x10002e5dL
            };
            for (long address : steadyReadTargets) addFunctionAndCallers(targets, address, 2);
        }
        if (base.equalsIgnoreCase("client.dll")) {
            addFunctionAndCallers(targets, 0x101ee590L, 2);
            addFunctionAndCallers(targets, 0x10272830L, 2);
            addFunctionAndCallers(targets, 0x10284300L, 2);
            long[] profileTargets = {
                0x10586a20L, 0x100e1fa0L, 0x100e2180L, 0x100e6280L,
                0x10587990L, 0x1058b060L, 0x100e3180L, 0x100e5840L,
                0x100e57b0L, 0x100e0990L, 0x10585e80L, 0x104f0c80L,
                0x101e5620L, 0x101edfa0L, 0x101f1460L, 0x101f1190L,
                0x101f0e30L, 0x101ec9e0L, 0x101ec8b0L
            };
            for (long address : profileTargets) addFunctionAndCallers(targets, address, 1);
            long[] lobbyHitchTargets = {
                // Main-thread samples during the synchronized 8.43 ms portal frame.
                0x100e2df5L, 0x100e205aL, 0x106b20f2L, 0x106b2692L,
                0x10285c91L, 0x102c57dbL
            };
            for (long address : lobbyHitchTargets) addFunctionAndCallers(targets, address, 2);
            addFunctionsReferencingString(targets, "CClientShadowMgr::BuildFlashlight", 2);
            addFunctionsReferencingString(targets, "CClientShadowMgr::UpdateBrushShadow", 2);
            addFunctionsReferencingString(targets,
                "CClientShadowMgr::UpdateProjectedTextureInternal", 2);
            addFunctionsReferencingString(targets,
                "CClientShadowMgr::UpdateProjectedTexture can only be used with flashlights!\n", 2);
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
