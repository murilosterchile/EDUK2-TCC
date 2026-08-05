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



(* $Id: eduk.mli,v 1.3 2005/02/28 16:56:58 poirriez Exp $ *)

(**
@author Vincent Poirriez

This is the module which contains the principal functions of the software
These functions are called [Eduk.forward] that computes
the optimal value for the knapsack profit and [Eduk.rebuildsol] that
build one optimal solution, i.e. one solution with profit equal
to the optimal profit.

*)


val check_threshold_dominance : int array -> Wandp.M.w -> unit

(** [reduction w p c hslice wb wab] computes the reduction phase
    of the forward algorithm. 
    It returns [((woptr,poptr),ibestw)]. 
    - [woptr] is the weight of the lightest optimal solution computed above
      the weights of the detected not-dominated items.
      All the optimal solutions below [woptr] are computed during the evaluation.
      No not-dominated item has a weight above [woptr].
    - [poptr] is the profit of the optimal solution for the capacity [woptr]
    - [ibestw] is the weight of the best encountered item.

 It may raise the exception [BOUNDS.Optimal (e,cp,s)].

 The computation is done by slices of height [hslice], using bounds (depending
 on the values of the flags [wb] and [wab].

 During the computation, the global data structures below are filled in:
 - [Datastruct.sequence_result]: the increasing sequence of optimal critical points.
 - [Datastruct.iteminfos]: the structure holding informations on the items
 - [Datastruct.decreasingS]: contains the remaining (pointers to) items to use
 - [Datastruct.required]: the queue of (pointers to) items wich are required to 
   build all the optimal critical points in [sequence_result].

*)
val reduction :
  Wandp.M.w array ->
  Wandp.M.p array ->
  Wandp.M.w ->
  Wandp.M.w -> bool -> bool -> (Wandp.M.w * Wandp.M.p) * Wandp.M.w

(** [standard w p c c' layer_height wb (wacc, pacc) creduct] is called after
    [reduction].
    It computes the optimal critical points till the capacity [c'] is reached
    or it remains only one item in [decreasingS] or the bound stops the computation.
    [c'] is roughly equal to [c/2] as when we know all the optimal critical points
    below [c/2] we can compute all those between [c/2] and [c].
    -[creduct] is the capacity at which the [reduction] phase stopped.
    -[(wacc,pacc)] is the last optimal critical point computed before the call.

   It may raise the exception [BOUNDS.Optimal (e,cp,s)].    
*)
val standard :
  Wandp.M.w array ->
  Wandp.M.p array ->
  Wandp.M.w ->
  Wandp.M.w ->
  int ->
  'a ->
  Wandp.M.w * Wandp.M.p ->
  Wandp.M.w -> bool * Wandp.M.w * Wandp.M.p * Wandp.M.w

(** [forward w p c wb wab] computes all the optimal critical points below
    [c/2] or is stopped if the computation of the bounds compute an optimal solution.
*)
val forward :
  Wandp.M.w array ->
  Wandp.M.p array ->
  Wandp.M.w -> bool -> bool -> Datastructtype.end_of_computation

(** [rebuildsol w p c resforward] take the result produced by [forward]
    and rebuilt an optimal solution wich is returned in the form
    of a list of pairs (index of the item, number of this item)
*)
val rebuildsol :
  Wandp.M.w array ->
  int array ->
  Wandp.M.w -> Datastructtype.end_of_computation -> (int * int) list


(** [Eduk.solver  w p c wb wab wmt zhbr dm nbmaxsol nbbests pp tr]
    returns  the solution in the form [[(x1,nbx1)..(xk,nbxk)],opt_cap,opt_pro]
    where 
    -[opt_cap] and [opt_pro] are the optimal values of the capacity and 
    the profit 
    - the [(xi,nbxi)] are for [xi] the index of the items and [nbxi] the
      number of [xi] to use. Only non zero nbxi are present.

   The parameters are of to kinds, those defining the problem, are not optional:
   - [w] the array of weights
   - [p] the array of profits
   - [c] the capacity

   Those that tune the solver are optional and have default values:
   - [wb] (true)  and [wab] (true) to be [true] if the use of branch and bound is wanted.
   - [zhbr] (true) to be true if the use of the zhbr dominance is to be used
   - [dm] (false) to be true if the use of the multiple dominance is to be used
   - [wmt] (false) to be true if it is wanted that the upper bound defined by Martello and Thot is used.
   -[nbmaxsol] (10000) the maximal optimal number of solutions built by the branch and bound process, this is to avoid pathological cases. Put -1 if no limit is wanted.
   - [nbbests] (-1) is the number of best items selected to build the core problem
     for the branch and bound process. If -1, then it is computed to be equal to the max of 100 and n/100.
   - [tr] (false) to be true if you want to print all the values of the optimal critical points put in [sequence_result].
*)
val solver: 
   ?wb:bool ->
   ?wab:bool ->
   ?wmt:bool ->
   ?zhbr:bool ->
   ?dm:bool ->
   ?nbmaxsol:int ->
   ?nbbests:int -> 
   ?pp:bool -> 
   ?tr:bool ->
   Wandp.M.w array ->
   Wandp.M.p array -> 
   Wandp.M.w -> 
   (int * int) list * int * int

(** The functions below are to be used by external programs*)
val resolve :
  Wandp.M.w array ->
  Wandp.M.p array -> Wandp.M.w -> (int * int) list * int * int
val resolve_array :
  Wandp.M.w array ->
  Wandp.M.p array -> Wandp.M.w -> int array * int array * int * int

val accumulate :
  Wandp.M.w array -> int array -> (int * int) list -> int * int

