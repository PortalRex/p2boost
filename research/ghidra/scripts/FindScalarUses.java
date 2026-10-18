// Lists every instruction whose operands contain one of the given scalars,
// grouped by containing function. Used to locate inlined container code by
// its member offsets. Arguments: <output.txt> <scalar> [<scalar> ...]
//@category p2boost

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindScalarUses extends GhidraScript {
	@Override
	protected void run( ) throws Exception {
		String[ ] args = getScriptArgs( );
		Set< Long > wanted = new HashSet<>( );
		for( int index = 1; index < args.length; ++index )
			wanted.add( Long.decode( args[ index ] ) );
		Address base = currentProgram.getImageBase( );
		try( PrintWriter out = new PrintWriter( new FileWriter( args[ 0 ] ) ) ) {
			for( Instruction instruction : currentProgram.getListing( ).getInstructions( true ) ) {
				for( int operand = 0; operand < instruction.getNumOperands( ); ++operand ) {
					for( Object object : instruction.getOpObjects( operand ) ) {
						if( !( object instanceof Scalar ) || !wanted.contains( ( ( Scalar )object ).getUnsignedValue( ) ) )
							continue;
						Function function = getFunctionContaining( instruction.getAddress( ) );
						out.println( String.format( "0x%x %s : %s", instruction.getAddress( ).subtract( base ),
							function == null ? "?" : function.getName( ), instruction ) );
					}
				}
			}
		}
	}
}
