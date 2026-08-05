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



(* $Id: slice.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $ *)
(*slice.ml*)

open Wandp.M
open Sequences
open Datastructtype
open Datastruct
open Init;;
  
(* In this file stands the core part of the algorithm. We call \emph{slice} the
   computation of all the optimal values for the knapsacks with capacities between
   a lower bound [binf] and an upperboud [bsup]. To this end, the considered items
   are those which are known to be not-dominated. These items are stored in
   an decreasing ordered chained-list.
*)
  
  
(* \paragraph{The contribution of an item type}\mbox{ }\\
   Here, the optimal solutions for the knapsacks of capacities
   less or equal than [binf] are known and stored in [sequence_result].
   The capacity of the knapsack considered is [bsup].
   Some item types have already been considered and their contributions
   are stored in [previous]. To help the visualisation of the algorithm,
   it's considered that they are at the left of the current item type.
   First, a type is defined to store the current state of the computation.
   Two exceptions are also defined.\\
*)
  
exception  Too_large
exception Last_empty
    
type state = {
    mutable pacc : Wandp.M.p ; (* The profit of the last kept critical point *)
    mutable eat_left: bool  ; (* A flag to tell if the critical point from the
                                 previous item types contribution is eaten *)
    mutable eat_new: bool   ; (* A flag to tell if the new critical point 
                                 is eaten *)
    mutable leftpt : Wandp.M.cp     ; (* the critical point from left *)
    mutable newpt : Wandp.M.cp ; (* the new built critical point *)
    mutable wk : Wandp.M.w    ; (* the weight of the current item type *)
    mutable pk : Wandp.M.p    ; (* the profit of the current item type *)
    mutable ijk : int * int           ; (* the index of the current item type *)
    mutable res : Seq.t       ; (* the result sequence of critical points *)
    mutable left : Seq.t      ; (* the previously built sequence of critical points *)
  }  
      ;;

(* We have now to check if the current item type contributes for the next critical point.
   The function below is named [merge_and_filter] because it will
   compute the merging of the sequence of critical points built with
   some previous item types and the sequence of critical points built with
   current item types. It also  filters the dominated critical points.\\
*)
  
let merge_and_filter state =
  let res = state.res in
  let (wleft,pleft,kleft) = state.leftpt
  and (wnew,pnew,knew) = state.newpt in
  if Wandp.M.equal_weights wleft wnew then
    begin
      state.eat_left <-  true;
      state.eat_new <- true;
      if Wandp.M.profit_smaller pleft pnew then
       if Wandp.M.profit_smallereq pnew state.pacc then () else
        begin
          state.pacc <-  pnew;
          Seq.put res state.newpt;
        end
      else if Wandp.M.profit_smallereq pleft state.pacc then () else
        begin
          state.pacc <-  pleft;
          Seq.put res state.leftpt;
        end
    end
  else if Wandp.M.weight_smaller wleft  wnew  then
    begin
      state.eat_left <- true;
      state.eat_new <- false;
      if Wandp.M.profit_smaller state.pacc pleft  then
        begin
          state.pacc <- pleft;
          Seq.put res state.leftpt;
        end
    end
  else (* wleft > wnew *)
    begin
      state.eat_new <- true;
      state.eat_left <- false;
      if Wandp.M.profit_smaller state.pacc pnew  then
        begin
          state.pacc <-  pnew;
          Seq.put res state.newpt;
        end
    end
      ;;

(* [merge_and_filter state] chooses which point is to be put in [state.res] and
   updates [state.res]. All the usefull information is updated in [state].
*)

(* [addtest frominlast state bsup  inlast] computes a new critical
   point using [(wk,pk)]. It builds it either from [sequence_result] or [res],
   depending if all the values in [sequence_result] are already used. It is necessary
   to use the currently build sequence  [res] due to the "vertical dependance" of
   the unbounded knapsack. This is the counter part of not using a lazy data structure.
   If the new weight exceeds the upper bound [bsup], then [Too_large] is raised.
   Else [frominlast]  and the out pointer of [sequence_result] or [res]
   are incremented. The new point is returned.
*)

