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



(* $Id: dominance.ml,v 1.2 2005/02/28 10:39:34 poirriez Exp $ *)
(*dominance.ml*)

(** Here we provide a set of dominance relations.*)
open Wandp.M

let basic_critical_point (w1,p1) (w2,p2) = 
  (weight_smallereq w2 w1) && (profit_smallereq p1 p2)

let simple w p i j = basic_critical_point (w.(j),p.(j)) (w.(i),p.(i))


(** An item type is threshold dominated under c iff there is a capicity below c which is
   a multiple of its weight and for which it doesn't contribute to the optimal solution. 
   This is equivalent to the following criteria.*)

let threshold_test last_contribution weight capacity =
        Wandp.M.weight_smallereq (Wandp.M.add_weights last_contribution weight) capacity

let multiple wi pi wj pj=
 profit_smallereq pj (mult_int_profit(quotient_weight wj wi) pi)

let zb wbest pbest wi pi = 
Wandp.M.substract_profit 
    (Wandp.M.mult_weight_profit wi pbest)
    (Wandp.M.mult_weight_profit wbest pi)

let zhubrougan wbest pbest wi pi wk pk =
  let wkmodwbest = wk mod wbest in
  wkmodwbest = zerow ||
  (wkmodwbest = wi mod wbest &&
   Wandp.M.profit_smallereq (zb wbest pbest wi pi) (zb wbest pbest wk pk)  )

let zhbr_if_sorted wbest i k =
 let mi,_ = mod_quot_weight i.w wbest 
 and mk,_ = mod_quot_weight k.w wbest 
 in
 equal_weights mi mk && weight_smallereq i.w k.w

let multiple_or_zhbr_if_sorted wbest i k =
weight_smallereq i.w k.w &&
  ((equal_weights (fst(mod_quot_weight i.w wbest)) (fst(mod_quot_weight k.w wbest)))
 || multiple i.w i.p k.w k.p)

let multiple_or_zhbr_if_w_sorted wbest pbest i k =
  wbest <> k.w &&
  (  zhubrougan wbest pbest i.w i.p k.w k.p
   || multiple i.w i.p k.w k.p)
let zhbr_if_w_sorted wbest pbest i k =
  wbest <> k.w &&
  zhubrougan wbest pbest i.w i.p k.w k.p
   
let multiple_item i k = multiple i.w i.p k.w k.p
