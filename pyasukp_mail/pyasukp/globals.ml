(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*  This file is part of PYAsUKP.                                          *)
(*									   *)
(*  PYAsUKP is free software; you can redistribute it and/or modify        *)
(*  it under the terms of the GNU General Public License as published by   *)
(*    the Free Software Foundation; either version 2 of the License, or	   *)
(*    (at your option) any later version.				   *)
(*    PYAsUKP is distributed in the hope that it will be useful,	   *)
(*    but WITHOUT ANY WARRANTY; without even the implied warranty of	   *)
(*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	   *)
(*    GNU General Public License for more details.			   *)
(*									   *)
(*  You should have received a copy of the GNU General Public License	   *)
(*  along with Foobar; if not, write to the Free Software		   *)
(*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*)
(***************************************************************************)

(*$Id: globals.ml,v 1.10 2005/05/04 19:30:48 poirriez Exp $*)
(*globals.ml*)

(*
  We store here the few global variables required by eduk, All are
 references to allow to change the value by getting the args of the command 
 line.

*)
(*
  [!w] is the array of weights, [!p] the array of profits, [!n] the number
  of items, [!c] the capacity.
*)

(*let (w: Wandp.M.w array ref) = ref ([||])
let (p: Wandp.M.p array ref) = ref ([||])
let n = ref 0
let c = ref Wandp.M.zerow*)

(* Used to specify the length arrays in the sequence result and the temporary buffers*)
let subsize_of_sequence_result = ref 200
let initial_size_of_sequence_result = ref 100
let subsize_ofbuffer = ref 200
let initial_size_ofbuffer = ref 100
let nb_incr = ref 200


(* Used to specify the length of arrays in the information set *)
let sub_size_info_set = ref 10
let size_info_set = ref 100

(* Used to specify the maximal number of reduction layers  and the minimal
   height of one layer*)
let nb_layers = ref 5
let layer_height = ref (Wandp.M.weight_of_int 100)
let nb2select = ref 10


(** The number of items taken in account *)
let rn = ref 100

(** The formula name used to generate (see [Parsecl].*)
and arg_form =  ref "weak"
and name = ref "Weak correlation "

(** The formula function used to generate (see [Parsecl].*)
and rf = ref(fun () -> ())

(** The array of weights *)
and (rw: Wandp.M.w array ref)= ref [||]

(** The array of profitts *)
and (rp: Wandp.M.p array ref) = ref [||]

(** The min value for generated profits *)
and pmin = ref Wandp.M.profit_unit

(** The min value for generated weights *)
and wmin = ref( Wandp.M.mult_int_weight 100 Wandp.M.weight_unit)

(** The max value for generated profits *)
and  pmax = ref Wandp.M.zerop

(** The max value for generated weights *)
and wmax = ref Wandp.M.zerow

(** The coef used to compute the capacity *)
and coefcap = ref 10    

(** The capacity value *)
let rc = ref( Wandp.M.zerow)

(** step and interest are used to compute profits with some formulae.*)
and step = ref(Wandp.M.mult_int_profit 50  Wandp.M.profit_unit)
and interest = ref 1

(** The seed value for random generators *)
and seed = ref 12345

(** Put false if you want that the generetade data are sorted in increasing order
    of weight. *)
and ns = ref true

(** The name of the data source file*)
and rs = ref ""

(** The name of the result file*)
and rr = ref ""

(** Flag to tell if you want to backtrack *)
and rback = ref true

(** Put it to false if you don't want to salve an ukp*)
and solveks = ref true

(** Put Some c in it if you want to solve the knapsack of capacity c*)
and tmprc = ref (None: Wandp.M.w option)

(** Put b in it if you want to solve the knapsack of acapacity c between wmax and b*)
and brc = ref (0)

(** Put false if you don't want to use branch and bound, only dynamic programming*)
and with_bounds = ref true

(** Put false if you don't want to use b&b *)
and with_all_bounds = ref true

(** Put false if you do not want to compute a preprocessing eliminaton using bounds.*)
and prepro = ref true

(** Put true if you want to force the usage of the upperbound defined by martello and toth.*)
and mt = ref false

(** Put true if you want to force the usage of the upperbound Uv defined by us.*)
and uv = ref false

(** Put true if you want to force the usage of both upperbounds .*)
and both = ref false

(** put true if you want to print all the optimal critical points put in [sequence_result]. *)
and trace = ref false

(** Put false if you do not want to youse the zhbr dominance rule.*)
and zhbr = ref true

(** Put false if you do not want to youse the multiple dominance rule.*)
and dm = ref false    

(** The maximum number of solutions built in the b&b process, put -1 if you want to use with no limitations.*)
and nbmaxsol = ref 10000

(** The number of bests items selected to build the core pb in the b&b initial process, if -1 then this number is the max of 100 and n/100.*)
and nbbests = ref (-1)

(** Which version of bandb *)
and bbnewv = ref false
and bbnewv2 = ref false
and bbnewv3 = ref false

let nbnode = ref 0
let d = ref 100

let print_variables () = 
begin
  Printf.printf "rn %d arg_form %s name %s pmin %d wmin %d pmax %d wmax %d\n
    coefcap %d rc %d step %d interest %d seed %d ns %B rs %s rback %B brc %d\n
    with_bounds %B with_all_bounds %B prepro %B mt %B uv %B zhbr %B dm %B \n
    nbmaxsol %d nbbests %d bbnewv %B\n
    "
    !rn !arg_form !name !pmin !wmin !pmax !wmax
    !coefcap !rc !step !interest !seed !ns !rs !rback !brc
    !with_bounds !with_all_bounds !prepro !mt !uv !zhbr !dm
    !nbmaxsol !nbbests !bbnewv ;
  flush stdout
end


