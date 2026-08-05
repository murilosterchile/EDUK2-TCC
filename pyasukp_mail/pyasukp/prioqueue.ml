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

(*$Id: prioqueue.ml,v 1.2 2005/02/28 10:39:35 poirriez Exp $*)
(*prioqueue.ml*)

type priority = Wandp.M.w
type 'a t = (priority * 'a) Chainlist.rt

type 'a conditional_add = Added  | PQ_to_continue of 'a t
    (* This type is used when we want to add and check if the element will be used *)

exception Empty
let order (p1,e1) (p2,e2) = Wandp.M.weight_smallereq p2 p1

let insert pq p e = Chainlist.insert_sorted order pq (p,e)

let add_test pq p v abort = 
 let pair = (p,v) in
 let dom = ref false in 
 let after = 
   Chainlist.tail_after 
     (fun ((p1,v1) as e) ->  
       ((dom := (abort v v1); !dom) or order e pair)) pq
 in
 if !dom then PQ_to_continue after else
 begin
   Chainlist.insert_head after pair; Added
 end

let single p v = Chainlist.create (p,v)

let take_equals_prio pq =
 try 
  let h,others =
    Chainlist.get_equiv_heads (fun (p1,_) (p2,_) -> Wandp.M.equal_weights p1 p2) pq
  in (snd h, List.rev_map snd others)
 with Chainlist.Empty -> raise Empty

let peek_fst_prio pq = 
 try fst(Chainlist.hd pq) with Chainlist.Empty -> raise Empty

let peek pq = 
 try snd(Chainlist.hd pq) with Chainlist.Empty -> raise Empty

let is_empty = Chainlist.is_empty 
let is_single = Chainlist.is_single
let is_single_or_empty pq = (is_single pq) or (is_empty pq)

let length = Chainlist.length
let fold f = Chainlist.fold (fun a (_,x) -> f  a x)
