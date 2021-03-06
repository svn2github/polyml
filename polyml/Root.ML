(*
    Copyright (c) 2009,2010 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*)

(* Compiler root file.  This gives the "use" instructions necessary to build the
   compiler and suitable for use with an IDE project file.
   It was constructed from the Poly/ML make files. *)
PolyML.print_depth 1;
PolyML.Compiler.reportUnreferencedIds := true;
use "basis/RuntimeCalls.ML";
use "mlsource/MLCompiler/Boot/Address.ML";
use "mlsource/MLCompiler/Boot/Misc.ML";
use "mlsource/MLCompiler/Boot/HashTable.ML";
use "mlsource/MLCompiler/Boot/UniversalTable.ML";
use "mlsource/MLCompiler/Boot/StretchArray.ML";
use "mlsource/MLCompiler/Boot/ml_bind.ML";
use "mlsource/MLCompiler/STRUCTVALSIG.sml";
use "mlsource/MLCompiler/PRETTYSIG.sml";
use "mlsource/MLCompiler/LEXSIG.sml";
use "mlsource/MLCompiler/MAKE_.ML";
use "mlsource/MLCompiler/CODETREESIG.ML";
use "mlsource/MLCompiler/STRUCT_VALS.ML";
use "mlsource/MLCompiler/CodeTree/BaseCodeTreeSig.sml";
use "mlsource/MLCompiler/CodeTree/CodetreeFunctionsSig.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE_FUNCTIONS.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE_LIFETIMES.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE_STATIC_LINK_AND_CASES.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE_REMOVE_REDUNDANT.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE_OPTIMISER.sml";
use "mlsource/MLCompiler/CodeTree/CODETREE.ML";
use "mlsource/MLCompiler/Pretty.sml";
use "mlsource/MLCompiler/CodeTree/CODECONSSIG.sml";
use "mlsource/MLCompiler/CodeTree/CODEGEN_TABLESIG.sml";
use "mlsource/MLCompiler/CodeTree/GENERATE_CODE.ML";
use "mlsource/MLCompiler/CodeTree/CODE_ARRAY.ML";
use "mlsource/MLCompiler/CodeTree/X86CODESIG.sml";
use "mlsource/MLCompiler/CodeTree/X86OUTPUTCODE.ML";
use "mlsource/MLCompiler/CodeTree/X86OPTIMISE.ML";
use "mlsource/MLCompiler/CodeTree/X86LOWLEVEL.ML";
use "mlsource/MLCompiler/Debug.ML";
use "mlsource/MLCompiler/CodeTree/CodeCons.i386.ML";
use "mlsource/MLCompiler/CodeTree/CODEGEN_TABLE.ML";
use "mlsource/MLCompiler/CodeTree/CodeGenTable.ML";
use "mlsource/MLCompiler/CodeTree/BaseCodeTree.sml";
use "mlsource/MLCompiler/CodeTree/GCode.i386.ML";
use "mlsource/MLCompiler/CodeTree/ml_bind.ML";
use "mlsource/MLCompiler/StructVals.ML";
use "mlsource/MLCompiler/LEX_.ML";
use "mlsource/MLCompiler/Symbols.ML";
use "mlsource/MLCompiler/Lex.ML";
use "mlsource/MLCompiler/DATATYPEREPSIG.sml";
use "mlsource/MLCompiler/VALUEOPSSIG.sml";
use "mlsource/MLCompiler/EXPORTTREESIG.sml";
use "mlsource/MLCompiler/STRUCTURESSIG.sml";
use "mlsource/MLCompiler/COMPILER_BODY.ML";
use "mlsource/MLCompiler/SymSet.ML";
use "mlsource/MLCompiler/TYPETREESIG.sml";
use "mlsource/MLCompiler/COPIERSIG.sml";
use "mlsource/MLCompiler/TYPEIDCODESIG.sml";
use "mlsource/MLCompiler/DATATYPE_REP.ML";
use "mlsource/MLCompiler/VALUE_OPS.ML";
use "mlsource/MLCompiler/TYPE_TREE.ML";
use "mlsource/MLCompiler/UTILITIES_.ML";
use "mlsource/MLCompiler/Utilities.ML";
use "mlsource/MLCompiler/PRINT_TABLE.ML";
use "mlsource/MLCompiler/PrintTable.ML";
use "mlsource/MLCompiler/ExportTree.sml";
use "mlsource/MLCompiler/ExportTreeStruct.sml";
use "mlsource/MLCompiler/TypeTree.ML";
use "mlsource/MLCompiler/COPIER.sml";
use "mlsource/MLCompiler/CopierStruct.sml";
use "mlsource/MLCompiler/TYPEIDCODE.sml";
use "mlsource/MLCompiler/TypeIDCodeStruct.sml";
use "mlsource/MLCompiler/DatatypeRep.ML";
use "mlsource/MLCompiler/ValueOps.ML";
use "mlsource/MLCompiler/PARSETREESIG.sml";
use "mlsource/MLCompiler/SIGNATURESSIG.sml";
use "mlsource/MLCompiler/DEBUGGERSIG.sml";
use "mlsource/MLCompiler/STRUCTURES_.ML";
use "mlsource/MLCompiler/PARSE_TREE.ML";
use "mlsource/MLCompiler/DEBUGGER_.sml";
use "mlsource/MLCompiler/Debugger.sml";
use "mlsource/MLCompiler/ParseTree.ML";
use "mlsource/MLCompiler/SIGNATURES.sml";
use "mlsource/MLCompiler/SignaturesStruct.sml";
use "mlsource/MLCompiler/Structures.ML";
use "mlsource/MLCompiler/PARSE_DEC.ML";
use "mlsource/MLCompiler/SKIPS_.ML";
use "mlsource/MLCompiler/Skips.ML";
use "mlsource/MLCompiler/PARSE_TYPE.ML";
use "mlsource/MLCompiler/ParseType.ML";
use "mlsource/MLCompiler/ParseDec.ML";
use "mlsource/MLCompiler/CompilerBody.ML";
use "mlsource/MLCompiler/CompilerVersion.sml";
use "mlsource/MLCompiler/Make.ML";
use "mlsource/MLCompiler/INITIALISE_.ML";
use "mlsource/MLCompiler/Initialise.ML";
use "mlsource/MLCompiler/ml_bind.ML";
