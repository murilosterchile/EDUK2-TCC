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



(* $Id: datastruct.ml,v 1.2 2005/02/28 10:39:34 poirriez Exp $ *)
(*datastruct.ml*)

open Globals
open Datastructtype
open Datastructtype.BOUNDS
open Sequences

(** The few global data structures *)

let (bound: t option ref) = ref None    
let (allbound: t option ref) = ref None    
let messbigint = ref true    

let iteminfos =
  let sis = !size_info_set and ssis = !sub_size_info_set in
  {
   last_contribution = Astore.M.create sis ssis (Wandp.M.zerow);
   nb_last_contribution_alone = Astore.M.create sis ssis 0;
   next_built_upon = Astore.M.create sis ssis (ref (0,1));
   item = Astore.M.create sis ssis 0;
   ratio = Astore.M.create sis ssis Wandp.M.zeror;
 } 


let decreasingS = Chainlist.create (0,0)
and sequence_result = 
  Seq.create !initial_size_of_sequence_result !subsize_of_sequence_result
and res = Seq.create !initial_size_ofbuffer !subsize_ofbuffer
and left = Seq.create !initial_size_ofbuffer !subsize_ofbuffer
and (required: int Queue.t) = Queue.create() 
and not_reached_c' = ref true



let refresh () =
  let sis = !size_info_set and ssis = !sub_size_info_set in
 not_reached_c' := true;
 Astore.M.reset iteminfos.last_contribution;
 Astore.M.reset iteminfos.nb_last_contribution_alone ;
 Astore.M.reset iteminfos.next_built_upon;
 Astore.M.reset iteminfos.item;
 Astore.M.reset iteminfos.ratio;
 Seq.reset sequence_result;
 Seq.reset res;
 Seq.reset left;
 decreasingS := !(Chainlist.create (0,0));
 Queue.clear required;
 bound := None;
 allbound:= None;
 messbigint := true;;

