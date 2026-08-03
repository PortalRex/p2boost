// Focused string/xref tracer for one Ghidra program.
// @category p2boost

import java.io.File;
import java.io.FileWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class TraceStringReferences extends GhidraScript {
    private Function owner(Address address) {
        Function function = getFunctionContaining(address);
        if (function != null) return function;
        Instruction instruction = getInstructionContaining(address);
        return instruction == null ? null : getFunctionContaining(instruction.getAddress());
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 2)
            throw new IllegalArgumentException("string and output file required");

        String target = arguments[0];
        StringBuilder report = new StringBuilder();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        if (target.startsWith("func:0x")) {
            long value = Long.parseUnsignedLong(target.substring(7), 16);
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function function = getFunctionAt(address);
            if (function == null) function = getFunctionContaining(address);
            report.append("function ").append(function == null ? "" : function.getEntryPoint()).append('\n');
            if (function != null) {
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (result.decompileCompleted()) report.append(result.getDecompiledFunction().getC()).append('\n');
            }
            decompiler.dispose();
            try (FileWriter writer = new FileWriter(new File(arguments[1]))) {
                writer.write(report.toString());
            }
            return;
        }

        if (target.startsWith("0x")) {
            long value = Long.parseUnsignedLong(target.substring(2), 16);
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            report.append("address ").append(address).append('\n');
            java.util.HashSet<Address> emitted = new java.util.HashSet<>();
            for (int offset = 0; offset < 0x80; ++offset) {
                Address field = address.add(offset);
                for (Reference reference : getReferencesTo(field)) {
                    Function function = owner(reference.getFromAddress());
                    report.append("  field +0x").append(Integer.toHexString(offset)).append(" ref ")
                        .append(reference.getFromAddress()).append(" owner ")
                        .append(function == null ? "" : function.getEntryPoint()).append('\n');
                    if (function != null && emitted.add(function.getEntryPoint())) {
                        DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                        if (result.decompileCompleted())
                            report.append(result.getDecompiledFunction().getC()).append('\n');
                    }
                }
            }
            decompiler.dispose();
            try (FileWriter writer = new FileWriter(new File(arguments[1]))) {
                writer.write(report.toString());
            }
            return;
        }

        for (Data data : currentProgram.getListing().getDefinedData(true)) {
            if (!data.hasStringValue() || !target.equals(String.valueOf(data.getValue()))) continue;

            Address stringAddress = data.getAddress();
            report.append("string ").append(stringAddress).append('\n');
            long value = stringAddress.getOffset();
            byte[] pointer = {
                (byte)(value & 0xff),
                (byte)((value >>> 8) & 0xff),
                (byte)((value >>> 16) & 0xff),
                (byte)((value >>> 24) & 0xff)
            };

            Address cursor = currentProgram.getMinAddress();
            while (cursor != null) {
                Address storage = currentProgram.getMemory().findBytes(cursor, pointer, null, true, monitor);
                if (storage == null) break;
                report.append("pointer ").append(storage).append('\n');
                Instruction instruction = getInstructionContaining(storage);
                for (int index = 0; instruction != null && index < 20; ++index)
                    instruction = instruction.getPrevious();
                for (int index = 0; instruction != null && index < 50; ++index) {
                    report.append("  instruction ").append(instruction).append('\n');
                    instruction = instruction.getNext();
                }
                Function storageOwner = owner(storage);
                if (storageOwner != null) {
                    report.append("  storage owner ").append(storageOwner.getEntryPoint()).append('\n');
                    DecompileResults result = decompiler.decompileFunction(storageOwner, 60, monitor);
                    if (result.decompileCompleted())
                        report.append(result.getDecompiledFunction().getC()).append('\n');
                }
                for (Reference reference : getReferencesTo(storage)) {
                    Function function = owner(reference.getFromAddress());
                    report.append("  ref ").append(reference.getFromAddress()).append(" owner ")
                        .append(function == null ? "" : function.getEntryPoint()).append('\n');
                    if (function != null) {
                        DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                        if (result.decompileCompleted())
                            report.append(result.getDecompiledFunction().getC()).append('\n');
                    }
                }
                cursor = storage.next();
            }
        }

        decompiler.dispose();
        try (FileWriter writer = new FileWriter(new File(arguments[1]))) {
            writer.write(report.toString());
        }
    }
}
