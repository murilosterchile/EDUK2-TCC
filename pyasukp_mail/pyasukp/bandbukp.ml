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



(* $Id: bandbukp.ml,v 1.5 2005/04/25 08:38:26 poirriez Exp $ *)
(* bandbukp.ml*)

(** This module implements a branch and bound algorithm to solve
   ukp.
*)

open Globals
open Wandp
open M
open Bounds
open Datastructtype.BOUNDS

let ub1item c wi pi =
  let seuil = max_int / pi in
  if seuil >= c then (c * pi) / wi else begin
    if !messbigint then begin
      prerr_endline "Integer overflow, using Big_int arithmetic";
      messbigint := false
    end;
    Big_int.int_of_big_int(
    Big_int.div_big_int 
      (Big_int.mult_int_big_int c (Big_int.big_int_of_int pi)) 
      (Big_int.big_int_of_int wi))
  end

let rec backtrack nb items bound (sol, z, cr) =
match sol with
| [] -> (-1,(sol, z, cr))
| (i,ni)::s -> begin
    incr nbnode;
   if i = nb -1 then 
     let ncr = add_weights cr (mult_int_weight ni items.(i).w) 
     and nz = substract_profit z (mult_int_profit ni items.(i).p) 
     in backtrack nb items bound (s,nz,ncr)
   else
   let cr' = add_weights cr items.(i).w
   and z' =  substract_profit z items.(i).p
   in
   let j = i+1 in
   let limz'=
     add_profits z' (ub1item cr' items.(j).w items.(j).p)
   in
   if profit_smaller limz' !(bound.z) then
     let ncr = add_weights cr (mult_int_weight ni items.(i).w) 
     and nz = substract_profit z (mult_int_profit ni items.(i).p) 
     in (i,(s,nz,ncr))
   else if ni = 1 then (i,(s,z',cr'))
   else (i,((i,ni-1)::s,z',cr'))
end

let lightest_worse items nb =
  let minweights = Array.create nb items.(nb-1).w in
  for i = nb-2 downto 0 do
    let nmin =
      if weight_smaller items.(i+1).w minweights.(i+1) then
        items.(i+1).w 
      else minweights.(i+1)
    in
    minweights.(i) <- nmin
  done;
  minweights

let actualsol items nsol=
  List.map (fun (i,ni) -> items.(i).i,ni) nsol

let greedy_fill bound items nb =
  let rc = ref bound.c in
  let ri = ref 0 in
  let sol = ref[] in
  let z = ref zerop in
  while Wandp.M.weight_smaller zerow !rc && !ri < nb do
    let mi,xi = Wandp.M.mod_quot_weight !rc items.(!ri).w in
    if xi > 0 then begin
      sol := (!ri,xi):: !sol;
      rc := mi;
      z := add_profits !z (mult_int_profit xi items.(!ri).p)
    end;
    incr ri;
  done;
  if profit_smaller !(bound.z) !z then begin
    bound.z := !z;
    if optimal_computed bound  then 
      raise (Optimal (bound,(zerow,zerop,(-3,-3)),actualsol items !sol)) 
    else (!sol, !z, !rc)
  end
  else (!sol, !z, !rc)

let rec complete lasti items minweights actualz bsol (sol,z,cr) i = 
(*  incr nbnode;*)
  if weight_smaller cr minweights.(i) || i = lasti then 
    if profit_smaller z !actualz then (sol,z,cr)
    else begin
      bsol := actualsol items sol;
      actualz := z;
      (sol,z,cr)
    end
  else 
    let j = i+1 in
    let wj = items.(j).w and pj = items.(j).p in
    if profit_smaller (add_profits z (ub1item cr wj pj)) !actualz then
      (sol,z,cr)
    else
      let mj,nj = mod_quot_weight cr wj in
      if nj = 0 then
        complete lasti items minweights actualz bsol (sol,z,cr) j
      else
        let nz= add_profits z (mult_int_profit nj pj) 
        in
        complete lasti items minweights actualz bsol ((j,nj)::sol, nz, mj) j

let rec traverse nb items minweights bound bsol (sol,z,cr)=
  if !nbmaxsol >= 0 && !nbnode > !nbmaxsol then (false,(!bsol,z,cr)) else
  let (i,(sol',z',cr')) = backtrack nb items bound (sol, z, cr) in
  if i < 0 then (true,(!bsol,z,cr)) else
  let (nsol,nz,ncr) = 
    complete (nb-1) items minweights bound.z bsol (sol',z',cr') i
  in
  if optimal_computed bound then begin
(*    Printf.fprintf stderr "\nNumber of nodes: %d\n" !nbnode; flush stderr;*)
    raise (Optimal (bound,(zerow,zerop,(-3,-3)),!bsol)) ;
  end
  else
    traverse nb items minweights bound bsol (nsol,nz,ncr)

(** [solve items nbitems bound] uses a b&b algorithm to solves the ukp problem
   using the [nbitems] first elements in [items]. It assumes that some upper 
   and lower bound is computed, these data are in [bound].
*)
let solve items nbbests bound =
  nbnode := 0;
  let n = Array.length items in
  let nbb = 
(*    if !nbbests = -1 then min n (max 100 (n / 100))*)
    if !nbbests = -1 then min n 500
    else min !nbbests n
  in 
  nbbests := nbb;
  Select.split_n_best (is_worse_mod bound.c) items 0 (n-1) nbb;
  let nbr, sitems = 
    if !zhbr then 
      let sitems = Select.quick is_heavyer items 0 (nbb -1) in
      let res=
      Select.elim2pass (Dominance.multiple_or_zhbr_if_w_sorted bound.b1.w bound.b1.p)  sitems nbb 
      in
      let sitems = Select.quick is_worse_weight items 0 (res -1) in
      res,sitems
    else if !dm then
      let sitems = Select.quick is_worse_weight items 0 (nbb -1) in
      Select.elim2pass Dominance.multiple_item sitems nbb , sitems
    else nbb,items
  in
  let nbdel =  Select.switchblock sitems nbr (nbb-1) in
  let minweights = lightest_worse sitems nbr in
  let (bsol,_,_) as greedysol = greedy_fill bound sitems nbr in
  let rbsol = ref (actualsol items bsol) in
  let (allbb,(sol,z,cr)) =  
    traverse (min nbr nbb) sitems minweights bound rbsol greedysol in
  if nbb = n && allbb then 
    begin
(*      Printf.fprintf stderr "\nNumber of nodes: %d\n" !nbnode;flush stderr;*)
      raise(Optimal (bound,(zerow,zerop,(-4,-4)),!rbsol)) 
    end
  else
  nbdel


