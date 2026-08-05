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



(* $Id: bounds.ml,v 1.7 2005/04/27 13:09:14 poirriez Exp $ *)
(* bounds.ml*)
open Sequences
open Datastructtype
open Datastructtype.BOUNDS
open Datastruct
open Wandp.M

let sort_of bound = 
  match bound.misc with
  | V _ -> Sv
  | MT _ -> Smt
  | Both _ -> Sboth
  | Uphc _ -> Suphalfc

let b2 m = match m with
| Both r -> r.mt.mtb2
| MT e -> e.mtb2
| _ -> invalid_arg "Bounds.b2"
let b3 m = match m with
| Both r -> r.mt.mtb3
| MT e -> e.mtb3
| _ -> invalid_arg "Bounds.b3"
    
let miscv b = match b.misc with
| Both r -> r.v
| V m -> m
| _ -> invalid_arg "Bounds.miscv"

let miscmt b = match b.misc with
| Both r -> r.mt
| MT m -> m
| _ -> invalid_arg "Bounds.miscmt"

let cap_mult_prof_quot_weight c p w =
  let seuil = max_int / p in
  if seuil >= c then (c * p) / w else begin
      if !messbigint then 
	prerr_endline "Integer overflow, I use Big_int arithmetic";
      messbigint := false;
      Big_int.int_of_big_int(
      Big_int.div_big_int 
        (Big_int.mult_int_big_int c (Big_int.big_int_of_int p)) 
        (Big_int.big_int_of_int w))
    end


