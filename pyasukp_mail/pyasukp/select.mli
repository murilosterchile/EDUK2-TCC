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



(* $Id: select.mli,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)

(** This module provides some selection tools *)


(** [the_min comp tab i] returns the minimal value w.r.t. [comb]
    between [tab.(0)] and [tab.(i)]. *)
val the_min : ('a -> 'a -> bool) -> 'a array -> int -> 'a

(** [is_lighter i j] check if [i] has a lower weight than [j].*)
val is_lighter : Wandp.M.item -> Wandp.M.item -> bool

(** [lightest t k ] returns the lightest item between [t.(0)] and [t.(k)].*)
val lightest : Wandp.M.item array -> int -> Wandp.M.item

(** [quick ord t i j] sort the sub-array [[|t.(i);..;t.(j)|]]
 w.r.t. [ord]. *)
val quick : ('a -> 'a -> bool) -> 'a array -> int -> int -> 'a array

(** [switchblock t d e] returns [k] and switch all
   elements of [t] between [d] and [e].
*)
val switchblock : 'a array -> int -> int -> int


(** [split_n_best comp tab debut fin n] put in the  [n] first 
   places of [tab] the elements such that, for all [j < n, k>=n,
   comp tab.(j) tab.(k)]. The  [n] first elements are not sorted.
   The elements previously at these places are stored elsewhere
   in [tab].
*)
val split_n_best :
  ('a -> 'a -> bool) -> 'a array -> int -> int -> int -> unit

(**  [partition_in_three elim comp tab debut fin ind_pivot] 
   returns [(pivot_a, pivot_b)] such that after the call
   the sub-array of [tab]  between [debut] et [fin] (included) 
   verifies : for all [debut <= i <pivot_a], 
   [comp tab.(i) tab.(ind_pivot) = true], for all [pivot_a <= i <pivot_b], 
   vérifient [comp tab.(i) tab.(ind_pivot)  = false] ; and 
   for all [pivot_b <= i <= fin], 
   [elim tab.(pivot_a) tab.(i) = true]
 *)
val partition_in_three :
  (Wandp.M.item -> Wandp.M.item -> bool) ->
  (Wandp.M.item -> Wandp.M.item -> bool) ->
  Wandp.M.item array -> int -> int -> int -> int * int

(** [multi_part comp elim nb tab] returns a list of indexes pairs [(d_0,f_0);..
   (d_s,f_s)] such that :
   for all i<j  k in \[d_i..f_i\]; m in \[d_j..f_j\] 
   implies [comp tab.(k) tab.(m)]. And
   f_0 - d_0 <= nb
   The elements with indexes r such that
   f_j<r<d_\{j+1\} are removed.
 *)
val multi_part :
  (Wandp.M.item -> Wandp.M.item -> bool) ->
  (Wandp.M.item -> Wandp.M.item -> bool) ->
  int -> int -> int -> Wandp.M.item array -> (int * int) list

(** [next_lightest tab remains nb pacc] returns
    [None] if all the items in [tab] with
    indexes in [remains] are with profit less than [pacc].
    Else it returns [Some ((tabrest,nbrest), next)] 
    where [tabrest] is an array of the [nbrest] lightest items of [tab].
    [next] is the list of indexes of the remaining items in [tab]. *)
val next_lightest :
  Wandp.M.item array ->
  (int * int) list ->
  int -> Wandp.M.p -> ((Wandp.M.item array * int) * (int * int) list) option

val elim2pass_from : ('a -> 'a -> bool) -> 'a array -> int -> int -> int
val elim2pass : ('a -> 'a -> bool) -> 'a array -> int -> int
(** [elim2pass domin tab  nb] deletes in O(n*n)
    the elements [j] of [tab]  such that exists [ i < j] with 
    [domin tab.(i) tab.(j) = true].
*)



