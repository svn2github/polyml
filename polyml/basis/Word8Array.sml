(*
    Title:      Standard Basis Library: Word8Array, Word8Vector and Byte Structures
    Author:     David Matthews
    Copyright   David Matthews 1999, 2005

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

(* G&R 2004 status: Complete. *)

local
    open RuntimeCalls; (* for POLY_SYS and EXC numbers *)
    
    (* We can't use the segment length for the length of the vector
       as we do for "normal" arrays and vectors.  There are two ways
       of handling this.  We could  implement byte vectors in the same
       way as strings, with a length word in the first word, or we
       could store the length separately, as with arrays.  We could, of
       course, treat arrays in the same way.  Implementing vectors as
       strings simplifies conversion between the two and that's the
       approach I've adopted. *)
    open LibrarySupport

    datatype vector = datatype LibrarySupport.Word8Array.vector
    datatype array = datatype LibrarySupport.Word8Array.array

    val System_lock: string -> unit   = RunCall.run_call1 POLY_SYS_lockseg;
    val System_loads: string*word->Word8.word = RunCall.run_call2 POLY_SYS_load_byte;
    val System_loadb: address*word->Word8.word = RunCall.run_call2 POLY_SYS_load_byte;
    val System_setb: address * word * Word8.word -> unit   = RunCall.run_call3 POLY_SYS_assign_byte;
    val System_move_bytes:
        address*word*address*word*word->unit = RunCall.run_call5 POLY_SYS_move_bytes
    val System_move_str:
        string*word*address*word*word->unit = RunCall.run_call5 POLY_SYS_move_bytes
    val System_isShort   : string -> bool = RunCall.run_call1 POLY_SYS_is_short
    val emptyVec: vector = (* This is represented by a null string not a null vector. *)
        RunCall.run_call1 POLY_SYS_io_operation POLY_SYS_emptystring;

    val maxLen = CharVector.maxLen

    val alloc = LibrarySupport.allocBytes
    
    val vecLength: vector -> int = RunCall.run_call1 RuntimeCalls.POLY_SYS_string_length;
    val wVecLength: vector -> word = RunCall.run_call1 RuntimeCalls.POLY_SYS_string_length;

    (* Casts between int and word. *)
    val intAsWord: int -> word = RunCall.unsafeCast
    and wordAsInt: word -> int = RunCall.unsafeCast

    infix 9 sub (* For what it's worth *)

in
    (* We don't use opaque matching because we need the internal representation of vector
       and array in the IO structures. *)
    structure Word8Vector : MONO_VECTOR =
        struct
        type elem = Word8.word
        type vector = vector

        val maxLen = maxLen;

        val length = vecLength
    
        fun op sub (v as Vector s, i: int): elem =
            if i < 0 orelse i >= length v then raise General.Subscript
            else if System_isShort s
            then RunCall.unsafeCast s 
            else System_loads (s, intAsWord i + wordSize)
     
        (* Because Word8Vector.vector is implemented as a string and Word8.word
           as a byte all these functions have the same implementation in
           Word8Vector and CharVector.  We might be able to avoid the casts
           by some clever use of opaque matching but we would have to do the
           conversion of Word8.word from char to an opaque type at the same
           time as converting Word8Vector.elem to preserve the sharing. *)
        (* Can't that be achieved by Word8Vector :> MONO_VECTOR where type elem = Word8.word ? *)
        val fromList: Word8.word list -> vector =
            RunCall.unsafeCast CharVector.fromList
        and tabulate: int * (int->Word8.word) -> vector =
            RunCall.unsafeCast CharVector.tabulate
        and concat: vector list -> vector = RunCall.unsafeCast CharVector.concat
        and map: (elem -> elem) -> vector -> vector =
            RunCall.unsafeCast CharVector.map
        and mapi: ((int * elem) -> elem) -> vector -> vector =
            RunCall.unsafeCast CharVector.mapi
        and update: vector * int * elem -> vector =
            RunCall.unsafeCast CharVector.update
            
        (* Create the other functions. *)
        structure VectorOps =
            VectorOperations(
                struct
                    type vector = vector and elem = elem
                    val length = RunCall.run_call1 RuntimeCalls.POLY_SYS_string_length
                    fun unsafeSub (Vector s, i) =
                        if System_isShort s then RunCall.unsafeCast s else System_loads(s, i + wordSize);
                    fun unsafeSet _ = raise Fail "Should not be called"
                end);
    
        open VectorOps;

        
        local
            (* Install the pretty printer for Word8Vector.vector *)
            (* We may have to do this outside the structure if we
               have opaque signature matching. *)
            fun pretty(depth: int) _ (x: vector) =
                let
                    open PolyML
                    val last = length x - 1
                    fun put_elem (index, w, (l, d)) =
                        if d = 0 then ([PrettyString "...]"], d+1)
                        else if d < 0 then ([], d+1)
                        else
                        (
                        PrettyString("0wx" ^ Word8.toString w) ::
                            (if index <> last then PrettyString "," :: PrettyBreak(1, 0) :: l else l),
                        d+1
                        )
                in
                    PrettyBlock(3, false, [],
                        PrettyString "fromList[" ::
                        (if depth <= 0 then [PrettyString "...]"]
                         else #1 (foldri put_elem ([PrettyString "]"], depth-last) x) )
                   )
                end
        in
            val () = PolyML.addPrettyPrinter pretty
        end
    
    end (* Vector *);

    structure Word8Array : MONO_ARRAY =
    struct  

        type array = array
        type elem = Word8.word
        type vector = vector
        val maxLen = maxLen;
    
        fun length(Array(l, _)) = wordAsInt l
        
        fun array (length, ini) =
        let
            (* The array is allocated containing zeros.  Some versions of
               the RTS will allow byte vectors to be allocated with other
               values but other versions don't.  For the moment assume
               that we have to initialise the array separately. *)
            val len = unsignedShortOrRaiseSize length
            val vec = alloc len
            fun init i = 
                if len <= i then ()
                else (System_setb(vec, i, ini); init(i+0w1))
        in
            init 0w0;
            Array(len, vec)
        end
    
        fun op sub (Array(l, v), i: int): elem =
            if i < 0 orelse i >= wordAsInt l then raise General.Subscript
            else System_loadb (v, intAsWord i)
    
        fun update (Array (l, v), i: int, new) : unit =
            if i < 0 orelse i >= wordAsInt l
            then raise General.Subscript
            else System_setb (v, intAsWord i, new);
    
        (* Create an array from a list. *)
        fun fromList (l : elem list) : array =
        let
            val length = unsignedShortOrRaiseSize(List.length l);
                
            (* Make a array initialised to zero. *)
            val vec = alloc length;
            
            (* Copy the list elements into the array. *)
            fun init (v, i, a :: l) = (System_setb(v, i, a); init(v, i + 0w1, l))
            |  init (_, _, []) = ();
            
        in
            init(vec, 0w0, l);
            Array(length, vec)
        end
            
        fun tabulate (length: int , f : int->elem): array =
        let
            val len = unsignedShortOrRaiseSize length
            val vec = alloc len
            (* Initialise it to the function values. *)
            fun init i = 
                if len <= i then ()
                else (System_setb(vec, i, f(wordAsInt i)); init(i+0w1))
        in
            init 0w0;
            Array(len, vec)
        end
        

        fun vector(Array(len, vec)) =
            if len = 0w0 then emptyVec
            else if len = 0w1
            then (* Single character string is the character itself. *)
                RunCall.unsafeCast (System_loadb (vec, 0w0))
            else
            let
                (* Make an array initialised to zero. *)
                val new_vec = allocString len
            in
                System_move_bytes(vec, 0w0, RunCall.unsafeCast new_vec, wordSize, len);
                System_lock new_vec;
                Vector new_vec
            end
    
        (* Copy an array into another.  It's possible for the arrays to be the
           same but in that case di must be zero (since len = dlen) and the copy is
           a no-op. *)
        fun copy {src=Array (len, s), dst=Array (dlen, d), di: int} =
            let
                val diW = unsignedShortOrRaiseSubscript di
            in
                if diW+len > dlen
                then raise General.Subscript
                else System_move_bytes(s, 0w0, d, diW, len)
            end
    
        (* Copy a vector into an array. *)
        fun copyVec {src as Vector s, dst=Array (dlen, d), di: int} =
            let
                val len = intAsWord(vecLength src)
                val diW = unsignedShortOrRaiseSubscript di
            in
                if diW + len > dlen
                then raise General.Subscript
                else if System_isShort s (* i.e. length s = 1 *)
                then (* Single character strings are represented by the character
                        so we just need to insert the character into the array. *)
                    System_setb(d, diW, RunCall.unsafeCast s)
                else System_move_str(s, wordSize, d, diW, len)
            end

        (* Create the other functions. *)
        structure ArrayOps =
            VectorOperations(
                struct
                    type vector = array and elem = elem
                    fun length(Array(len, _)) = len
                    fun unsafeSub(Array(_, v), i) = System_loadb(v, i)
                    and unsafeSet(Array(_, v), i, c) = System_setb(v, i, c)
                end);
    
        open ArrayOps;
    
        local
            (* Install the pretty printer for Word8Array.array *)
            (* We may have to do this outside the structure if we
               have opaque signature matching. *)
            fun pretty(depth: int) _ (x: array) =
                let
                    open PolyML
                    val last = length x - 1
                    fun put_elem (index, w, (l, d)) =
                        if d = 0 then ([PrettyString "...]"], d+1)
                        else if d < 0 then ([], d+1)
                        else
                        (
                        PrettyString("0wx" ^ Word8.toString w) ::
                            (if index <> last then PrettyString "," :: PrettyBreak(1, 0) :: l else l),
                        d+1
                        )
                in
                    PrettyBlock(3, false, [],
                        PrettyString "fromList[" ::
                        (if depth <= 0 then [PrettyString "...]"]
                         else #1 (foldri put_elem ([PrettyString "]"], depth-last) x) )
                   )
                end
        in
            val () = PolyML.addPrettyPrinter pretty
        end
    end (* Word8Array *);
    
    structure Word8VectorSlice:> MONO_VECTOR_SLICE where type elem = Word8.word where type vector = Word8Vector.vector =
    (* We use opaque matching here simply to remove a confusing reference to VectorSliceOps when the
       type is printed. *)
    struct
        type vector = vector and elem = Word8.word

        structure VectorSliceOps =
            VectorSliceOperations(
                struct
                    type vector = vector and elem = Word8.word
                    val vecLength = wVecLength
                    fun unsafeVecSub(Vector s, i: word) =
                        if System_isShort s then RunCall.unsafeCast s
                        else System_loads(s, i + wordSize)
                    fun unsafeVecUpdate _ = raise Fail "Should not be called" (* Not applicable *)
                end);
    
        open VectorSliceOps;

        (* vector: get the slice out.  Since the underlying vector is implemented using the basic
           string type we can use substring here. *)
        fun vector slice : vector =
        let
            val (Vector vector, start, length) = base slice
        in
            Vector(RunCall.unsafeCast(unsafeSubstring(RunCall.unsafeCast vector, intAsWord start, intAsWord length)))
        end;
        
        (* It would be more efficient to do these as single operations but it's probably too complicated. *)
        fun concat L = Word8Vector.concat(List.map vector L)
        fun map f slice = Word8Vector.map f (vector slice)
        fun mapi f slice = Word8Vector.mapi f (vector slice)
    
    end (* Word8VectorSlice *);

    local
        (* Install the pretty printer for Word8VectorSlice.slice *)
        (* We may have to do this outside the structure if we
           have opaque signature matching. *)
        fun pretty(depth: int) _ (x: Word8VectorSlice.slice) =
            let
                open PolyML Word8VectorSlice
                val last = length x - 1
                fun put_elem (index, w, (l, d)) =
                    if d = 0 then ([PrettyString "...]"], d+1)
                    else if d < 0 then ([], d+1)
                    else
                    (
                    PrettyString("0wx" ^ Word8.toString w) ::
                        (if index <> last then PrettyString "," :: PrettyBreak(1, 0) :: l else l),
                    d+1
                    )
            in
                PrettyBlock(3, false, [],
                    PrettyString "fromList[" ::
                    (if depth <= 0 then [PrettyString "...]"]
                     else #1 (foldri put_elem ([PrettyString "]"], depth-last) x) )
               )
            end
    in
        val _ = PolyML.addPrettyPrinter pretty
    end;

    structure Word8ArraySlice:> MONO_ARRAY_SLICE where type elem = Word8.word where type vector = Word8Vector.vector
                    where type vector_slice = Word8VectorSlice.slice where type array = Word8Array.array =
    struct
        structure VectorSliceOps =
            VectorSliceOperations(
                struct
                    type vector = array and elem = Word8.word
                    fun unsafeVecSub(Array(_, s), i) = System_loadb(s, i)
                    and unsafeVecUpdate(Array(_, s), i, x) = System_setb (s, i, x)
                    and vecLength(Array(l, _)) = l
                end);
    
        open VectorSliceOps;

        type elem = Word8.word
        type vector = vector
        type array = array
        type vector_slice = Word8VectorSlice.slice

        (* vector: get the slice out. *)
        fun vector slice: vector =
            let
                val (Array(_, vec), start, length) = base slice
            in
                if length = 0 then emptyVec
                else if length = 1
                then (* Single character string is the character itself. *)
                    RunCall.unsafeCast (System_loadb (vec, intAsWord start))
                else
                let
                    val len = intAsWord length
                    (* Make an array initialised to zero. *)
                    val new_vec = allocString len
                in
                    System_move_bytes(vec, intAsWord start, RunCall.unsafeCast new_vec, wordSize, len);
                    System_lock new_vec;
                    Vector new_vec
                end
            end

        (* Copy a slice into an array.  N.B. The arrays could be the same. *)
        fun copy {src, dst, di: int} =
        let
            val (src, start, length) = base src
        in
            if di < 0 orelse di+length > Word8Array.length dst
            then raise General.Subscript
            else (* We can't use System_move_bytes because of the potential overlap problem.
                    Instead we use explicit copying choosing to copy up or down depending
                    on the index whether the source and destination are the same or not. *)
            let
                fun copyUp n =
                if n = length then ()
                else (Word8Array.update(dst, n+di, Word8Array.sub(src, n+start)); copyUp(n+1))
                
                and copyDown n =
                if n < 0 then ()
                else (Word8Array.update(dst, n+di, Word8Array.sub(src, n+start)); copyDown(n-1))
            in
                if di > start then copyDown(length-1) else copyUp 0
            end
            (* System_move_bytes(s, intAsWord start, d, intAsWord di, intAsWord length) *)
        end
    
        (* Copy a vector slice into an array. *)
        fun copyVec {src: Word8VectorSlice.slice, dst=Array (dlen, d), di: int} =
            let
                val (Vector source, i, l) = Word8VectorSlice.base src
                val len = intAsWord l and offset = intAsWord i
                val diW = unsignedShortOrRaiseSubscript di
            in
                if diW + len > dlen
                then raise General.Subscript
                else if System_isShort source (* i.e. length s = 1 *)
                then (* Single character strings are represented by the character
                        so we just need to insert the character into the array. *)
                    System_setb(d, diW + offset, RunCall.unsafeCast source)
                    (* The source is represented by a string whose first word is the length. *)
                else System_move_str(source, offset + wordSize, d, diW, len)
            end
        
    end (* Word8ArraySlice *);

    local
        (* Install the pretty printer for Word8ArraySlice.slice *)
        (* We may have to do this outside the structure if we
           have opaque signature matching. *)
        fun pretty(depth: int) _ (x: Word8ArraySlice.slice) =
            let
                open PolyML Word8ArraySlice
                val last = length x - 1
                fun put_elem (index, w, (l, d)) =
                    if d = 0 then ([PrettyString "...]"], d+1)
                    else if d < 0 then ([], d+1)
                    else
                    (
                    PrettyString("0wx" ^ Word8.toString w) ::
                        (if index <> last then PrettyString "," :: PrettyBreak(1, 0) :: l else l),
                    d+1
                    )
            in
                PrettyBlock(3, false, [],
                    PrettyString "fromList[" ::
                    (if depth <= 0 then [PrettyString "...]"]
                     else #1 (foldri put_elem ([PrettyString "]"], depth-last) x) )
               )
            end
    in
        val () = PolyML.addPrettyPrinter pretty
    end

end;
