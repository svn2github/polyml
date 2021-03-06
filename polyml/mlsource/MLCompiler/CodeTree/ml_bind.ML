(*
    Copyright (c) 2000
        Cambridge University Technical Services Limited

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

structure CodetreeFunctions =
    CODETREE_FUNCTIONS(structure BASECODETREE = BaseCodeTree)

structure CodetreeLifetimes =
    CODETREE_LIFETIMES(
        structure BASECODETREE = BaseCodeTree
        structure CODETREE_FUNCTIONS = CodetreeFunctions
    )

structure CodetreeStaticLinkAndCases =
    CODETREE_STATIC_LINK_AND_CASES(
        structure BASECODETREE = BaseCodeTree
        structure CODETREE_FUNCTIONS = CodetreeFunctions
    )

structure CodetreeRemoveRedundant =
    CODETREE_REMOVE_REDUNDANT(
        structure BASECODETREE = BaseCodeTree
        structure CODETREE_FUNCTIONS = CodetreeFunctions
    )

structure CodetreeOptimiser =
    CODETREE_OPTIMISER(
        structure BASECODETREE = BaseCodeTree
        structure CODETREE_FUNCTIONS = CodetreeFunctions
        structure REMOVE_REDUNDANT = CodetreeRemoveRedundant
    )

structure CodeTree = 
  CODETREE (
    structure PRETTY        = Pretty
    structure GCODE         = GCode
    structure DEBUG         = Debug
    structure BASECODETREE  = BaseCodeTree
    structure CODETREE_FUNCTIONS = CodetreeFunctions
    structure LIFETIMES     = CodetreeLifetimes
    structure STATIC_LINK_AND_CASES = CodetreeStaticLinkAndCases
    structure OPTIMISER     = CodetreeOptimiser
  );

