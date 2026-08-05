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

(*$Id: wintpfloat.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)

(*wintpfloat.ml*)

(** This module provides 
   weight and profit operations when weight are [int] and profits are [float].
   The names  of the values should be sufficient.*) 


type w = int
type p = float
type r = float
type cp = (w * p)

      (* the third part is the index of the used item if the critical
         point is built using a unique item, else it is [None]*)

let weight ((w,_),_) = w
let profit ((_,p),_) = p
let used_item (_,i) = i
let weight_and_profit (wp,_) = wp

let weight_are_integers = true
let zerow = 0
let weight_unit = 1
let zerop = 0.
let profit_unit = 1.

let (equal_weights: w -> w -> bool) = (=)
let (weight_smaller: w -> w -> bool) = (<)
let (weight_smallereq: w -> w -> bool) = (<=)
let weight_compare = ( - )
let add_weights = (+)
let mult_int_weight (i:int) (w:w) = i * w
let quotient_weight w w' = w / w'
let substract_weight = (-)
let div_weight_int w i = w/i
let weight_of_int (i:int) = i
let mod_quot_weight w w' = (w mod w', w / w')
let weight_of_string = int_of_string
let string_of_weight = string_of_int
let add_int_weight i w = i + w
let rand_weight w = Random.int w
let max_weight w w' = max w w'
let min_weight w w' = min w w'

let epsilon = 1e-012
let (equal_profits: p -> p -> bool) = 
 fun a b -> a= b or (a < b && a >= b +. epsilon) or a <= b -. epsilon
let (profit_smaller: p -> p -> bool) = (<)
let (profit_smallereq: p -> p -> bool) =  
  fun a b -> a <= b or a <= b +. epsilon
let profit_compare = ( -. )
let add_profits = (+.)
let mult_int_profit i p = (float i) *. p
let substract_profit p p' = p -. p'
let ratio_profit_weight p w =  p /. (float w)
let profit_of_string = float_of_string
let string_of_profit = string_of_float
let profit_of_weight x = (float x)
let max_profit p1 p2 = max p1 p2
let rand_profit p = Random.float p

        
let mult_weight_ratio  w  r = (float w) *. r
let mult_weight_profit  w  p = (float w) *. p
let round_floor_ratio  r =  r

let profit_to_ratio p =  p
let substract_ratio = ( -. )

let zeror = 0.
let equal_ratios = ( = )
let ratio_smaller = ( < )
let reatio_compare = ( -. )
