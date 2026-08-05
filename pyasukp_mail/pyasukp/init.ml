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

(* $Id: init.ml,v 1.4 2005/04/27 13:09:48 poirriez Exp $ *)
(*init.ml*)

open Globals
open Wandp.M
open Parsecl
open Sequences
open Datastruct
open Datastructtype


let rwith_wp = ref Bounds.with_mt_wp

    
let structures w p c wb wab =
  let (imin1,imax,((ib1,ib2,ib3) as  bests), remains) =
    Prepro.ends_bests_others w p c in
  if !trace then begin
    Printf.printf "imin1 %d imax %d ib1 %d ib2 %d ib3 %d\n" imin1.i imax.i ib1.i ib2.i ib3.i;
    flush stdout;
  end;
  let notdom i =  
    i = ib1 || 
    (if imin1 <> ib1 then 
      not (Dominance.multiple imin1.w imin1.p i.w i.p ||
      Dominance.multiple ib1.w ib1.p i.w i.p ) 
    else not (Dominance.multiple imin1.w imin1.p i.w i.p))
  in
  let remainsnotdom =  List.filter notdom remains in
  let remains_array = Array.of_list (imin1::remainsnotdom) in
  if !trace then begin
    Array.iteri (fun j it -> Printf.printf " %d " it.w; if j mod 10 = 0 then Printf.printf "\n") remains_array;
    flush stdout
  end;
  let nbr = Array.length remains_array in
  bound := Bounds.create_bound bests c imin1 remains_array (wb || wab);
  if wab then allbound := !bound;
  (match !bound with
  | Some b  -> (rwith_wp := ( Bounds.with_wp b); if !trace then (print_endline "\n bound:";print_endline (BOUNDS.to_string b);flush stdout))
  | _ -> ());
  let nb_not_context_dom, imin =
    if !prepro then
      match !bound with
      | Some bound ->
          let item_context_dom item =
            Bounds.is_context_dominated 
              bound (!rwith_wp bound item.w item.p (-1,item.i))
          in
          let nb = 
            Prepro.remove_in_array item_context_dom remains_array nbr
          in
          let imin =
            if item_context_dom imin1 then
              Select.lightest remains_array (nb-1)
            else imin1 
          in nb, imin
      | None -> nbr, imin1
    else nbr, imin1
  in
  if nb_not_context_dom = 1 then begin
    match !bound with 
      Some bound -> raise(Datastructtype.BOUNDS.Optimal (bound,(zerow,zerop,(-1,-1)),[])) ;
    | None -> ()
  end;
  let rarray = 
    if nb_not_context_dom = nbr then remains_array 
    else Array.sub remains_array 0 nb_not_context_dom 
  in
  rarray,nb_not_context_dom, imin.w, imin, imax.w, imax, bests
    
let add_in_decreasingS w items wk rk k =
  Chainlist.put_in_sorted 
    (fun (j1,j2) -> 
      let itemi = Astore.M.get items.item (j1,j2) 
      and ratioi = Astore.M.get items.ratio (j1,j2) in
      Wandp.M.ratio_smaller ratioi rk or 
      (Wandp.M.equal_ratios ratioi rk && 
       Wandp.M.weight_smaller wk w.(itemi))) k
    
(* A simple function to introduce an item type in the data structures.
   We simply have to check if it is not dominated. We assume that the optimal
   solutions are known for all the capacities less or equal than its weight
 *)
    
let introduce w (waccprev, paccprev) item =
  let pi = item.p in
  let ok = 
    match !bound with
    | None -> true
    | Some bound ->  
        not (Bounds.is_context_dominated 
               bound (!rwith_wp bound item.w item.p (-1,item.i)))
  in
  if ok &&  Wandp.M.profit_smaller paccprev pi then begin
    let ij= Astore.M.add iteminfos.ratio item.r
    and wi = item.w in
    add_in_decreasingS w iteminfos wi item.r ij decreasingS;
    ignore(Astore.M.add iteminfos.next_built_upon (ref (0,1)));
    ignore(Astore.M.add iteminfos.last_contribution  wi);
    ignore(Astore.M.add iteminfos.nb_last_contribution_alone 1);
    ignore(Astore.M.add iteminfos.item item.i);
    if Wandp.M.equal_weights wi waccprev then
      Seq.put_replace sequence_result (wi, pi, ij)
    else  Seq.put sequence_result (wi, pi, ij);
    Queue.add item.i required;
    (true, pi)
  end
  else (false, paccprev)
      
let init imin1 =
  Seq.put sequence_result (zerow,zerop, (0,0) );
  let wimin = imin1.w and pimin = imin1.p and kmin = (0,0) in
  let put_imin1 =
    match !allbound with
    | None -> true 
    | Some bound -> 
        not (Bounds.is_context_dominated bound 
               (!rwith_wp bound wimin pimin (-2,imin1.i))) 
  in
  if put_imin1 then begin
    Seq.put sequence_result (wimin,pimin, (0,0) );
    ignore(Astore.M.add iteminfos.item imin1.i);
    ignore(Astore.M.add iteminfos.last_contribution wimin);
    ignore(Astore.M.add iteminfos.nb_last_contribution_alone 1);
    ignore(Astore.M.add iteminfos.next_built_upon  (ref( Seq.lastin sequence_result)));
    ignore(Astore.M.add iteminfos.ratio imin1.r);
    Queue.add imin1.i required ;
  end
  else begin
    Chainlist.delete (0,0) decreasingS
  end;
  (wimin,pimin)
    
    
