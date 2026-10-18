// Writes every function body range as "start_rva,end_rva,entry_rva,name" so
// research/tools/etw_profile.py can aggregate CPU samples per function.
// Arguments: <output.csv>
//@category p2boost

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class ExportFunctionRanges extends GhidraScript {
	@Override
	protected void run( ) throws Exception {
		Address base = currentProgram.getImageBase( );
		try( PrintWriter out = new PrintWriter( new FileWriter( getScriptArgs( )[ 0 ] ) ) ) {
			for( Function function : currentProgram.getFunctionManager( ).getFunctions( true ) ) {
				long entry = function.getEntryPoint( ).subtract( base );
				for( AddressRange range : function.getBody( ) ) {
					out.println( String.format( "0x%x,0x%x,0x%x,%s", range.getMinAddress( ).subtract( base ),
						range.getMaxAddress( ).subtract( base ) + 1, entry, function.getName( ) ) );
				}
			}
		}
	}
}
