// Ghidra script: find functions with mov reg,[reg+0xbc] followed by call reg
// (the D3D9 SetViewport vtable dispatch pattern).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class FindViewportCommit extends GhidraScript {

	@Override
	public void run() throws Exception {
		Listing listing = currentProgram.getListing();
		InstructionIterator it = listing.getInstructions(true);
		int count = 0;
		while (it.hasNext()) {
			Instruction ins = it.next();
			String mnem = ins.getMnemonicString();
			if (mnem.equals("MOV")) {
				String op = ins.getDefaultOperandRepresentation(1).toLowerCase();
				if (op.contains("0xbc")) {
					// look ahead a few instructions for a CALL to the same register
					Instruction next = ins.getNext();
					for (int i = 0; i < 8 && next != null; i++) {
						if (next.getMnemonicString().equals("CALL")) {
							Function f = getFunctionContaining(ins.getAddress());
							println("SetViewport dispatch @ " + ins.getAddress() + " in " + (f != null ? f.getName() + "@" + f.getEntryPoint() : "no-function"));
							count++;
							break;
						}
						next = next.getNext();
					}
				}
			}
		}
		println("total: " + count);
	}
}