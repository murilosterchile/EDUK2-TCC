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



(* $Id: datastruct.mli,v 1.2 2005/02/28 10:39:34 poirriez Exp $ *)

open Sequences
(** The few global data structures *)

val bound : Datastructtype.BOUNDS.t option ref
val allbound : Datastructtype.BOUNDS.t option ref

(** Contains true if we printed once that we use BigInt*)
val messbigint : bool ref


val iteminfos : Datastructtype.iteminfos

(** The list of currently used items *)
val decreasingS : (int * int) Chainlist.rt

(** The increasing sequence of optimal critical points*)
val sequence_result : Seq.t

(** [res] and [left] are used in the core of the algorithm
    to store the critical ppoints produced and comming from
    the previous "column".*)
val res : Seq.t
val left : Seq.t

(** The queue of items that have to be used to obtain this optimal solution*)
val required : int Queue.t

(**A flag to say of the capaity is reached *)
val not_reached_c' : bool ref

(** The function to refressh all these global data structs *)
val refresh : unit -> unit
