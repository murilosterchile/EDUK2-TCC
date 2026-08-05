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



(* $Id: wandp.ml,v 1.3 2005/04/16 08:31:28 poirriez Exp $ *)
(*wandp.ml*)

(** We can choose the module corresponding to the type of our data. Nothing
 imply that weights and profits are small integers. The only requirement
 is that our [module Wandp.M] specifying these types satisfies the 
 [signature W_AND_P] below.*)

(** Most of the name explain the purpose   of the value. *)
(*** M is not actually  constrain to this signature to avoid the cost of polymorphism
 *)
module type W_AND_P =
  sig
    type w                                   (** the type of the weights*)
    and p                                    (** the type of the profits*)
    and r                                    (** the type of the ratio*)
    and cp                                   (** the type of a critical point *)
    type item = {i: int (**the index*); w: w; p: p; r: r}

    val weight_are_integers: bool
    val zerow: w
        (** The null weight *)
    val weight_unit: w
        (* The unity for the type [w]*)
    val zerop: p
        (** The null profit*)
    val profit_unit: p
        (** The unity for p*)
    val zeror : r
        (** The null ratio *)
    val p : cp -> p
    val w : cp -> w
    val build_item: int -> w -> p -> r -> item
    val zero_item: item

(** All the weight tools, names are explicit*)
    val equal_weights: w -> w -> bool
    val weight_smaller: w -> w -> bool
    val weight_smallereq: w -> w -> bool
    val weight_compare: w -> w -> int
        (** weight comparison tools *)
    val add_weights: w -> w -> w

    val mult_int_weight: int -> w -> w
    val quotient_weight: w -> w -> int
        (** integer division*)
    val substract_weight: w -> w -> w
    val div_weight_int: w -> int -> w
    val weight_of_int: int -> w
    val mod_quot_weight: w -> w -> w * int
        (** euclidian division, returns modulo and quotient*)
    val weight_of_string: string -> w
    val string_of_weight: w -> string
    val add_int_weight: int -> w -> w
    val rand_weight: w -> w
    val max_weight: w -> w -> w
    val min_weight: w -> w -> w

(** All the profits tools, name are explicit*)
    val equal_profits: p -> p -> bool
    val profit_smaller: p -> p -> bool
    val profit_smallereq: p -> p -> bool
    val profit_compare: p -> p -> int
    val add_profits: p -> p -> p
    val mult_int_profit: int -> p -> p
    val substract_profit: p -> p -> p
    val profit_of_string: string -> p
    val string_of_profit: p -> string
    val profit_of_weight: w -> p
    val profit_of_int: int -> p
    val rand_profit: p -> p
    val max_profit: p -> p -> p
    val quotient_profit : p -> p -> int
    val ratio_profit_weight:  p -> w -> r
    val mult_weight_ratio : w -> r -> r
    val mult_weight_profit : w -> p -> p
    val round_floor_ratio : r -> p
    val profit_to_ratio : p -> r
    val substract_ratio : r -> r -> r
    val equal_ratios: r -> r -> bool
    val ratio_smaller: r -> r -> bool
    val ratio_smallereq: r -> r -> bool
    val ratio_compare : r -> r -> int
  end


(** Here we choose that weights and profits are "small" integers, assuming that
   some module [Wintpint] is provided.*)
module M (** : W_AND_P *)= Wintpint

(*** when weights  are "small" integers and profits ar floats *)
(*** module M = Wintpfloat;; *)
open M
let is_worse_weight i j = 
 M.ratio_smaller i.r j.r ||
(M.equal_ratios i.r j.r && 
 M.weight_smallereq j.w i.w)

let is_worse_large i j = 
 M.ratio_smaller i.r j.r ||
(M.equal_ratios i.r j.r && 
 M.weight_smaller i.w j.w)

let is_worse_mod c i j = 
 M.ratio_smaller i.r j.r ||
(M.equal_ratios i.r j.r && 
 let mi,qi = M.mod_quot_weight c i.w
 and mj,qj = M.mod_quot_weight c j.w
 in M.weight_smallereq mj mi)

let is_worse i j = M.ratio_smaller i.r j.r 

let is_lighter i j =
M.weight_smaller i.w j.w
let is_heavyer i j =
M.weight_smaller j.w i.w
