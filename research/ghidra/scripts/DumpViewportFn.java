// Ghidra script: dump functions containing SetViewport dispatch calls.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DumpViewportFn extends GhidraScript {

	@Override
	public void run() throws Exception {
		// dump the functions around the known dispatch sites
		long[] targets = { 0x10039a20L, 0x1003c340L, 0x10009d30L, 0x10012780L };
		for (long t : targets) {
			Address addr = toAddr(t);
			Function f = getFunctionContaining(addr);
			if (f == null) {
				createFunction(addr, null);
				f = getFunctionContaining(addr);
			}
			if (f == null) { println("no function at " + addr); continue; }
			println("==== function " + f.getName() + " @ " + f.getEntryPoint() + " ====");
			Instruction cur = getInstructionAt(f.getEntryPoint());
			int n = 0;
			while (cur != null && n < 250) {
				println(cur.getAddress() + ": " + cur.toString());
				cur = cur.getNext();
				n++;
			}
		}
	}
}