let create_mt_upon  (b1,b2,b3) pcurrent c =
  let w1 = b1.w and w2 = b2.w and p1 = b1.p and p2 = b2.p
  and w3 = b3.w and p3 = b3.p in
  let cb, xb_1 = mod_quot_weight c w1 in
  let c', xb_2 = mod_quot_weight cb w2 in
  let z' = add_profits (mult_int_profit xb_1 p1) (mult_int_profit xb_2 p2) in
  let c'p3w3 = cap_mult_prof_quot_weight c' p3 w3
  in
  let u0 = z'+ c'p3w3    in
  let itrunc = (w2-c'+w1-1)/w1 in
  let c'itruncw1p2w2 = 
    let seuil = max_int / (c'+itrunc*w1) in
    if seuil >= p2 then ((c'+itrunc*w1)*p2)/w2 else begin
      if !messbigint then prerr_endline "Integer overflow, using Big_int arithmetic";
      messbigint := false;
      Big_int.int_of_big_int(
      Big_int.div_big_int 
        (Big_int.mult_int_big_int (c'+itrunc*w1) (Big_int.big_int_of_int p2)) 
        (Big_int.big_int_of_int w2))
    end
  in
  let ub1 = z' + c'itruncw1p2w2 -itrunc*p1 in
  let xb_3 = quotient_weight c' w3 in
  {c = c ;
   u =  add_profits pcurrent (max_profit u0 ub1) ;
   z = ref ( add_profits pcurrent (add_profits z' (mult_int_profit xb_3 p3))) ;
   b1 = b1;
   xb_1 = xb_1 ;
   misc = MT{
   xb_2 = xb_2 ;
   xb_3 = xb_3;
   mtb2 = b2;
   mtb3 = b3;}
 } 
(** [create_upon pcurrent c (b1,b2,b3)] returns the bounds data for a knapsack of capacity [c]
   with the three best items [b1,b2,b3] and adding the initial profit [pcurrent]. One
   can understand this as the computation of the bounds for the remaining capacity of 
   a knapsack, knowing the optimal solution for some lower capacity knapsack.
 *)
    
let optimal_computed b = equal_profits !(b.z) b.u
    
let create_mt  bests3 c = 
  let b = create_mt_upon  bests3 zerop c in
  if optimal_computed b  then begin
    rbound := Some b; 
    raise (Optimal (b,(zerow,zerop,(-1,-1)),[]))
  end
  else b

    
let with_mt_wp bound wcpt pcpt ipt =
  let remain_c = substract_weight bound.c wcpt in
  let bi = create_mt_upon (bound.b1, b2 bound.misc, b3 bound.misc) pcpt remain_c  in
  if profit_smaller !(bound.z) !(bi.z) then bound.z := !(bi.z);
  if optimal_computed bound  then begin
    rbound := Some bound; 
    raise (Optimal (bi,(wcpt,pcpt,ipt),[]))
  end
  else bi
(** [with_wp bounds wcpt pcpt] compute the bounds for the knapsack knowing a previous
   [bounds] data and the optimal profit [pcpt] for the knapsack of capacity [wcpt]. 
   It updates the best known solution if necessary. It returns the bounds data for
   the knapsack of the remaining capacity, adding the profit [pcpt] to the bounds.*)
    
    
(** We can now use the bounds information to detect if a critical point can be discarded.
   If the computed upper bound using the critical point is lower than the current
   known best solution, the critical point will not be used in the opetimal solution.
   One as to be awared that this information is dependant of the capacity of the knapsack.
   Thus, using this information will disable to use the stored informations to know
   the optimal solution for some knapsacks with a different capacity. That's why we
   call this dominance relation [context_dominance]. It's also known as variable reduction*)
    
let is_context_dominated bound bound_with_cpt =
  profit_smaller bound_with_cpt.u !(bound.z)
    ;;

(** In the case when all items verify, assuming that the imin is the index of the lightest item,
   (pi- wi) <= (wi/wimin)(pimin -wimin) we can use a tighter upper bound than the MT bound.
   Let us call v bound this bound and denote it by uv. *)

let diff_profit_weight p w = substract_profit p (profit_of_weight w)
exception Exists
let array_exists pred tab =
  try
    for i = 0 to Array.length tab  -1 do if pred tab.(i) then raise Exists done;
    false
  with Exists -> true
      
let psi_ minitem =  
  let wm = minitem.w + 1 in
  let plus = if wm mod minitem.p = 0 then 0 else 1 in
  plus + wm/minitem.p

let delta_ psi it = psi * it.p - it.w

let alpha_i minitem delta_1 psi it = 
 ((float (delta_ psi it))/.(float delta_1))/.(float (it.w/minitem.w))

let alpha_ items minitem psi delta_1 =
  Array.fold_right 
    (fun it alph -> 
      if it = minitem then alph else 
      max alph (alpha_i minitem delta_1 psi it)) 
    items 0.

let compute_uv alpha delta_1 psi xb_imin c = 
  add_profits 
    (profit_of_weight c)  
    (truncate((alpha *. float((mult_int_profit xb_imin delta_1)))/.(float psi))) 

let create_v b1 items minitem psi delta_1 alpha c=
  let xb_imin = quotient_weight c minitem.w
  in
  let b =
    {c = c ;
     u = compute_uv  (max 1. alpha) delta_1 psi xb_imin c ;
     z = ref (mult_int_profit xb_imin minitem.p) ;
     b1 = b1;
     xb_1 = xb_imin ;
     misc = V{
     imin = minitem;
     alpha = alpha;
     psi = psi;
     delta_1 = delta_1};
   }
  in 
  if optimal_computed b  then begin
    rbound := Some b; 
    raise (Optimal (b,(zerow,zerop,(-1,-1)),[]) )
  end
  else b
    
let with_v_wp bound wcpt pcpt ipt =
  let remain_c = substract_weight bound.c wcpt in
  let miscv = miscv bound in
  let xb_imin =  quotient_weight remain_c miscv.imin.w in
  let bi = 
    {c = remain_c ;
     u = add_profits 
       pcpt (compute_uv (max 1. miscv.alpha) miscv.delta_1 miscv.psi xb_imin remain_c) ;
     z = ref (add_profits pcpt (mult_int_profit xb_imin miscv.imin.p)) ;
     b1 = bound.b1;
     xb_1 = xb_imin;
     misc = V miscv;
   }
  in
  if profit_smaller !(bound.z) !(bi.z) then bound.z := !(bi.z);
  if optimal_computed bound  then begin
    rbound := Some bound; 
    raise (Optimal (bi,(wcpt,pcpt,ipt),[])) 
  end
  else bi

let with_both_wp bound wcpt pcpt ipt =
  let remain_c = substract_weight bound.c wcpt in
  let bmiscv = miscv bound in
  let xb_imin =  quotient_weight remain_c bmiscv.imin.w in
  let biv = 
    {c = remain_c ;
     u = add_profits 
       pcpt (compute_uv (max 1. bmiscv.alpha) bmiscv.delta_1 bmiscv.psi xb_imin remain_c) ;
     z = ref (add_profits pcpt (mult_int_profit xb_imin bmiscv.imin.p)) ;
     b1 = bound.b1;
     xb_1 = xb_imin;
     misc = V bmiscv;
   }
  in
  let bimt = create_mt_upon (bound.b1, b2 bound.misc, b3 bound.misc) pcpt remain_c 
  in
  if profit_smaller !(bound.z) !(biv.z) then bound.z := !(biv.z);
  if profit_smaller !(bound.z) !(bimt.z) then bound.z := !(bimt.z);
  let u = min bimt.u biv.u in
  let sb = if u = bimt.u then Smt else (*Printf.printf "Not saw ukp but Sv min than S3\n"; Sv *)Sv in
  let bi = 
    {c = remain_c ;
     u = u;
     z = ref (max !(bimt.z) !(biv.z));
     b1 = bound.b1;
     xb_1 = xb_imin;
     misc = Both{mt= miscmt bimt; v = bmiscv; sb = sb}
   } in
  if optimal_computed bound  then begin
    rbound := Some bound; 
    raise (Optimal (bi,(wcpt,pcpt,ipt),[])) 
  end
  else  bi

let create_both ((b1,_,_) as bests) items minitem psi delta_1 alpha c =
 let bmt = create_mt bests c 
 and bv = create_v b1 items minitem psi delta_1 alpha c in
 let u = min bmt.u bv.u in
 let sb = if u = bmt.u then Smt else (*Printf.printf "Not saw ukp but Sv %d min than S3 %d\n" bv.u bmt.u ; Sv *)
 Sv in
 {c= bmt.c;
  u = u;
  z = ref (max !(bmt.z) !(bv.z));
  b1 = bmt.b1;
  xb_1 = bmt.xb_1;
  misc = Both{mt= miscmt bmt; v = miscv bv; sb = sb}
}

(** Je change pour créer un Mt dès que je sais que ce n'est pas un Saw UKP
    Le calcul des deux bornes coute trop cher. *)
let create_both ((b1,_,_) as bests) items minitem psi delta_1 alpha c =
 let bmt = create_mt bests c 
 and bv = create_v b1 items minitem psi delta_1 alpha c in
 let u = min bmt.u bv.u in
 let sb = if u = bv.u then Sv else Smt in
 {c= bmt.c;
  u = u;
  z = ref (max !(bmt.z) !(bv.z));
  b1 = bmt.b1;
  xb_1 = bmt.xb_1;
  misc = 
   if sb = Sv || !(Globals.both) then Both{mt= miscmt bmt; v = miscv bv; sb = sb}
   else MT (miscmt bmt)
}

let create_bound bests c minitem items flag = 
  if flag then
    if !(Globals.mt) then  Some (create_mt  bests c)
    else 
      let (b1,_,_) = bests in
      let psi = psi_ minitem in
      let delta_1 = delta_ psi minitem in
      let alpha = alpha_ items minitem psi delta_1 in
      if alpha <= 1.|| !(Globals.uv) then 
	Some (create_v b1 items minitem psi delta_1 alpha c)
      else 
	begin
	  (*Printf.printf "alpha: %f, psi: %d\n" alpha psi;
	  flush stdout;*)
	  Some(create_both bests items minitem psi delta_1 alpha c)
	end
  else None
      
let compute_lb c (b1, b2, b3) wcpt pcpt =
  let remain_c = substract_weight c wcpt in
  let w1 = b1.w and w2 = b2.w and p1 = b1.p and p2 = b2.p
  and w3 = b3.w and p3 = b3.p in
  let cb, xb_1 = mod_quot_weight remain_c w1 in
  let c', xb_2 = mod_quot_weight cb w2 in
  let z' = add_profits (mult_int_profit xb_1 p1) (mult_int_profit xb_2 p2) in
  let xb_3 = quotient_weight c' w3 in 
  (add_profits pcpt (add_profits z' (mult_int_profit xb_3 p3)),xb_1,xb_2,xb_3)
    
let bound_up_half_c umisc  bound wcpt pcpt ipt =
  let (remain_c: Wandp.M.w) = substract_weight bound.c wcpt in
  let ((wp,pp,_) as cp),stb =
    Seq.search_down1_from ( (>=) remain_c) sequence_result umisc.stbpt in
  umisc.cp <- cp;
  umisc.stbpt <- stb;
  let bi = 
    {c = remain_c ;
     u = add_profits pcpt pp;
     z = ref (add_profits pcpt pp) ;
     b1 = bound.b1;
     xb_1 = bound.xb_1;
     misc = Uphc umisc;
   }
  in
  if profit_smaller !(bound.z) !(bi.z) then bound.z := !(bi.z);
  if optimal_computed bound  then begin
    rbound := Some bound; 
    raise (Optimal (bi,(wcpt,pcpt,ipt),[])) 
  end
  else bi

let with_wp bound = 
  match bound.misc with
  | V _ -> with_v_wp
  | MT _ ->with_mt_wp
  | Both _ -> with_both_wp
  | Uphc m ->  bound_up_half_c m
  
      
;;





