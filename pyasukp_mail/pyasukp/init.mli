(***************************************************************************)
(*                                  PYAsUKP                                *)
(*    PYAsUKP: Yet Another solver (for the) Unbounded Knapsack Problem     *)
(*           Vincent Poirriez with Nicola Yanev and Rumen Andonov          *)
(*                          LAMIH-ROI UMR CNRS 8530                        *)
(*  Copyright (C) 1998-2005  Vincent Poirriez				   *)
(* <vincent Dot poirriez At univ-valenciennes Dot fr>                      *)
(*   Distributed only by permission.                                       *)
(*   This program is free software; See the file LICENSE  for precise      *)
(*   descriptions of the license.                                          *)
(***************************************************************************)

(* $Id: init.mli,v 1.2 2005/02/24 16:04:56 poirriez Exp $ *)

(**
@author Vincent Poirriez
This module provides the functions to initialize the data structures.
First, the [structures] function is used to return all the data structures.
It seems to be more efficient to store the information about the used item
types in a store and have an indirection to this store rather than directly
in a list of item information.
It seems also more efficient to store in separate data structures the different parts of
the information.
A side effect of the structures function is to compute the upper bounds if it is required.
*)

val rwith_wp :
  (Datastructtype.BOUNDS.t ->
   Wandp.M.w -> Wandp.M.p -> int * int -> Datastructtype.BOUNDS.t)
  ref

(** [structures w p c wb wab] go through the whole set of items
    defined by [w] the array of weights and [p] the array of
    profits. The three [(ib1,ib2,ib3)] best items ( in term of ratio p/w) are
    peeked up together with the two lightest [(imin1,imin2)].
    The items [i] appearing to be dominated via the rules: 
    [Dominance.multiple  imin1.w imin1.p i.w i.p] or [Dominance.multiple ib1.w ib1.p i.w i.p]
    are excluded.
    The initial  bound is computed, using the function pointed by
    [rwith_wp]. If [Parsecl.prepro] contains [true], then 
    an other pass is performed to eliminate the items dominated in the context of [c].

    The value returned is 
    [(rarray,nbitems, iminw, imin, imaxw, imax, bests)] where
    - [rarray] is the array of items to consider
    - [nbitems] is the number of items in [rarray]
    - [iminw] is the weight of the lightest item 
    - [imin] is the lightest item
    - [imaxw] is the weight of the heavyest item 
    - [imax] is the heavyest item
    - [bests] are the three best items [(ib1,ib2,ib3)].
    
*)
val structures :
  Wandp.M.w array ->
  Wandp.M.p array ->
  Wandp.M.w ->
  bool ->
  bool ->
  Wandp.M.item array * int * Wandp.M.w * Wandp.M.item * Wandp.M.w *
  Wandp.M.item * (Wandp.M.item * Wandp.M.item * Wandp.M.item)

(** [add_in_decreasingS ratios weights decreasingS item_index] adds the item 
   [item_index] to its place in [decreasingS] w.r.t. the decreasing order of ratios (profit/weight) 
   In case of equality of the ratios, the lightest item is the first one.
   [decreasingS] is a mutable structure.*)
val add_in_decreasingS :
  Wandp.M.w array ->
  Datastructtype.iteminfos ->
  Wandp.M.w -> Wandp.M.r -> int * int -> (int * int) Chainlist.rt -> unit

(** A simple function to introduce an item type in the data structures.
  [introduce w (waccprev, paccprev) item]
    simply checks if [item]  is not dominated. We assume that the optimal
   solutions are known for all the capacities less or equal than its weight.
   It returns [(true,item.p)]  if [item] is introduced else it returns
   [(false,paccprev)]
 *)
val introduce :
  Wandp.M.w array ->
  Wandp.M.w * Wandp.M.p -> Wandp.M.item -> bool * Wandp.M.p

(** [Init.init imin1] put the lightest item in the data structures,
    initialize the [sequence_result] with its corresponding value.
    If it is required, it checks if this lightest item is dominated
    in the context of the capacity. If so, it is not introduced.

    It assumes that [Init.structures] was called previously.*)
val init : Wandp.M.item ->  Wandp.M.w * Wandp.M.p
