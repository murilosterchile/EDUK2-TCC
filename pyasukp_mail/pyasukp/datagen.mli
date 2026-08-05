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

(*$Id: datagen.mli,v 1.2 2005/04/16 08:31:28 poirriez Exp $*)


(**
  This module provides data generators for knapsacks. All these generators
  (except the first one) return two arrays of data, one of weight, one of profits.
*)

(** [random_warray m wmax wmin] returns an array of [m] weights
randomly choosen within \[[wmin]..[wmax]\].
*)
val random_warray : int -> Wandp.M.w -> Wandp.M.w -> Wandp.M.w array

(** [notcor m wmin wmax pmin pmax ] returns two array of [m] weights
  and profits randomly choosen within respectiv \[[wmin]..[wmax]\]
  and \[[pmin]..[pmax]\].
*)
val notcor : 
  int -> 
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p ->  Wandp.M.w array * Wandp.M.p array

(** [chung m wmin step ns] returns two arrays of [m] weights and profits 
    such that [w.(i) = (i -1) + wmin] and [p.(i) = w.(i)+step].
    If [ns=true] then after the generation, the two arrays are pairewise
    shuffled.

    This formula is a generalisation of the one published in
    {i C-S. Chung, M.~S. Hung, and W.~O. Rom 1988 }

    {i A Hard Knapsack Problem}.
    {i Naval Research Logistics, 35:85--98, 1988.}
*)
val chung : int -> Wandp.M.w -> Wandp.M.p -> bool -> Wandp.M.w array * Wandp.M.p array

(** [avischung m step ns] is just the application [chung m (1+m*(m+1)) step ns]
*)
val avischung : int -> Wandp.M.p-> bool -> Wandp.M.w array * Wandp.M.p array

(** [avissubset m  ns] is just the application [avischung m 0 ns]*)
val avissubset : int -> bool -> Wandp.M.w array * Wandp.M.p array

(** [weakcor m wmin wmax step ] returns two array of [m] weights
  and profits randomly choosen within respectiv \[[wmin]..[wmax]\]
  and for all [i] [p.(i)] choosen within \[[w.(i)-step .. w.(i)+step]\]
*)
val weakcor : 
  int -> 
   Wandp.M.w -> Wandp.M.w -> Wandp.M.p  ->Wandp.M.w array * Wandp.M.p array

(** [randcor m wmin wmax step ] returns two array of [m] weights
  and profits randomly choosen within respectiv \[[wmin]..[wmax]\]
  and for all [i] [p.(i)] choosen within \[[w.(i)-w.(i)/step .. w.(i)+w.(i)/step]\]
*)
val randcor : 
  int -> 
   Wandp.M.w -> Wandp.M.w -> int  ->Wandp.M.w array * Wandp.M.p array


(** [strongcor m wmin wmax step ] returns two array of [m] weights
  and profits randomly choosen within respectiv \[[wmin]..[wmax]\]
  and for all [i] [p.(i) = w.(i)+step ]
*)
val strongcor : 
  int -> 
   Wandp.M.w -> Wandp.M.w -> Wandp.M.p  ->Wandp.M.w array * Wandp.M.p array

(**[nosimpledom m wmin wmax pmin pmax ns] returns two array of [m] weights
  and profits. Weights are randomly choosen within \[[wmin]..[wmax]\]
  with no duplication. Then the weight array is sorted in increasing order.
  Similarly, the profits are randomly choosen within \[[pmin]..[pmax]\]
  with no duplication. Then the profitt array is sorted in increasing order.
  And finally if [ns=true], thesy are shuffled pairwise.

  This way it is garanteed that no item is simply dominated by an other one.
*)
val nosimpledom :
  int ->
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p -> bool -> Wandp.M.w array * Wandp.M.p array

(**[nosimpledom2] is equivalent to [nosimpledom] except that it 
   guarantees that [p.(1)>w.(1)]
*)
val nosimpledom2 :
  int ->
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p -> bool -> Wandp.M.w array * Wandp.M.p array
(**[nosimpledomcircl n wmin wmax pmin pmax ns k] is equivalent to 
   [nosimpledom] except that it guarantees that 
   [p.(i)=w.(i)+(1+k*i/3)+x] with 0<=x<5.
*)
val nosimpledomstep:
  int ->
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p -> bool -> int -> Wandp.M.w array * Wandp.M.p array

