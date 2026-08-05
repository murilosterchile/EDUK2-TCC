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

(*$Id: wintpint.ml,v 1.3 2005/04/16 08:31:28 poirriez Exp $*)

(*wintpint.ml*)

(* This module provides 
   weight and profit operations when both data types are [int]. The names
   of the values should be sufficient.*) 


type w = int
type p = int
type r = float
type cp = w * p * (int * int)
type item = {i : int; w : w; p : p; r : r}


let w = fst 
let p = snd

let weight_are_integers = true
let zerow = 0
let weight_unit = 1
let zerop = 0
let profit_unit = 1

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
let rand_weight w = try Random.int w with _ -> 0
let max_weight w w' = max w w'
let min_weight w w' = min w w'

let (equal_profits: p -> p -> bool) = (=)
let (profit_smaller: p -> p -> bool) = (<)
let (profit_smallereq: p -> p -> bool) = (<=)
let profit_compare = ( - )
let add_profits = (+)
let mult_int_profit i p = i * p
let substract_profit p p' = p - p'
let ratio_profit_weight p w = (float p) /. (float w)
let profit_of_string = int_of_string
let string_of_profit = string_of_int
let add_int_profit i p = i + p
let profit_of_weight x = x
let profit_of_int x = x
let max_profit p1 p2 = max p1 p2
let rand_profit w = try Random.int w with _ -> 0
let quotient_profit p q = p/q

let mult_weight_ratio  w  r = (float w) *. r
let mult_weight_profit  w p =  w * p
let round_floor_ratio  r = int_of_float r

let profit_to_ratio p = (float p)
let substract_ratio = ( -. )        
let zeror = 0.
let equal_ratios = ( = )
let ratio_smaller = ( < )
let ratio_smallereq = ( <= )
let ratio_compare = ( -. )

let zero_item = {i = 0; w = zerow; p = zerop; r = zeror}

let build_item  i wi pi = {i = i; w = wi; p = pi; r = ratio_profit_weight pi wi}