let addtest frominlast state bsup inlast =
  let wold,pold,kold = 
    if !inlast then
      if Seq.isempty sequence_result then 
        begin
          inlast := false;
          frominlast := Seq.item_out sequence_result;
          Seq.get state.res
        end
      else
        Seq.peek sequence_result
    else
      Seq.get state.res
  in
  let wnew = Wandp.M.add_weights wold state.wk in
  if Wandp.M.weight_smaller bsup wnew then
    begin
      if !inlast then frominlast := Seq.item_out sequence_result;
      raise Too_large
    end
  else
    begin
      Seq.junk sequence_result;
      state.newpt <- (wnew, Wandp.M.add_profits pold state.pk, state.ijk)
    end
      
(*[filter pt res state] returns [true] if [pt] is dominated by [state.pacc]
   If [pt] is not dominated then it is put in [res] and [state.pacc] is updated.
*)
let filter ((w,p,_) as pt) res state =
  let is_better = Wandp.M.profit_smaller state.pacc p in
  if is_better then
    begin
      state.pacc <- p;
      Seq.put res pt
    end;
  not is_better
    
let build frominlast state bsup =
  let leftemptyflag = ref (not(Seq.isempty state.left))
  and domflag = ref true
  and inlast = ref (not(Seq.isempty sequence_result))
  in
  try
    (
     addtest frominlast state bsup inlast  ;
     while !leftemptyflag do
       state.leftpt <- Seq.peek state.left;
       merge_and_filter state;
       if state.eat_left then 
         begin
           Seq.junk state.left;
           leftemptyflag := not(Seq.isempty state.left)
         end;
       if state.eat_new then 
         addtest frominlast state bsup  inlast
     done;
 (*left est vide et bsup n'est pas encore atteint depuis sequence_result*)
     domflag :=true;
     while !domflag do
       let ((_, ppt, _) as pt) = state.newpt
       in
       if Wandp.M.profit_smaller state.pacc ppt then
         begin
           state.pacc <- ppt;
           Seq.put state.res pt;
           domflag := false;
         end
       else
         addtest frominlast state bsup inlast
     done;
     while true do
       addtest frominlast state bsup  inlast;
       let _,ppt,_ = state.newpt in state.pacc <- ppt;
       Seq.put state.res state.newpt;
     done;
    )
  with (Too_large | Last_empty) ->
    if !leftemptyflag then (*empty left now*)
      begin
        while !leftemptyflag & !domflag do 
          let ((wpt,ppt,_) as leftpt) = Seq.get state.left
          in
          domflag := filter leftpt state.res state;
          if Seq.isempty state.left then leftemptyflag := false
        done;
        while !leftemptyflag do 
          let (_,ppt,_) as leftpt = Seq.get state.left in state.pacc <- ppt;
          Seq.put state.res leftpt;
          leftemptyflag :=  not(Seq.isempty state.left) 
        done
      end
        ;;

(* \paragraph{To compute one slice}\mbox{ }\\
Here is given the function to compute the contribution of all the
items in [decreasingS] up to [bsup] It returns the capacity reached together with
the optimal profit for the knapsack of capacity [bsup]
*)
let zerocp = (Wandp.M.zerow,Wandp.M.zerop, (0,0)) 
 let state = {
   pacc =  Wandp.M.zerop;
   eat_left = false;
   eat_new = false;
   leftpt = zerocp ;
   newpt = zerocp;
   wk = Wandp.M.zerow ; 
   pk = Wandp.M.zerop ; 
   ijk = 0, 0;
   res = res;
   left = left;
 }  

let refresh_st state =
  Seq.reset state.res;
  state.eat_left <- false;
  state.eat_new <- false;
  state.leftpt <- zerocp;
  state.newpt <- zerocp
      
let one w p paccprev bsup =
  let pacc = ref paccprev in
  Seq.reset left;
  let consider_one_item jik=
    begin
      let rfiak = Astore.M.get iteminfos.next_built_upon jik 
      and itemk = Astore.M.get iteminfos.item jik in
      let wk = w.(itemk)
      and pk = p.(itemk) in
      refresh_st state;
      state.pacc <-  paccprev;
      state.wk <- wk ; 
      state.pk <- pk ; 
      state.ijk <- jik;
      Seq.jumpout sequence_result !rfiak;
      if not(Seq.isempty sequence_result) then begin
        build rfiak state bsup ;
      end;
      let tmp = state.res in
      state.res <- state.left;
      state.left <- tmp;
      Seq.resetout state.left; 
      pacc := state.pacc
    end
  in
  Chainlist.iter consider_one_item decreasingS;
  if not (Seq.isempty state.left) then begin
    let (_, lastp, _) =  Seq.lastval state.left in
    pacc := lastp end;
  ((bsup, !pacc), state.left)
    
;;
