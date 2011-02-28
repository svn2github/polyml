(* Example code for
   let
    fun addOne x = x+1
   in
    addOne 2
   end
*)
local
    open PolyML.CodeTree RuntimeCalls
    val addFn = rtsFunction POLY_SYS_aplus
    val one = mkConstant (RunCall.unsafeCast 1)
    val addOne =
        mkFunction(mkCall(addFn, [mkLoad(~1, 0), one]), 1, "addOne")
    val two = mkConstant (RunCall.unsafeCast 2)
    val block =
        mkEnv[mkDec(1, addOne), mkCall(mkLoad(1, 0), [two])]
    val run = genCode block ()
    val result = valOf (evalue run)
in
    val r: int = RunCall.unsafeCast result
end;
