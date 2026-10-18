// Resolves module RVAs from a CPU profile to their containing functions.
// Arguments: <output.txt> [decompile] <rva> [<rva> ...]
// Each RVA is relative to the program's image base, matching etw_profile.py.
//@category p2boost

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DescribeRvas extends GhidraScript {
	@Override
	protected void run( ) throws Exception {
		String[ ] args = getScriptArgs( );
		boolean decompile = args.length > 1 && args[ 1 ].equals( "decompile" );
		int first = decompile ? 2 : 1;
		Address base = currentProgram.getImageBase( );
		DecompInterface decompiler = new DecompInterface( );
		decompiler.openProgram( currentProgram );
		Set< Function > described = new LinkedHashSet<>( );
		try( PrintWriter out = new PrintWriter( new FileWriter( args[ 0 ], true ) ) ) {
			out.println( "== " + currentProgram.getName( ) + " base=" + base );
			for( int index = first; index < args.length; ++index ) {
				long rva = Long.decode( args[ index ] );
				Address address = base.add( rva );
				Function function = getFunctionContaining( address );
				if( function == null ) {
					out.println( String.format( "0x%x -> (no function)", rva ) );
					continue;
				}
				long entry = function.getEntryPoint( ).subtract( base );
				out.println( String.format( "0x%x -> %s entry=0x%x size=%d", rva, function.getName( ), entry, function.getBody( ).getNumAddresses( ) ) );
				StringBuilder callers = new StringBuilder( );
				for( Reference reference : getReferencesTo( function.getEntryPoint( ) ) ) {
					Function caller = getFunctionContaining( reference.getFromAddress( ) );
					callers.append( String.format( " %s@0x%x", reference.getReferenceType( ).getName( ), reference.getFromAddress( ).subtract( base ) ) );
					if( caller != null )
						callers.append( "(" + caller.getName( ) + ")" );
				}
				out.println( "   refs:" + callers );
				described.add( function );
			}
			if( decompile ) {
				for( Function function : described ) {
					DecompileResults results = decompiler.decompileFunction( function, 120, monitor );
					out.println( "---- " + function.getName( ) );
					out.println( results.decompileCompleted( ) ? results.getDecompiledFunction( ).getC( ) : "decompile failed" );
				}
			}
		}
	}
}