val nosimpledomstep2:
  int ->
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p -> bool -> int -> Wandp.M.w array * Wandp.M.p array

(** Weight are generated to with no common dividers *)
val primsw:
  int ->
  Wandp.M.w -> 
  Wandp.M.w -> 
  Wandp.M.p -> Wandp.M.p -> bool -> int -> Wandp.M.w array * Wandp.M.p array

(**[yapnosimpledom] is equivalent to [nosimpledom] except that it 
   guarantees that if [a] is the index of the lightest item and [b]
   the indew of the heaviest then [w.(b) mod w.(a) <> 0] and
   [p.(b) >= (p.(a) -w.(a))*w.(b)/p.(b) + w.(b)]
*)
val yapnosimpledom :
  int ->
  Wandp.M.w ->
  Wandp.M.w ->
  Wandp.M.p -> 
  Wandp.M.p -> bool -> Wandp.M.w array * Wandp.M.p array * Wandp.M.w

(** [unrsubset m wmin wmax interest sort] returns two array of [m] weights
  and profits. Weights are randomly choosen within \[[wmin]..[wmax]\]
  with no duplication. Then profits are built in the following way:
  [p.(i) <- w.(i)*interest]. Then if [sort=false] then the two arrays are pairwise shuffled.*)
val unrsubset :
  int -> 
  Wandp.M.w -> Wandp.M.w -> int -> bool -> Wandp.M.w array * Wandp.M.p array

(** [subset] is equivalent to [unrsubset] except that it does not guarantee
    that two items are not equal.*)
val subset :
  int -> 
  Wandp.M.w -> Wandp.M.w -> int -> bool -> Wandp.M.w array * Wandp.M.p array

(** [realhard m wmin wmax] returns two array of [m] weights
  and profits. Weights are randomly choosen within \[[wmin]..[wmax]\]
  with no duplication. Then profits are built in the following way:
  [p.(i) <- w.(i) + i].*)
val realhard : int ->  Wandp.M.w -> Wandp.M.w -> Wandp.M.w array *  Wandp.M.p array

(** [saw m wmin wmax step]  returns two array of [m] weights
  and profits. Weights are randomly choosen within \[[wmin]..[wmax]\]
  with no duplication. Then profits are built in the following way:
  [p.(1) <- w.(1) + step], then for all  [i>1] [p.(i) <- p.(i-1) + alpha]
  where [alpha] is randomly choosen in \[[0..w.(i)+step*(w.(i)/w.(1))]\].
  At the end, the arrays are pairwise shuffled.

  So, we are guaranteed  that the ukp generated is a saw ukp.
*)
val saw : 
  int -> 
  Wandp.M.w  -> Wandp.M.w -> Wandp.M.p -> Wandp.M.w array * Wandp.M.p array

(** [unr_saw m wmin wmax pmin pmax step] is equivalent to [saw] 
    except that it does not guarantee that two items are not equal 
    and that profits are choosen within \[[pmin..pmax]\].*)
val unr_saw :
   int -> 
   Wandp.M.w -> 
   Wandp.M.w -> 
   Wandp.M.p -> Wandp.M.p -> Wandp.M.p -> Wandp.M.w array * Wandp.M.p array

(** [harddecreasingratio m wmin wmax pmin pmax]  returns two array of 
   [m] weights  and profits. 

   Weights are randomly choosen within \[[wmin]..[wmax]\]
   with no duplication. Then profits are built in the following way:
  [p.(1) <- x], with [x] in \[pmin..pmax\] then for all  [i>1] 
  [p.(i) <- max((1+p.(i-1)),((w.(i)*(p.(i-1)/w.(i-1))))]
  
  At the end, the arrays are pairwise shuffled.

  So, we are guaranteed  that the ukp generated is a saw ukp.
*)
val harddecreasingratio :
   int -> 
   Wandp.M.w -> 
   Wandp.M.w -> 
   Wandp.M.p -> Wandp.M.p -> Wandp.M.w array * Wandp.M.p array

(** [hardincreasingratio]  is equivalent to harddecreasingratio
   except that for all [i>2] [p.(i)<- (w.(i)*p.(i-1)/w.(i-1)) + i - 1]
*)
val hardincreasingratio :
   int -> 
   Wandp.M.w -> 
   Wandp.M.w -> 
   Wandp.M.p -> Wandp.M.p -> Wandp.M.w array * Wandp.M.p array
